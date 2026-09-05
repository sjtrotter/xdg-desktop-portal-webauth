#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# mtls-server.py -- the identity provider the tests sign in against.
#
# A three step flow, which is the shape of a real authorization code flow with
# the protocol taken out:
#
#   GET /start   302 -> /login?state=<echo>
#   GET /login   302 -> <completion uri>?code=<random>&state=<echo>
#   GET /cb      the completion URI, WHEN it points back here -- served only so
#                that a run which fetched it can be told apart from one which
#                did not. The backend must never fetch it.
#   GET /wait    a page that does not redirect, for the runs that have to
#                interrupt a flow rather than let it finish.
#
# With --require-client-cert the socket is wrapped with ssl.CERT_REQUIRED and a
# CA file, so a client that cannot produce a certificate signed by that CA does
# not complete the handshake and gets no response at all. That is the whole
# point of the fixture: the TLS layer, not the application, is what a smart card
# satisfies.
#
# --cookie sets a cookie on /start and reports, in the completion URI, whether
# the engine ALREADY had one when it arrived. That is how the storage-partition
# tests tell "shared" from "ephemeral" without looking inside the engine: within
# one flow the cookie always comes back, so the question has to be asked of the
# first request of a run.
#
# WHAT IT PRINTS: one line per request, method and path only. The query carries
# the authorization code and the state, so it is never logged, never echoed into
# a page, and the code is generated per request rather than fixed.

import argparse
import http.server
import os
import secrets
import ssl
import sys
import threading
import urllib.parse

CODE_BYTES = 24


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "portal-webauth-fixture/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _note(self, path):
        line = f"{self.command} {path}\n"
        sys.stderr.write(f"request {line}")
        sys.stderr.flush()
        if self.server.access_log:
            with open(self.server.access_log, "a", encoding="utf-8") as handle:
                handle.write(line)

    def _redirect(self, target, cookie=None):
        body = b"redirecting\n"
        self.send_response(302)
        self.send_header("Location", target)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        if cookie:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()
        self.wfile.write(body)

    def _page(self, text, status=200):
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        state = query.get("state", [""])[0]
        self._note(parsed.path)

        if parsed.path == "/start":
            cookie = None
            args = {}
            if state:
                args["state"] = state
            if self.server.use_cookie:
                # WHETHER THE ENGINE ARRIVED WITH THE COOKIE A PREVIOUS RUN SET,
                # asked BEFORE this run sets one: inside a single flow the cookie
                # always comes back, so only the first request of a run can tell
                # a persistent store from an ephemeral one. The answer travels
                # down the redirect chain into the completion URI, where the
                # client can assert on it.
                seen = "portal_fixture=" in (self.headers.get("Cookie") or "")
                args["seen"] = "yes" if seen else "no"
                cookie = f"portal_fixture={self.server.cookie_value}; Path=/; Max-Age=3600"
            target = "/login"
            if args:
                target += "?" + urllib.parse.urlencode(args)
            self._redirect(target, cookie=cookie)
            return

        if parsed.path == "/login":
            seen = self.headers.get("Cookie", "")
            self.server.seen_cookies.append(seen)
            code = secrets.token_urlsafe(CODE_BYTES)
            args = {"code": code}
            if state:
                args["state"] = state
            if self.server.use_cookie:
                args["cookie"] = query.get("seen", ["no"])[0]
            self._redirect(self.server.completion_uri + "?" + urllib.parse.urlencode(args))
            return

        if parsed.path == "/wait":
            # A page that goes nowhere, so that a flow can be interrupted rather
            # than finishing on its own: the cancel and Close tests need a window
            # that is still open when they reach for it.
            self._page("<html><body><h1>waiting for you</h1></body></html>")
            return

        if parsed.path == "/cookie":
            seen = self.headers.get("Cookie", "")
            self._page(f"<html><body>cookie {'yes' if seen else 'no'}</body></html>")
            return

        if parsed.path == "/":
            self._page("<html><body>fixture</body></html>")
            return

        self._page("<html><body>not found</body></html>", status=404)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description="A TLS fixture identity provider")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--ca", help="CA that client certificates must chain to")
    parser.add_argument("--require-client-cert", action="store_true")
    parser.add_argument("--completion-uri", default="https://example.invalid/cb")
    parser.add_argument("--cookie", action="store_true", help="set and report a session cookie")
    parser.add_argument("--cookie-value", default="one")
    parser.add_argument("--access-log", help="append 'METHOD path' per request to this file")
    parser.add_argument("--port-file", help="write the listening port here once bound")
    args = parser.parse_args()

    server = Server(("127.0.0.1", args.port), Handler)
    server.completion_uri = args.completion_uri
    server.use_cookie = args.cookie
    server.cookie_value = args.cookie_value
    server.access_log = args.access_log
    server.seen_cookies = []

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.cert, args.key)
    if args.require_client_cert:
        if not args.ca:
            parser.error("--require-client-cert needs --ca")
        context.verify_mode = ssl.CERT_REQUIRED
        context.load_verify_locations(args.ca)
    server.socket = context.wrap_socket(server.socket, server_side=True)

    port = server.socket.getsockname()[1]
    if args.port_file:
        with open(args.port_file, "w", encoding="utf-8") as handle:
            handle.write(str(port))
    sys.stderr.write(
        "listening port=%d client-certs=%s\n"
        % (port, "required" if args.require_client_cert else "off")
    )
    sys.stderr.flush()

    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        thread.join()
    except KeyboardInterrupt:
        server.shutdown()

    return 0


if __name__ == "__main__":
    sys.exit(main())
