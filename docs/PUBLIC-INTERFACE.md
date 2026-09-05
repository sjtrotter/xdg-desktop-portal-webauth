# The public interface is not in this repository

This document used to specify the public web-authentication interface at length, because
this repository shipped the frontend that exported it. It does not any more: the frontend
is a branch of xdg-desktop-portal, and **the XML on that branch is the specification**. See
[decisions/0010-backend-only-frontend-lives-upstream.md](decisions/0010-backend-only-frontend-lives-upstream.md).

## Where it is

```
repository   a local checkout of xdg-desktop-portal   (remote: flatpak/xdg-desktop-portal)
branch       experimental/certificate-webauthentication
commit       3a32e9b  web-authentication: Add an experimental WebAuthentication portal
public XML   data/org.freedesktop.portal.experimental.WebAuthentication.xml
impl XML     data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml
frontend     desktop-portal/web-authentication.c
mock backend tests/templates/webauthentication.py
tests        tests/test_webauthentication.py
```

The impl half is also here, as a verbatim tracking copy:
[`../backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml`](../backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml).
The public half is deliberately **not** copied: this repository has no reason to hold a
second copy of an interface it does not implement, and a stale one would be worse than
none.

Everything below is a summary for orientation. Where it disagrees with the XML, the XML is
right.

## Summary

Interface `org.freedesktop.portal.experimental.WebAuthentication`, on bus name
`org.freedesktop.portal.Desktop`, object path `/org/freedesktop/portal/desktop`,
`version` property `1`.

**It is not exported unless xdg-desktop-portal was started with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` containing `web-authentication`** (or `all`).
With the gate off it is absent from introspection and `Properties.Get` fails; a client
must treat that as *unavailable* and fall through, not as an error. It is experimental and
can change or be removed without a version bump.

```
Start (s parent_window, s start_uri, s completion_uri, a{sv} options) → o handle  [Request]
```

One method. It hands the desktop a URI to open and the exact URI whose navigation ends the
flow, and answers with the URI the flow finished on. There is **no `Session` object**:
`session_mode` names a website data store, not a portal Session, despite the word.

### The URIs

- `start_uri` must be an absolute **`https`** URI with a host.
- `completion_uri` must be an absolute URI with a host, **no userinfo** and no wildcard.
- A malformed request is a **D-Bus error out of `Start()`**, before any backend is asked
  and any window is opened.

### Options

| Key | Type | Meaning |
|---|---|---|
| `handle_token` | `s` | last element of the Request path, so the caller can subscribe before calling. Not forwarded to the backend. |
| `activation_token` | `s` | window activation, passed through. |
| `session_mode` | `s` | `shared` (default) or `ephemeral`. **An unknown value is an error, never a fallback.** An application may ask for `ephemeral`; it can never turn `ephemeral` off if policy requires it. `shared` means shared between flows run by this backend — never with the user's browser. |
| `timeout` | `u` | seconds; default 300, ceiling 900. A caller can only shorten it. |
| `title` | `s` | a short hint about what the sign-in is for. Application-supplied text, shown as such, never as the identity of the window. |

Unknown keys are dropped, not forwarded.

### Results, on the Request's `Response` signal

- `completion_uri` (`s`) — the matched URI, complete and undecoded, including query and
  fragment, obtained from the web engine rather than reconstructed. Present only when the
  response is `0`.
- `reason` (`s`) — an optional stable symbol explaining a non-zero response: `timeout`,
  `no_display`, `no_backend`, `backend_disappeared`, `backend_completion_mismatch`,
  `backend_protocol_error`, `session_terminated`, `no_certificate_adapter`. It is a
  diagnostic, never a substitute for the response code, and consumers must tolerate its
  absence.

Response codes are the portal's usual three: `0` completed, `1` cancelled, `2` other.

## Completion matching

The rule, from the public XML, is matched against **every navigation the web view is asked
to make, in any frame**, on the parsed URI:

- scheme and host compared **case insensitively**;
- effective ports compared with **default ports normalised** (`:443` equals the default);
- **paths compared exactly**;
- **no userinfo**;
- **query and fragment carry the result of the flow and take no part in matching**;
- **a match ends the flow and is never loaded**, whichever frame it happened in. The XML said
  "subframe navigations do not end the flow" until 2026-09-04; no backend on WebKitGTK 6 could
  keep that promise, and the wording now says what is enforced. See
  [IMPL-INTERFACE.md](IMPL-INTERFACE.md).

It is enforced twice and the two checks answer different questions: the **backend** decides
when to stop the browser, and must complete the transaction *before the matched navigation
loads*, because the completion URI routinely carries the credential the flow was for. The
**frontend** re-checks the URI the backend returned against the one the application asked
for, before the application sees anything; on a mismatch the response becomes `2` with
`reason` `backend_completion_mismatch` and the URI is discarded. That second check is why
"you get what you asked for" is a statement the *portal* makes, on the bus name the
application trusts, rather than a relay of whatever backend a distribution installed.

The frontend's half is implemented and tested upstream —
`web-authentication.c:completion_uri_matches()`, `test_completion_mismatch_rejected`, and
the negative control `test_completion_normalisation_accepted`. This repository's half is
[`../backend/src/completion.h`](../backend/src/completion.h) and has no implementation
yet; [IMPL-INTERFACE.md](IMPL-INTERFACE.md) explains why both exist.

## What version 1 does not support

Navigation (GET) completions only. No `response_mode=form_post`, no SAML HTTP-POST, no
prefix matching, no wildcards, no external scheme dispatch. Version 1 is honestly "web
authorization navigation", not universal protocol-agnostic authentication — see
[decisions/0005-service-shape.md](decisions/0005-service-shape.md).

## Accessibility

The chrome carries a security decision, and a user who cannot perceive it cannot make that
decision. AT-SPI exposure for every backend-owned control including any in-process
certificate chooser and PIN prompt, keyboard-only operation, meaningful focus order
including across a hand-off to another portal's windows, screen-reader announcement of the
verified caller and the current origin, no meaning conveyed by colour alone, accessible
error and cancellation states, and focus restored to the calling application on close.

These are **backend** obligations — this repository's — because the frontend draws nothing.
They are acceptance criteria rather than polish. See [SECURITY.md](SECURITY.md).

## Poking it by hand

[`../tools/trigger-webauthentication.sh`](../tools/trigger-webauthentication.sh) makes
these calls with `gdbus`, using `https://example.invalid/...` placeholders so nothing can
reach a real identity provider by accident, and includes the two rejection cases. Its
`start_uri` and `completion_uri` are overridable with `START_URI` and `COMPLETION_URI`.
[`../tools/dev-stack.sh`](../tools/dev-stack.sh) starts a development frontend and this
backend on a private bus first.
