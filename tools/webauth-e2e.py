#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# webauth-e2e.py -- an application, doing what an application does.
#
# It calls the PUBLIC interface,
# org.freedesktop.portal.WebAuthentication.X1, on
# org.freedesktop.portal.Desktop, and waits for the Response signal on the
# Request object it gets back. It never names a backend, never reads a .portal
# file and never calls an impl interface: that is the whole point of the split,
# and a test that cheated on it would not be testing it.
#
# What it checks, and each of these is a claim in docs/PUBLIC-INTERFACE.md:
#
#   * the response code and, when one is expected, the reason symbol;
#   * that the completion URI comes back complete, with the query the flow ended
#     on -- the authorization code and the state it echoed;
#   * that the state is the one this client sent, compared in full;
#   * that the completion URI matches the one that was asked for.
#
# It prints the shape of what came back and never its query: the code in it is
# the credential the whole exercise was about.
#
#     tools/webauth-e2e.py --start-uri https://localhost:8443/start \
#         --completion-uri https://example.invalid/cb
#     tools/webauth-e2e.py ... --session-mode ephemeral --expect-cookie no
#     tools/webauth-e2e.py ... --cancel-after 2000 --expect-response 1
#
# --entra-authorize builds the start URI for a REAL Entra ID authorization code
# flow instead of taking one, so that a live run against the identity provider
# is one command rather than a URL assembled by hand. See docs/TESTING.md,
# "Live run against Entra". It writes the PKCE code verifier to a file with mode
# 0600 and NEVER prints it: a verifier on a terminal is a verifier in a scrollback
# buffer, and with the authorization code it is the whole credential.
#
#     tools/webauth-e2e.py --entra-authorize \
#         --entra-authority https://login.microsoftonline.us/<tenant> \
#         --entra-verifier-file ~/.cache/webauth-verifier \
#         --completion-uri https://login.microsoftonline.com/common/oauth2/nativeclient

import argparse
import base64
import hashlib
import os
import secrets
import sys
import urllib.parse

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

PORTAL_BUS_NAME = "org.freedesktop.portal.Desktop"
PORTAL_OBJECT_PATH = "/org/freedesktop/portal/desktop/experimental"
INTERFACE = "org.freedesktop.portal.WebAuthentication.X1"
REQUEST_INTERFACE = "org.freedesktop.portal.Request"


# ------------------------------------------------------------------ Entra ID
#
# The AVD public client, and the redirect its registration forces. THE REDIRECT
# IS THE COMMERCIAL nativeclient URL FOR BOTH CLOUDS -- there is no .us variant
# and asking for one is rejected with AADSTS50011 -- which is exactly why this
# portal exists: nothing may ever be allowed to FETCH that URL, because its query
# carries the authorization code.
ENTRA_AVD_CLIENT_ID = "a85cf173-4192-42f8-81fa-777a763e6e2c"
ENTRA_NATIVECLIENT_REDIRECT = "https://login.microsoftonline.com/common/oauth2/nativeclient"
ENTRA_USGOV_AVD_SCOPE = "https://www.wvd.azure.us/.default openid profile offline_access"


def pkce_pair():
    """A code verifier and its S256 challenge, RFC 7636 §4."""
    verifier = base64.urlsafe_b64encode(secrets.token_bytes(64)).rstrip(b"=").decode("ascii")
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    challenge = base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")
    return verifier, challenge


def write_verifier(path, verifier, state):
    """0600 BEFORE ANYTHING IS WRITTEN, not after.

    os.open with O_CREAT|O_EXCL and mode 0600 is the only way to create a file
    that was never, for any instant, readable by another user; opening it and
    chmod-ing afterwards leaves exactly that window. O_EXCL because a file
    holding half a credential is never overwritten in place: a previous one is
    moved aside to <path>.prev (replacing an older .prev) so a rerun works.
    """
    if os.path.lexists(path):
        previous = path + ".prev"
        if os.path.lexists(previous):
            os.unlink(previous)
        os.rename(path, previous)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        handle.write(f"code_verifier={verifier}\nstate={state}\n")


def entra_authorize_uri(args, state):
    """The authorization endpoint URL for an authorization code flow with PKCE."""
    verifier, challenge = pkce_pair()
    write_verifier(args.entra_verifier_file, verifier, state)

    query = {
        "client_id": args.entra_client_id,
        "response_type": "code",
        "redirect_uri": args.completion_uri,
        "scope": args.entra_scope,
        "state": state,
        "code_challenge": challenge,
        "code_challenge_method": "S256",
        # A fresh sign-in rather than whatever the shared store remembers: a live
        # run is testing the CARD, and a silently reused session tests nothing.
        "prompt": "login",
    }

    return args.entra_authority.rstrip("/") + "/oauth2/v2.0/authorize?" + urllib.parse.urlencode(
        query
    )


class Run:
    def __init__(self, bus, args):
        self.bus = bus
        self.args = args
        self.loop = GLib.MainLoop()
        self.response = None
        self.results = {}
        self.failed = None

    def on_response(self, connection, sender, path, interface, signal, params):
        self.response = int(params[0])
        self.results = {key: params[1][key] for key in params[1].keys()}
        self.loop.quit()

    def on_timeout(self):
        self.failed = "no Response signal arrived"
        self.loop.quit()
        return GLib.SOURCE_REMOVE

    def close_request(self, handle):
        print(f"Close {handle}")
        try:
            self.bus.call_sync(
                PORTAL_BUS_NAME,
                handle,
                REQUEST_INTERFACE,
                "Close",
                None,
                None,
                Gio.DBusCallFlags.NONE,
                5000,
                None,
            )
        except GLib.Error as error:
            print(f"Close failed: {error.message}")
        return GLib.SOURCE_REMOVE

    def start(self):
        options = {"handle_token": GLib.Variant("s", "webauthe2e")}
        if self.args.session_mode:
            options["session_mode"] = GLib.Variant("s", self.args.session_mode)
        if self.args.title:
            options["title"] = GLib.Variant("s", self.args.title)
        if self.args.timeout:
            options["timeout"] = GLib.Variant("u", self.args.timeout)

        start_uri = self.args.start_uri
        if self.args.state and getattr(self.args, "state_in_start_uri", True):
            joiner = "&" if "?" in start_uri else "?"
            start_uri += joiner + urllib.parse.urlencode({"state": self.args.state})

        reply = self.bus.call_sync(
            PORTAL_BUS_NAME,
            PORTAL_OBJECT_PATH,
            INTERFACE,
            "Start",
            GLib.Variant(
                "(sssa{sv})",
                ("", start_uri, self.args.completion_uri, options),
            ),
            GLib.VariantType("(o)"),
            Gio.DBusCallFlags.NONE,
            30000,
            None,
        )

        handle = reply[0]
        print(f"Start handle={handle}")

        self.bus.signal_subscribe(
            None,
            REQUEST_INTERFACE,
            "Response",
            handle,
            None,
            Gio.DBusSignalFlags.NONE,
            self.on_response,
        )

        if self.args.cancel_after:
            GLib.timeout_add(self.args.cancel_after, self.close_request, handle)

        GLib.timeout_add(self.args.wait, self.on_timeout)
        self.loop.run()


def check(run, args):
    problems = []

    # Close() is not answered, by design: the frontend unexports the Request
    # instead of emitting a Response, so an application that closes its own
    # request gets silence. What has to be true is that the silence is the
    # backend having stopped, which dev-stack.sh checks in the backend's log.
    if args.expect_no_response:
        if run.response is None:
            print("Response none=expected-after-close")
            return []
        return [f"a Response arrived after Close(): response={run.response}"]

    if run.failed:
        return [run.failed]

    print(f"Response response={run.response}")
    print("Response results=" + ",".join(sorted(run.results)))

    if run.response != args.expect_response:
        problems.append(f"expected response {args.expect_response}, got {run.response}")

    reason = run.results.get("reason")
    if reason is not None:
        print(f"Response reason={reason}")
    if args.expect_reason and reason != args.expect_reason:
        problems.append(f"expected reason {args.expect_reason!r}, got {reason!r}")

    completion = run.results.get("completion_uri")

    if args.expect_response != 0:
        if completion is not None:
            problems.append("a non-zero response must not carry a completion_uri")
        return problems

    if completion is None:
        return problems + ["no completion_uri in the results"]

    parsed = urllib.parse.urlsplit(completion)
    wanted = urllib.parse.urlsplit(args.completion_uri)
    query = urllib.parse.parse_qs(parsed.query)

    # The shape only: the query carries the authorization code.
    print(f"completion scheme={parsed.scheme} host={parsed.netloc} path={parsed.path}")
    print("completion query keys=" + ",".join(sorted(query)))

    if (parsed.scheme.lower(), parsed.netloc.lower(), parsed.path) != (
        wanted.scheme.lower(),
        wanted.netloc.lower(),
        wanted.path,
    ):
        problems.append("the completion URI is not the one that was requested")

    code = query.get("code", [None])[0]
    if code:
        print(f"completion code length={len(code)}")
    elif args.require_code:
        problems.append("no code in the completion URI")

    if args.state:
        got = query.get("state", [None])[0]
        if got != args.state:
            problems.append("the state did not come back unchanged")
        else:
            print("completion state=echoed")

    if args.expect_cookie:
        got = query.get("cookie", [None])[0]
        print(f"completion cookie={got}")
        if got != args.expect_cookie:
            problems.append(f"expected cookie {args.expect_cookie}, got {got}")

    return problems


def main():
    parser = argparse.ArgumentParser(description="Drive the public WebAuthentication portal")
    parser.add_argument("--start-uri")
    parser.add_argument("--completion-uri", required=True)
    parser.add_argument(
        "--entra-authorize",
        action="store_true",
        help="build the start URI for a real Entra ID authorization code flow "
        "instead of taking --start-uri",
    )
    parser.add_argument("--entra-client-id", default=ENTRA_AVD_CLIENT_ID)
    parser.add_argument(
        "--entra-authority",
        help="https://login.microsoftonline.us/<tenant> for US Gov, "
        "https://login.microsoftonline.com/<tenant> for commercial",
    )
    parser.add_argument("--entra-scope", default=ENTRA_USGOV_AVD_SCOPE)
    parser.add_argument(
        "--entra-verifier-file",
        help="where to write the PKCE code verifier, created 0600 and never overwritten. "
        "It is NEVER printed.",
    )
    parser.add_argument("--session-mode", choices=["shared", "ephemeral"])
    parser.add_argument("--title")
    parser.add_argument("--timeout", type=int, help="the portal's timeout option, in seconds")
    parser.add_argument("--wait", type=int, default=60000, help="give up after N ms")
    parser.add_argument("--start-path", help=argparse.SUPPRESS)
    parser.add_argument("--cancel-after", type=int, help="call Request.Close() after N ms")
    parser.add_argument("--expect-response", type=int, default=0)
    parser.add_argument(
        "--expect-no-response",
        action="store_true",
        help="assert that NO Response arrives, which is what Request.Close() means: "
        "the frontend unexports the request rather than answering it",
    )
    parser.add_argument("--expect-reason")
    parser.add_argument("--expect-cookie", choices=["yes", "no"])
    parser.add_argument("--no-require-code", dest="require_code", action="store_false")
    parser.add_argument("--state", default=secrets.token_urlsafe(9))
    args = parser.parse_args()

    if args.entra_authorize:
        if not args.entra_authority:
            parser.error("--entra-authorize needs --entra-authority")
        if not args.entra_verifier_file:
            parser.error("--entra-authorize needs --entra-verifier-file")
        if args.start_uri:
            parser.error("--entra-authorize builds the start URI; do not pass --start-uri")
        # The state goes into the authorize URL itself, so it must not also be
        # appended to the start URI the way the fixture flow's is.
        args.start_uri = entra_authorize_uri(args, args.state)
        args.state_in_start_uri = False
        parsed = urllib.parse.urlsplit(args.start_uri)
        print(f"start scheme={parsed.scheme} host={parsed.netloc} path={parsed.path}")
        print(f"code verifier written to {args.entra_verifier_file} (mode 0600)")
    elif not args.start_uri:
        parser.error("one of --start-uri or --entra-authorize is required")
    else:
        args.state_in_start_uri = True

    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    run = Run(bus, args)
    try:
        run.start()
    except GLib.Error as error:
        print(f"Start failed: {error.message}")
        return 1

    problems = check(run, args)

    if problems:
        for problem in problems:
            print(f"FAIL {problem}")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
