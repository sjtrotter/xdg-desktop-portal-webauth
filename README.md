# xdg-desktop-portal-webauth

> **Development has stopped (2026-10-16).** Most native applications are better served by the
> system browser with authorization code + PKCE and a registered or loopback redirect
> (RFC 8252), which keeps browser single sign-on and needs no new desktop service. This
> repository stays available as a reference; open issues are not scheduled. The Certificate
> portal ([xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate))
> continues on its own. Details: [#14](https://github.com/sjtrotter/xdg-desktop-portal-webauth/issues/14).

A backend for an experimental xdg-desktop-portal interface that runs a web sign-in for an
application. The application passes a start URI and the completion URI it expects; the portal
opens a WebKitGTK window, lets the user sign in, stops the navigation that reaches the
completion URI before it loads, and returns that URI to the application. Cookies, redirects
and client-certificate prompts stay in the portal window.

The backend does not parse OAuth, SAML or any other protocol. The application builds the start
URI and reads the result from the completion URI.

The interface is experimental and not merged upstream. The frontend lives on a branch of a
fork of xdg-desktop-portal; see [Upstreaming](docs/UPSTREAMING.md).

## Components

| Part | Where | Role |
|---|---|---|
| Frontend | branch [`experimental/integration`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration) of a fork of xdg-desktop-portal | identifies the caller, validates the URIs, enforces the deadline, checks the returned URI |
| Backend | this repository | shows the window, intercepts the completion navigation, answers client-certificate challenges |
| Certificate portal | [xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate) | supplies the client certificate, chooser and PIN prompt |

Applications call `org.freedesktop.portal.experimental.WebAuthentication1` on
`org.freedesktop.portal.Desktop`. The frontend calls this backend's
`org.freedesktop.impl.portal.experimental.WebAuthentication1` on
`org.freedesktop.impl.portal.desktop.webauth`. Both use the object path
`/org/freedesktop/portal/desktop`. The public method is `Start`; the result arrives on the
returned Request.

The frontend exports the interface only when a backend is configured and `WebAuthentication1`
is listed in `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` in the frontend's environment (comma
separated, no spaces). Add `Certificate1` to the same variable to enable client certificates
through the Certificate portal.

## Try it

Build dependencies are GLib/GObject/GIO 2.80 or newer, GTK 4.10, libadwaita 1.4
and WebKitGTK 6.0 2.40; p11-kit is optional. Meson enforces the GLib requirement and
warns about GLib API newer than 2.80; `-Dwerror=true` makes that an error.

```console
$ meson setup build
$ ninja -C build
$ meson test -C build
$ ./build/src/xdg-desktop-portal-webauth --help
```

To open a sign-in window against a local fixture identity provider, headless, build the
frontend branch and install Xvfb, then:

```console
$ export XDP_BUILD=/path/to/xdg-desktop-portal/build
$ tools/softhsm-fixture.sh
$ tools/ui-smoke.sh
```

`tools/ui-smoke.sh --mtls` adds a client-certificate challenge, and
`tools/portal-stack.sh` runs both portals together, with the certificate answered through the
Certificate portal. [Testing](docs/TESTING.md) has the prerequisites, the cancel and timeout
runs, and runs against a real identity provider.

## Client certificates

`--cert-adapter` picks how the backend answers a TLS client-certificate challenge:

| Value | Behaviour |
|---|---|
| `auto` (default) | `portal` when the Certificate interface and its PKCS#11 module configuration are present, otherwise `pkcs11` |
| `portal` | the Certificate portal's PKCS#11 module; the chooser and PIN prompt belong to that portal |
| `pkcs11` | the token named by `--client-cert-uri`, PIN from `--client-cert-pin-file` |
| `none` | client certificates disabled; a challenge gets no certificate |

This process has no certificate chooser and no PIN prompt of its own
([ADR 0007](docs/decisions/0007-certificate-adapter.md)).

Both `--client-cert-uri` and `--client-key-uri` reject `pin-value`; supply the PIN through
`--client-cert-pin-file`.

## Status

| Feature | State | Notes |
|---|---|---|
| `Start`, impl Request `Close`, caller checks | done | only the owner of `org.freedesktop.portal.Desktop` may call the backend |
| Sign-in window | done | security chrome, parented to the caller's window, downloads, popups and permission requests refused, TLS errors fail closed |
| Completion interception | done | a match in any frame ends the flow; the completion URI is never fetched |
| Storage | done | `shared` per application, `ephemeral` per `Start`, cookies included |
| Client certificates through the Certificate portal | done | the main path |
| Client certificates from a named PKCS#11 token | done | fallback |
| One certificate chooser per handshake | missing | with `portal`, a fresh WebKitGTK handshake raises two choosers and one PIN prompt |
| Caller name in the certificate chooser | partial | the chooser names this backend, not the application |
| Grant lifetime tied to the sign-in | partial | a grant outlives the sign-in, and a TLS connection outlives the grant |
| Second consumer, second backend | partial | a OneDrive wrapper ran against a fixture; there is no second backend |
| Upstream proposal | not planned | development stopped, see the notice above |
| Independent security review | missing | |

Hardware evidence, all with one PIV card, one reader, OpenSC, GNOME 50 on Wayland and one US
Government tenant: on 2026-09-05 the backend completed a live Microsoft Entra ID certificate
sign-in, and later that day `entra-token-helper` and a FreeRDP fork used it to open an Azure
Virtual Desktop session. Not yet run: a second card, a PIN-pad reader, card removal, KDE and a
Flatpak runtime.

## Known problems

- With `--cert-adapter portal`, a fresh WebKitGTK client-certificate handshake shows two
  certificate choosers, because each PKCS#11 module instance (UI process and network process)
  acquires its own grant. The `pkcs11` adapter has no chooser.
- The certificate chooser names this backend as the caller. Passing the original
  application's identity through needs frontend work.
- Certificate grants and authenticated TLS connections can outlive the sign-in that created
  them ([Security](docs/SECURITY.md)).
- Any application can open a phishing page in the portal window; some identity providers
  refuse embedded web views; browser single sign-on is not available; version 1 completes on
  navigations only ([ADR 0005](docs/decisions/0005-service-shape.md)).

## Documentation

| Document | Contents |
|---|---|
| [ARCHITECTURE](docs/ARCHITECTURE.md) | processes and responsibilities |
| [PUBLIC-INTERFACE](docs/PUBLIC-INTERFACE.md) | the interface applications call |
| [IMPL-INTERFACE](docs/IMPL-INTERFACE.md) | the backend interface and configuration |
| [SECURITY](docs/SECURITY.md) | threat model, controls and gaps |
| [TESTING](docs/TESTING.md) | commands, coverage and dated evidence |
| [UPSTREAMING](docs/UPSTREAMING.md) | proposal status and remaining work |
| [ROADMAP](docs/ROADMAP.md), [SPIKES](docs/SPIKES.md) | planned work and feasibility evidence |
| [decisions](docs/decisions/) | architecture decision records |
| [CONTRIBUTING](CONTRIBUTING.md), [SECURITY policy](SECURITY.md) | working on the code, reporting vulnerabilities |

## Related repositories

- [xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate):
  the Certificate backend and its PKCS#11 module.
- [entra-token-helper](https://github.com/sjtrotter/entra-token-helper): a Microsoft Entra ID
  and Azure Virtual Desktop token client, the first consumer of this portal.
- [xdg-desktop-portal fork](https://github.com/sjtrotter/xdg-desktop-portal): the frontend
  branches `experimental/certificate`, `experimental/webauthentication` and
  `experimental/integration`.

FreeRDP integration is planned as a helper speaking the protocol in
[FreeRDP/FreeRDP#13340](https://github.com/FreeRDP/FreeRDP/pull/13340) that forwards the sign-in
to this portal ([Roadmap](docs/ROADMAP.md)).

## License

LGPL-2.1-or-later, the license of xdg-desktop-portal, its GTK and GNOME backends and the
Certificate backend ([ADR 0004](docs/decisions/0004-license.md)). `src/tls/portal-token.h` is
identical in both portal backends; derived files keep their attribution.

## AI assistance

The code and documentation were written with AI assistance (Anthropic Claude, OpenAI Codex)
under the author's direction. The author is responsible for their content.
