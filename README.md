# xdg-desktop-portal-webauth

This repository holds two independent programs. `backend/` is an out-of-tree backend for the
experimental `org.freedesktop.impl.portal.experimental.WebAuthentication` portal interface: it opens
a GTK4 + WebKitGTK 6.0 window on a URI an application asked for, watches every navigation, and ends
the flow on the first one matching the completion URI the application gave, without loading it. It
returns that URI and never interprets it. `clients/entra/` is the Entra ID / Azure Virtual Desktop
token client that uses the portal and owns the OAuth half: PKCE, code exchange, refresh, the
proof-of-possession grant, and a Secret Service account store. Its meson project is
`entra-token-client` and its binary is `entra-token-helper`.

There is no frontend here. The public interface is exported by xdg-desktop-portal itself, on a branch
of that project. TLS client certificates are answered by a second backend,
`xdg-desktop-portal-certificate`, in its own repository.

## Status

Last checked 2026-09-06.

| | State |
|---|---|
| Impl interface: `Start`, `Close`, exactly one `Response`, same-UID and frontend-owner checks | Implemented (`backend/src/webauthentication-impl.c`) |
| Hosted WebKitGTK window: security chrome, parenting to `parent_window`, downloads and popups and permissions refused, TLS errors fail closed | Implemented |
| Completion interception: a match in any frame ends the flow and is never fetched | Implemented (`backend/src/completion.c`, `test-completion.c`) |
| Storage partitioning: `shared` per app id, `ephemeral` per `Start`, cookie jar included | Implemented |
| Client certificates through the `portal` provider (the certificate portal's client-side PKCS#11 module) | Implemented, and the primary path |
| Client certificates through the `pkcs11` provider (any p11-kit token named on the command line) | Implemented, as the fallback |
| In-process certificate chooser or PIN prompt | Not implemented, by decision ([0007](docs/decisions/0007-certificate-adapter.md)) |
| Entra client: `login`, `token`, `accounts`, `logout`, bearer and proof-of-possession, keyring cache | Implemented |
| Live sign-in against a real identity provider | Proven 2026-09-05 (see below) |
| One certificate chooser per handshake | Not implemented. A WebKitGTK handshake raises two, and one PIN prompt |
| Caller attribution in the certificate portal's chooser | Partial. That window names this backend, not the application that asked |
| Per-transaction isolation of certificate authority | Partial. A grant outlives the transaction, and a connection outlives the grant ([docs/SECURITY.md](docs/SECURITY.md)) |
| A second consumer, and a second backend for the interface | Not implemented |
| Proposed upstream | No. No issue, no pull request, no maintainer contact |
| Independent security review, a second maintainer | Not done |

Two runs on real hardware, both on 2026-09-05:

- 10:42. A live Entra ID sign-in against a US Government tenant. The portal window reached Entra,
  `certauth` challenged for a client certificate, WebKit's network process resolved it through the
  certificate portal's PKCS#11 module, the shell prompted for the PIN, the card produced an RSA-PSS
  signature, and the completion navigation was intercepted with the authorization code captured.
- 14:21. The whole chain: an sdl-freerdp fork branch, `entra-token-helper`, both portals, the card,
  and an Azure Virtual Desktop session. Real tokens were exchanged. The ARM gateway token came from
  the keyring cache. The RDS proof-of-possession token needed one interactive re-authentication
  through the portal window, which the tenant's Conditional Access policy explains.

The hardware evidence is narrow: one PIV card, one reader, OpenSC, GNOME 50 on Wayland, one tenant.
Not exercised at all: a second card, a PIN-pad reader, card removal mid-operation, KDE, a Flatpak
runtime.

The frontend is on the branch `experimental/certificate-webauthentication` of a personal fork of
xdg-desktop-portal, 10 commits on upstream `86bd3e2`, with `a6b06d4` defining this interface. Its
`tests/test_webauthentication.py` has 16 test functions and 35 parametrised cases, run twice over the
host and Flatpak app-info fixture. The branch is pushed and has been proposed to nobody. The
`org.freedesktop.portal.experimental.*` namespace is what upstream set aside for unfinished portals,
not a sign that this one was accepted.

## How it works

```
  an application: entra-token-helper, or anything else
       |
       |  org.freedesktop.portal.experimental.WebAuthentication      [gated]
       v  on org.freedesktop.portal.Desktop
  xdg-desktop-portal                            a branch of ANOTHER project
       derives the app id, validates both URIs, filters the options, applies
       policy, mints the Request, keeps the deadline, guarantees one Response,
       re-checks the completion URI before the application sees it.
       No window, no web engine, no toolkit, no card.
       |
       |  org.freedesktop.impl.portal.experimental.WebAuthentication
       v  NOT an interface applications may call
  xdg-desktop-portal-webauth                    backend/, THIS REPOSITORY
       GTK4 + WebKitGTK 6.0: the window, the security chrome, the storage
       partition, the navigation interception, the TLS client certificate.
       |
       |  PKCS#11, through p11-kit, in this process and in WebKit's
       v  network process
  xdg-desktop-portal-certificate                SEPARATE REPOSITORY, optional
       the certificate chooser, the PIN prompt, the signing key on the card.
```

Applications call xdg-desktop-portal and nothing else. They never name a backend, never read a
`.portal` file, and cannot tell which backend served them.

The interface is not exported unless xdg-desktop-portal was started with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication` (or `all`). That is the default state of
every machine. With the gate off, `entra-token-helper` exits `40` and names the missing variable.

## Interface at a glance

Public, on `org.freedesktop.portal.Desktop`. The XML on the frontend branch is the specification;
[docs/PUBLIC-INTERFACE.md](docs/PUBLIC-INTERFACE.md) summarises it.

```
Start(s parent_window, s start_uri, s completion_uri, a{sv} options) -> o request_handle
```

One method. `start_uri` must be absolute `https` with a host. `completion_uri` must be absolute, with
no userinfo and no wildcard. Options are `handle_token`, `activation_token`, `session_mode`
(`shared` or `ephemeral`), `timeout` (default 300 s, clamped to 900 s) and `title`. Unknown keys are
dropped and an unknown `session_mode` is an error. The answer arrives on
`org.freedesktop.portal.Request`: `Close()` cancels, and `Response(u, a{sv})` fires exactly once with
`0` completed and `completion_uri` in the results, `1` cancelled, or `2` other with an optional
`reason`.

Impl, for backends only. It follows the shape of the other impl portals, with the frontend-derived
`app_id` passed as an argument. See
[docs/IMPL-INTERFACE.md](docs/IMPL-INTERFACE.md) and the verbatim tracking copy at
[`backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml`](backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml).

```
Start(o handle, s app_id, s parent_window, s start_uri, s completion_uri, a{sv} options)
    -> (u response, a{sv} results)
```

## The Entra client CLI

`entra-token-helper` has four verbs. The full contract, including the exit codes, is
[docs/ENTRA-CLIENT-CLI.md](docs/ENTRA-CLIENT-CLI.md).

| Verb | Purpose |
|---|---|
| `login` | Sign in through the portal and store the account and its refresh token. Always interactive. |
| `token` | Acquire an access token, bearer or proof-of-possession. Silent when the cache allows it. |
| `accounts` | List stored accounts. |
| `logout` | Remove one account or all of them. |

stdout carries only the result; everything else goes to stderr. Exit codes are contractual: `10`
interaction required, `20` cancelled, `30` no such account, `40` provider unavailable, `50`
authorization server error, `64` usage, `70` internal. A dispatcher should treat `40` as a decline
and fall through.

```console
$ entra-token-helper login --cloud usgov --tenant <tenant-id>
Signed in as <user>@<tenant-domain>

$ entra-token-helper token --cloud usgov --tenant <tenant-id> \
    --account <user>@<tenant-domain> --req-cnf eyJraWQiOiI8a2V5LWlkPiJ9 --json
{
  "schema": 1,
  "status": "ok",
  "token": "eyJ0eXAiOiJhdCtqd3Qi...",
  "token_type": "pop",
  "expires_in": 3599,
  "account": "<user>@<tenant-domain>"
}
```

A proof-of-possession token is never cached, so each one is a fresh grant. On the tested tenant the
RDS scope required one interactive re-authentication, answered inside the portal window.

## Try it

The two components are separate meson projects with no build-time dependency in either direction.
The backend needs GLib, GIO, GTK 4, libadwaita and WebKitGTK 6.0. The client needs GLib, GIO,
libsoup-3, json-glib and libsecret. All are required.

```console
$ meson setup build-backend backend       && ninja -C build-backend && meson test -C build-backend
$ meson setup build-entra   clients/entra && ninja -C build-entra   && meson test -C build-entra
```

The unit tests need no display and no bus. The backend has five suites (`completion`, `options`,
`storage`, `redact`, `harden`); the client has six (`pkce`, `callback`, `jwt`, `discovery`, `cache`,
`redact`).

The top-level `meson.build` is a convenience umbrella that includes both through `subprojects/` while
they share a repository ([0006](docs/decisions/0006-two-repositories.md)):

```console
$ meson setup build && ninja -C build
$ ./build/subprojects/xdg-desktop-portal-webauth/xdg-desktop-portal-webauth --help
$ ./build/subprojects/entra-token-client/entra-token-helper --help
```

The end-to-end scripts each stand up a private bus with `dbus-run-session` and a headless X server,
so none of them touches the session bus or the display. They need a build of the frontend branch;
point at it with `XDP_BUILD`.

```console
$ tools/softhsm-fixture.sh                  # a CA, a server certificate, a token, a PIN file
$ tools/ui-smoke.sh                         # the window, driven by a fixture identity provider
$ tools/ui-smoke.sh --mtls                  # the fixture server demands a client certificate
$ tools/ui-smoke.sh --cancel                # Escape, expecting response 1
$ tools/portal-stack.sh                     # BOTH portals, one bus, one Xvfb, a real handshake
$ tools/portal-stack.sh --second-start
$ tools/portal-stack.sh --cancel-chooser
$ tools/entra-e2e.sh                        # the client through all four verbs, mock authority
$ tools/trigger-webauthentication.sh all    # the PUBLIC interface with gdbus, as an app would
```

Every run also checks, from the fixture server's access log, that the completion URI was never
fetched. `tools/portal-stack.sh` prints the chooser count at the end; three would be a regression.
[docs/TESTING.md](docs/TESTING.md) has the rest, including what only a real tenant and a real card
can answer.

The backend takes `--cert-adapter auto|portal|pkcs11|none` and, for `pkcs11`, `--client-cert-uri`.
`auto` prefers `portal` and falls through when that portal is not running or its module is not in
p11-kit's configuration. Installed files are `$libexecdir/xdg-desktop-portal-webauth`,
`$datadir/xdg-desktop-portal/portals/webauth.portal`,
`$datadir/dbus-1/services/org.freedesktop.impl.portal.desktop.webauth.service` and the interface XML
in `$datadir/dbus-1/interfaces`. To select it explicitly, in `portals.conf`:

```ini
[preferred]
org.freedesktop.impl.portal.experimental.WebAuthentication=webauth
```

## How it relates to FreeRDP

Upstream pull request [FreeRDP/FreeRDP#13340](https://github.com/FreeRDP/FreeRDP/pull/13340), opened
2026-09-04 and reviewed favourably for 3.32, moves FreeRDP's AAD web view out of process. FreeRDP
speaks JSON-RPC over pipes to a helper it starts; the `navigate` request carries a URL, a redirect
URI prefix and a timeout, and the helper answers with the redirect URL verbatim. OAuth stays inside
FreeRDP. The helper is selected with `/azure:auth-helper:<path|autodetect>`, and a follow-up will
pass the OAuth parameters as JSON so the helper can build the URL itself.

That request is close to what this portal already does. The intended integration is therefore a small
helper that speaks #13340's protocol and forwards `navigate` to
`org.freedesktop.portal.experimental.WebAuthentication`, which puts the window, the security chrome
and the card behind the portal without FreeRDP linking a web engine. Under that split, FreeRDP does
not need the Entra client's OAuth code, and the client keeps its own callers.

The `client/entra-token-helper` branch used in the 2026-09-05 run is a fork-only proof of concept. It
is not proposed upstream and is not the integration path. `sso-mib` remains the sibling provider for
Intune-enrolled devices, which hold a device-bound primary refresh token this project cannot mint.

## Why not

- A loopback redirect with `xdg-open`: the only redirect URI registered for the AVD public client is
  a remote HTTPS one, so nothing local can receive the completion.
  [0002](docs/decisions/0002-no-loopback-redirect.md)
- A browser extension and a native messaging host: per-browser packaging, broad URL-observation
  permissions, and failure under private browsing or enterprise policy. Experimental at best.
  [0005](docs/decisions/0005-service-shape.md)
- Impersonating the Microsoft Identity Broker's D-Bus name: no stable contract to implement, and the
  name asserts enrollment semantics a browser flow cannot honour.
  [0003](docs/decisions/0003-no-broker-impersonation.md)
- A service that returns tokens instead of completions: that is an identity broker, with client
  registration, consent, rotation and policy attached. [0005](docs/decisions/0005-service-shape.md)
- Keeping it inside one RDP client: it buries a desktop identity capability in one plugin and strands
  every other client. [0001](docs/decisions/0001-standalone-helper.md)

## Related repositories

- [github.com/sjtrotter/xdg-desktop-portal-webauth](https://github.com/sjtrotter/xdg-desktop-portal-webauth)
  is this repository.
- [github.com/sjtrotter/xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate)
  is the certificate portal backend: the chooser, the PIN prompt, and the PKCS#11 module this backend
  loads.
- [github.com/sjtrotter/xdg-desktop-portal, branch `experimental/certificate-webauthentication`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/certificate-webauthentication)
  is the frontend for both portals.

## Known problems and open questions

- One WebKitGTK handshake raises two certificate choosers and one PIN prompt. The UI process and the
  network process each acquire a grant, and a grant belongs to the D-Bus peer that acquired it. One
  chooser was only ever reached on the archived `experimental/certificate-webauthentication+delegation`
  branch, whose process-tree delegation is not part of the proposed interface.
- The certificate portal's chooser names this backend rather than the application that asked. The fix
  belongs in the shared frontend and is not written.
- A grant outlives the transaction that provoked it, and an authenticated connection outlives the
  grant. [docs/SECURITY.md](docs/SECURITY.md) says what closing a transaction does not do.
- Nothing has been proposed upstream, to xdg-desktop-portal or to FreeRDP. There is no second
  consumer of the interface and no second backend for it, and both are what would show the interface
  is more than this backend with a bus name.
- The API is a phishing launcher: any same-UID application can ask for a convincing corporate sign-in
  page. Portal-controlled chrome showing the real origin and a frontend-derived caller identity is
  the only defence, and same-UID isolation is weak on an unrestricted desktop.
- Identity providers may refuse embedded engines. The shared session store is shared only within this
  backend, never with Firefox or Chrome, so real browser SSO is not reproduced.
- Version 1 is navigation completions only: no `form_post`, no prefix matching, two session modes. A
  protocol-agnostic remit could grow without limit if that is not held.

## Documents

| | |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | The two components, the process split, and what each owns |
| [docs/PUBLIC-INTERFACE.md](docs/PUBLIC-INTERFACE.md) | The interface applications call, and where its XML lives |
| [docs/IMPL-INTERFACE.md](docs/IMPL-INTERFACE.md) | The backend contract |
| [docs/ENTRA-CLIENT-CLI.md](docs/ENTRA-CLIENT-CLI.md) | The CLI contract: verbs, options, JSON, exit codes |
| [docs/TESTING.md](docs/TESTING.md) | The tiers, the commands, and what each run proved |
| [docs/SECURITY.md](docs/SECURITY.md) | Threat model, boundaries, and the known gaps |
| [docs/SPIKES.md](docs/SPIKES.md) | The feasibility questions and their answers |
| [docs/UPSTREAMING.md](docs/UPSTREAMING.md) | The frontend branch and what it forced this repository to change |
| [docs/ROADMAP.md](docs/ROADMAP.md) | What is next, in order |
| [docs/decisions/](docs/decisions/) | The architecture decision records |

[CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md) are at the top level.

## License

LGPL-2.1-or-later. This matches xdg-desktop-portal, xdg-desktop-portal-gtk,
xdg-desktop-portal-gnome, the frontend branch this backend is written against, and the sibling
`xdg-desktop-portal-certificate`, with which this repository shares
[`backend/src/tls/portal-token.h`](backend/src/tls/portal-token.h) byte for byte. Files derived from
xdg-desktop-portal and xdg-desktop-portal-gtk keep their attribution under the same licence. Both
binaries are separate processes from FreeRDP (Apache-2.0) and are reached over D-Bus and a CLI, so no
linking question arises. The reasoning is in
[docs/decisions/0004-license.md](docs/decisions/0004-license.md).

## AI assistance

The code and documents in this repository were drafted with the assistance of Anthropic Claude and
OpenAI Codex, under direction and review. The architectural decisions, the verified facts about the
AVD application registration and the sovereign-cloud constants, and the judgement about what to build
are the author's.
