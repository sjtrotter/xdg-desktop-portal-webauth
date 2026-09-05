# Testing

What has been run, how to run it again, and what none of it proves.

Three tiers, and they answer different questions:

| Tier | What it covers | What it needs |
|---|---|---|
| **1. Unit** | The rules: the completion match, the options, the storage partition's name, the redaction | Nothing. No display, no bus, no network. `meson test` |
| **2. End to end, headless** | Everything between a D-Bus call and a rendered page: the real frontend, the real backend, a real web engine, a real TLS handshake | Xvfb, a built frontend, a SoftHSM fixture |
| **3. Against a real identity provider** | The only thing that can answer whether this works for the case the project exists for | A tenant, a card, and a person. **Not done.** |

---

## Tier 1 — the unit tests

```console
$ meson setup build-backend backend && ninja -C build-backend
$ meson test -C build-backend
```

```
1/4 unit - xdg-desktop-portal-webauth:completion OK   8 subtests passed
2/4 unit - xdg-desktop-portal-webauth:options    OK   2 subtests passed
3/4 unit - xdg-desktop-portal-webauth:storage    OK   5 subtests passed
4/4 unit - xdg-desktop-portal-webauth:redact     OK   7 subtests passed

Ok:                4
Fail:              0
```

`backend/tests/test-completion.c` carries the frontend's own fixtures, marked `FRONTEND`, so that
the two implementations of the completion rule can be seen to agree
([IMPL-INTERFACE.md](IMPL-INTERFACE.md)).

### With the sanitizers

```console
$ meson setup build-asan backend -Db_sanitize=address,undefined -Db_lundef=false
$ ninja -C build-asan && meson test -C build-asan
```

All four pass with LeakSanitizer on. If `libasan` is not installed system wide, unpack it into a
scratch directory and set `LIBRARY_PATH` (to link) and `LD_LIBRARY_PATH` (to run).

---

## Tier 2 — end to end, headless

### What it stands up

`tools/ui-smoke.sh` starts an Xvfb and runs `tools/dev-stack.sh` inside it. That, on a **private bus**
made by `dbus-run-session`, starts:

- `xdg-permission-store` from the frontend build — xdg-desktop-portal refuses to start without it;
- `tools/mtls-server.py`, a fixture identity provider on a port of its own choosing;
- the **development frontend** from the branch `experimental/certificate-webauthentication`, with
  `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication` and an `XDG_DESKTOP_PORTAL_DIR`
  holding this repository's `webauth.portal`, a symlink to every other `.portal` on the machine, and
  the machine's effective `portals.conf` with one line added;
- this backend;
- `tools/webauth-e2e.py`, which calls the **public** interface and nothing else.

Nothing touches the session bus and nothing opens a window on the session display.

### Prerequisites

```console
$ tools/softhsm-fixture.sh                      # the CA, the server certificate, the token, the PIN
$ export XDP_BUILD=/path/to/xdg-desktop-portal/build
$ export XVFB=/path/to/Xvfb XDOTOOL=/path/to/xdotool
```

If the frontend was built against a scratch prefix (libdex usually is), put its `LD_LIBRARY_PATH`
and `PKG_CONFIG_PATH` in `.xdp-env` at the top of this repository; `tools/dev-stack.sh` sources it.

### The fixture server's certificate, and the one trust override in the binary

The fixture's server certificate is issued by a fixture CA that nothing on the machine trusts —
correctly, and there is no way to make GnuTLS trust it for one process without editing the machine's
p11-kit or system trust. So the backend has a development-only option,
`--debug-trust-certificate HOST=FILE`, which pins one certificate for one host through
`webkit_network_session_allow_tls_certificate_for_host()`. `tools/dev-stack.sh` passes it; the
installed D-Bus service file does not, and there is no other trust override anywhere in this
backend — no "continue anyway", no TLS-errors policy option, nothing reachable from the bus.

### The five runs

Each is one command, and each was run on 2026-09-04 on Fedora 44 with WebKitGTK 2.52.5, GTK 4.22.4,
libadwaita 1.9.3, GLib 2.88.3, glib-networking 2.80 (GnuTLS backend), GnuTLS 3.8.13 and
p11-kit 0.26.5, against xdg-desktop-portal at `experimental/certificate-webauthentication`.

Every one of them also asserts, from the **server's** access log, that the completion URI was never
fetched. That is the guarantee the whole design exists for, and it is checked from the far end of
the wire rather than from the client's side.

#### (i) The plain flow, no client certificates

```console
$ tools/ui-smoke.sh
```

```
dev-stack.sh: fixture server on port 45259 (client certificates: off)
dev-stack.sh: frontend: web authentication portal provided
Start handle=/org/freedesktop/portal/desktop/request/1_15/webauthe2e
Response response=0
Response results=completion_uri
completion scheme=https host=example.invalid path=/cb
completion query keys=code,state
completion code length=32
completion state=echoed
PASS
dev-stack.sh: the completion URI was never fetched (/cb absent from the access log)
```

The completion URI is `https://example.invalid/cb`, which does not resolve and is not the fixture
server. It never had to: the navigation was matched and ignored before the engine fetched anything.

#### (ii) Mutual TLS, with the client certificate on a PKCS#11 token

```console
$ tools/ui-smoke.sh --mtls
```

The fixture server is wrapped with `ssl.CERT_REQUIRED` and the fixture CA, so a client that produces
no certificate does not complete the handshake — `curl` without `--cert` gets
`tlsv13 alert certificate required`.

```
dev-stack.sh: fixture server on port 33375 (client certificates: required)
Start handle=/org/freedesktop/portal/desktop/request/1_15/webauthe2e
Response response=0
Response results=completion_uri
completion query keys=code,state
completion code length=32
PASS
dev-stack.sh: the completion URI was never fetched (/cb absent from the access log)
```

and from the backend's own log, which is what says the token was actually used:

```
webauth-Message: certificate-challenge host=localhost
webauth-Message: certificate-answered provider=pkcs11 host=localhost
webauth-Message: certificate-pin-requested provider=pkcs11
webauth-Message: navigation outcome=matched uri=https://example.invalid:-1/[3]
webauth-Message: completed app_id=- reason=- seconds=0
```

The private key is `CKA_SENSITIVE` on the token, so the handshake signature was made by the module
inside WebKit's network process, addressed by URI. The PIN came from
`--client-cert-pin-file`, never from argv and never from a `pin-value` in the URI, which the backend
refuses.

This run was repeated with a backend built `-Db_sanitize=address,undefined`: it passes, with **no
sanitizer findings**.

#### (iii) The user cancels

```console
$ tools/ui-smoke.sh --cancel --start-path=/wait
```

`/wait` is a page that does not redirect, so the window is still open when xdotool sends Escape.

```
Response response=1
Response results=reason
Response reason=user_cancelled
PASS
```

#### (iv) Shared and ephemeral storage

The fixture server sets a cookie and reports, in the completion query, whether the engine **arrived**
carrying one from a previous run. Four runs share one `--data-home`, as an application and its
portal would share a store between sign-ins.

`--as-app` is needed here and nowhere else: an **unidentified** caller is narrowed to ephemeral by
the backend on purpose ([`backend/src/storage.h`](../backend/src/storage.h)), so reaching the shared
mode at all needs a caller the frontend can name. The flag puts the client in an
`app-<id>-<n>.scope` cgroup with a matching `.desktop` file, which is what a desktop environment does
when it launches an application and what `shared/xdp-app-info-host.c` reads.

```console
$ DH=/tmp/webauth-store; mkdir -p $DH
$ tools/ui-smoke.sh --cookie --session-mode=shared \
    --as-app=org.example.WebauthClient --data-home=$DH -- --expect-cookie no
$ tools/ui-smoke.sh --cookie --cookie-value=two --session-mode=shared \
    --as-app=org.example.WebauthClient --data-home=$DH -- --expect-cookie yes
$ tools/ui-smoke.sh --cookie --cookie-value=three --session-mode=ephemeral \
    --as-app=org.example.WebauthClient --data-home=$DH -- --expect-cookie no
$ tools/ui-smoke.sh --cookie --cookie-value=four --session-mode=shared \
    --as-app=org.example.WebauthClient --data-home=$DH -- --expect-cookie yes
```

```
(iv a) shared, run 1     completion cookie=no    PASS
(iv b) shared, run 2     completion cookie=yes   PASS
(iv c) ephemeral         completion cookie=no    PASS
(iv d) shared, run 4     completion cookie=yes   PASS
```

The store is `$XDG_DATA_HOME/xdg-desktop-portal-webauth/<app id>/{data,cache}`, created 0700, with
the cookie jar at `data/cookies.sqlite`. A `WebKitNetworkSession` with a data directory does **not**
persist cookies on its own; the cookie manager has to be given a file, and (iv b) is the test that
would fail if that line were removed.

#### (v) The frontend forwards `Close()` mid-flow

```console
$ tools/ui-smoke.sh --start-path=/wait \
    "--expect-backend-log=cancelled .*reason=request_closed" \
    -- --cancel-after 4000 --expect-no-response --wait 20000
```

```
Start handle=/org/freedesktop/portal/desktop/request/1_15/webauthe2e
Close /org/freedesktop/portal/desktop/request/1_15/webauthe2e
Response none=expected-after-close
PASS
dev-stack.sh: backend log matched: cancelled .*reason=request_closed
    webauth-Message: cancelled app_id=- reason=request_closed seconds=4
```

**No `Response` is emitted after `Close()`, and that is upstream's behaviour, not a gap here:**
`xdp_request_dex_handle_close()` unexports the request before forwarding the close, and
`xdp_request_dex_emit_response()` returns early for an unexported request. The application's
evidence that anything happened is that its `Close()` returned; the backend's evidence is the log
line above.

### The control: the frontend's own suite

The frontend must still pass its own tests against its python-dbusmock backend, unchanged by
anything here:

```console
$ cd /path/to/xdg-desktop-portal/tests
$ BUILDDIR=../build ./run-test.sh ./test_webauthentication.py -q
38 passed, 34 warnings in 14.78s
```

---

## Tier 3 — against a real identity provider

**Nothing in this repository has ever talked to Entra ID, and no card has ever been in a reader for
it.** Tier 2 is a rehearsal: a software token in a headless X server has no reader, no PIN retry
counter to spend, and nothing to pull out mid-handshake.

The run to do, once there is a tenant and a card, needs no code that is not already here. Start the
stack with `--live` so the window is on the real display, and point the client at the authority:

```console
$ tools/dev-stack.sh --live --no-e2e --keep
```

and then, from another terminal on the same session bus, with the card in the reader and the
certificate portal's module installed (or `--cert-adapter pkcs11` and the card's own token URI):

```console
$ tools/webauth-e2e.py \
    --start-uri 'https://login.microsoftonline.us/<tenant-id>/oauth2/v2.0/authorize?client_id=a85cf173-4192-42f8-81fa-777a763e6e2c&response_type=code&redirect_uri=https%3A%2F%2Flogin.microsoftonline.com%2Fcommon%2Foauth2%2Fnativeclient&scope=https%3A%2F%2Fwww.wvd.azure.us%2F.default%20openid%20profile%20offline_access&code_challenge_method=S256&code_challenge=<challenge>' \
    --completion-uri 'https://login.microsoftonline.com/common/oauth2/nativeclient' \
    --session-mode ephemeral --title 'Sign in to AVD' --timeout 300
```

Note what that command demonstrates and what it does not. The completion URI is the **commercial**
`nativeclient` URL even against the US Government authority, because there is no `.us` variant
([decisions/0002](decisions/0002-no-loopback-redirect.md)) — so the flow ends on a navigation to a
Microsoft-operated page that must never be loaded, which is exactly the case tier 2 rehearses with
`https://example.invalid/cb`.

What tier 3 would answer that tier 2 cannot:

- whether the authority's `certauth.<authority>` redirect raises the client-certificate challenge on
  a host the backend considers related to the page it is showing;
- whether a real PIV card's PKCS#11 module behaves the way SoftHSM does when WebKit's network
  process resolves the URI — a different module, a different login model, a reader that can be
  slow or absent;
- whether the identity provider accepts a portal-owned WebKitGTK engine at all, or classifies it as
  an embedded user-agent and refuses (see the honest caveat in
  [`backend/src/webkit-session.h`](../backend/src/webkit-session.h));
- what the window looks like to somebody who has to decide whether to trust it.

Until that has been done, the correct description of this backend is "it works against a fixture".
