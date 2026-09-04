# The public portal interface

Status: **incubating, version 1, nothing implemented.** The machine-readable description is
[`service/frontend/data/io.github.sjtrotter.portal.WebAuthentication1.xml`](../service/frontend/data/io.github.sjtrotter.portal.WebAuthentication1.xml);
this document explains what it means and why it is shaped this way.

**This is the interface applications call, and the only one they may.** Behind it is a portal
frontend that routes to a backend over a second, private interface; that one is
[IMPL-INTERFACE.md](IMPL-INTERFACE.md) and is not for applications. An application sees a portal,
not an architecture: it never names a backend, never reads a `.portal` file, and cannot tell which
backend served it.

**On the names.** Both interfaces live in a project-controlled reverse-DNS namespace with a major
version in it, as the D-Bus specification recommends. They are deliberately **not**
`org.freedesktop.portal.*` / `org.freedesktop.impl.portal.*`: that namespace belongs to
xdg-desktop-portal, and shipping a name from it would assert an ownership and an acceptance that do
not exist. What *is* copied — closely, deliberately and in full — is xdg-desktop-portal's shape:
the `Desktop` bus name, the object path, the `Request` pattern, the frontend/backend split and the
`.portal` discovery mechanism. Copying a shape is not claiming a namespace. Why the shape was
adopted before acceptance is
[decisions/0008-build-to-the-upstream-shape.md](decisions/0008-build-to-the-upstream-shape.md); the
exact rename that acceptance would mean is [UPSTREAMING.md](UPSTREAMING.md).

```
bus name         io.github.sjtrotter.portal.WebAuthentication
object path      /io/github/sjtrotter/portal/WebAuthentication
interface        io.github.sjtrotter.portal.WebAuthentication1
request objects  /io/github/sjtrotter/portal/WebAuthentication/request/<sender>/<handle_token>
request interface io.github.sjtrotter.portal.Request
```

This is this project's own incubating bus name — not a stand-in shared with any sibling
project — standing in for the interface's eventual home alongside every other portal on
`org.freedesktop.portal.Desktop`. See
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md), "Per-project bus names during
incubation".

## What the portal is for

An application needs the user to complete a sign-in that only works in a browser. It knows the URI
to open, and it knows what the end of the flow looks like: a navigation to a URI it recognises. It
needs that browsing to happen somewhere it can trust — somewhere that will answer a smart-card
challenge, and that will stop *before* the final URI is fetched.

That is the whole job: **one interactive web authentication transaction, returning an
uninterpreted completion artifact.** Neither half of the portal knows what OAuth is. The backend
knows just enough about smart cards to recognise a client-certificate challenge and hand it to an
adapter — preferably one in another process altogether. See "Client certificates" below.

## Interface summary

```
io.github.sjtrotter.portal.WebAuthentication1

  Start(s parent_window, s start_uri, s completion_uri, a{sv} options) → o request_handle
  property u version                                                        (1)

io.github.sjtrotter.portal.Request

  Close()                                  cancel — there is no separate Cancel method
  Response(u response, a{sv} results)      emitted exactly once
```

## `Start`

| Argument | Type | Meaning |
|---|---|---|
| `parent_window` | `s` | The caller's window, in the xdg-desktop-portal window-identifier format (`x11:<xid>`, `wayland:<exported handle>`). May be empty. An invalid or expired identifier degrades to an unparented window; it never aborts the transaction. |
| `start_uri` | `s` | The URI to open. Absolute `https`, with a host. |
| `completion_uri` | `s` | The URI whose navigation ends the transaction. Matched **exactly**; see below. |
| `options` | `a{sv}` | See below. |
| → `request_handle` | `o` | The `Request` object path. Subscribe to `Response` on it **before** calling `Start`. |

### Options

| Key | Type | Meaning |
|---|---|---|
| `handle_token` | `s` | Caller-chosen unguessable token forming the last element of `request_handle`, so the caller can subscribe before calling and avoid the response race. |
| `activation_token` | `s` | XDG activation token authorising the window to take focus. |
| `session_mode` | `s` | `shared` (default) or `ephemeral`. See "Sessions and storage". |
| `timeout` | `u` | Seconds. The caller may only *shorten* the default of 300; the hard ceiling is 900. |
| `title` | `s` | A purpose hint. Shown beneath the portal's own chrome and marked as coming from the application. Never the window's trusted identity. |

Unknown option **keys** are ignored, so an option can be added without a version bump. Unknown
**values** for a known key are an error: silently falling back from `ephemeral` to `shared` would
be a security decision made by a typo.

Note what is *not* an option, because the caller must not control it: whether TLS errors are
ignored, whether the security chrome is shown, which storage partition is used, arbitrary
certificate trust, PIN persistence, an unlimited timeout, and the verified application label shown
to the user.

## `Response`

Emitted exactly once per transaction, on `request_handle`.

| `response` | Meaning | `results` |
|---|---|---|
| `0` | A top-level navigation matched `completion_uri`. | `completion_uri s` — the matched URI, complete and **undecoded**, including query and fragment, obtained from the engine rather than reconstructed. |
| `1` | Cancelled: the user closed the window, cancelled in a certificate chooser or PIN prompt, or the caller called `Close()`. | optionally `reason s` |
| `2` | Ended some other way: timeout, no backend configured, no engine, no display, the backend terminated or misbehaved, or a client certificate was needed and no adapter could run. | optionally `reason s` |

These are the portal convention's codes with the portal convention's meanings. The distinction
between `1` and `2` matters: `1` means the user said no and an immediate automatic retry is wrong;
`2` means the portal could not do its job, and falling through to another mechanism is reasonable.

`completion_uri` is returned undecoded because the caller must do its own strict parsing, and any
normalisation performed here would have to be either undone or trusted.

A non-zero response may carry an optional `reason` key: a stable symbol such as `timeout`,
`no_display`, `session_terminated`, `no_certificate_adapter`, or one of the split's own —
`no_backend`, `backend_disappeared`, `backend_completion_mismatch`, `backend_protocol_error`
([IMPL-INTERFACE.md](IMPL-INTERFACE.md)). It is a diagnostic, never a substitute for the response
code, and consumers must tolerate its absence.

A malformed *request* is not a response at all: the frontend returns a D-Bus error from `Start`,
before any backend is woken and any window is opened.

## Completion matching

This is the security-critical rule of the interface, and this section is its single definition —
[IMPL-INTERFACE.md](IMPL-INTERFACE.md) explains where it is enforced (the backend, against live
navigations; and the frontend again, against what the backend returns) and why one rule needs two
enforcement points. Matching is **exact**, on **parsed** URIs, and there is **no prefix mode in
version 1**.

A top-level navigation completes the transaction when all of the following hold:

1. Schemes equal, compared case-insensitively.
2. Hosts equal, compared case-insensitively after IDNA normalisation.
3. Effective ports equal, with explicit and default ports normalised deliberately (`:443` and the
   default are the same port).
4. Paths equal, **exactly**.
5. The candidate carries no userinfo.
6. Query and fragment carry the result and take **no part** in matching.
7. Nothing about either URI is malformed: control characters, malformed percent escapes, `%00`,
   backslashes in authority-sensitive positions, and URIs that parse differently between libraries
   are rejected rather than normalised.

So `https://example.com/callback` matches `https://example.com/callback?code=…#x`, and matches
none of:

```
https://example.com/callback.evil        different path
https://example.com/callbacker           different path
https://example.com@evil.invalid/        host is evil.invalid; userinfo forbidden anyway
https://EXAMPLE.COM:443/callback         matches, in fact — case and default port normalise
```

The reason `%00` is called out: a decoder that turns it into an embedded NUL lets a component
carry more than the C string it decodes to shows, and every subsequent comparison stops at the
NUL, so a different host or path can pass as the expected one.

**Custom schemes** (`ms-appx-web://`, an application's registered scheme) are allowed, with exact
structural matching, no wildcard schemes, and — importantly — **never dispatched externally**. The
navigation is intercepted before any attempt at external protocol handling. Naming a scheme does
not prove owning it: scheme ownership belongs to the identity provider's client registration and
to the caller's own validation, and the portal's contribution is to show the frontend-derived caller
identity in its chrome and bind the result to the connection that asked.

**One completion URI in version 1.** Intermediate HTTP and JavaScript redirects are ordinary
navigation and need no API feature. If a demonstrated consumer ever needs several valid completion
URIs, the answer is an array of independently parsed exact rules — never one broad common prefix.

**Path-prefix matching**, if it ever becomes necessary, requires a path-segment boundary and must
be a separately named mode. It is not what version 1 does.

## What version 1 does not support

**POST completions.** OpenID Connect's `response_mode=form_post` and SAML's HTTP-POST binding
deliver the credential-bearing fields in a form body, not a URI. They cannot be represented as a
`completion_uri`, and squeezing a form body into a synthetic URI is not an acceptable substitute:
it would invent a representation, hide a content type, and change the memory, logging and
credential-handling obligations of every consumer.

Supporting them later means a separately reviewed result of roughly the shape
`{ method, uri, content_type, body_fd }`, added behind a capability. Until then, version 1 is
honestly "web authorization **navigation**", not universal protocol-agnostic authentication.

## Client certificates

The backend satisfies a TLS client-certificate challenge through an **internal adapter** with two
implementations. Which one ran is not visible through this interface, and callers must not depend on
either.

**`portal` — preferred where available.** The backend asks the certificate portal (repository
`smartcard-portal`, public interface `io.github.sjtrotter.portal.Certificate1`, a separate project),
as an ordinary client of *its* public interface — a backend never calls another backend. Its
`AcquireCredential` returns a grant — the certificate and chain, the permitted operations and
mechanisms, an expiry — after the user has chosen and unlocked in *its* windows. The PIN never
reaches the backend. The operation is then satisfied either by **brokered signing** (a `Sign` call
per operation, behind a GnuTLS external-signer path) or by an **experimental PKCS#11 endpoint**.

**`inproc` — the fallback, and the path known to work.** Enumerate with p11-kit, show the backend's
own chooser and PIN prompt, and build the certificate with `g_tls_certificate_new_from_pkcs11_uris()`
against the **system** p11-kit configuration.

### Why the fallback is not scaffolding

The ends of the chain are documented and fine: `g_tls_certificate_new_from_pkcs11_uris()` takes
certificate and key URIs with the key accessed only later during use, and
`webkit_credential_new_for_certificate()` takes a `GTlsCertificate`. The middle is not:

- **A PKCS#11 URI cannot name a socket.** p11-kit remoting needs `p11-kit-client.so`, a
  `P11_KIT_SERVER_ADDRESS`, and the client module registered in p11-kit *configuration* for GnuTLS
  and OpenSSL consumers to find it.
- **GLib's constructor has no module parameter.** GnuTLS can load providers programmatically; GLib's
  public constructor does not expose that.
- **WebKit's network process may not see a module registered after it started** — and it is not even
  settled which process opens the socket, or when.

So "call the smart card portal and build a certificate from a returned URI" is an assumption until spike
[S2](SPIKES.md) has completed a real WebKitGTK mutual-TLS handshake that way. Until then this
portal has **no hard dependency** on the smart card portal: a machine without one still signs in.

Stock `p11-kit server` also scopes to a *token*, not to an object, so "a module scoped to the chosen
certificate" needs a restricted facade that does not exist yet. If dynamic registration turns out to
be impossible, the most plausible shape is one permanently registered broker module exposing
synthetic grant-bound slots — a change to the other project's interface, not to this one.

### What the backend owns either way

Recognising the challenge, determining and displaying the origin that raised it, refusing challenges
from hosts unrelated to the page being shown, binding the whole thing to one cancellable
transaction, and releasing whatever the adapter held on **every** exit path. Cancelling in either
chooser or either PIN prompt cancels this transaction, producing response `1`.

One thing the split did **not** fix and made visible: under the `portal` adapter the smart card
portal derives *the backend's* identity, not the application's, so its consent window names
`webauth-portal-gtk`. The original app id can only be passed as untrusted text. Attested delegation
across one portal hop is a protocol neither project has; see [SECURITY.md](SECURITY.md).

When neither adapter can run, the challenge is declined and the transaction ends with response `2`
and reason `no_certificate_adapter`.

See [decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md).

## Sessions and storage

```
session_mode = shared | ephemeral        (default: shared)
```

**`shared`** is one website data store used by all of the backend's transactions. It is the
normal desktop SSO behaviour, and it is the closest thing this portal has to what
`ASWebAuthenticationSession` and Custom Tabs do by default. It must be described plainly, in the
documentation and in the UI: **shared means shared among the backend's own transactions. It is
not Firefox, not Chrome, and not the user's default browser.** It does not inherit their accounts,
enterprise policies, extensions, device registration or browser-bound credentials.

**`ephemeral`** runs the transaction in an ephemeral website data store created with it and
destroyed with it. "Use the persistent store and clear it afterwards" is not equivalent and is
race-prone. A caller may *request* ephemeral; a caller may never *defeat* a policy that forces it.

**There is no per-application persistent mode in version 1.** Per-app jars are not what the
platform analogues do, they sacrifice cross-application SSO while keeping long-lived tracking and
stale-session risk, and — decisively — they would have to be keyed on a caller identity that, for
an unsandboxed host caller, cannot be established (see "Caller identity"). The frontend/backend
split makes such a mode *more* plausible than it was, since a sandboxed app id derived by the
frontend is authenticated metadata, but more plausible is not decided. If experience later
shows a need, it should be called `app_persistent` and should define exactly how the partition
identity is established.

**A partition is not a cookie jar.** The boundary covers cookies, local and session storage,
IndexedDB, HTTP and disk caches, service workers, HSTS and related network state, HTTP
authentication credentials, permission decisions, and client-certificate selection memory.
Downloads and autofill are disabled rather than partitioned.

The **PIN** is never partitioned, because it is never persisted: with the portal adapter it is
entered in another process and never arrives here at all; with the in-process adapter it is read,
passed to the challenge, and the buffer cleared on every exit path. A certificate choice is not
remembered across transactions by the backend under either adapter.

What *is* partitioned, and easy to forget, is the engine's own **client-certificate selection
memory**: WebKit will happily remember which certificate satisfied which origin, and that memory
belongs to the storage partition like everything else.

## Caller identity

An executable path is not an application identity. A same-UID process can execute another path,
manipulate its launch context, or connect straight to the bus. **The frontend derives it and the
backend is told**, which is the largest single thing the split buys: the derivation happens in a
process the application cannot talk to, and its result reaches the window that names the
application as a fact rather than as a claim. Note that this interface has no `app_id` argument at
all, precisely so that there is nothing for a caller to assert. The interface distinguishes:

- **Sandboxed** — a Flatpak or Snap identity obtained through the containment framework's
  mediation. Authenticated metadata.
- **Cgroup-derived** — a desktop identity inferred from the unit. A useful *label*, not a security
  principal.
- **Unverified** — an unsandboxed peer. Its unique D-Bus name and UID are reliable; its
  application publisher is not established at all. A caller-supplied app id is only a claim.

Consequences, all enforced rather than advised:

- Every result is bound to the **initiating unique D-Bus connection**. If that connection goes
  away, the transaction is cancelled and nobody else receives the result.
- The chrome displays what the frontend verified, and says plainly when it could not.
- An unverified label is **never** the sole key for a storage partition, and an unverifiable host
  caller gets `shared` or `ephemeral` — never a partition it named.
- First use by an unidentified host caller may warrant an explicit confirmation, particularly
  before client-certificate access.
- Requests are rate-limited. Repeated background requests from one connection are the cheapest way
  to turn this portal into a phishing launcher.

Same-UID is a necessary check, not a complete authorization policy. See [SECURITY.md](SECURITY.md).

## Fixed behaviour

These are protocol, not options. A caller cannot turn any of them off, and an implementation that
makes one configurable has changed the interface.

- Request handles and transaction binding; the response codes and their meanings.
- URI parsing and matching rules, exactly as above.
- Interception of the completion navigation **before load or submit**.
- The frontend re-checking the returned completion URI against the requested one.
- Caller bus disconnection cancels the transaction.
- Exactly one terminal response per transaction; a committed completion wins over a simultaneous
  `Close()`; every other late event is discarded.
- No TLS-error bypass; no arbitrary certificate trust exception.
- Portal-controlled security chrome, showing the real page origin and the frontend-derived caller
  identity independently of any caller-supplied text.
- The storage partition is selected by portal policy, in the frontend.
- A default timeout and a hard maximum.
- Maximum URI and response sizes.
- No URI logging and no page-content logging, at any level.
- Defined cleanup when the backend crashes: one defined failure response, no leaked windows, no
  orphaned partitions. With the split this is the frontend's obligation, and its reason symbol is
  `backend_disappeared`.

## Cancellation and timeouts

`Close()` is the cancellation mechanism; there is deliberately no separate `Cancel` method.

- Closing the window produces `1`.
- `Close()` never produces a later success response.
- Caller bus disconnection cancels immediately.
- A timeout closes the web view and answers `2`.
- A completion already atomically committed wins over a simultaneous cancellation.
- Certificate and PIN dialogs belong to the transaction and close with it.
- Browser-session termination produces one defined failure.
- `Close()` on an already-answered request is a no-op, not an error: a caller that times out
  locally at the moment the user finishes must not get an error and must not get two responses.

The 300-second default with a 900-second ceiling is chosen because smart-card and MFA flows make
short fixed timeouts hostile — a user hunting for a card reader is not a stalled transaction.

## Parenting and activation

`parent_window` uses the existing portal window-identifier convention: Wayland identifiers are
exported through `xdg_foreign`, X11 uses an XID, and empty is allowed when the caller genuinely has
no parent.

`activation_token` is separate and not interchangeable. The parent makes the window transient and
modal to the caller; the activation token authorises taking focus. An invalid or expired parent
degrades to an unparented, portal-controlled window and must never abort authentication. A
background caller without a valid activation token does not silently steal focus.

## Accessibility

Acceptance criteria, not refinements. WebKit covers accessibility of the *web content*; it covers
neither the security chrome, nor the certificate and PIN dialogs, nor the transaction's state
transitions — exactly the parts that carry the security decisions.

- AT-SPI exposure for every portal-owned control, including the in-process chooser and PIN prompt
  where that adapter is in use. (Under the portal adapter those are the smart card portal's
  controls and carry the same obligation on its side.)
- Keyboard-only certificate selection and PIN entry.
- Meaningful focus order, including across any hand-off to another portal's windows.
- Screen-reader announcement of the verified requesting application and the current site.
- Scalable text, high contrast, reduced-animation behaviour.
- No information conveyed by colour alone.
- Accessible error and cancellation states.
- Correct focus restoration to the calling application when the window closes.

## Versioning

The `version` property carries the interface version. Within version 1: the argument list of
`Start` does not change, the response codes keep their meanings, and new option and result keys may
be added — consumers must ignore keys they do not recognise. Anything else needs version 2, and a
new major version appears in the bus name, the object path and the interface name.

Until this has been implemented, exercised by a second consumer, and argued about in public,
version 1 should be read as "what it will be if this survives contact", not as a promise.

## Example

Caller side, with the standard subscribe-before-call ordering:

```
handle_token = unguessable_random()
handle = "/io/github/sjtrotter/portal/WebAuthentication/request/<unique name>/" + handle_token
subscribe(handle, "Response", on_response)

WebAuthentication1.Start(
    parent_window  = "wayland:<exported handle>",
    start_uri      = "https://login.microsoftonline.us/<tenant-id>/oauth2/v2.0/authorize?...",
    completion_uri = "https://login.microsoftonline.com/common/oauth2/nativeclient",
    options = { handle_token:     handle_token,
                activation_token: "<token>",
                session_mode:     "shared",
                timeout:          300,
                title:            "Sign in to Azure Virtual Desktop" })

on_response(0, { completion_uri:
    "https://login.microsoftonline.com/common/oauth2/nativeclient?code=…&state=…" })
```

Note that the completion URI here is a **commercial-cloud** URL even though the sign-in ran
against the US Government authority. That is what the Azure Virtual Desktop application
registration says, and it is exactly the kind of protocol-specific fact this portal must not know
about and must not "correct".

What the caller does next — check `state`, insist on exactly one of `code` or `error`, decode
strictly, exchange the code — is in [ENTRA-CLIENT-CLI.md](ENTRA-CLIENT-CLI.md) and
[`clients/entra/src/oauth/callback.h`](../clients/entra/src/oauth/callback.h). None of it is this
portal's business.
