#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Fixture redirects echo state and issue a random code; completion must be intercepted before load.
# Queries must stay out of logs and page content; see docs/TESTING.md.

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

    def _client_cn(self):
        # Read the certificate accepted by TLS from the socket; log only its fixture common name.
        try:
            peer = self.connection.getpeercert()
        except (AttributeError, ValueError):
            return None
        if not peer:
            return None
        for rdn in peer.get("subject", ()):
            for key, value in rdn:
                if key == "commonName":
                    return value
        return None

    def _note(self, path):
        line = f"{self.command} {path}\n"
        cn = self._client_cn()
        sys.stderr.write(f"request {line}")
        if cn is not None:
            sys.stderr.write(f"client-cn={cn}\n")
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
                # Check the cookie on the first request to detect state persisted across flows.
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
            # This page keeps the window open for cancellation tests.
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
