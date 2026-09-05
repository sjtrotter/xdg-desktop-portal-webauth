# xdg-desktop-portal-webauth, and the Entra token client

Author: Stephen J. Trotter (sjtrotter)

**Status: the portal backend works against a fixture, through the certificate portal; the Entra
client is still a sketch.** The backend opens a real WebKitGTK window, intercepts the completion
navigation before it loads, and answers a TLS client-certificate challenge with a certificate whose
private key stays on a PKCS#11 token — verified end to end on a private bus under Xvfb, including
mutual TLS, cancellation, `Close()`, and shared versus ephemeral storage
([docs/TESTING.md](docs/TESTING.md)). Since 2026-09-04 that certificate can come from
**`xdg-desktop-portal-certificate`**, through its client-side PKCS#11 module, with the card, the
chooser and the PIN in that service and never in this process: both portals run against each other
headless in [`tools/portal-stack.sh`](tools/portal-stack.sh), and every hop is proved from the log
of the process that made it. **No token has ever been acquired by this code, and it has never talked
to a real identity provider or a real card.** `clients/entra/` is still a stub that exits `70`.

## It is a portal backend, plus a client

Web authentication is **an xdg-desktop-portal frontend and backend pair** — and the frontend is
xdg-desktop-portal itself. This repository ships the backend, and an application that uses it.

```
   an application                      clients/entra/, or anything else
        │
        │  org.freedesktop.portal.experimental.WebAuthentication   ← the ONLY interface
        │  on org.freedesktop.portal.Desktop            [GATED]      applications may call
        ▼
   xdg-desktop-portal                  ANOTHER REPOSITORY, on a branch
        derives the app id, validates the URIs, filters the options,
        applies policy, mints the Request, guarantees one Response,
        re-checks the completion URI before the application sees it.
        No window. No web engine. No toolkit. No card.
        │
        │  org.freedesktop.impl.portal.experimental.WebAuthentication
        ▼                                              ← NOT for applications
   xdg-desktop-portal-webauth          backend/
        GTK4 + WebKitGTK 6.0: the window, the security chrome, the storage
        partition, the navigation interception, the TLS client certificate.
        │
        │  org.freedesktop.portal.experimental.Certificate  (as an ordinary client)
        ▼
   ...back out to the same xdg-desktop-portal, and in again to
   xdg-desktop-portal-certificate      SEPARATE REPOSITORY, optional
```

**Applications talk to xdg-desktop-portal and to nothing else.** They never name a backend, never
read a `.portal` file, never call an `impl` interface, and cannot tell which backend served them. A
machine that installs a different backend — a Qt one, a system-browser one, a headless paste one —
changes nothing in any application.

**`[GATED]` is load-bearing.** The interface is experimental and is not exported unless
xdg-desktop-portal was started with `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`.
With the gate off, `entra-token-helper` exits `40` (unavailable) and says which variable is
missing. That is the default state of every machine, and it is the intended behaviour.

The frontend is a local-only branch, `experimental/certificate-webauthentication`, commits
`3f46e3c..661e441`. It has been built and tested (38 pytest cases for this portal, all green,
against a python-dbusmock backend) and **has not been proposed to anyone**. Its interfaces live in
the `org.freedesktop.portal.experimental.*` namespace, which is what upstream set aside for portals
that are not finished — not a claim that this one has been accepted.
[docs/decisions/0010](docs/decisions/0010-backend-only-frontend-lives-upstream.md) records why the
frontend moved there and the incubating one was deleted;
[docs/decisions/0008](docs/decisions/0008-build-to-the-upstream-shape.md) is why the split exists at
all, and 0010 preserves it. [docs/UPSTREAMING.md](docs/UPSTREAMING.md) has the whole picture,
including what the branch's XML forced this repository to change.

> **Names.** The repository is still called `entra-token-helper`, which was its original scope and
> now describes only the smaller part of it. The backend is `xdg-desktop-portal-webauth`; renaming
> the repository itself is a later decision.

## The missing primitive

Every other platform has an operating-system primitive for "show this login page in a browser I
control, and tell me where the flow ended":

| Platform | Primitive |
|---|---|
| Apple | [`ASWebAuthenticationSession`](https://developer.apple.com/documentation/authenticationservices/aswebauthenticationsession) |
| Android | [Custom Tabs](https://developer.android.com/develop/ui/views/layout/webapps/overview-of-android-custom-tabs) + [AppAuth](https://github.com/openid/AppAuth-Android) |
| Windows | [`WebAuthenticationBroker`](https://learn.microsoft.com/en-us/uwp/api/windows.security.authentication.web.webauthenticationbroker) |
| Linux | *nothing* |

On Linux, every application needing an interactive web sign-in builds its own web view — and, if its
users have smart cards, its own TLS client-certificate chooser and its own PIN prompt. Almost none
of them do, which is why smart-card sign-in works in Firefox and nowhere else.

This project is the middle of **three** layers: the missing primitive, and the first thing to use
it. The bottom layer — the smart card portal — is a separate project in its own repository.

```
  org.freedesktop.portal.experimental.Certificate   layer 1
    certificate chooser, PIN prompt, brokered signing   frontend: the same xdg-desktop-portal
        ▲                                               backend: xdg-desktop-portal-certificate — SEPARATE REPO
        │ D-Bus: AcquireCredential → grant   (the BACKEND calls it, as an ordinary client)
        │        …but only if a GnuTLS external-signer path exists; otherwise the
        │        in-process fallback runs instead and this arrow is absent
        │
  org.freedesktop.portal.experimental.WebAuthentication   layer 2
    frontend: xdg-desktop-portal (branch)         backend: backend/ — THIS REPOSITORY
        ▲
        │ D-Bus: Start → Response { completion_uri }
        │
  entra-token-helper                 layer 3   clients/entra/ — THIS REPOSITORY
    OAuth, PKCE, token cache, sovereign clouds
        ▲
        │ GetCommonAccessToken callback
     FreeRDP
```

Layer 1 knows nothing about the web. Layer 2 knows nothing about OAuth, and as little about cards as
the chosen adapter allows. Layer 3 owns no windows. None of them knows anything about RDP. The
frontend/backend split is *inside* layers 1 and 2, and it is invisible from layer 3 — and both
layers now share one frontend process, which is what makes the delegation problem below solvable.

## Layer 1 — the Certificate portal (*a separate backend, and not a hard dependency*)

**Its backend is not in this repository.** `xdg-desktop-portal-certificate` ships
`xdg-desktop-portal-certificate`; its frontend is the same xdg-desktop-portal branch as ours, and
the public interface is `org.freedesktop.portal.experimental.Certificate` on
`org.freedesktop.portal.Desktop`. It owns the trusted certificate chooser and the PIN prompt **for
every application on the machine** — a mail client, a VPN dialog, a code-signing tool and a browser
all need one — and returns a *grant*, held as a `Session` object: the certificate, and the
operations and mechanisms it permits.

That is the **preferred** way for layer 2's backend to satisfy a certificate challenge, because it
takes the PIN out of the backend's process entirely. It is **not a dependency**, and it has got
further from being one rather than closer: the branch's Certificate interface has **no
`OpenPkcs11Endpoint`** — an fd-returning method needs its own review, so it was deferred — which
leaves brokered `Sign` as the only way to use a grant, and brokered `Sign` needs an external-signer
path in WebKitGTK/glib-networking that is not known to exist.

**Spike [S2](docs/SPIKES.md) has since answered how it will connect, and it is not brokered
`Sign`.** WebKitGTK carries a client certificate to its network process as a **PKCS#11 URI** and
resolves it there — the key never leaves the token — and it asks for the token PIN itself. There is
no external-signer seam, and no `GTlsInteraction` on a `WebKitNetworkSession`. So the seam between
the two projects is a **p11-kit module the Certificate portal publishes**, and the backend names the
token it presents: [`backend/src/tls/portal-token.h`](backend/src/tls/portal-token.h) is that
agreement.

The certificate path is therefore an **adapter with two providers** — `portal` and `pkcs11`.

**`portal` is the primary path and it now works.** `xdg-desktop-portal-certificate` ships
`libpkcs11-portal-certificate.so`, registers it with p11-kit, and presents the certificate the user
granted as a token named by the URIs in
[`backend/src/tls/portal-token.h`](backend/src/tls/portal-token.h). The whole path — WebKit's
`authenticate`, this provider, p11-kit, the module in this process **and** in WebKit's network
process, the certificate portal's chooser and PIN prompt, `C_Sign`, a completed mutual-TLS
handshake — has been run headless end to end by
[`tools/portal-stack.sh`](tools/portal-stack.sh), with every hop proved from the log of the process
that made it. [docs/TESTING.md](docs/TESTING.md) tier 2b is the command, the transcript, and two UX
findings that came out of it: **one handshake puts up two choosers**, because the certificate is
built in this process and used in the network process, and a **second sign-in in the same backend
process puts up none at all**.

`pkcs11` is the fallback: any p11-kit token named on the backend's command line, for an operator
with a card and no certificate portal, and for exercising mutual TLS with no second service in the
picture. `auto` prefers `portal` and falls through when the portal is not running or its module is
not in p11-kit's configuration.

**There is no in-process chooser and no in-process PIN prompt**, and there will not be one: that is
the window the Certificate portal exists to own. See
[docs/decisions/0007-certificate-adapter.md](docs/decisions/0007-certificate-adapter.md).

## Layer 2 — the web authentication portal (`backend/`, plus a frontend elsewhere)

Two per-user processes that between them perform **one interactive web authentication transaction
and return an uninterpreted completion artifact.** The public interface is defined by the frontend
branch and summarised in [docs/PUBLIC-INTERFACE.md](docs/PUBLIC-INTERFACE.md); the backend contract
is [`backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml`](backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml)
— a verbatim tracking copy of the branch's file — and [docs/IMPL-INTERFACE.md](docs/IMPL-INTERFACE.md).

```
org.freedesktop.portal.experimental.WebAuthentication   on org.freedesktop.portal.Desktop
                                                        [not exported unless the gate is set]
  Start(s parent_window, s start_uri, s completion_uri, a{sv} options) → o request_handle
        options: handle_token, activation_token,
                 session_mode: shared|ephemeral, timeout (≤900), title

org.freedesktop.portal.Request

  Close()                                cancel — there is no separate Cancel method
  Response(u response, a{sv} results)    0 completed { completion_uri }, 1 cancelled, 2 other

org.freedesktop.impl.portal.experimental.WebAuthentication      NOT for applications

  Start(o handle, s app_id, s parent_window, s start_uri, s completion_uri, a{sv} options)
        → (u response, a{sv} results)
```

The transaction pattern is `org.freedesktop.portal.Request`'s, and the impl signature is
`org.freedesktop.impl.portal.Account`'s with the frontend-derived `app_id` prepended. That
signature survived the move upstream unchanged, which is the best evidence the shape was right.

The portal:

- derives the caller's app id in the frontend, where the application cannot influence it, and hands
  it to the backend as a fact;
- validates both URIs in the frontend before any window opens, and re-checks the URI the backend
  returns before any application sees it;
- opens the start URI in a web view the backend owns (GTK4 + WebKitGTK 6.0);
- answers TLS client-certificate challenges through an **adapter**: the smart card portal where
  that is available and proven, an in-process PKCS#11 chooser and PIN prompt otherwise;
- shows **security chrome the caller cannot influence**: the real page origin, and the caller's
  identity as *the frontend* established it, not as the caller described itself;
- watches every top-level navigation and completes on the first one **exactly matching**
  `completion_uri`;
- **closes the window before that navigation loads**, so the URI carrying the credential is never
  fetched;
- returns the completion URI, complete and undecoded.

And it **never interprets that URI**. No OAuth, no token exchange, no credential storage. That is
what makes it reusable, and it is a boundary that would be easy to cross once and impossible to
uncross. See [docs/decisions/0005-service-shape.md](docs/decisions/0005-service-shape.md).

**Backend preference**, once there is more than one — and each of these is a separate *backend*,
selected in `portals.conf`, not a mode inside one process. All of them would install a `.portal`
file naming `org.freedesktop.impl.portal.experimental.WebAuthentication`:

1. **The system browser**, whenever the completion mechanism lets it securely return the result —
   loopback HTTP, claimed HTTPS app links, registered custom schemes. This is what RFC 8252 prefers
   and it should be the default wherever it is possible.
2. **A portal-owned WebKitGTK backend**, when redirect interception or a delegated client
   certificate requires it. That is the AVD/PIV case below, it is why this project exists, and it is
   the backend in this repository.
3. **Manual paste**, as the recovery and headless fallback.
4. **A browser extension**, only as an experimental, explicitly installed integration — never the
   reference.

That this list became a packaging decision rather than a code path is the clearest practical
dividend of building to the upstream shape. What it cost is per-request capability negotiation: a
backend implements the whole interface or does not claim it. See
[docs/decisions/0008](docs/decisions/0008-build-to-the-upstream-shape.md).

## Layer 3 — the Entra ID / AVD token client (`clients/entra/`)

The first consumer: the token client that FreeRDP-based RDP clients call. It owns everything the
portal deliberately does not — the OAuth protocol, the sovereign-cloud constants, the token
cache — and it owns no windows at all.

It builds the authorization URL with `state` and PKCE S256, calls the portal frontend, validates the URI
that comes back (exact redirect match, `state` compared in constant time, exactly one of `code` or
`error`, strict percent-decoding), exchanges the code, supports the proof-of-possession variant
using the `req_cnf` FreeRDP supplies, caches refresh tokens in the Secret Service keyring, and
presents four CLI verbs with a documented exit-code contract:
[docs/ENTRA-CLIENT-CLI.md](docs/ENTRA-CLIENT-CLI.md).

### The problem it solves

An Azure Virtual Desktop connection is not authenticated with a password. FreeRDP needs two OAuth
access tokens per connection: a bearer token for the ARM gateway, which resolves the workspace, and
a proof-of-possession token for the session host, bound to a key FreeRDP generates itself and passes
down as `req_cnf` (base64url JSON carrying the key's `kid`). FreeRDP asks for both through its
`GetAccessToken` callback, dispatched via the `GetCommonAccessToken` chain. FreeRDP knows what token
it wants; it deliberately does not know how to go and get one.

Getting one is a human process, and on a smart-card tenant a *human plus card* process. The
authorization request goes to an Entra ID authority; the authority redirects to
`certauth.<authority>`, which challenges for a TLS client certificate; the certificate lives on a
PIV card behind a PKCS#11 token that wants a PIN; and only then does the authority redirect back
with an authorization code.

On Linux, nothing owns that job for a non-enrolled device. `sso-mib` covers Intune-enrolled devices
by talking over D-Bus to the Microsoft Identity Broker, which already holds a device-bound primary
refresh token; it has nothing to offer a machine that is not enrolled. Toolkit OAuth libraries exist
(librest, QtNetworkAuth), but no toolkit ships a certificate chooser or a PIN prompt for its web
view — precisely the hard part, and the reason there is a layer 1 at all. FreeRDP's terminal paste flow works today only because Firefox has
its own card UI: the user authenticates in a real browser and pastes the redirect URL back. Every
GUI client that wants to do better has to build the same web view, chooser and PIN prompt from
scratch. Remmina has; KRDC and the SDL client have not. Layers 1 and 2 are that work, factored out
for everybody — the card half where every application can reach it, the web half where every
web sign-in can. Layer 3 is what is left once both are gone.

### The hard constraint

The Azure Virtual Desktop public client id is `a85cf173-4192-42f8-81fa-777a763e6e2c`. Its **only**
registered redirect URI is:

```
https://login.microsoftonline.com/common/oauth2/nativeclient
```

There is no `.us` variant — asking for one against the US Government authority is rejected with
`AADSTS50011` — and no loopback redirect. Registering one would mean editing a Microsoft-owned
application registration, which is not available to us.

Two consequences shape everything:

1. The completion is a real, remote, **commercial-cloud** HTTPS URL even when the sign-in happened
   against `login.microsoftonline.us`. Nothing local can receive it. Only a web view hosted on this
   machine can observe that navigation.
2. It must be intercepted and the window closed **before the page loads**: the authorization code is
   in its query string, and letting the web view fetch it would send the code to a
   Microsoft-operated page with no part in this exchange.

That single fact rules out every "just shell out to the user's browser" design for this flow, and it
is why the portal needs a backend with a real web view of its own rather than being a wrapper around
`xdg-open`.
See [docs/decisions/0002-no-loopback-redirect.md](docs/decisions/0002-no-loopback-redirect.md).

### Cloud constants

These live in the **Entra client**, never in the portal.

| | Commercial | US Government |
|---|---|---|
| Authority | `login.microsoftonline.com` | `login.microsoftonline.us` |
| AVD scope | `https://www.wvd.microsoft.com/.default openid profile offline_access` | `https://www.wvd.azure.us/.default openid profile offline_access` |
| Completion URI | `https://login.microsoftonline.com/common/oauth2/nativeclient` | *the same* — there is no `.us` variant |
| Public client id | `a85cf173-4192-42f8-81fa-777a763e6e2c` | *the same* |

## Current capabilities

**This table is the one place that says what works.** Every other document in this repository
defers to it; where one of them disagrees, this is right and it is a bug. Last checked 2026-09-04.

| | Status | |
|---|---|---|
| The impl interface: `Start`, `Close`, one `Response`, same-UID and frontend-owner checks | **Implemented** | `backend/src/webauthentication-impl.c`, unit- and end-to-end tested |
| A hosted WebKitGTK window with security chrome, parented to `parent_window` | **Implemented** | origin and caller shown; downloads, popups and every permission refused; TLS errors fail closed with no bypass |
| Completion interception — a match in **any** frame ends the flow and is never loaded | **Implemented** | proved from the fixture server's access log: the completion URI is never fetched |
| Storage partitioning: `shared` per app id, `ephemeral` per `Start` | **Implemented** | including the cookie jar, which a data directory alone does not persist |
| Deadline, exactly one terminal result, teardown on every exit path | **Implemented** | `backend/src/transaction.c` |
| Client certificates through the `pkcs11` provider | **Implemented** | a token named on the command line; the fallback, and what the tests use with no portal in the picture |
| Client certificates through the `portal` provider | **Implemented** | the primary path. Both portals on one private bus, headless, a real WebKitGTK sign-in signed by the card's key: `tools/portal-stack.sh` |
| Caller attribution to the certificate portal's chooser | **Partial** | the certificate portal's window names **this backend**, not the application. The fix is in-process in the shared frontend and is **not written** |
| One chooser per sign-in | **Partial** | **two**, about three seconds apart: the certificate is resolved in this process and again in WebKit's network process. Measured, not solved |
| Per-transaction isolation of certificate authority | **Partial** | grants survive the transaction and an authenticated connection survives the grant. See [SECURITY.md](docs/SECURITY.md), "What closing a transaction does NOT do" |
| A real identity provider | **Not implemented** | nothing here has ever talked to Entra ID, and no card has ever been in a reader for it |
| The Entra client, `clients/entra/` | **Not implemented** | a stub that exits `70`. No token has been acquired by this code |
| A second, unrelated consumer | **Not implemented** | the exit criterion, and the thing every reviewer asked for first |
| A second backend for the interface | **Not implemented** | which is what would show the interface is not this backend with a bus name |
| Rate limiting, and a browser-backed session | **Not implemented** | the first belongs to the frontend; the second is a different backend |
| Independent security review, a second maintainer | **Not implemented** | — |

## Ownership split

| Layer | Owns | Does not own |
|---|---|---|
| **FreeRDP** | RDP wire protocol; `.rdp`/`.rdpw` parsing; sovereign-cloud constants; ARM and WST transports; generating the RDS PoP key and formatting `req_cnf`; the RDS-AAD nonce handshake; asking for a token through `GetAccessToken` | OAuth flows, browsers, certificates, PINs, token storage |
| **xdg-desktop-portal-certificate** (layer 1, *separate repo, optional*) | The trusted certificate chooser; the PIN prompt; the PIN; brokered signing | Anything about why a certificate was wanted |
| **xdg-desktop-portal** (the frontend of layers 1 and 2) | The bus name applications call; deriving the caller's app id; validating the URIs it forwards and re-checking the one that comes back; option filtering; storage-mode and timeout policy; the Request object and exactly one Response | A window, a web engine, a toolkit, a display connection, a card, a PIN, or any protocol meaning |
| **xdg-desktop-portal-webauth** (layer 2, backend) | Hosting a web view; security chrome; parenting to `parent_window`; storage partitioning; navigation interception before load; recognising and scoping TLS client-certificate challenges and running an adapter for them | The application's identity (it is told), the decision to accept a request, the option vocabulary, the storage policy, or any protocol meaning. It owns the card only as far as the chosen adapter forces it to. |
| **Entra client** (layer 3) | OAuth: `state`, PKCE, redirect validation, code exchange, refresh, the PoP variant; sovereign authorities; the account and token cache; the CLI contract | Windows, web views, certificates, PINs |
| **Client apps** (Remmina, KRDC, sdl-freerdp, gtk-frdp) | Session UX; one line of glue that installs a callback invoking the client | Any of the above |

The Entra client never chooses a cloud, and no part of the portal learns there is such a thing. FreeRDP
resolves the authority from the `.rdp`/`.rdpw` file and passes it in with the scope and, for PoP
requests, the `req_cnf` it generated.

## The CLI at a glance

Four verbs; the full contract is [docs/ENTRA-CLIENT-CLI.md](docs/ENTRA-CLIENT-CLI.md).

```console
$ entra-token-helper login \
    --authority login.microsoftonline.us \
    --tenant <tenant-id> \
    --scope 'https://www.wvd.azure.us/.default' \
    --scope openid --scope profile --scope offline_access
Signed in as <user>@<tenant-domain>
```

Acquire the ARM gateway bearer token (silent if a refresh token is cached; otherwise one
transaction):

```console
$ entra-token-helper token \
    --authority login.microsoftonline.us \
    --tenant <tenant-id> \
    --scope 'https://www.wvd.azure.us/.default' \
    --account <user>@<tenant-domain>
eyJ0eXAiOiJKV1Qi...
```

Acquire the session-host PoP token, bound to the key FreeRDP generated:

```console
$ entra-token-helper token --json \
    --authority login.microsoftonline.us \
    --tenant <tenant-id> \
    --scope 'https://www.wvd.azure.us/.default' \
    --req-cnf eyJraWQiOiI8a2V5LWlkPiJ9 \
    --account <user>@<tenant-domain> \
    --prompt never
{
  "schema": 1,
  "status": "ok",
  "token": "eyJ0eXAiOiJhdCtqd3Qi...",
  "token_type": "pop",
  "expires_in": 3599,
  "account": "<user>@<tenant-domain>"
}
```

```console
$ entra-token-helper accounts
<user>@<tenant-domain>  login.microsoftonline.us  <tenant-id>

$ entra-token-helper logout --account <user>@<tenant-domain>
Removed <user>@<tenant-domain>
```

Every one of those currently exits `70` with `not implemented (design sketch)`. Only `--help` and
`--version` succeed, in both binaries.

## Building, and testing it on a dev machine

The two components are **independent meson projects** with no build-time dependency in either
direction. That is how the claim that the portal is protocol-independent stays testable rather than
aspirational: `backend/` must contain no Entra, Azure, OAuth or RDP identifier, and that is a grep.
The only mentions that survive it are three comments explaining *why* a rule exists — an OAuth
`state`, a convincing sign-in page — and none of them is a code path.

```console
$ meson setup build-backend backend       && ninja -C build-backend
$ meson setup build-entra   clients/entra && ninja -C build-entra
```

The top-level build is a convenience umbrella that includes both as meson subprojects while they
share a repository, and disappears when they are split
([docs/decisions/0006-two-repositories.md](docs/decisions/0006-two-repositories.md)):

```console
$ meson setup build && ninja -C build
$ ./build/subprojects/xdg-desktop-portal-webauth/xdg-desktop-portal-webauth --help
$ ./build/subprojects/entra-token-client/entra-token-helper --help
```

The **backend** needs GLib, GIO, GTK 4, libadwaita and WebKitGTK 6.0, all required: every method it
implements opens a web view, and a backend that cannot must not claim the interface. It links
against no PKCS#11 library of its own — a certificate is named by URI and GnuTLS resolves it. The
**client** is still a stub and needs only GLib and GIO; libsecret is declared optional and unused.

```console
$ meson test -C build-backend        # the rules: completion, options, storage, redaction,
                                     #            and the hardening window's counting
```

Then the real thing, and none of it touches your session bus or your display:

**The joint run first**, because it is the path this project is for: the certificate comes from the
certificate portal and this process never sees a PIN.

```console
$ (cd ../xdg-desktop-portal-certificate && meson setup build && ninja -C build)
$ ../xdg-desktop-portal-certificate/tools/softhsm-fixture.sh
$ tools/portal-stack.sh              # both portals, one private bus, one Xvfb
$ tools/portal-stack.sh --second-start
$ tools/portal-stack.sh --cancel-chooser -- --expect-response 2 \
      --expect-reason no_certificate_adapter --no-require-code
```

Then the fallback provider, which needs no second service:

```console
$ tools/softhsm-fixture.sh           # a CA, a server certificate, a token, a PIN file
$ tools/ui-smoke.sh                  # Xvfb + private bus + the whole stack, plain https
$ tools/ui-smoke.sh --mtls           # the server demands a client certificate
$ tools/ui-smoke.sh --cancel --start-path=/wait
```

`tools/ui-smoke.sh` starts an Xvfb and runs `tools/dev-stack.sh` inside it. That starts, on a
**private bus** made by `dbus-run-session`: `xdg-permission-store` (the portal refuses to start
without it), a fixture identity provider, a development xdg-desktop-portal from the branch with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication` and an `XDG_DESKTOP_PORTAL_DIR` of its
own, this backend, and `tools/webauth-e2e.py` — which calls the **public** interface exactly as an
application would. Point it at your frontend build with
`XDP_BUILD=/path/to/xdg-desktop-portal/build`. Every run also checks, from the fixture server's
access log, that the completion URI was never fetched.

[docs/TESTING.md](docs/TESTING.md) has all of it, including what the runs proved and what only a
real tenant and a real card can answer.

```console
$ tools/trigger-webauthentication.sh          # version + Start + both rejection cases
$ tools/trigger-webauthentication.sh version
$ tools/trigger-webauthentication.sh monitor  # watch the Response signals
```

calls the **public** interface with `gdbus`, exactly as an application would — on
`org.freedesktop.portal.Desktop`, never on this backend's name. Its URIs are
`https://example.invalid/...` placeholders, overridable with `START_URI` and `COMPLETION_URI`. If
the frontend was started without the gate, every call fails with "no such interface"; that is the
gate working.

Installed files, for the backend: `$libexecdir/xdg-desktop-portal-webauth`,
`$datadir/xdg-desktop-portal/portals/webauth.portal` (the real directory — that is where the
frontend looks, and it is what every out-of-tree backend does),
`$datadir/dbus-1/services/org.freedesktop.impl.portal.desktop.webauth.service`, and the interface
XML in `$datadir/dbus-1/interfaces`. To select it explicitly, put this in `portals.conf`:

```ini
[preferred]
org.freedesktop.impl.portal.experimental.WebAuthentication=webauth
```

## How this relates to FreeRDP

**Now:** no FreeRDP changes are required. FreeRDP's `GetCommonAccessToken` seam is public (setter
and getter since 3.16), and a frontend can save the current callback, install its own, and chain to
the saved one on decline — exactly how `sso-mib` installs itself. A client can install a callback
that runs `entra-token-helper token …` today.

**Later:** propose a small typed, size-versioned token-provider API in `client/common` —
`{ token_type, authority, tenant, client_id, decoded scope, req_cnf, parsed kid }` in, and an
explicit success / declined / cancelled / error result out, with userdata and register/unregister —
so providers need not reproduce `sso-mib`'s manual chaining, decode `req_cnf` themselves, or learn
FreeRDP setting names. `instance->GetAccessToken` stays as the last-resort frontend callback, and
the terminal paste flow stays as the headless fallback.

`sso-mib` is the **sibling** provider, not the competitor: it serves Intune-enrolled devices through
the Microsoft Identity Broker, this client serves everything else, and a sensible dispatch order
tries the broker first and falls through. [docs/ROADMAP.md](docs/ROADMAP.md) lists the FreeRDP-side
follow-ups.

## Why not X

**A loopback redirect (`http://127.0.0.1:<port>/`) with `xdg-open`.** The right shape for a native
OAuth client (RFC 8252), and it would use the user's real browser with its real card UI. Impossible
*for this flow*: the loopback URI is not registered for client `a85cf173-…`, and only a web view on
this machine can see a navigation to the `nativeclient` URL. For flows where it *is* possible, a
system-browser session is the preferred implementation and is on the roadmap.

**A browser extension plus a native messaging host.** Also reuses the browser's card UI. It trades
one hard problem for several: extension packaging and review per browser family, per-distro native
host manifests, broad URL-observation permissions, correlating concurrent transactions, profile
selection, extension update trust, and outright failure under private browsing or enterprise policy.
Experimental integration at best.

**Keep it in Remmina.** The chooser and PIN prompt already work there, which is why it is tempting.
It strands KRDC, sdl-freerdp and gtk-frdp behind the same missing UI, and it buries a desktop
identity capability inside one client's RDP plugin. The proven code is the *input* to this project,
not its home.

**Impersonate the Microsoft Identity Broker's D-Bus name.** Every existing `sso-mib` caller would
then work unchanged. Rejected: MS-OAPXBC specifies the broker's OAuth extensions, not Microsoft's
local D-Bus ABI, so there is no stable contract to implement; Microsoft can change object paths,
methods and activation at will; the name asserts device-enrollment semantics (PRT, Conditional
Access, hardware-backed keys) a browser flow cannot honour; and taking the bus name collides with a
real broker if one is later installed. See
[docs/decisions/0003-no-broker-impersonation.md](docs/decisions/0003-no-broker-impersonation.md).

**A service that returns tokens instead of completions.** Tempting — callers would need no OAuth
code, refresh tokens would never reach applications, and cross-application SSO would be real. But it
immediately needs client registration rules, cache keys, account selection, refresh rotation,
consent and incremental scopes, claims challenges, PoP/DPoP, logout, enrollment semantics and
per-application policy. That is not a web authentication service; it is an identity broker, and
there are already several.
[docs/decisions/0005-service-shape.md](docs/decisions/0005-service-shape.md).

**Ship a non-experimental `org.freedesktop.portal.*` name.** It would make integration look
official. It would also assert an acceptance that does not exist. `experimental` is the namespace
upstream set aside for portals in this state, it is not exported by default, and it can change or be
removed without a version bump — which is an accurate description of where this is.

## Why this might be a bad idea

Recorded properly rather than argued away. The full versions, with what each one implies, are in
[docs/decisions/0005-service-shape.md](docs/decisions/0005-service-shape.md).

1. **Identity providers may reject embedded engines.** RFC 8252 prefers an external user-agent. A
   portal-owned engine is better than one embedded in the requesting application — the requester
   cannot reach the DOM — but it is not the system browser, and some providers may still block it.
2. **The API is a phishing launcher.** Any same-UID application can ask for a convincing corporate
   sign-in page. Only portal-controlled chrome, showing the real origin and an independently
   established caller identity, stands against that. Caller-supplied titles are actively dangerous.
3. **Shared session state amplifies a malicious caller**, which can start a flow riding a session
   the user already established.
4. **Client certificates raise the stakes.** A flow here can end with a hardware token
   authenticating. The `portal` provider takes the PIN out of this process entirely and is not
   usable yet; the `pkcs11` provider reads a PIN the operator put in a file. Either way the backend
   can *provoke* that operation, naming an origin, on behalf of a caller it may be unable to
   identify.
5. **A web engine becomes security-critical infrastructure** — for distributions and for this
   project, permanently.
6. **"Protocol-agnostic" can become unbounded scope** and turn this into a browser. Version 1 is
   deliberately narrow: navigation completions only, no POST, no prefix matching, two session modes.
7. **There may be only one real consumer.** If AVD is the only one, the generic layer is governance
   without reuse. A second credible consumer is a precondition for pursuing standardisation.
8. **It cannot reproduce real browser SSO.** Its shared store is shared only within the backend —
   not with Firefox or Chrome, and not with their accounts, policies, extensions or device
   registration.
9. **Same-UID isolation is weak on an unrestricted desktop.** This helps sandboxed applications; it
   cannot claim strong separation between mutually hostile unsandboxed ones.

10. **The frontend/backend split was built before anyone asked for it.** The design review called
    it premature, and it may be: it doubles the D-Bus surface, adds a failure mode (a backend dying
    mid-transaction), splits the completion matcher into two implementations of one rule, and gives
    up per-request capability negotiation. It was done anyway, to avoid a second rewrite and to make
    the upstream patch a rename; the full argument and every cost is
    [docs/decisions/0008](docs/decisions/0008-build-to-the-upstream-shape.md). What has changed is
    that the frontend half lives in xdg-desktop-portal's tree, where the people whose review
    matters can see it
    ([docs/decisions/0010](docs/decisions/0010-backend-only-frontend-lives-upstream.md)). That is
    worth having, and it is **not** a transfer of maintenance: **until the branch is accepted it is
    ours** — ours to rebase, to keep green, and to redesign when upstream asks. It also removes the
    "collapse it back" escape hatch, which was part of the cost and remains so.

**And the exit criterion:** if caller identity, displayed origin and storage partitioning cannot be
made convincing, **collapse the browser layer back into the Entra client**. A narrowly scoped Entra/AVD helper is better than a generic authentication portal with an
ill-defined trust model.

## Prior art and neighbours

Stated carefully, because none of it is adoption of this idea.

- **xdg-desktop-portal has no OAuth or web-authentication portal**, existing or proposed. A search
  of its issue tracker finds only FIDO2/WebAuthn discussions —
  [#987 "FIDO2 Portal Proposal"](https://github.com/flatpak/xdg-desktop-portal/issues/987) and
  [#989 "FIDO U2F/WebAuthn abstraction/permission/portal"](https://github.com/flatpak/xdg-desktop-portal/issues/989)
  — plus a closed
  [#311 "Add password portal"](https://github.com/flatpak/xdg-desktop-portal/issues/311). The
  nearest *shipped* neighbours are
  [`org.freedesktop.portal.Account`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Account.html)
  (basic user information) and
  [`org.freedesktop.portal.Secret`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Secret.html).
- **[linux-credentials/credentialsd](https://github.com/linux-credentials/credentialsd)** proposes an
  `org.freedesktop.portal.Credentials` interface for passkeys and WebAuthn, with a reference
  implementation and experimental Firefox and Chromium integration. It is FIDO2-only: no OAuth, no
  hosted web-view sign-in. Adjacent work rather than overlapping work, and a useful precedent for how
  such an interface gets proposed and what it is asked to justify.
- **Microsoft's Linux identity broker** is itself a per-user D-Bus-activated service, with
  [sso-mib](https://github.com/siemens/sso-mib) as its C client — evidence that the shape works on
  Linux, and a reminder that a vendor-specific service is not a platform primitive.

## Repository layout

```
backend/                    the portal BACKEND — its own meson project.
                            Laid out like every out-of-tree backend.
  data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml
                                         a VERBATIM COPY of the frontend branch's file,
                                         which it must track
  data/webauth.portal.in                 DBusName, Interfaces, UseIn
  data/org.freedesktop.impl.portal.desktop.webauth.service.in   D-Bus activation
  data/org.freedesktop.impl.portal.Request.xml   the shared backend Request, also a copy
  src/                                   webauthentication-impl.c  the impl skeleton, the peer check
                                         request-impl.c            Close(), and nothing else
                                         transaction.c             one answer, every exit path
                                         webkit-session.c          the engine and the interception
                                         chrome.c                  the trusted part of the window
                                         external-window.c         x11:/wayland: parenting
                                         completion.c              the matching rule
                                         storage.c options.c redact.c
  src/tls/                               client_cert.c             the adapter and its selection
                                         client_cert_portal.c      the Certificate portal's token
                                         client_cert_pkcs11.c      any p11-kit token, by URI
                                         portal-token.h            the names the other repo must use
                                         harden.c/.h               PR_SET_DUMPABLE, and the one
                                                                   window in which it yields
  tests/                                 the rules, with no display and no bus
clients/entra/              layer 3 — entra-token-client (its own meson project)
  src/                                   CLI stub plus header sketches: OAuth, clouds,
                                         cache, IPC schema
spikes/                     webkit-client-cert.c — S2, kept because its answer is load-bearing
tools/                      softhsm-fixture.sh, mtls-server.py, dev-stack.sh, ui-smoke.sh,
                            portal-stack.sh (BOTH portals, one bus, one Xvfb),
                            webauth-e2e.py, trigger-webauthentication.sh, lib.sh
docs/                       ARCHITECTURE, PUBLIC-INTERFACE, IMPL-INTERFACE, TESTING,
                            UPSTREAMING, ENTRA-CLIENT-CLI, SECURITY, SPIKES, ROADMAP,
                            decisions/
tests/                      the offline test strategy; the tests themselves are backend/tests/
```

`backend/` mirrors the sibling `xdg-desktop-portal-certificate` repository's own top-level `src/` + `data/`, as
closely as having a second component allows. The frontend is not here: it is a branch of
xdg-desktop-portal. Layer 1's backend is not here either: it is `xdg-desktop-portal-certificate`, a separate
repository, and layer 2 runs without it.

## License

LGPL-2.1-or-later. This matches `xdg-desktop-portal`, `xdg-desktop-portal-gtk` and
`xdg-desktop-portal-gnome`, and the frontend branch this backend is written against, so code can
move into any of them without a relicensing step; it also matches the sibling
`xdg-desktop-portal-certificate`, with which this repository shares a header byte for byte
(`backend/src/tls/portal-token.h`). No code was ever copied from Remmina's RDP plugin — the
chooser and PIN prompt that would have been lifted live in the certificate portal instead, and
`backend/src/tls/` holds no card handling at all. Files derived from xdg-desktop-portal and
xdg-desktop-portal-gtk (`backend/src/request-impl.c`, `backend/src/external-window.c`,
`backend/src/completion.c`'s rule, the two verbatim XML copies) keep their attribution under the
same licence. Both binaries are separate processes from FreeRDP (Apache-2.0), spoken to over CLI
and D-Bus boundaries, so no linking question arises in either direction, and consumers only speak
D-Bus, so the licence places no constraint on them. The reasoning, and the earlier
GPL-2.0-or-later decision this supersedes, are in
[docs/decisions/0004-license.md](docs/decisions/0004-license.md).

## AI assistance

The design in this repository was drafted with the assistance of Anthropic Claude and OpenAI Codex,
under the direction and review of the author. The architectural decisions, the verified facts about
the AVD application registration and the sovereign-cloud constants, and the judgement about what to
build are the author's.
