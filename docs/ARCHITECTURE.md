# Architecture

This repository builds the GTK4 and WebKitGTK backend. The frontend is xdg-desktop-portal on
[`experimental/integration`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration).
[Testing](TESTING.md) records fixture and live-provider evidence.

```
application (for example, entra-token-helper)
    |
    | org.freedesktop.portal.experimental.WebAuthentication1
    v
xdg-desktop-portal, branch experimental/integration
    |
    | org.freedesktop.impl.portal.experimental.WebAuthentication1
    v
xdg-desktop-portal-webauth
    |
    | PKCS#11 through the Certificate portal's client-side module
    | org.freedesktop.portal.experimental.Certificate1
    v
xdg-desktop-portal (the same frontend process)
    |
    | org.freedesktop.impl.portal.experimental.Certificate1
    v
xdg-desktop-portal-certificate (optional, separate repository)
```

Both experimental interfaces use `/org/freedesktop/portal/desktop`. The
[public guide](PUBLIC-INTERFACE.md) defines export conditions and the
[impl guide](IMPL-INTERFACE.md) defines backend discovery and configuration.
[ADR 0008](decisions/0008-build-to-the-upstream-shape.md) records the split;
[ADR 0010](decisions/0010-backend-only-frontend-lives-upstream.md) records frontend ownership.
[Upstreaming](UPSTREAMING.md) describes maintenance of the branch.

## Ownership

| Component | Responsibilities |
|---|---|
| Application | Protocol validation, token exchange and account policy |
| Frontend | Caller identity, UID checks, URI and option validation, request policy, backend selection, public Request, deadline and returned-URI check |
| This backend | Impl Request, window parenting, web view, security chrome, storage, live completion interception, TLS challenges and teardown |
| Certificate backend | Certificate choice, consent, PIN and key operations |

The split follows `desktop-portal/account.c` and xdg-desktop-portal-gtk's `src/account.c`.
`xdp_invocation_get_app_info()` derives identity through `shared/xdp-app-info*.c`;
`XdpOptionKey` tables filter options; `xdp-portal-config.c` discovers backends;
`xdp-request-dex.c` owns public Requests. The frontend exports the Request with
`xdp_request_dex_export()` before forwarding the call, then validates the backend result.

Version 1 remembers no consent in `xdp-permissions.c` /
`org.freedesktop.impl.portal.PermissionStore`. Any future permission-store policy requires
separate review and credible sandbox identity. [Caller-identity rules](SECURITY.md#access-control-and-caller-identity)
define host-label limits and the frontend's outstanding rate limiting.

## Backend components

`data/` holds the `.portal` file, activation file and tracking XML. `src/` contains:

| Component | Role |
|---|---|
| [`webauthentication-impl.h`](../src/webauthentication-impl.h) | Impl skeleton, frontend-owner check and defensive argument validation |
| [`transaction.h`](../src/transaction.h) | One terminal result, deadline, Request and teardown hooks |
| [`webkit-session.h`](../src/webkit-session.h) | Web view, navigation and TLS decisions; uses the assigned storage mode |
| [`chrome.h`](../src/chrome.h) | Frontend-established caller, identity level, engine origin and separately labeled application title |
| [`external-window.h`](../src/external-window.h) | `x11:<xid>` / `wayland:<handle>` parsing and parenting; invalid handles produce an unparented window |
| [`completion.h`](../src/completion.h) | Parsed URI matching, also implemented in the frontend |
| [`storage.h`](../src/storage.h) | Storage-mode selection and persistent paths |
| [`tls/`](../src/tls/) | Certificate adapter and its providers |
| [`redact.h`](../src/redact.h) | Typed logging fields and credential redaction |

[Security](SECURITY.md#implemented-controls-and-gaps) lists engine controls and their limits.
[Storage rules](SECURITY.md#fixed-behavior-and-storage) define per-app persistence, ephemeral
sessions and engine state coverage.

## Certificate adapter

Both providers construct a `GTlsCertificate` using `g_tls_certificate_new_from_pkcs11_uris()`:

- `portal` resolves the synthetic token described in
  [`portal-token.h`](../src/tls/portal-token.h) through the Certificate portal's installed
  PKCS#11 module. This is the preferred provider;
  [Security](SECURITY.md#portal-provider) defines its token and PIN contract.
- `pkcs11` resolves a p11-kit token named by `--client-cert-uri`, with a PIN supplied through
  `--client-cert-pin-file`; [Security](SECURITY.md#pkcs11-provider) defines PIN handling.

The Certificate backend is optional. Both frontends live in the same xdg-desktop-portal process;
this backend calls the Certificate public interface as an ordinary client. Backends do not call
other backends directly. [S2](SPIKES.md#s2-webkit-and-brokered-certificates) records WebKit
URI transport, its API limits and dated module tests.
[ADR 0007](decisions/0007-certificate-adapter.md) records the adapter decision.

[Security](SECURITY.md#attribution-and-consent) specifies certificate attribution and
delegation requirements. [Testing](TESTING.md#two-grants-per-fresh-handshake) records grants
and chooser counts.

## Consumer and process lifetime

[entra-token-helper](https://github.com/sjtrotter/entra-token-helper) is a one-shot CLI;
[Roadmap](ROADMAP.md#current-evidence) describes its protocol and account responsibilities.
The projects have no build dependency in either direction
([ADR 0006](decisions/0006-two-repositories.md)). Its `docs/CLI.md` defines its request contract.

Applications follow the [public Request contract](PUBLIC-INTERFACE.md#start) without selecting
backend names or reading `.portal` files. The first consumer reports an unavailable interface
with exit 40, allowing another provider to run.

Concurrent requests have separate impl calls, windows and Requests. Neither portal serializes
transactions. Per-account refresh serialization belongs to the consumer and remains outstanding;
rotating refresh tokens require atomic replacement and serialization.

Both portal halves are D-Bus activated. A terminal transaction unexports its impl Request;
[transaction rules](SECURITY.md#transactions) and
[grant lifetimes](SECURITY.md#certificate-grant-and-connection-lifetimes) define teardown.

## AVD integration evidence

FreeRDP requests an ARM gateway bearer token through `ACCESS_TOKEN_TYPE_AVD`, then an RDS
proof-of-possession token through `ACCESS_TOKEN_TYPE_AAD`. [S1](SPIKES.md#s1-refresh-to-a-new-proof-of-possession-key)
records the 2026-09-05 Government-tenant result; [Testing](TESTING.md#live-token-exchange)
provides commands and detailed evidence.

The client validates the response and exchanges the authorization code with its PKCE verifier.
FreeRDP owns the RDS PoP key, constructs base64url `req_cnf` containing its `kid`, supplies the
session-host resource scope, uses the ARM token in `FreeRDP_GatewayHttpExtAuthBearer`, and
completes the RDS-AAD nonce handshake itself. The client does not own device enrollment,
PRTs or Conditional Access device claims; those belong to the Microsoft broker and `sso-mib`.

## Alternative backends

A system-browser, paste or browser-extension implementation would be a separate backend selected
through `portals.conf`, with its own `.portal` declaration. The preference is a system browser
where the completion can be returned safely under RFC 8252, a hosted engine where interception or
a card requires it, a paste fallback for headless use, and explicitly installed experimental
extensions. Capability negotiation is absent from version 1. [Roadmap](ROADMAP.md) tracks these
implementations and accessibility work.
