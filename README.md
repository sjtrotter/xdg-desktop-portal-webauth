# webauth-service

**Status: design sketch. Nothing works yet.** This repository contains design documents, a
repository skeleton, and two stub binaries that build and print usage. No web view has been opened
and no token has ever been acquired by this code.

> **Names.** The repository is still called `entra-token-helper`, which was its original scope and
> now describes only the smaller half of it. The working title is **`webauth-service`**; the rename
> waits until the interface name is settled, because renaming twice is worse than renaming late.
>
> The D-Bus interface ships as **`io.github.sjtrotter.WebAuthentication1`** — a project-controlled
> reverse-DNS name with a major version, as the D-Bus specification recommends. It is deliberately
> **not** in the `org.freedesktop.portal.*` namespace. Becoming an xdg-desktop-portal interface is a
> possible *destination*, with a long list of prerequisites; see
> [docs/ROADMAP.md](docs/ROADMAP.md) phase 2. Nothing has been proposed to anyone, and no maintainer
> has been asked.

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
it. The bottom layer — the smart card service — is a separate project in its own repository.

```
  io.github.sjtrotter.Smartcard1     layer 1   smartcard-portal — SEPARATE REPOSITORY
    certificate chooser, PIN prompt, brokered signing            (optional, preferred)
        ▲
        │ D-Bus: AcquireCredential → grant
        │        …or, if p11-kit forwarding cannot register a module late,
        │        the in-process fallback runs instead and this arrow is absent
        │
  io.github.sjtrotter.WebAuthentication1   layer 2   service/ — THIS REPOSITORY
    web view, security chrome, completion matching, certificate ADAPTER
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
the chosen adapter allows. Layer 3 owns no windows. None of them knows anything about RDP.

## Layer 1 — the smart card service (*a separate project, and not a hard dependency*)

**Not in this repository.** `smartcard-portal` (working name), interface
`io.github.sjtrotter.Smartcard1`, being sketched in parallel. It would own the trusted certificate
chooser and the PIN prompt **for every application on the machine** — a mail client, a VPN dialog, a
code-signing tool and a browser all need one — and return a *grant*: the certificate, the operations
it permits, and either brokered `Sign`/`Decrypt` or a PKCS#11 endpoint.

That is the **preferred** way for layer 2 to satisfy a certificate challenge, because it takes the
PIN out of the web service's process entirely. It is **not a dependency for v0**, because the step
that would connect the two is unproven: a PKCS#11 URI cannot name a socket, GLib's
`g_tls_certificate_new_from_pkcs11_uris()` has no module parameter, and WebKit's network process may
not see a module registered after it started.

So layer 2 keeps the certificate path behind an **adapter** with two implementations — `portal`
(preferred, unproven) and `inproc` (the fallback, and the path known to work) — and a machine with no
smart card service installed still signs in. Spike [S2](docs/SPIKES.md) is what decides when that
changes. See
[docs/decisions/0007-certificate-adapter.md](docs/decisions/0007-certificate-adapter.md).

## Layer 2 — the web authentication service (`service/`)

A per-user, D-Bus-activated service that performs **one interactive web authentication transaction
and returns an uninterpreted completion artifact.** The interface description is
[`service/data/io.github.sjtrotter.WebAuthentication1.xml`](service/data/io.github.sjtrotter.WebAuthentication1.xml);
it is explained in [docs/SERVICE-INTERFACE.md](docs/SERVICE-INTERFACE.md).

```
io.github.sjtrotter.WebAuthentication1

  Start(s parent_window, s start_uri, s completion_uri, a{sv} options) → o request_handle
        options: handle_token, activation_token,
                 session_mode: shared|ephemeral, timeout, title

io.github.sjtrotter.WebAuthentication1.Request

  Close()                                cancel — there is no separate Cancel method
  Response(u response, a{sv} results)    0 completed { completion_uri }, 1 cancelled, 2 other
```

The transaction pattern is copied closely from `org.freedesktop.portal.Request`, because that
pattern is right and callers already know it. Copying a pattern is not claiming a namespace.

The service:

- opens the start URI in a web view it owns (GTK4 + WebKitGTK 6.0);
- answers TLS client-certificate challenges through an **adapter**: the smart card service where
  that is available and proven, an in-process PKCS#11 chooser and PIN prompt otherwise;
- shows **security chrome the caller cannot influence**: the real page origin, and the caller's
  identity as *the service* established it, not as the caller described itself;
- watches every top-level navigation and completes on the first one **exactly matching**
  `completion_uri`;
- **closes the window before that navigation loads**, so the URI carrying the credential is never
  fetched;
- returns the completion URI, complete and undecoded.

And it **never interprets that URI**. No OAuth, no token exchange, no credential storage. That is
what makes it reusable, and it is a boundary that would be easy to cross once and impossible to
uncross. See [docs/decisions/0005-service-shape.md](docs/decisions/0005-service-shape.md).

**Version 0 is one service, not a frontend and a backend.** Internally it is a D-Bus transaction
layer, a browser-session interface (a C vtable), and a WebKitGTK implementation. There is no
`org.freedesktop.impl.portal.*` ABI, because imitating a portal's names confers none of a portal's
properties while doubling its D-Bus surface, activation and crash handling, versioning obligations,
packaging and transaction-lifetime bugs. The vtable is where that split will happen if it ever
should.

**Browser session preference**, once there is more than one:

1. **The system browser**, whenever the completion mechanism lets it securely return the result —
   loopback HTTP, claimed HTTPS app links, registered custom schemes. This is what RFC 8252 prefers
   and it should be the default wherever it is possible.
2. **A service-owned WebKitGTK session**, when redirect interception or a delegated client
   certificate requires it. That is the AVD/PIV case below, and it is why this project exists.
3. **Manual paste**, as the recovery and headless fallback.
4. **A browser extension**, only as an experimental, explicitly installed integration — never the
   reference.

## Layer 3 — the Entra ID / AVD token client (`clients/entra/`)

The first consumer: the token client that FreeRDP-based RDP clients call. It owns everything the
service deliberately does not — the OAuth protocol, the sovereign-cloud constants, the token
cache — and it owns no windows at all.

It builds the authorization URL with `state` and PKCE S256, calls the service, validates the URI
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
is why the service needs a real web view of its own rather than being a wrapper around `xdg-open`.
See [docs/decisions/0002-no-loopback-redirect.md](docs/decisions/0002-no-loopback-redirect.md).

### Cloud constants

These live in the **Entra client**, never in either service.

| | Commercial | US Government |
|---|---|---|
| Authority | `login.microsoftonline.com` | `login.microsoftonline.us` |
| AVD scope | `https://www.wvd.microsoft.com/.default openid profile offline_access` | `https://www.wvd.azure.us/.default openid profile offline_access` |
| Completion URI | `https://login.microsoftonline.com/common/oauth2/nativeclient` | *the same* — there is no `.us` variant |
| Public client id | `a85cf173-4192-42f8-81fa-777a763e6e2c` | *the same* |

## Ownership split

| Layer | Owns | Does not own |
|---|---|---|
| **FreeRDP** | RDP wire protocol; `.rdp`/`.rdpw` parsing; sovereign-cloud constants; ARM and WST transports; generating the RDS PoP key and formatting `req_cnf`; the RDS-AAD nonce handshake; asking for a token through `GetAccessToken` | OAuth flows, browsers, certificates, PINs, token storage |
| **smartcard-portal** (layer 1, *separate repo, optional*) | The trusted certificate chooser; the PIN prompt; the PIN; brokered signing or a PKCS#11 endpoint | Anything about why a certificate was wanted |
| **webauth-service** (layer 2) | Hosting a web view; security chrome and caller identity; recognising and scoping TLS client-certificate challenges and running an adapter for them; storage partitioning; navigation interception; returning the completion URI | Any protocol meaning. It does not know what OAuth is. It owns the card only as far as the chosen adapter forces it to. |
| **Entra client** (layer 3) | OAuth: `state`, PKCE, redirect validation, code exchange, refresh, the PoP variant; sovereign authorities; the account and token cache; the CLI contract | Windows, web views, certificates, PINs |
| **Client apps** (Remmina, KRDC, sdl-freerdp, gtk-frdp) | Session UX; one line of glue that installs a callback invoking the client | Any of the above |

The Entra client never chooses a cloud, and the service never learns there is such a thing. FreeRDP
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

## Building

The two components are **independent meson projects** with no build-time dependency in either
direction, which is how the claim that layer 1 is protocol-independent stays testable. Each builds
on its own:

```console
$ meson setup build-service service && ninja -C build-service
$ meson setup build-entra clients/entra && ninja -C build-entra
```

The top-level build is a convenience umbrella that includes both as meson subprojects while they
share a repository, and disappears when they are split
([docs/decisions/0006-two-repositories.md](docs/decisions/0006-two-repositories.md)):

```console
$ meson setup build && ninja -C build
$ ./build/subprojects/webauth-service/webauth-service --help
$ ./build/subprojects/entra-token-client/entra-token-helper --help
```

The stubs need only GLib and GIO. WebKitGTK 6.0, GTK 4, p11-kit and libsecret are declared optional
and reported in the configure summary; nothing uses them yet.

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

**Ship an `org.freedesktop.portal.*` name now.** It would make integration look official. It would
also assert an ownership and an acceptance that do not exist, and the D-Bus specification recommends
a controlled reverse-domain namespace with a major interface version anyway.

## Why this might be a bad idea

Recorded properly rather than argued away. The full versions, with what each one implies, are in
[docs/decisions/0005-service-shape.md](docs/decisions/0005-service-shape.md).

1. **Identity providers may reject embedded engines.** RFC 8252 prefers an external user-agent. A
   service-owned engine is better than one embedded in the requesting application — the requester
   cannot reach the DOM — but it is not the system browser, and some providers may still block it.
2. **The API is a phishing launcher.** Any same-UID application can ask for a convincing corporate
   sign-in page. Only service-controlled chrome, showing the real origin and an independently
   established caller identity, stands against that. Caller-supplied titles are actively dangerous.
3. **Shared session state amplifies a malicious caller**, which can start a flow riding a session
   the user already established.
4. **Client certificates raise the stakes.** A flow here can end with a hardware token
   authenticating. The `portal` adapter would take the PIN out of this process entirely; the
   `inproc` fallback does not, and it is what ships until S2 passes. Either way the service can
   *provoke* that prompt, naming an origin, on behalf of a caller it may be unable to identify.
5. **A web engine becomes security-critical infrastructure** — for distributions and for this
   project, permanently.
6. **"Protocol-agnostic" can become unbounded scope** and turn this into a browser. Version 1 is
   deliberately narrow: navigation completions only, no POST, no prefix matching, two session modes.
7. **There may be only one real consumer.** If AVD is the only one, the generic layer is governance
   without reuse. A second credible consumer is a precondition for pursuing standardisation.
8. **It cannot reproduce real browser SSO.** Its shared store is shared only within the service —
   not with Firefox or Chrome, and not with their accounts, policies, extensions or device
   registration.
9. **Same-UID isolation is weak on an unrestricted desktop.** This helps sandboxed applications; it
   cannot claim strong separation between mutually hostile unsandboxed ones.

**And the exit criterion:** if caller identity, displayed origin and storage partitioning cannot be
made convincing, **collapse the browser layer back into the Entra client**. A narrowly scoped Entra/AVD helper is better than a generic authentication service with an
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
service/                    layer 2 — webauth-service (its own meson project)
  data/…WebAuthentication1.xml   the incubating D-Bus interface
  src/                           header sketches: transaction layer, identity, storage,
                                 completion matching, browser session, WebKit session,
                                 chrome, redaction
  src/tls/                       the certificate adapter and both implementations:
                                 client_cert.h, client_cert_portal.h,
                                 client_cert_inproc.h, pkcs11.h, chooser.h, pin.h
clients/entra/              layer 3 — entra-token-client (its own meson project)
  src/                           CLI stub plus header sketches: OAuth, clouds, cache, IPC schema
docs/                       ARCHITECTURE, SERVICE-INTERFACE, ENTRA-CLIENT-CLI,
                            SECURITY, SPIKES, ROADMAP, decisions/
tests/                      the offline test strategy (no tests yet)
```

Layer 1 is not here: it is `smartcard-portal`, a separate repository, and layer 2 runs without it.

## License

GPL-2.0-or-later. The in-process certificate chooser and PIN prompt are derived from Remmina
(GPL-2.0-or-later), and reusing proven card-handling code is worth more than license convenience.
Both binaries are separate processes from FreeRDP (Apache-2.0), spoken to over CLI and D-Bus
boundaries, so no linking question arises in either direction. See
[docs/decisions/0004-license.md](docs/decisions/0004-license.md) for the alternative considered.

## AI assistance

The design in this repository was drafted with the assistance of Anthropic Claude and OpenAI Codex,
under the direction and review of the author. The architectural decisions, the verified facts about
the AVD application registration and the sovereign-cloud constants, and the judgement about what to
build are the author's.
