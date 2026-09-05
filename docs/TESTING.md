# Testing

What has been run, how to run it again, and what none of it proves.

Three tiers, and they answer different questions:

| Tier | What it covers | What it needs |
|---|---|---|
| **1. Unit** | The rules: the completion match, the options, the storage partition's name, the redaction | Nothing. No display, no bus, no network. `meson test` |
| **2. End to end, headless** | Everything between a D-Bus call and a rendered page: the real frontend, the real backend, a real web engine, a real TLS handshake | Xvfb, a built frontend, a SoftHSM fixture |
| **2b. Both portals at once** | The primary path: the certificate comes from the **certificate portal**, through its client-side PKCS#11 module, and the card, the chooser and the PIN never enter this process | the above, plus a built `xdg-desktop-portal-certificate` and its fixture |
| **3. Against a real identity provider** | The only thing that can answer whether this works for the case the project exists for | A tenant, a card, and a person. **Not done.** |

**The portal provider is the primary path and the pkcs11 provider is the fallback.** Tier 2 runs
the fallback, because it is the one that needs no second service; tier 2b runs the path this
project exists for. A change that passes tier 2 and not tier 2b has not been tested.

---

## Tier 1 — the unit tests

```console
$ meson setup build-backend backend && ninja -C build-backend
$ meson test -C build-backend
```

```
1/5 unit - xdg-desktop-portal-webauth:completion OK   8 subtests passed
2/5 unit - xdg-desktop-portal-webauth:options    OK   2 subtests passed
3/5 unit - xdg-desktop-portal-webauth:storage    OK   5 subtests passed
4/5 unit - xdg-desktop-portal-webauth:redact     OK   7 subtests passed
5/5 unit - xdg-desktop-portal-webauth:harden     OK   2 subtests passed

Ok:                5
Fail:              0
```

`test-harden.c` covers the counting of the window in which `PR_SET_DUMPABLE(0)` yields so that
xdg-desktop-portal can identify this process — an unbalanced pair would leave it open for the life
of the process, which is exactly the exposure the window exists to bound. It deliberately does not
harden the test binary.

`backend/tests/test-completion.c` carries the frontend's own fixtures, marked `FRONTEND`, so that
the two implementations of the completion rule can be seen to agree
([IMPL-INTERFACE.md](IMPL-INTERFACE.md)).

### With the sanitizers

```console
$ meson setup build-asan backend -Db_sanitize=address,undefined -Db_lundef=false
$ ninja -C build-asan && meson test -C build-asan
```

All five pass with LeakSanitizer on. If `libasan` is not installed system wide, unpack it into a
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

## Tier 2b — both portals at once, and the card behind the web view

This is the run the project exists for, and it is the one to run first when something has changed.
`tools/portal-stack.sh` puts **one private bus inside one headless X server** and stands up both
portals against each other:

- `xdg-permission-store`, and the **development frontend** with **both** gates:
  `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=certificate,web-authentication`, and an
  `XDG_DESKTOP_PORTAL_DIR` holding this repository's `webauth.portal`, the sibling's
  `certificate.portal`, a symlink to every other `.portal` on the machine, and the machine's
  effective `portals.conf` with **two** lines added;
- `xdg-desktop-portal-certificate` from `${CERTIFICATE_REPO:-../xdg-desktop-portal-certificate}`,
  with `--module` pointing at that repository's SoftHSM fixture and `--pin-prompt=gtk`;
- a `$XDG_CONFIG_HOME/pkcs11/modules/xdg-desktop-portal-certificate.module` of this run's own,
  naming the **built** module, so that p11-kit offers the portal token to this backend and to
  WebKit's network process without anything being installed system wide;
- this backend with `--cert-adapter portal` — named, not `auto`, so that a run in which the portal
  provider is unavailable **fails** instead of falling through to the pkcs11 provider and proving
  nothing;
- `tools/mtls-server.py` demanding a client certificate and trusting **only the fixture card's own
  certificate**, so a handshake completes only if the certificate the module handed over is that
  one;
- `tools/webauth-e2e.py` on the public interface.

`xdotool` answers the chooser and the PIN prompt with the sibling's driver loop — it answers
whatever appears rather than following a script, because *how many* windows appear is one of the
things this run exists to find out. The PIN goes through the environment and into `xdotool`'s stdin,
never argv.

### Prerequisites

```console
$ (cd ../xdg-desktop-portal-certificate && meson setup build && ninja -C build)
$ ../xdg-desktop-portal-certificate/tools/softhsm-fixture.sh
$ export XDP_BUILD=/path/to/xdg-desktop-portal/build
$ export XVFB=/path/to/Xvfb XDOTOOL=/path/to/xdotool
```

### The run

```console
$ tools/portal-stack.sh
```

Run on 2026-09-04, Fedora 44, WebKitGTK 2.52.5, GTK 4.22.4, libadwaita 1.9.3, GLib 2.88.3,
glib-networking (GnuTLS backend), GnuTLS 3.8.13, p11-kit 0.27, SoftHSM 2, against
xdg-desktop-portal at `experimental/certificate-webauthentication`:

```
=== hop by hop ===
  ok    1 webkit asked for a certificate   certificate-challenge host=localhost
  ok    2 the portal provider took it      certificate-challenge provider=portal
  ok    2 this process became identifiable process-hardening outcome=identifiable-begin
  ok    3 p11-kit loaded the module here   xdg-desktop-portal-webauth:227563): pkcs11-portal-certificate-DEBUG
  ok    3 the module got a credential here xdg-desktop-portal-webauth:227563): ... grant acquired
  ok    3 the module ran in the network process (process:227639): ... grant acquired
  ok    4 the frontend identified the caller chooser-shown app_id=(none) identity=unidentified
  ok    5 the backend created a grant      grant-created
  ok    6 the PIN was accepted             login-ok
  ok    6 the signature was produced       operation-completed
  ok    2 the hardening window closed      process-hardening outcome=identifiable-end
  ok    2 this backend answered from portal certificate-answered provider=portal
  ok    7 the server saw the card's CN     client-cn=Portal Test User
  ok    8 the flow completed               PASS
  ok    completion never fetched           /cb absent from the access log

module instances: 2
choosers granted: 2
PIN prompts:      1
signatures:       1

portal-stack: PASS
```

The run now prints one line above these, `0 the portal came after the challenge`, added on
2026-09-05 with the change that made the provider lazy. It is not a log line but an ordering
assertion over `backend.log`: the line number of the first `certificate-challenge` against the line
number of the first `pkcs11-portal-certificate` line, from either module instance. It fails if
anything reached the certificate portal before WebKit asked for a certificate.

Each line is from the process that made it: `backend.log` for this backend and for both module
instances (the network process inherits this process's stderr), `certificate.log` for the sibling,
`server.log` for the identity provider, `access.log` for the guarantee. Nothing here is inferred
from an exit code.

The signature is `RSA_PSS`: that is what TLS 1.3 asks for, and it went through `C_SignInit`/`C_Sign`
in the module, the portal's `Sign` on the public interface, and `C_Sign` on the SoftHSM token
inside the certificate backend.

### The finding: ONE handshake, TWO choosers

`module instances: 2` and `choosers granted: 2` are not a bug in either service and they are not
noise. One TLS handshake needs the certificate in **two processes**:

1. **this backend's UI process** builds the `GTlsCertificate`, because
   `webkit_credential_new_for_certificate()` takes an object and not a URI. Building it imports the
   certificate object, which loads the module here and makes it acquire a grant;
2. **WebKit's network process** owns the handshake and re-resolves the URI itself — spike S2's
   result, and the whole reason the design is a module rather than a brokered `Sign`. That is a
   second p11-kit module instance, in a second process, with a grant of its own.

Two module instances mean two `CreateSession`/`AcquireCredential` pairs, and therefore **two
choosers asking the same question**. The user answers the same prompt twice for one sign-in. Only
one PIN prompt appears, because only the network process signs.

**Both of them stand inside the challenge**, and hop 0 is the check that says so: nothing in this
backend reaches the certificate portal until WebKit asks for a certificate, so the first line the
module writes to `backend.log` comes after `certificate-challenge`. The first chooser belongs to
this process building the `GTlsCertificate` in the `authenticate` handler; the second belongs to the
network process re-resolving the URI a moment later. The whole handshake waits for both, and the
gap between them is however long a chooser takes to answer — not a fixed interval, and not something
that happens before the user has done anything.

Neither half can be removed as things stand. The UI process cannot hand WebKit a URI, and the
network process cannot be handed the UI process's grant: a grant belongs to the D-Bus peer that
acquired it, which is the point of the design. What could fix it lives upstream or in the sibling —
a way for one application to reuse a live grant across its own processes, or a WebKit API that
takes a PKCS#11 URI. **It is a real UX defect and it should be recorded as one.**

### The finding: a second `Start` shows nothing at all

```console
$ tools/portal-stack.sh --second-start
```

Two complete sign-ins against the same backend process:

```
second Start: grants before=2 after=2 (a new chooser is a new grant)

module instances: 2
choosers granted: 2
PIN prompts:      1
signatures:       1
```

The second `Start` produced **no chooser, no PIN prompt, no signature and no certificate challenge
at all** — the backend's log has one `certificate-challenge` for two sign-ins. Two things caused
that, and they are worth telling apart:

- the module keeps its grant until `C_Finalize`, its expiry, or the portal invalidating it, so
  neither module instance had to ask again. This is deliberate: releasing the grant with the last
  PKCS#11 session put the chooser up on every object import and every signature;
- WebKit reused the connection to the identity provider, so the second flow did not perform a TLS
  handshake at all. The server saw `GET /start` and `GET /login` a second time on a connection that
  had already presented the card's certificate.

Note that the session mode was `ephemeral` both times, and it made no difference: an ephemeral
`WebKitNetworkSession` is a fresh **website data store**, not a fresh network process, so the module
instance and its grant survive it. A run that wants a second chooser has to restart the backend.

### The finding: cancelling the chooser used to hang, and now does not

```console
$ tools/portal-stack.sh --cancel-chooser --     --expect-response 2 --expect-reason no_certificate_adapter --no-require-code
```

```
Response response=2
Response results=reason
Response reason=no_certificate_adapter
PASS
```

Escape at the certificate chooser → `AcquireCredential` answers cancelled → the module has no
credential → `g_tls_certificate_new_from_pkcs11_uris()` fails → this backend declines the challenge
→ the handshake fails → the transaction ends, **1.9 seconds after the Escape**.

Before the fix in `webkit-session.c` it did not end at all: `webkit_authentication_request_cancel()`
makes WebKit report the load as `WEBKIT_NETWORK_ERROR_CANCELLED`, which this backend treated as its
own interception of the completion URI, so nothing finished the transaction. The window stayed open
for 120 seconds until the application's own timeout, and what the application was finally told was
`request_closed` rather than why.

`no_certificate_adapter` is the honest answer and not an ideal one: what reaches this backend is
GnuTLS reporting that the object was not available, and it cannot tell "the user refused" from "no
provider could run". [IMPL-INTERFACE.md](IMPL-INTERFACE.md) says so.

### The other PIN prompt

```console
$ tools/portal-stack.sh --pin-prompt=system
```

runs the same stack against the sibling's system-prompter path instead of its own window: its
`certificate-test-prompter` owns `org.gnome.keyring.SystemPrompter` on the private bus and answers
with the fixture PIN. Nothing is typed and `xdotool` is not used for the PIN, so the run also proves
that path needs no display of the certificate backend's own.

---

## Tier 3 — against a real identity provider

**Nothing in this repository has ever talked to Entra ID, and no card has ever been in a reader for
it.** Tier 2 is a rehearsal: a software token in a headless X server has no reader, no PIN retry
counter to spend, and nothing to pull out mid-handshake.

### Live run against Entra

**Do not run this from a script and do not run it unattended.** It uses the real session bus, the
real certificate backend with no `--module`, the real card in the real reader, and a real tenant. It
spends a real PIN retry counter if the PIN is wrong.

#### What has to be true first

- the card is in the reader and `p11tool --list-tokens` shows it (OpenSC through p11-kit; the
  certificate backend finds it the same way);
- `xdg-desktop-portal-certificate` is installed, or built and reachable through
  `$CERTIFICATE_REPO`. Its p11-kit module file does not have to be installed by hand: `--live`
  installs it itself, once, into the **real** per-user configuration —
  `$XDG_CONFIG_HOME/pkcs11/modules/xdg-desktop-portal-certificate.module` (default
  `~/.config/pkcs11/modules`), created 0700/0600, and only if nothing is already there. The file
  carries `enable-in: xdg-desktop-portal-webauth, WebKitNetworkProcess` — **not** `disable-in`,
  because `pkcs11.conf(5)` says not to set both on one module and p11-kit's
  `is_module_enabled_unlocked()` (`p11-kit/modules.c`) takes the `enable-in` branch and never
  consults `disable-in` when both are present. An `enable-in` allowlist of exactly the two
  processes that need this module is what keeps it off every other p11-kit consumer on the
  machine — ssh, curl, browsers included — which is the reason it was never safe to install
  globally with no restriction. Verify with `p11-kit list-modules` (must not show it) versus
  `exec -a xdg-desktop-portal-webauth p11-kit list-modules` (must show it); remove it with
  `tools/portal-stack.sh --uninstall-module`, which refuses to touch a file it did not write;
- the frontend on the branch is what owns `org.freedesktop.portal.Desktop` — `--live` takes the
  name for the duration and the system portal comes back by activation afterwards;
- `<tenant>` is the directory (tenant) id of the US Government tenant.

#### The command

```console
$ tools/portal-stack.sh --live --pin-prompt=system -- \
    --entra-authorize \
    --entra-authority 'https://login.microsoftonline.us/<tenant>' \
    --entra-client-id a85cf173-4192-42f8-81fa-777a763e6e2c \
    --entra-scope 'https://www.wvd.azure.us/.default openid profile offline_access' \
    --entra-verifier-file "$XDG_RUNTIME_DIR/webauth-code-verifier" \
    --completion-uri https://login.microsoftonline.com/common/oauth2/nativeclient \
    --session-mode ephemeral \
    --title 'Sign in to AVD' \
    --timeout 300 \
    --wait 300000
```

`--entra-authorize` builds the authorize URL rather than having it typed: the AVD public client id,
`response_type=code`, the scope, PKCE **S256**, a random `state`, and `prompt=login` so the run
tests the card rather than a remembered session. It prints only the scheme, host and path of the URL
it built. **The code verifier is written to `--entra-verifier-file` with mode 0600 and is never
printed**; the file is refused if it already exists. With the authorization code, that verifier is
the whole credential — delete it after the exchange.

The equivalent, if the URL is to be pasted rather than built:

```console
$ tools/portal-stack.sh --live --pin-prompt=system -- \
    --start-uri '<authorize URL>' \
    --completion-uri https://login.microsoftonline.com/common/oauth2/nativeclient \
    --session-mode ephemeral
```

#### What the card is actually for

Nothing in the authorize URL asks for a certificate. The card is used **later**: when the tenant's
conditional access policy requires certificate-based authentication, Entra redirects the sign-in
page to `certauth.login.microsoftonline.us`, and that host demands a TLS client certificate. That
is the challenge this backend answers — from the certificate portal's token, with the chooser and
the PIN prompt drawn by that service.

`--pin-prompt=system` is the right choice for a real run because it puts the PIN field in the
desktop's own prompter rather than in a window this project drew.

#### What to watch, and where

| | |
|---|---|
| the challenge arrived, and on which host | this backend's log: `certificate-challenge host=certauth.login.microsoftonline.us` |
| the portal provider took it | `certificate-challenge provider=portal` |
| the module reached the portal | `pkcs11-portal-certificate-DEBUG: grant acquired` — set `G_MESSAGES_DEBUG="webauth pkcs11-portal-certificate"` |
| the card signed | the certificate backend's `login-ok` and `operation-completed` |
| the flow ended without fetching the redirect | `navigation outcome=matched`, and `completed` |

Expect **two choosers, both after the redirect to `certauth.`**, for the reason tier 2b measures:
the certificate is built in this backend's process and used in WebKit's network process. Neither can
appear before the challenge — a chooser on the sign-in page, before the redirect, would not be this
backend, which reaches the portal only from the `authenticate` handler; hop 0 of the headless run is
that ordering asserted from the log. On a real card that is two grants and, unlike the
fixture, possibly two PIN prompts — the second grant is a second login on the token, and whether the
backend's session is still logged in decides it. **That is the first thing to find out.**

#### What only this run can answer

The completion URI is the **commercial** `nativeclient` URL even against the US Government
authority, because there is no `.us` variant
([decisions/0002](decisions/0002-no-loopback-redirect.md)) — so the flow ends on a navigation to a
Microsoft-operated page that must never be loaded, which is exactly the case tier 2 rehearses with
`https://example.invalid/cb`. That much is already proven. What is not:

- whether the authority's `certauth.<authority>` redirect raises the client-certificate challenge on
  a host the backend considers related to the page it is showing;
- whether a real PIV card's PKCS#11 module behaves the way SoftHSM does when WebKit's network
  process resolves the URI — a different module, a different login model, a reader that can be
  slow or absent;
- whether the second grant costs a second PIN entry on a real card, which decides whether the
  two-chooser finding above is an annoyance or a blocker;
- whether the identity provider accepts a portal-owned WebKitGTK engine at all, or classifies it as
  an embedded user-agent and refuses (see the honest caveat in
  [`backend/src/webkit-session.h`](../backend/src/webkit-session.h));
- what the window looks like to somebody who has to decide whether to trust it.

Until that has been done, the correct description of this backend is "it works against a fixture".
