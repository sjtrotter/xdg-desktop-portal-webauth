# xdg-desktop-portal-webauth

An out-of-tree backend for the experimental
`org.freedesktop.impl.portal.experimental.WebAuthentication` portal interface. It opens a GTK4 +
WebKitGTK 6.0 window on a URI an application asked for, watches every navigation, and ends the flow
on the first one matching the completion URI the application gave, without loading it. It returns
that URI and never interprets it. It holds no OAuth code and issues no tokens.

There is no frontend here. The public interface is exported by xdg-desktop-portal itself, on a branch
of that project. TLS client certificates are answered by a second backend,
`xdg-desktop-portal-certificate`, in its own repository. The first consumer, the Entra ID / Azure
Virtual Desktop token client `entra-token-helper`, is in a third
([github.com/sjtrotter/entra-token-helper](https://github.com/sjtrotter/entra-token-helper)).

## Status

Last checked 2026-09-07.

| | State |
|---|---|
| Impl interface: `Start` returning `(u, a{sv})`, `Close` on the impl Request, same-UID and frontend-owner checks | Implemented (`src/webauthentication-impl.c`) |
| Hosted WebKitGTK window: security chrome, parenting to `parent_window`, downloads and popups and permissions refused, TLS errors fail closed | Implemented |
| Completion interception: a match in any frame ends the flow and is never fetched | Implemented (`src/completion.c`, `tests/test-completion.c`) |
| Storage partitioning: `shared` per app id, `ephemeral` per `Start`, cookie jar included | Implemented |
| Client certificates through the `portal` provider (the certificate portal's client-side PKCS#11 module) | Implemented, and the primary path |
| Client certificates through the `pkcs11` provider (any p11-kit token named on the command line) | Implemented, as the fallback |
| In-process certificate chooser or PIN prompt | Not implemented, by decision ([0007](docs/decisions/0007-certificate-adapter.md)) |
| First consumer: `entra-token-helper`, in a [separate repository](https://github.com/sjtrotter/entra-token-helper) | Working against this backend |
| Live sign-in against a real identity provider | Proven 2026-09-05 (see below) |
| One certificate chooser per handshake | Not implemented. A WebKitGTK handshake raises two, and one PIN prompt |
| Caller attribution in the certificate portal's chooser | Partial. That window names this backend, not the application that asked |
| Per-transaction isolation of certificate authority | Partial. A grant outlives the transaction, and a connection outlives the grant ([docs/SECURITY.md](docs/SECURITY.md)) |
| A second consumer, and a second backend for the interface | Not implemented |
| Proposed upstream | No. No issue and no pull request; two comments announcing the work on 2026-09-05 (flatpak/xdg-desktop-portal#662, FreeRDP/FreeRDP#13328) |
| Independent security review, a second maintainer | Not done |

Two runs on real hardware, both on 2026-09-05, when the client still shared this repository:

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
  xdg-desktop-portal-webauth                    THIS REPOSITORY
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
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication` (or `all`). The experimental interface is
disabled by default. With the gate off the interface is not on the bus at all, and a caller that
needs it has to degrade or fail.

## Interface at a glance

Public, on `org.freedesktop.portal.Desktop`. The XML on the frontend branch is the specification;
[docs/PUBLIC-INTERFACE.md](docs/PUBLIC-INTERFACE.md) summarises it.

```
Start(s parent_window, s start_uri, s completion_uri, a{sv} options) -> o request_handle
```

One method. `start_uri` must be absolute `https` with a host. `completion_uri` must be absolute, with
no userinfo and no wildcard in the host. Options are `handle_token`, `activation_token`, `session_mode`
(`shared` or `ephemeral`), `timeout` (default 300 s, clamped to 900 s) and `title`. Unknown keys are
dropped and an unknown `session_mode` is an error. The answer arrives on
`org.freedesktop.portal.Request`: `Close()` cancels, and `Response(u, a{sv})` fires exactly once with
`0` completed and `completion_uri` in the results, `1` cancelled, or `2` other with an optional
`reason`.

Impl, for backends only. It follows the shape of the other impl portals, with the frontend-derived
`app_id` passed as an argument. See
[docs/IMPL-INTERFACE.md](docs/IMPL-INTERFACE.md) and the verbatim tracking copy at
[`data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml`](data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml).

```
Start(o handle, s app_id, s parent_window, s start_uri, s completion_uri, a{sv} options)
    -> (u response, a{sv} results)
```

## Try it

One meson project. It needs GLib, GIO, GTK 4, libadwaita and WebKitGTK 6.0. All are required.

```console
$ meson setup build && ninja -C build && meson test -C build
$ ./build/src/xdg-desktop-portal-webauth --help
```

The unit tests need no display and no bus. There are five suites: `completion`, `options`,
`storage`, `redact`, `harden`.

`ui-smoke.sh` and `portal-stack.sh` stand up a private bus with `dbus-run-session` and a headless X
server, so they touch neither the session bus nor the display; `portal-stack.sh --live` and the
trigger script use the real desktop. They need a build of the frontend branch; point at it with
`XDP_BUILD`.

```console
$ tools/softhsm-fixture.sh                  # a CA, a server certificate, a token, a PIN file
$ tools/ui-smoke.sh                         # the window, driven by a fixture identity provider
$ tools/ui-smoke.sh --mtls                  # the fixture server demands a client certificate
$ tools/ui-smoke.sh --cancel                # Escape, expecting response 1
$ tools/portal-stack.sh                     # BOTH portals, one bus, one Xvfb, a real handshake
$ tools/portal-stack.sh --second-start
$ tools/portal-stack.sh --cancel-chooser
$ tools/trigger-webauthentication.sh all    # the PUBLIC interface with gdbus, as an app would
```

The fixture-backed runs (`ui-smoke.sh`, `portal-stack.sh`) also check, from the fixture server's
access log, that the completion URI was never fetched. `tools/portal-stack.sh` prints the chooser count at the end; three would be a regression.
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
not need the Entra client's OAuth code, and that client keeps its own callers.

The `client/entra-token-helper` branch used in the 2026-09-05 run is a fork-only proof of concept. It
is not proposed upstream and is not the integration path. `sso-mib` remains the sibling provider for
Intune-enrolled devices, which hold a device-bound primary refresh token this project cannot mint.

## Why not

- A browser extension and a native messaging host: per-browser packaging, broad URL-observation
  permissions, and failure under private browsing or enterprise policy. Experimental at best.
  [0005](docs/decisions/0005-service-shape.md)
- A service that returns tokens instead of completions: that is an identity broker, with client
  registration, consent, rotation and policy attached. [0005](docs/decisions/0005-service-shape.md)
- A frontend in this repository: the public interface belongs to xdg-desktop-portal, so it is
  carried on a branch of that project instead.
  [0010](docs/decisions/0010-backend-only-frontend-lives-upstream.md)

Three more rejected alternatives belong to the client and are recorded as ADRs 0001 to 0003 in
[its repository](https://github.com/sjtrotter/entra-token-helper): a loopback redirect with
`xdg-open`, impersonating the Microsoft Identity Broker's D-Bus name, and keeping the work inside
one RDP client.

## Related repositories

- [github.com/sjtrotter/xdg-desktop-portal-webauth](https://github.com/sjtrotter/xdg-desktop-portal-webauth)
  is this repository.
- [github.com/sjtrotter/xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate)
  is the certificate portal backend: the chooser, the PIN prompt, and the PKCS#11 module this backend
  loads.
- [github.com/sjtrotter/entra-token-helper](https://github.com/sjtrotter/entra-token-helper) is the
  Entra ID / Azure Virtual Desktop token client, the first consumer of this portal. Its CLI contract
  is `docs/CLI.md` there.
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
- Neither the portals nor the FreeRDP fork branch has been proposed upstream. There is no second
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
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | The process split, and what each part owns |
| [docs/PUBLIC-INTERFACE.md](docs/PUBLIC-INTERFACE.md) | The interface applications call, and where its XML lives |
| [docs/IMPL-INTERFACE.md](docs/IMPL-INTERFACE.md) | The backend contract |
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
[`src/tls/portal-token.h`](src/tls/portal-token.h) byte for byte. Files derived from
xdg-desktop-portal and xdg-desktop-portal-gtk keep their attribution under the same licence. This
backend is a separate process from FreeRDP (Apache-2.0) and is reached over D-Bus, so no linking
question arises. The reasoning is in
[docs/decisions/0004-license.md](docs/decisions/0004-license.md).

## AI assistance

The code and documents in this repository were drafted with the assistance of Anthropic Claude and
OpenAI Codex, under direction and review. The architectural decisions and the judgement about what
to build are the author's.
