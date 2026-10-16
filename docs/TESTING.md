# Testing

Dated transcripts establish results only for the recorded environment and revision.

| Tier | Coverage | Requirements |
|---|---|---|
| 1. Unit | Completion, options, storage paths, redaction and hardening | Built backend; no display, bus or network |
| 2. Headless backend | Frontend, backend, engine and TLS handshake through `pkcs11` | Xvfb, built frontend and SoftHSM fixture |
| 2b. Both portals | Certificate module, chooser, PIN and signing through `portal` | Tier 2 tools plus Certificate backend/module and its fixture |
| 2c. Consumer | Entra client against a mock authority without certificate service | Client checkout, Xvfb and built frontend/backend |
| 3. Live identity provider | Tenant policy and real card behavior | Tenant, card, reader and operator |

Run both tier 2 and tier 2b when changing certificate behavior. Tier 2c is owned by
[entra-token-helper](https://github.com/sjtrotter/entra-token-helper); it shares the public
portal interface with the backend fixtures.

## Tier 1: unit tests

```console
$ meson setup build && ninja -C build
$ meson test -C build
```

[The test guide](../tests/README.md) lists the `unit` suite, hardening checks and shared
completion fixtures. The client's suites run in
[its repository](https://github.com/sjtrotter/entra-token-helper).

### Sanitizers

```console
$ meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false
$ ninja -C build-asan && meson test -C build-asan
```

The recorded sanitizer run passed all five tests with LeakSanitizer enabled. If libasan is unavailable, unpack it into a
scratch directory and set `LIBRARY_PATH` (to link) and `LD_LIBRARY_PATH` (to run).

---

## Tier 2: headless end-to-end tests

### Stack

`tools/ui-smoke.sh` supplies Xvfb and runs `tools/dev-stack.sh` on a private bus created by
`dbus-run-session`. The stack includes the frontend build's `xdg-permission-store`,
`tools/mtls-server.py`, the frontend from `experimental/integration`, this backend and
`tools/webauth-e2e.py` calling the public interface.

The launcher builds a temporary `XDG_DESKTOP_PORTAL_DIR` with `webauth.portal`, symlinks to
other installed declarations and the effective `portals.conf` with webauth routing added.
It appends `WebAuthentication1` to `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL`, preserving existing
names. Manual frontend startup requires the same opt-in and a configured backend. Use comma
separators without spaces.

The default headless run leaves the desktop session bus and display untouched. `dev-stack.sh`
without ui-smoke uses the selected display. Its `--live` mode uses the real session bus.

### Prerequisites

Build this backend first. The frontend build must contain
`desktop-portal/xdg-desktop-portal` and `document-portal/xdg-permission-store`.
The fixture needs SoftHSM 2, `certtool`, `pkcs11-tool` and OpenSSL; the stack needs Python with GObject
introspection, `dbus-run-session` and Xvfb. Cancellation needs xdotool. Set `BACKEND` to override
`build/src/xdg-desktop-portal-webauth`, and `SOFTHSM_DIR` to override
`${TMPDIR:-/tmp}/xdp-webauth-softhsm`.

```console
$ tools/softhsm-fixture.sh                      # the CA, the server certificate, the token, the PIN
$ export XDP_BUILD=/path/to/xdg-desktop-portal/build
$ export XVFB=/path/to/Xvfb XDOTOOL=/path/to/xdotool
```

For a frontend built against a scratch prefix, put exported LD_LIBRARY_PATH and PKG_CONFIG_PATH
values in `.xdp-env` at the repository root. Both launchers source it; XDP_ENV overrides its path.

### Fixture certificate trust

`--debug-trust-certificate HOST=FILE` pins the fixture server certificate for one host through
`webkit_network_session_allow_tls_certificate_for_host()`. `dev-stack.sh` passes it for localhost.
The installed D-Bus service does not pass it, and no trust override is reachable through D-Bus.

### Recorded runs

Recorded 2026-09-04 on Fedora 44 with WebKitGTK 2.52.5, GTK 4.22.4,
libadwaita 1.9.3, GLib 2.88.3, glib-networking 2.80 (GnuTLS backend), GnuTLS 3.8.13 and
p11-kit 0.26.5, against xdg-desktop-portal at `experimental/certificate-webauthentication`.
Use `experimental/integration` for current runs.

The default completion, `https://example.invalid/cb`, targets a different server from the
localhost fixture. The fixture access log cannot show requests to that host. A missing `/cb`
entry proves only that the fixture received no such request; it proves neither absence of a
completion fetch elsewhere nor absence of DNS lookup. The matched-navigation log and returned
completion provide interception evidence. The transcripts below retain the scripts' stronger
access-log wording; it is not a verified conclusion.

#### (i) Plain flow without client certificates

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

The returned completion URI is `https://example.invalid/cb`.

#### (ii) Mutual TLS with a PKCS#11 token

```console
$ tools/ui-smoke.sh --mtls
```

The fixture server uses ssl.CERT_REQUIRED with its CA. A client without a certificate cannot
complete the handshake; curl without --cert receives `tlsv13 alert certificate required`.

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

The backend log records token use:

```
webauth-Message: certificate-challenge host=localhost
webauth-Message: certificate-answered provider=pkcs11 host=localhost
webauth-Message: certificate-pin-requested provider=pkcs11
webauth-Message: navigation outcome=matched uri=https://example.invalid:-1/[3]
webauth-Message: completed app_id=- reason=- seconds=0
```

The token key has CKA_SENSITIVE. WebKit's network process performs the handshake signature through
its module. The backend reads the PIN from `--client-cert-pin-file`;
[Security](SECURITY.md#pkcs11-provider) documents the key-URI validation gap.
This run also passed with `-Db_sanitize=address,undefined`, without sanitizer findings.

#### (iii) User cancellation

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

The fixture server sets a cookie and reports, in the completion query, whether the engine arrived
carrying one from a previous run. Four runs share one `--data-home`, as an application and its
portal would share a store between sign-ins.

The backend forces an empty app ID to ephemeral ([`src/storage.h`](../src/storage.h)).
`--as-app` supplies an identifiable host caller for shared-storage checks. It requires a
delegated systemd user cgroup and creates an app-<id>-<n>.scope with a matching desktop file,
which the frontend's shared/xdp-app-info-host.c uses to derive a host identity.

```console
$ DH=/tmp/webauth-store
$ mkdir -p "$DH"
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

The cookie manager needs an explicit persistent file; a WebKitNetworkSession data directory
alone does not persist cookies. [Storage rules](SECURITY.md#fixed-behavior-and-storage)
define the persistent paths and permissions.

#### (v) Forwarded `Close()` during a flow

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

No Response follows public Close. `xdp_request_dex_handle_close()` unexports the Request
before forwarding, and `xdp_request_dex_emit_response()` skips unexported requests. The
application observes its Close return; the backend logs `request_closed`.

### Frontend control suite

Run the frontend's tests against its python-dbusmock backend:

```console
$ cd /path/to/xdg-desktop-portal/tests
$ BUILDDIR=../build ./run-test.sh ./test_webauthentication.py -q
```

Use the `experimental/integration` branch for current runs.

---

## Manual invocation and rejection checks

Start a private-bus stack with the selected display:

```console
$ tools/dev-stack.sh --no-e2e --keep
```

In another terminal, set DBUS_SESSION_BUS_ADDRESS to the printed bus address and run:

```console
$ tools/trigger-webauthentication.sh all
```

The default URIs use `example.invalid`; `START_URI` and `COMPLETION_URI` override them.
The script supports `version`, `introspect`, `start`, `reject-scheme`, `reject-mode` and `monitor`.
Use it for invocation and rejection checks. Its short-lived `gdbus call` connection exits after
Start, cancelling the request under the caller-disconnection contract. The monitor uses another
connection and cannot keep that request alive.

For completion testing, use the persistent client on the printed bus:

```console
$ tools/webauth-e2e.py --start-uri '<start URI>' --completion-uri '<completion URI>'
```

## Tier 2b: both portals

`tools/portal-stack.sh` runs both backends and the frontend on one private bus inside Xvfb.

`portal-stack.sh` enables `WebAuthentication1,Certificate1` in the frontend
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` list and preserves caller-supplied names. The stack includes:

- `xdg-permission-store`, and the development frontend with both experimental interfaces
  backed, through an
  `XDG_DESKTOP_PORTAL_DIR` holding this repository's `webauth.portal`, the sibling's
  `certificate.portal`, a symlink to every other `.portal` on the machine, and the machine's
  effective `portals.conf` with two lines added;
- `xdg-desktop-portal-certificate` from `${CERTIFICATE_REPO:-../xdg-desktop-portal-certificate}`,
  with `--module` pointing at that repository's SoftHSM fixture and `--pin-prompt=gtk`;
- a `$XDG_CONFIG_HOME/pkcs11/modules/xdg-desktop-portal-certificate.module` of this run's own,
  naming the built module, so that p11-kit offers the portal token to this backend and to
  WebKit's network process without anything being installed system wide;
- this backend with `--cert-adapter portal`, so provider unavailability fails the test;

- `tools/mtls-server.py` demanding a client certificate and trusting only the fixture card's own
  certificate, so a handshake completes only if the certificate the module handed over is that
  one;
- `tools/webauth-e2e.py` on the public interface.

`xdotool` responds to chooser and PIN windows as they appear. The fixture PIN travels through
the environment and xdotool stdin, without an argv value.

The driver runs during a TLS handshake. Slow interaction can produce
`load-failed detail=Operation_was_cancelled`, response `2` / `load_failed`, even when credential
acquisition succeeds. Recorded timings were 11.9 seconds with one-second per-keystroke delays
(failed) and 7.9 seconds with the current pacing. Network-process startup and enumeration took
8.5 seconds in another run. Headless focus requires a mapped window; the driver's handle function
retries for ten seconds. Recheck timing failures on an idle machine. The fixture system prompter
answers without typed PIN interaction.

### Prerequisites

Build this backend and the Certificate backend/module first. Set CERTIFICATE_REPO when its
checkout is elsewhere; CERT_BUILD, CERT_BACKEND and CERT_MODULE_SO override the build and
binaries. The Certificate fixture defaults to `${TMPDIR:-/tmp}/xdp-certificate-softhsm`;
override with CERT_SOFTHSM_DIR. The script checks that fixture even in live mode. OpenSSL and
Python GObject introspection are required. Private-mode UI driving needs xdotool.

```console
$ (cd ../xdg-desktop-portal-certificate && meson setup build && ninja -C build)
$ ../xdg-desktop-portal-certificate/tools/softhsm-fixture.sh
$ export XDP_BUILD=/path/to/xdg-desktop-portal/build
$ export XVFB=/path/to/Xvfb XDOTOOL=/path/to/xdotool
```

### Fixture run

```console
$ tools/portal-stack.sh
```

Recorded 2026-09-05 on Fedora 44, WebKitGTK 2.52.5, GTK 4.22.4, libadwaita 1.9.3, GLib 2.88.3,
glib-networking (GnuTLS backend), GnuTLS 3.8.13, p11-kit 0.27, SoftHSM 2, against
xdg-desktop-portal at `experimental/certificate-webauthentication`:

```
=== hop by hop ===
  ok    1 webkit asked for a certificate   certificate-challenge host=localhost
  ok    2 the portal provider took it      certificate-challenge provider=portal
  ok    2 this process became identifiable process-hardening outcome=identifiable-begin
  ok    3 p11-kit loaded the module here   xdg-desktop-portal-webauth:378854): pkcs11-portal-certificate-DEBUG
  ok    3 the module got a credential here xdg-desktop-portal-webauth:378854): ... grant acquired
  ok    3 the module ran in the network process (process:378930): ... grant acquired
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

`backend.log` includes this backend and both module instances, since the network process inherits
stderr. `certificate.log` records the Certificate backend, `server.log` the identity provider and
`access.log` requests received by the fixture. The [access-log limitation](#recorded-runs)
also applies here. TLS 1.3 requests RSA_PSS; the signature passes
through C_SignInit/C_Sign in the module, public Sign and C_Sign on SoftHSM.

### Two grants per fresh handshake

The UI process constructs the GTlsCertificate, importing the certificate through one module
instance. WebKit's network process resolves the URI and signs through another. Each instance
has a separate D-Bus connection and grant, producing two CreateSession/AcquireCredential pairs,
two choosers about three seconds apart and one PIN prompt.

WebKit requires a certificate object, and grants cannot be transferred between
D-Bus peers. No WebKit API removes the UI import. App-info-bound grants could reduce duplicate
consent for sandbox callers; see the Certificate portal's ADR 0011. The excluded process-tree
delegation branch is `experimental/certificate-webauthentication+delegation`;
[Security](SECURITY.md) describes its limits.

`choosers granted` counts grant-created events. Expect two on a fresh run; a third indicates a
regression such as chain verification acquiring a credential.

### Repeated Start

```console
$ tools/portal-stack.sh --second-start
```

Two complete sign-ins against the same backend process:

```
second Start: grants before=2 after=2

module instances: 2
choosers granted: 2
PIN prompts:      1
signatures:       1
```

The second Start produced no chooser, PIN prompt, signature or certificate challenge. Both module
grants remained valid until expiry, invalidation or C_Finalize. The server saw GET /start and
GET /login over the first flow's authenticated connection, so no new handshake occurred.

Both Starts used ephemeral sessions. Each session has a fresh website data store, while the
network process, module grants and connections can survive. Restart the backend when a fresh
authenticated transport is required.

### Chooser cancellation

```console
$ tools/portal-stack.sh --cancel-chooser -- \
    --expect-response 2 --expect-reason credential_unavailable --no-require-code
```

```
Response response=2
Response results=reason
Response reason=credential_unavailable
PASS
```

Escape cancels AcquireCredential, the module returns no credential, certificate construction
fails and the handshake ends. Measured time from Escape to terminal result: 1.9 seconds.
GnuTLS exposes the same unavailable-object failure for user refusal and unavailable providers,
so the result is `2` / `credential_unavailable`. [The impl guide](IMPL-INTERFACE.md) describes
this diagnostic limit.

### System PIN prompt

```console
$ tools/portal-stack.sh --pin-prompt=system
```

This runs the same stack using the sibling's system-prompter path. Its certificate-test-prompter
owns org.gnome.keyring.SystemPrompter on the private bus and answers with the fixture PIN.
The PIN needs no typed interaction or Certificate-backend PIN window.

---

## Tier 2c: consumer integration

This tier lives in the client's repository,
[entra-token-helper](https://github.com/sjtrotter/entra-token-helper), and is run from a checkout of
it. `tools/entra-e2e.sh` starts Xvfb, a private bus, xdg-permission-store, the development
frontend, this backend with no certificate adapter and an Entra-shaped mock authority.
It runs entra-token-helper as an
ordinary application calling the public interface. It finds this backend through `WEBAUTH_REPO`
(default `../xdg-desktop-portal-webauth`) and `BACKEND` (default
`$WEBAUTH_REPO/build/src/xdg-desktop-portal-webauth`), and the frontend through `XDP_BUILD`.

The fixture checks:

- A cached token request with `--prompt never` leaves the mock event log unchanged, without a
  portal call, window or discovery fetch.
- A forced `interaction_required` PoP refresh opens a second portal transaction whose shared
  session cookie permits a click-through sign-in.
- A missing session bus produces exit 40. A caller cannot distinguish all unavailable portal
  configurations from absence of the portal.

The projects have no build-time or fixture dependency on each other beyond selecting a built
backend for this run.

---

## Tier 3: live identity provider

On 2026-09-05 the backend completed a live Entra ID sign-in at 10:42 and the full
FreeRDP-to-AVD chain at 14:21. Entra certauth requested a certificate, the Certificate portal
prompted for a PIN, and a real PIV card produced an RSA-PSS signature. The completion navigation
was intercepted with the authorization code. The ARM token came from the keyring cache; RDS
proof-of-possession required interactive re-authentication. Evidence covers one PIV card, one
reader, OpenSC, GNOME 50 on Wayland and one US Government tenant. A second card, a PIN-pad reader,
card removal, KDE and a Flatpak runtime remain untested.

### Live run against Entra

Attend this run and invoke it manually. It uses the session bus, Certificate backend without
`--module`, card, reader and tenant. A wrong PIN consumes a card retry.

#### Prerequisites

- the card is in the reader and `p11tool --list-tokens` shows it (OpenSC through p11-kit; the
  certificate backend finds it the same way);
- `xdg-desktop-portal-certificate` is installed, or built and reachable through
  `$CERTIFICATE_REPO`. Its p11-kit module file does not have to be installed by hand: `--live`
  installs it itself, once, into the real per-user configuration;
  `$XDG_CONFIG_HOME/pkcs11/modules/xdg-desktop-portal-certificate.module` (default
  `~/.config/pkcs11/modules`), created 0700/0600, and only if nothing is already there. The file
  uses `enable-in: xdg-desktop-portal-webauth, WebKitNetworkProcess`.
  pkcs11.conf(5) forbids setting both enable-in and disable-in. In p11-kit/modules.c,
  is_module_enabled_unlocked() takes the enable-in branch when both are present. The allowlist
  restricts loading to this backend and WebKitNetworkProcess, excluding unrelated consumers
  such as ssh, curl and browsers. Verify with `p11-kit list-modules` (must not show it) and
  `exec -a xdg-desktop-portal-webauth p11-kit list-modules` (must show it). Remove it with
  `tools/portal-stack.sh --uninstall-module`, which refuses to touch a file it did not write;

- the frontend on the branch is what owns `org.freedesktop.portal.Desktop`; `--live` takes the
  name for the duration and the system portal comes back by activation afterwards;
- `<tenant>` is the directory (tenant) id of the US Government tenant.

#### Command

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

`--entra-authorize` builds the authorize URL with the AVD public client id,
`response_type=code`, the scope, PKCE S256, a random `state`, and `prompt=login` so the run
requires fresh sign-in. It prints only the scheme, host and path of the URL
it built. The code verifier and state are written to `--entra-verifier-file` with mode 0600.
The verifier is never printed. An existing file is moved to `<path>.prev`, deleting any older
`.prev`, before the new file is created. The verifier and authorization code permit token exchange.
Delete both the current file and its backup after the exchange:

```console
$ rm -f -- "$XDG_RUNTIME_DIR/webauth-code-verifier" "$XDG_RUNTIME_DIR/webauth-code-verifier.prev"
```

To supply an authorize URL directly:

```console
$ tools/portal-stack.sh --live --pin-prompt=system -- \
    --start-uri '<authorize URL>' \
    --completion-uri https://login.microsoftonline.com/common/oauth2/nativeclient \
    --session-mode ephemeral
```

#### Certificate authentication

When tenant policy requires certificate-based authentication, Entra redirects to
certauth.login.microsoftonline.us and requests a TLS client certificate. This backend answers
through the Certificate portal token; that service owns the chooser and PIN prompt.
Use `--pin-prompt=system` for the desktop's prompter.

#### Log evidence

| | |
|---|---|
| the challenge arrived, and on which host | this backend's log: `certificate-challenge host=certauth.login.microsoftonline.us` |
| the portal provider took it | `certificate-challenge provider=portal` |
| the module reached the portal | `pkcs11-portal-certificate-DEBUG: grant acquired`; set `G_MESSAGES_DEBUG="webauth pkcs11-portal-certificate"` |
| the card signed | the certificate backend's `login-ok` and `operation-completed` |
| the flow intercepted a matching navigation and completed | `navigation outcome=matched`, and `completed` |

The 2026-09-05 live run also produced two grants and one PIN prompt (see the sibling
`xdg-desktop-portal-certificate` README): the backend's session stays logged in across the second
grant. [Tier 2b](#two-grants-per-fresh-handshake) explains the counts and regression check.
Consent deduplication remains unimplemented.

#### Tenant evidence

The commercial nativeclient redirect applies even against the US Government authority:
[the client's ADR 0002](https://github.com/sjtrotter/entra-token-helper/blob/main/docs/decisions/0002-no-loopback-redirect.md)
records the registration and AADSTS50011 behavior. The redirect must be intercepted before load.

The 2026-09-05 run confirmed related certauth challenges, OpenSC/PIV use, one PIN prompt for two
grants and acceptance of this WebKitGTK engine by that tenant. Other providers may reject an
embedded user-agent, as [`webkit-session.h`](../src/webkit-session.h) describes. Card/module
compatibility, PIN behavior and usability still need broader testing.

---

### Live token exchange

[entra-token-helper](https://github.com/sjtrotter/entra-token-helper) drives the portal and
exchanges the returned code for a token. These commands reproduce the consumer path from the
2026-09-05 chain under the [live-run precautions](#live-run-against-entra).

#### Start the stack without a client

```console
$ tools/portal-stack.sh --live --pin-prompt=system --no-e2e --keep
```

`--no-e2e` skips `tools/webauth-e2e.py`; the client names its own authority without a fixture
identity provider. `--keep` leaves the stack running and prints its bus address.
The [live-stack prerequisites](#prerequisites-2) apply, with the desktop's prompter handling PINs.

#### Invoke the client on the printed bus

```console
$ export TENANT='<directory-tenant-id>'
$ export DBUS_SESSION_BUS_ADDRESS='<the address the script printed>'
$ export PATH="/path/to/entra-token-helper/build:$PATH"

$ entra-token-helper --verbose login \
    --cloud usgov \
    --authority "https://login.microsoftonline.us/$TENANT" \
    --client-id a85cf173-4192-42f8-81fa-777a763e6e2c \
    --scope 'https://www.wvd.azure.us/.default openid profile offline_access'
```

This opens the sign-in window on the desktop, redirects to
`certauth.login.microsoftonline.us`, raises two certificate choosers, asks for the PIN once, and
ends with `Signed in as <you>@<tenant-domain>`. The login command prints no token.

Request an ARM gateway bearer token; a second request should be silent:

```console
$ entra-token-helper --verbose token \
    --authority login.microsoftonline.us --tenant "$TENANT" \
    --client-id a85cf173-4192-42f8-81fa-777a763e6e2c \
    --scope 'https://www.wvd.azure.us/.default openid profile offline_access' \
    --format json
```

Request a session-host proof-of-possession token with a confirmation blob generated by
FreeRDP. `<blob>` is base64url of `{"kid":"<thumbprint>"}` for the FreeRDP-owned key; `<host>`
is the session host name from the connection:

```console
$ entra-token-helper --verbose token \
    --authority login.microsoftonline.us --tenant "$TENANT" \
    --client-id a85cf173-4192-42f8-81fa-777a763e6e2c \
    --scope 'ms-device-service://termsrv.wvd.microsoft.com/name/<host>/user_impersonation' \
    --req-cnf '<blob>' \
    --format json
```

The 2026-09-05 run required a second window for the RDS-AAD scope's remote-desktop trust
interstitial. The mock's `--fail-pop-refresh 1` forces the `interaction_required` path for tier 2c.

#### Client log evidence

| | |
|---|---|
| the request the client built | its own `--verbose` line `authorize authority=login.microsoftonline.us tenant=<hash> pop=no`; and it names the tenant as a hash and never the URL |
| the portal answered | `portal outcome=COMPLETED` |
| the URI was this transaction's | `callback outcome=CODE`; `UNRELATED` or `INVALID` indicates failure |
| the grant | `token-request grant=authorization_code pop=no`, then `token-response outcome=ok` |
| the account was stored | `keyring outcome=stored authority=login.microsoftonline.us` |
| the second run was silent | `cache outcome=hit`, and no `token-request` line at all |

#### Evidence and remaining checks

The 2026-09-05 chain demonstrates a PoP token accepted by FreeRDP through the fork-only
`client/entra-token-helper` proof of concept. The intended helper integration is FreeRDP #13340.
Repeat checks for client-built prompt=select_account, ID-token preferred_username, refresh-token
lifetime under Conditional Access and interstitial usability across tenants. These are consumer
checks; the backend does not interpret the authorization response.

#### Proof-of-concept command

The fork-only branch from the 2026-09-05 run builds this command, omitting empty values.
[Roadmap](ROADMAP.md#freerdp-integration) describes David Fort's FreeRDP #13340 helper protocol
and its review by akallabeth for 3.32.

```
entra-token-helper token \
  --authority login.microsoftonline.us \
  --tenant <GatewayAvdAadtenantid, or common> \
  --client-id <GatewayAvdClientID> \
  [--req-cnf <blob>] \
  --scope <one per occurrence, decoded>
```

The shim reads a token followed by a newline from stdout without requesting --json. The
authority is a host, from `FreeRDP_GatewayAzureActiveDirectory`, whose default is
`login.microsoftonline.com`; the tenant is separate. The shim omits --prompt, so auto applies.

---

### OneDrive wrapper

The abraunegg OneDrive client (Fedora package `onedrive`, tested at v2.5.11) is an existing
application whose file-based authentication interface needs no patch for the wrapper. `--auth-files <authUrlFile>:<responseUrlFile>`
makes it write the Microsoft authorize URL to the first file and poll until the second exists,
expecting the whole redirect URI a user would otherwise have copied out of a browser.
[`../tools/onedrive-auth.py`](../tools/onedrive-auth.py) sits between those two files and
`Start()`. It reads the authorize URL, takes the completion URI from that URL's `redirect_uri`
(the nativeclient path on the same host if there is none), and writes the URI the portal returns.
It deletes neither file; the client deletes both itself once the response file appears.

Exit status is 0 completed, 1 cancelled, 2 anything else. It logs one line per state change to
stderr, with URI scheme, host and path. It also logs result query-key names and fragment presence,
without query values or the authorization code.

#### Live desktop recipe

In one terminal:

```console
$ tools/portal-stack.sh --live --no-e2e --keep
```

In another terminal on the session bus, use a separate confdir to preserve ~/.config/onedrive:

```console
$ export A=$XDG_RUNTIME_DIR/onedrive-auth-url B=$XDG_RUNTIME_DIR/onedrive-response-url

$ onedrive --confdir ~/.config/onedrive-portal --reauth --auth-files "$A:$B" &
$ tools/onedrive-auth.py "$A:$B"
```

On a successful live run, onedrive writes the authorize URL and waits for its response file.
The wrapper logs authorize, completion, request and the output-file path. The portal returns the
nativeclient completion without fetching it; onedrive then consumes the response and removes
both files. The recorded evidence below does not establish a completed live OneDrive sign-in.

#### Headless

The recorded wrapper test uses the fixture identity provider. With
`tools/portal-stack.sh --pin-prompt=system --keep` holding the stack up, an authorize URL naming
the fixture's `/start` with `redirect_uri=https://example.invalid/cb` is written to the first file
by hand, and the wrapper is run on the same bus:

```
onedrive-auth: authorize scheme=https host=localhost:33159 path=/start
onedrive-auth: completion scheme=https host=example.invalid path=/cb
onedrive-auth: request /org/freedesktop/portal/desktop/request/1_25/onedriveauth51ab5ec22dcc4878
onedrive-auth: completed, response written to .../fb
```

The response file is mode 0600 and holds the completion URI with its `code` and `state`.
The fixture's access log contains no `/cb`; the [access-log limitation](#recorded-runs) applies.
This exercises the wrapper with a manually written input file; it does not establish successful
OneDrive integration.

With the same fixture stack, `onedrive` writes a `login.microsoftonline.com` authorize URL.
The wrapper derives `/common/oauth2/nativeclient` from its `redirect_uri` and opens the window.
The fixture cannot answer for Microsoft; that attempt ended at
`the portal answered 2, reason timeout` with exit 2.
