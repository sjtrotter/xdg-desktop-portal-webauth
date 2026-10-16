# Public interface

Applications call the frontend in
[xdg-desktop-portal, branch `experimental/integration`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration).
The branch's `data/org.freedesktop.portal.experimental.WebAuthentication1.xml` is authoritative.
[ADR 0010](decisions/0010-backend-only-frontend-lives-upstream.md) records frontend ownership.

| Component | Path on the frontend branch |
|---|---|
| Public XML | `data/org.freedesktop.portal.experimental.WebAuthentication1.xml` |
| Impl XML | `data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml` |
| Frontend | `desktop-portal/web-authentication.c` |
| Mock backend | `tests/templates/webauthentication.py` |
| Tests | `tests/test_webauthentication.py` |

This repository's
[impl XML](../data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml)
is byte-identical to the frontend branch's impl XML apart from the provenance comment.

## Export and versioning

| Property | Value |
|---|---|
| Bus name | `org.freedesktop.portal.Desktop` |
| Object path | `/org/freedesktop/portal/desktop` |
| Interface | `org.freedesktop.portal.experimental.WebAuthentication1` |
| `version` | `1` |

Export requires a configured backend and `WebAuthentication1` in
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` in the frontend's startup environment. To enable the
Certificate portal too, use `WebAuthentication1,Certificate1`. The list contains comma-separated
names without spaces. Without these conditions the interface is absent from introspection and
`Properties.Get` fails. Clients must handle an unavailable interface.

Experimental public interfaces use `org.freedesktop.portal.experimental.<Name>1`; their impl
counterparts use `org.freedesktop.impl.portal.experimental.<Name>1`, at the same object path.
A breaking change increments the trailing number; a non-breaking change increments `version`.
The experimental interfaces can be removed and have no stability guarantee.

## Start

```
Start(s parent_window, s start_uri, s completion_uri, a{sv} options) -> o handle
```

`Start` returns a Request handle. Subscribe to its `org.freedesktop.portal.Request.Response`
signal before calling, using the predictable path from `handle_token`. The frontend exports
the Request before forwarding to a backend. There is no portal Session object; `session_mode`
selects website storage.

`start_uri` must be absolute HTTPS with a host. `completion_uri` must be absolute, without
userinfo or a wildcard host. HTTP and HTTPS require a host. Private-use redirects such as
`com.example.app:/oauth2redirect` are accepted with a path and no authority, per RFC 8252
section 7.1. A URI with neither host nor path is rejected. Both URIs have bounded length;
control characters, backslashes and malformed URI syntax are rejected.

Malformed requests produce a D-Bus error before a backend is called. A sandboxed application
without network access receives `org.freedesktop.portal.Error.NotAllowed`.

### Options

| Key | Type | Meaning |
|---|---|---|
| `handle_token` | `s` | Valid object-path element naming the Request; consumed by the frontend. |
| `activation_token` | `s` | Window activation token, passed through to the backend. |
| `session_mode` | `s` | `shared` (default) or `ephemeral`; unknown values fail. This backend shares persistent storage per app ID and forces an empty app ID to ephemeral. Browser profiles are separate. |
| `timeout` | `u` | Positive seconds; default 300, clamped to 900. The frontend enforces the deadline and forwards the value for the backend's timer. |
| `title` | `s` | Single-line application-supplied hint, at most 256 characters. Displayed separately from verified identity. |

Unknown keys are dropped. The frontend adds `app_identity_level` for the backend.

### Response and cancellation

`Response(u response, a{sv} results)` uses `0` completed, `1` cancelled and `2` other.
A completed response contains `completion_uri` (`s`), the complete URI obtained from the engine,
including query and fragment. The frontend rechecks it against the requested URI.

A non-zero response may contain `reason` (`s`), a diagnostic symbol. Frontend symbols include
`timeout`, `backend_disappeared`, `backend_completion_mismatch` and
`backend_protocol_error`. Backend examples include `no_display`, `session_terminated` and
`credential_unavailable`. Clients must tolerate absent and unfamiliar reasons.

At most one Response is emitted. `Request.Close()` cancels and unexports the Request; it does
not produce a later Response. Timeout closes the impl Request and emits `2` with reason `timeout`.
[The impl guide](IMPL-INTERFACE.md) describes the failure paths.

## Completion matching

Match every navigation in any frame against parsed URI components:

- Compare scheme and host case insensitively. A host-bearing URI cannot match a hostless URI.
- Compare effective ports, with HTTP port 80 and HTTPS port 443 as defaults.
- Compare paths exactly in the parser's encoded form. GLib normalizes unreserved percent
  escapes, percent-escape case and dot segments; `%63allback` equals `callback`, `%2f` equals
  `%2F`, and `/a/../callback` equals `/callback`. Internationalized hosts match their punycode
  spelling. Empty path and `/` differ, as do hosts with and without a trailing dot.
- Reject userinfo. Reject a wildcard host in the requested completion URI.
- Exclude query and fragment from the comparison; they carry the result.

A matching navigation ends the flow before loading, including in a subframe. The frontend
discards a mismatched returned URI with `2` / `backend_completion_mismatch`. Applications must
validate their protocol response, including OAuth `state` and code cardinality.
[The impl guide](IMPL-INTERFACE.md#completion-checks) describes the WebKitGTK frame-identity limit.

The frontend matcher is `web-authentication.c:completion_uri_matches()`, covered by
`test_completion_mismatch_rejected`, `test_completion_normalisation_accepted` and
`test_completion_near_miss_refused`. This backend's matcher is
[`completion.h`](../src/completion.h) / `completion.c`, covered by `test-completion.c`.
[The impl guide](IMPL-INTERFACE.md) describes fixture sharing.

## Version 1 limits

Version 1 supports navigation (GET) completions. It has no POST completion shape
(`response_mode=form_post` or SAML HTTP-POST), prefix matching, wildcard matching or external
scheme dispatch. [ADR 0005](decisions/0005-service-shape.md) records the scope decision.

## Accessibility requirements

Backend-owned controls must expose AT-SPI labels, keyboard operation and meaningful focus
order, including across Certificate portal windows. The verified caller and current origin
must be announced to screen readers. Errors and cancellation must be accessible, color must
not be the sole signal, and focus must return to the calling application on close. The
Certificate portal owns its chooser and PIN controls. [Security](SECURITY.md) records coverage.

## Manual calls

[Testing](TESTING.md#manual-invocation-and-rejection-checks) gives invocation and rejection
checks with [`trigger-webauthentication.sh`](../tools/trigger-webauthentication.sh) and
completion testing with the persistent client.
[`dev-stack.sh`](../tools/dev-stack.sh) supplies the private-bus frontend and backend.
