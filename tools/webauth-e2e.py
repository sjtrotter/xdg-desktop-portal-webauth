#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Exercise the public WebAuthentication interface and validate the returned protocol response; see docs/TESTING.md.

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
PORTAL_OBJECT_PATH = "/org/freedesktop/portal/desktop"
INTERFACE = "org.freedesktop.portal.experimental.WebAuthentication1"
REQUEST_INTERFACE = "org.freedesktop.portal.Request"


# The AVD client registration requires the commercial nativeclient redirect in both clouds.
ENTRA_AVD_CLIENT_ID = "a85cf173-4192-42f8-81fa-777a763e6e2c"
ENTRA_NATIVECLIENT_REDIRECT = "https://login.microsoftonline.com/common/oauth2/nativeclient"
ENTRA_USGOV_AVD_SCOPE = "https://www.wvd.azure.us/.default openid profile offline_access"


def pkce_pair():
    verifier = base64.urlsafe_b64encode(secrets.token_bytes(64)).rstrip(b"=").decode("ascii")
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    challenge = base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")
    return verifier, challenge


def write_verifier(path, verifier, state):
    # The verifier must be private from creation; retain the prior value in .prev.
    if os.path.lexists(path):
        previous = path + ".prev"
        if os.path.lexists(previous):
            os.unlink(previous)
        os.rename(path, previous)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        handle.write(f"code_verifier={verifier}\nstate={state}\n")


def entra_authorize_uri(args, state):
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
        # Force sign-in so a stored session cannot bypass token authentication.
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
        self.failed = "Response signal timed out"
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

    # Request.Close unexports the public request without Response; dev-stack.sh checks backend termination.
    if args.expect_no_response:
        if run.response is None:
            print("Response none=expected-after-close")
            return []
        return [f"Response received after Close(): response={run.response}"]

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
            problems.append("Nonzero response contains completion_uri")
        return problems

    if completion is None:
        return problems + ["Results contain no completion_uri"]

    parsed = urllib.parse.urlsplit(completion)
    wanted = urllib.parse.urlsplit(args.completion_uri)
    query = urllib.parse.parse_qs(parsed.query)

    # Completion queries carry authorization codes and must stay out of logs.
    print(f"completion scheme={parsed.scheme} host={parsed.netloc} path={parsed.path}")
    print("completion query keys=" + ",".join(sorted(query)))

    if (parsed.scheme.lower(), parsed.netloc.lower(), parsed.path) != (
        wanted.scheme.lower(),
        wanted.netloc.lower(),
        wanted.path,
    ):
        problems.append("Completion URI does not match the request")

    code = query.get("code", [None])[0]
    if code:
        print(f"completion code length={len(code)}")
    elif args.require_code:
        problems.append("Completion URI contains no code")

    if args.state:
        got = query.get("state", [None])[0]
        if got != args.state:
            problems.append("Completion state does not match the request")
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
        help="build an Entra ID authorization code flow URI",
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
        help="PKCE verifier output file (mode 0600); existing content moves to .prev",
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
        help="require no Response signal after Request.Close()",
    )
    parser.add_argument("--expect-reason")
    parser.add_argument("--expect-cookie", choices=["yes", "no"])
    parser.add_argument("--no-require-code", dest="require_code", action="store_false")
    parser.add_argument("--state", default=secrets.token_urlsafe(9))
    args = parser.parse_args()

    if args.entra_authorize:
        if not args.entra_authority:
            parser.error("--entra-authorize requires --entra-authority")
        if not args.entra_verifier_file:
            parser.error("--entra-authorize requires --entra-verifier-file")
        if args.start_uri:
            parser.error("--entra-authorize cannot be combined with --start-uri")
        # Entra URLs already contain state; fixture URLs receive it in start().
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
