# Backend interface

This experimental version 1 interface connects xdg-desktop-portal to a desktop backend.
Applications use the [public interface](PUBLIC-INTERFACE.md). This backend checks that every
`Start` and impl `Request.Close()` comes from the owner of `org.freedesktop.portal.Desktop`;
see [Security](SECURITY.md).

| Property | Value |
|---|---|
| Bus name | `org.freedesktop.impl.portal.desktop.webauth` |
| Object path | `/org/freedesktop/portal/desktop` |
| Interface | `org.freedesktop.impl.portal.experimental.WebAuthentication1` |
| Impl Request | Frontend handle path, exported on the backend bus name |
| Backend declaration | `$datadir/xdg-desktop-portal/portals/webauth.portal` |

## XML source

[The installed XML](../data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml)
is byte-identical to `data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml` on
the frontend's `experimental/integration` branch apart from the provenance comment.
Update the source there, then copy it here. The planned packaging uses xdg-desktop-portal's
pkg-config interfaces directory when a release supplies this file.
[ADR 0008](decisions/0008-build-to-the-upstream-shape.md) records the interface split.

## Start and Request

```
Start(o handle, s app_id, s parent_window, s start_uri, s completion_uri, a{sv} options)
    -> (u response, a{sv} results)
```

`handle` is the public Request path. Export `org.freedesktop.impl.portal.Request` there on
the backend's bus name for forwarded `Close()` calls. `app_id` is frontend-derived; an empty
value means unidentified. `parent_window` follows it in the argument list.

The method returns the result directly. The frontend converts it into a public Response and
uses a proxy timeout of `G_MAXINT` with a separate transaction deadline. The convention matches
`org.freedesktop.impl.portal.Account.GetUserInformation(o handle, s app_id, s window,
a{sv} options) -> (u response, a{sv} results)`.

### Forwarded options

| Key | Type | Backend obligation |
|---|---|---|
| `activation_token` | `s` | Passed through; use for activation where supported. |
| `session_mode` | `s` | Validated `shared` or `ephemeral`. Honor the isolation requirement or fail. |
| `timeout` | `u` | Always forwarded; default 300 seconds, ceiling 900. Run a backend timer. |
| `title` | `s` | Untrusted, length-limited application text. |
| `app_identity_level` | `s` | Display `sandboxed`, `host` or `unidentified` as established by the frontend. |

The frontend consumes `handle_token` and drops unknown keys. Backends ignore unknown options;
frontends ignore unknown result keys. Version 1 has no capability negotiation: a backend claiming
the interface must implement its contract. Selection of an alternative backend is through
`portals.conf`.

## Enforcement

| Concern | Frontend | Backend |
|---|---|---|
| Caller identity | Derives app ID and identity level; checks caller UID | Checks frontend owner; consumes supplied identity |
| URI validation | Rejects malformed requests before forwarding | Revalidates independently |
| Options and storage policy | Filters keys, validates values and selects mode | Validates defensively; may force empty app ID to ephemeral |
| Rate limiting | Required per connection; unimplemented | No per-application connection to limit |
| Public Request | Exports before impl call; binds results to caller | Exports impl Request for Close |
| Public Response | Emits at most once; handles backend failure | Returns once |
| Completion | Rechecks returned URI | Intercepts matching navigation before load, in every frame |
| Parent window | Forwards identifier opaquely | Parses `x11:` or `wayland:` and parents window |
| UI, engine and TLS | No display or toolkit | Owns window, security chrome, storage and certificate adapter |
| Frontend death | Application sees bus-name loss | Cancels transactions and destroys windows |

### Completion checks

The [matching rule and application checks](PUBLIC-INTERFACE.md#completion-matching) apply
to live backend navigations and frontend verification of the returned URI.

[`completion.c`](../src/completion.c) and the frontend's `completion_uri_matches()` use GLib.
Both matchers parse with `G_URI_FLAGS_ENCODED`; unreserved escapes still normalize.
`https://example.com/call%62ack` matches `https://example.com/callback`.
[The public guide](PUBLIC-INTERFACE.md#completion-matching) gives further examples.
[`test-completion.c`](../tests/test-completion.c) includes frontend cases marked `FRONTEND`.
Additional backend fixtures still need porting to the frontend; a disagreement blocks release.

`WebKitNavigationPolicyDecision` has no frame identity in WebKitGTK 6.0. A navigation action's
frame name identifies a link target and can be NULL; `WebKitFrame` belongs to web-process
extensions. `on_decide_policy()` therefore intercepts in any frame, at navigation and response
decisions. Embedded content can end the flow with an injected completion URI; frontend matching
and application protocol validation remain required.

### Results and reasons

The XML gives an open list of reasons: `timeout`, `no_display`, `no_engine`, `user_cancelled`,
`session_terminated` and `credential_unavailable`. This backend also defines the following in
[`transaction.h`](../src/transaction.h):

| Reason | Condition | Response |
|---|---|---|
| `request_closed` | Frontend forwarded Close | `1` |
| `unrelated_certificate_challenge` | Challenge host unrelated to the flow | `2` |
| `tls_error` | Server certificate verification failed | `2` |
| `load_failed` | Page load failed without a more specific reason | `2` |
| `invalid_request` | Backend revalidation failed | `2` |
| `no_storage` | Required data store could not be created | `2` |

The frontend forwards unfamiliar reason strings. A cancelled certificate chooser produces
`credential_unavailable`, response `2`: GnuTLS reports an unavailable object for cancellation,
missing providers and policy refusal, so this backend cannot distinguish them.
[Testing](TESTING.md) records 1.9 seconds from Escape to completion.

A malformed impl request returns `(2, {"reason": "invalid_request"})`. A D-Bus error from the
impl call would reach the application as `backend_disappeared`.

### Deadlines and lifetimes

The frontend races the impl call against `dex_timeout_new_seconds(timeout)`. The backend timer
is in [`transaction.c`](../src/transaction.c). [Transaction rules](SECURITY.md#transactions)
define the deadlines and teardown obligations.

Public Close unexports the Request before forwarding, so no Response follows it. A committed
completion remains terminal. Backend death produces `2` / `backend_disappeared`; frontend death
cancels the window. A success response with a missing or wrong-typed `completion_uri` result
produces `backend_protocol_error`; an unparsable `completion_uri` string or a mismatched URI
produces `backend_completion_mismatch`. An impl call failure produces `backend_disappeared`.
If no backend is configured, or opt-in is missing, the public interface is absent.

`portal_release()` logs `certificate-released grant=retained_until_expiry`.
[Grant and connection lifetimes](SECURITY.md#certificate-grant-and-connection-lifetimes)
explain why transaction teardown leaves grants and authenticated connections alive.

## Configuration

Installed files are `$libexecdir/xdg-desktop-portal-webauth`, the `.portal` declaration above,
`$datadir/dbus-1/services/org.freedesktop.impl.portal.desktop.webauth.service` and interface XML
under `$datadir/dbus-1/interfaces`.

To implement another backend, own `org.freedesktop.impl.portal.desktop.<name>` in a
D-Bus-activatable executable and implement the interface at `/org/freedesktop/portal/desktop`.
Install `<name>.portal` into `$datadir/xdg-desktop-portal/portals/`:

```ini
[portal]
DBusName=org.freedesktop.impl.portal.desktop.<name>
Interfaces=org.freedesktop.impl.portal.experimental.WebAuthentication1;
UseIn=<desktop>
```

Select it in `portals.conf`; use `webauth` as `<name>` for this backend:

```ini
[preferred]
org.freedesktop.impl.portal.experimental.WebAuthentication1=<name>
```

Start the frontend with `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=WebAuthentication1`.
For Certificate too, use `WebAuthentication1,Certificate1`. Preserve existing names and append
only missing names, separated by commas without spaces.
[`dev-stack.sh`](../tools/dev-stack.sh) sets up this backend on a private bus;
`portal-stack.sh` adds Certificate. [Testing](TESTING.md) gives prerequisites.

The backend accepts `--cert-adapter auto|portal|pkcs11|none`.
[Security](SECURITY.md#portal-provider) defines `auto` selection and its limited fallback. `pkcs11` uses
`--client-cert-uri`, an optional `--client-key-uri`, and `--client-cert-pin-file` where needed.
All backends must meet the backend obligations in [Security](SECURITY.md).
