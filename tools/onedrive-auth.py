#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# onedrive-auth.py -- an unmodified application through the portal.
#
# The abraunegg OneDrive client (Fedora package `onedrive`) has
# `--auth-files <authUrlFile>:<responseUrlFile>`: it writes the Microsoft
# authorize URL to the first file and then polls until the second file exists,
# expecting the whole redirect URI the user would otherwise have copied out of a
# browser address bar. That is the same pair of URIs
# org.freedesktop.portal.WebAuthentication.X1.Start takes and returns, so the
# client needs no patch: this script sits between the two files and the portal.
#
#     onedrive --confdir ~/.config/onedrive --reauth \
#         --auth-files /run/user/$UID/a:/run/user/$UID/b &
#     tools/onedrive-auth.py /run/user/$UID/a:/run/user/$UID/b
#
# The completion URI is the `redirect_uri` of the authorize URL the client
# wrote -- https://login.microsoftonline.com/common/oauth2/nativeclient for the
# common endpoint, the same path on login.microsoftonline.us and the other
# clouds -- and not a value this script chooses. If the URL carries no
# redirect_uri, the nativeclient path on the URL's own host is used.
#
# It does not delete either file. The client deletes both itself once the
# response file appears, observed against onedrive v2.5.11.
#
# Exit status: 0 the flow completed and the response file was written, 1 the
# user cancelled, 2 anything else, with one line saying what.
#
# WHAT IT LOGS: one line per state change, on stderr, carrying scheme, host and
# path only. The response URI's query is the authorization code and the
# authorize URL's query is the request that code answers; neither is ever
# printed, and URLs in D-Bus error text have their query stripped.

import argparse
import os
import re
import secrets
import sys
import time
import urllib.parse

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

PORTAL_BUS_NAME = "org.freedesktop.portal.Desktop"
PORTAL_OBJECT_PATH = "/org/freedesktop/portal/desktop/experimental"
INTERFACE = "org.freedesktop.portal.WebAuthentication.X1"
REQUEST_INTERFACE = "org.freedesktop.portal.Request"
REQUEST_PREFIX = "/org/freedesktop/portal/desktop/request"

NATIVECLIENT_PATH = "/common/oauth2/nativeclient"

QUERY_IN_URL = re.compile(r"(?i)\b([a-z][a-z0-9+.-]*://[^\s?#]*)[?#]\S*")


def note(line):
    sys.stderr.write(f"onedrive-auth: {line}\n")
    sys.stderr.flush()


def redact(text):
    """Anything URL-shaped, with its query and fragment taken off."""
    return QUERY_IN_URL.sub(r"\1?<redacted>", " ".join(str(text).split()))


def shape(uri):
    """scheme, host and path: what can be logged about a URI that carries a credential."""
    parsed = urllib.parse.urlsplit(uri)
    if parsed.netloc:
        return f"scheme={parsed.scheme} host={parsed.netloc} path={parsed.path}"
    return f"scheme={parsed.scheme} path={parsed.path}"


def split_auth_files(values):
    """--auth-files syntax: one 'a:b' argument, or the two paths as two arguments."""
    if len(values) == 2:
        return values[0], values[1]
    parts = values[0].split(":")
    if len(parts) != 2 or not parts[0] or not parts[1]:
        raise ValueError(
            "expected <authUrlFile>:<responseUrlFile>, or the two paths as two arguments"
        )
    return parts[0], parts[1]


def wait_for_url(path, deadline):
    """The client writes the file and then the URL; an empty file is not yet an answer."""
    while time.monotonic() < deadline:
        try:
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read().strip()
            if text:
                return text
        except FileNotFoundError:
            pass
        except OSError as error:
            raise RuntimeError(f"cannot read {path}: {error.strerror}") from error
        time.sleep(0.25)
    raise TimeoutError(f"no authorize URL in {path}")


def completion_uri_for(start_uri):
    parsed = urllib.parse.urlsplit(start_uri)
    if parsed.scheme.lower() != "https" or not parsed.hostname:
        raise ValueError("the authorize URL is not an absolute https URL with a host")

    redirect = urllib.parse.parse_qs(parsed.query).get("redirect_uri", [""])[0]
    if redirect:
        return redirect
    return urllib.parse.urlunsplit(("https", parsed.netloc, NATIVECLIENT_PATH, "", ""))


def write_response(path, uri):
    """0600 before anything is written, and renamed into place: the client polls for
    this file's existence, so it must never be seen half written."""
    directory = os.path.dirname(os.path.abspath(path))
    temporary = os.path.join(directory, f".{os.path.basename(path)}.{os.getpid()}")
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(uri + "\n")
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


class Flow:
    def __init__(self, bus, args, start_uri, completion_uri):
        self.bus = bus
        self.args = args
        self.start_uri = start_uri
        self.completion_uri = completion_uri
        self.loop = GLib.MainLoop()
        self.response = None
        self.results = {}
        self.failure = None

    def on_response(self, connection, sender, path, interface, signal, params):
        if self.response is not None:
            return
        self.response = int(params[0])
        self.results = {key: params[1][key] for key in params[1].keys()}
        self.loop.quit()

    def on_timeout(self):
        self.failure = "no Response signal arrived"
        self.loop.quit()
        return GLib.SOURCE_REMOVE

    def run(self):
        # The Request path is predictable from the unique name and handle_token,
        # so the Response subscription goes on before Start() is called and the
        # signal cannot be missed. The returned handle is still checked, because
        # a frontend older than 0.9 of xdg-desktop-portal mints its own.
        token = "onedriveauth" + secrets.token_hex(8)
        sender = self.bus.get_unique_name()[1:].replace(".", "_")
        expected = f"{REQUEST_PREFIX}/{sender}/{token}"
        self.bus.signal_subscribe(
            None, REQUEST_INTERFACE, "Response", expected, None,
            Gio.DBusSignalFlags.NONE, self.on_response,
        )

        options = {
            "handle_token": GLib.Variant("s", token),
            "session_mode": GLib.Variant("s", "shared"),
            "timeout": GLib.Variant("u", self.args.timeout),
            "title": GLib.Variant("s", "OneDrive"),
        }

        reply = self.bus.call_sync(
            PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, INTERFACE, "Start",
            GLib.Variant(
                "(sssa{sv})",
                (self.args.parent_window, self.start_uri, self.completion_uri, options),
            ),
            GLib.VariantType("(o)"), Gio.DBusCallFlags.NONE, 30000, None,
        )

        handle = reply[0]
        if handle != expected:
            self.bus.signal_subscribe(
                None, REQUEST_INTERFACE, "Response", handle, None,
                Gio.DBusSignalFlags.NONE, self.on_response,
            )
        note(f"request {handle}")

        # The frontend keeps the deadline and answers 2 with reason `timeout`;
        # this one is only for a frontend that has stopped answering at all.
        GLib.timeout_add_seconds(self.args.timeout + 30, self.on_timeout)
        self.loop.run()


def main():
    parser = argparse.ArgumentParser(
        description="Run the OneDrive client's --auth-files handshake through the "
        "WebAuthentication portal",
    )
    parser.add_argument(
        "auth_files", nargs="+",
        metavar="<authUrlFile>:<responseUrlFile>",
        help="the paths given to onedrive --auth-files, as one colon-separated "
        "argument or as two arguments",
    )
    parser.add_argument(
        "--parent-window", default="",
        help="the calling window, in the portal's parent-window notation "
        "(wayland:<handle> or x11:<xid>); empty for none",
    )
    parser.add_argument(
        "--timeout", type=int, default=300,
        help="seconds to wait for the authorize URL, and the portal's own timeout "
        "option for the sign-in (default 300)",
    )
    args = parser.parse_args()

    try:
        auth_path, response_path = split_auth_files(args.auth_files)
    except ValueError as error:
        note(f"error {error}")
        return 2

    if args.timeout < 1:
        note("error --timeout must be at least 1 second")
        return 2

    try:
        note(f"waiting for the authorize URL in {auth_path}")
        start_uri = wait_for_url(auth_path, time.monotonic() + args.timeout)
        completion_uri = completion_uri_for(start_uri)
    except (RuntimeError, TimeoutError, ValueError) as error:
        note(f"error {error}")
        return 2

    note(f"authorize {shape(start_uri)}")
    note(f"completion {shape(completion_uri)}")

    try:
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    except GLib.Error as error:
        note(f"error no session bus: {redact(error.message)}")
        return 2

    flow = Flow(bus, args, start_uri, completion_uri)
    try:
        flow.run()
    except GLib.Error as error:
        # No backend and no portal at all look the same to a caller, by design.
        note(f"error Start failed: {redact(error.message)}")
        return 2

    if flow.failure:
        note(f"error {flow.failure}")
        return 2

    if flow.response == 1:
        note("cancelled")
        return 1

    reason = flow.results.get("reason")
    if flow.response != 0:
        detail = f", reason {reason}" if reason else ""
        note(f"error the portal answered {flow.response}{detail}")
        return 2

    uri = flow.results.get("completion_uri")
    if not uri:
        note("error the portal completed without a completion URI")
        return 2
    parsed = urllib.parse.urlsplit(uri)
    keys = ",".join(sorted(urllib.parse.parse_qs(parsed.query, keep_blank_values=True)))
    note(f"result query keys={keys or '(none)'} fragment={'yes' if parsed.fragment else 'no'}")

    try:
        write_response(response_path, uri)
    except OSError as error:
        note(f"error cannot write {response_path}: {error.strerror}")
        return 2

    # Not removed here: onedrive deletes both files itself once this one appears.
    note(f"completed, response written to {response_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
