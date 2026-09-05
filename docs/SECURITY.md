# Security model

Status: the backend is implemented; the Entra client is still a sketch. This document states the
rules the implementation must satisfy, and — in the table under "What is enforced today" — which of
them the code in `backend/` actually enforces, which are the frontend's, and which are still only
written down. A rule with no implementation is marked as such rather than left to be assumed.

There are four boundaries. **Two are in this repository.**

The **portal frontend** boundary establishes who is asking and what may be asked: it owns the bus
name applications call, derives the app id, validates the arguments, applies the policy a caller may
not influence, and guarantees exactly one answer. It draws nothing and it never sees a card, a PIN
or a page. **That boundary is xdg-desktop-portal**, branch
`experimental/certificate-webauthentication` — not this repository
([decisions/0010](decisions/0010-backend-only-frontend-lives-upstream.md)). Frontend-side rules
below are recorded as *provided by xdg-desktop-portal*, and are stated here because a backend's
obligations only make sense alongside them, not because this repository implements them.

The **portal backend** boundary protects the browser session: whoever can drive it can show the
user a page of their choosing, cause a request for a card signature, and learn the URI a flow ended
at. That is `backend/`, and it is the half this document holds this repository to. The **Entra
client** boundary protects the identity: whoever can drive it can mint tokens for a cloud account.
The fourth — the **Certificate portal**, whose backend is a separate project — protects the card
itself: it owns the certificate chooser, the PIN prompt, and the PIN.

**The public interface is gated.** `org.freedesktop.portal.experimental.WebAuthentication` is not
exported unless the portal was started with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`. With the gate off, no application can
reach any of this and this backend is never activated. That is a property of the frontend and this
repository cannot change it in either direction. Installing this backend on a machine whose portal
does not know the interface adds no attack surface: the `.portal` file names an interface nothing
matches.

They are separated so that none has to be trusted with another's job. The frontend never sees a
page; the backend never sees an application; neither ever sees a token; the Entra client never
touches a card and never opens a window; the Certificate portal never learns what protocol any of it
is for.

The frontend/backend split is new, and its security consequences cut both ways. It is recorded in
[decisions/0008-build-to-the-upstream-shape.md](decisions/0008-build-to-the-upstream-shape.md); the
parts of it that are security judgements rather than plumbing are in this document, marked as they
arise.

The standard for both is set by the client side. A refresh token for an AVD tenant is in practice
**months of standing access** to whatever that account can reach, redeemable without the smart card
that originally produced it. A design that is merely "as safe as the RDP client" is not safe enough,
because the RDP client never held anything that durable.

---

# Part 1 — the web authentication portal

Two processes, one contract. Where a rule belongs to one of them specifically, it says so; the
enforcement table is in [IMPL-INTERFACE.md](IMPL-INTERFACE.md).

## What is enforced today

Line by line, for `backend/` as it stands. "Frontend" means xdg-desktop-portal on the branch, not
this repository. What the end-to-end runs actually demonstrated is [TESTING.md](TESTING.md).

| Control | Where | State |
|---|---|---|
| Only the owner of `org.freedesktop.portal.Desktop` may call the impl interface, re-resolved from the bus on every accept | `src/webauthentication-impl.c` | **Implemented** |
| The same check on `Request.Close()` | `src/request-impl.c` | **Implemented** |
| `start_uri` re-validated: absolute, https, host, no userinfo, no control characters, no backslash, bounded length | `src/completion.c` | **Implemented** |
| `completion_uri` re-validated: absolute, host, no userinfo | `src/completion.c` | **Implemented** |
| Exact completion matching on parsed URIs, ports normalised, query and fragment ignored | `src/completion.c`, unit-tested against the frontend's fixtures | **Implemented** |
| The matched navigation is **ignored, never loaded** | `src/webkit-session.c` | **Implemented**, and checked end to end from the server's access log |
| A completion match in **any** frame ends the flow and is never loaded | `src/webkit-session.c` | **Implemented**, and it is what the XML now promises. WebKitGTK 6.0 exposes no frame identity on a navigation policy decision, so no backend on this engine can do less or more. See [IMPL-INTERFACE.md](IMPL-INTERFACE.md) |
| `session_mode` obeyed as a decision; an unknown value is an error | `src/storage.c` | **Implemented** |
| `ephemeral` uses an ephemeral network session from creation | `src/webkit-session.c` | **Implemented** |
| An unidentified caller is narrowed to `ephemeral` | `src/storage.c` | **Implemented** |
| The persistent store is per application, 0700, and is not named by the caller | `src/storage.c` | **Implemented** |
| The deadline, starting when the window opens | `src/transaction.c` | **Implemented** |
| Exactly one terminal result; late events discarded | `src/transaction.c` | **Implemented** |
| The window is destroyed on every exit path, and the adapter released | `src/transaction.c`, `src/webkit-session.c` | **Implemented** |
| The frontend vanishing cancels every transaction | `src/webauthentication-impl.c` | **Implemented** |
| TLS errors fail closed, with no bypass reachable from the bus | `src/webkit-session.c` | **Implemented** |
| Downloads, popups, JavaScript-opened windows and every permission request refused | `src/webkit-session.c` | **Implemented** |
| Certificate challenges from a host unrelated to the page are declined | `src/webkit-session.c` | **Implemented** |
| The PIN is never in argv and a `pin-value` URI is refused | `src/tls/client_cert.c` | **Implemented** |
| Structural redaction: no entry point can log a URI, a query, a certificate URI or a PIN | `src/redact.c`, unit-tested | **Implemented** |
| `PR_SET_DUMPABLE(0)` and `RLIMIT_CORE 0` | `src/main.c` | **Implemented** for this process. `execve` resets dumpable, so WebKit's network and web processes are **not** covered |
| Chrome states the frontend-established caller, its honesty level, and the engine's origin | `src/chrome.c` | **Implemented**. Never reviewed by a designer, and never seen by a user who had to make a decision from it |
| App id derivation, option filtering, the returned-URI re-check, exactly one `Response` | Frontend | **Implemented there** |
| Rate limiting | Frontend | **Not implemented anywhere** |
| The `portal` certificate provider | `src/tls/client_cert_portal.c` | **Written, and reports itself unavailable**: the module it needs does not exist |
| Accessibility: AT-SPI labels, keyboard operation, focus return | `src/chrome.c` | **Partly**: labels and Escape are there; nothing has been tested with a screen reader |

## What is being protected

Naming the assets first, because "it just shows a web page" understates every one of them:

- **Persistent authenticated web sessions.** The shared store holds live sign-in sessions for
  whatever has been signed into through this portal.
- **The ability to induce smart-card operations.** A flow here can end with a hardware token
  authenticating. Under the `portal` provider a chooser and a PIN prompt stand in the way, in the
  Certificate portal's own windows; under `pkcs11` the operator named a token on this backend's
  command line and the only interaction is the card's own. Either way the ability to *provoke* that
  operation, naming an origin of the caller's choosing, is a capability and not a rendering
  feature.
- **The engine's remembered client-certificate selections**, which live in the storage partition.
- **The user's trust in portal-controlled UI.** If the chrome can be made to lie, everything above
  it is worthless.
- **Returned authorization codes and assertions.** A completion URI routinely carries the
  credential the entire flow was for.

## Threat model

**In scope.**

- A process running as *another* user attempting to start a transaction, read a completion, or
  reach another user's storage.
- A same-UID process starting a transaction with a hostile `start_uri`, or with a `completion_uri`
  chosen to capture a completion from a flow it did not start.
- A same-UID process using the portal as a **phishing launcher** — showing a convincing corporate
  sign-in page under an application name that is not its own.
- A hostile application starting a flow that rides an already-authenticated shared session.
- A hostile or compromised page inside the web view attempting to complete the transaction early,
  to provoke a certificate request naming a host of its choosing, to reach persistent state it
  should not, or to escape into the rest of the desktop.
- Credential-bearing URIs reaching a log, a crash dump, or a bug report.

**Out of scope.**

- An attacker already running as the same UID with a debugger attached. Same-UID isolation is what
  the OS gives us; no user-space process defends against `ptrace` from its own user. **This portal
  materially helps sandboxed applications; it cannot claim strong separation between mutually
  hostile unsandboxed ones**, and it must not be described as though it can.
- The security of WebKit's own sandbox, beyond using it correctly and keeping up with it.
- A compromised session bus.

## Access control

**Same UID only.** The frontend serves one user's session bus. The peer's UID is checked against the
frontend's own *before* the request is parsed, not after. No cross-user mode, no root mode. Same-UID
is a necessary check and not a complete authorization policy — which is why everything in "Caller
identity" exists.

### The impl interface is not for applications

This is the split's own security obligation, and it did not exist when there was one process.

`org.freedesktop.impl.portal.experimental.WebAuthentication` takes an `app_id` as an *argument*. An
application that reached it directly would name itself, choose its own storage mode, bypass rate
limiting, and bypass the frontend's re-check of the returned URI. So:

- **The backend refuses any sender that does not own `org.freedesktop.portal.Desktop`.** It checks
  every invocation. A refusal is an error return and a logged outcome symbol, never a window. This
  is now literally the same mechanism upstream relies on rather than an analogue of it under our
  own names: the impl bus names are not proxied into sandboxes by the portal machinery, and a
  Flatpak's D-Bus policy does not grant them.
- **Distributions should apply D-Bus policy** to the backend's bus name, as upstream backends
  expect: the impl service is addressed by the portal frontend and nothing else. This is defence in
  depth, not the primary mechanism, because a session-bus policy on a same-UID desktop is a
  convention more than a wall.
- **The backend is not activatable into a useful state by anyone else.** With no frontend to answer,
  a `Start` from an application gets an error; there is no path that opens a window for a caller the
  backend cannot check.
- **And the honest limit, stated rather than hidden:** on an unrestricted desktop a same-UID process
  can talk to any session-bus name it likes, and the checks above are the frontend's word against a
  peer's. This is the same limit "Out of scope" already names — same-UID isolation is weak — and the
  split neither fixes it nor makes it worse. What the split *does* fix is that the honest app id now
  comes from somewhere the application cannot influence at all. What it costs is one more name on
  the bus that must be treated as privileged.

**Caller identity is resolved, never asserted.** *(Provided by xdg-desktop-portal:
`xdp_invocation_get_app_info()`.)* An executable path is not an application identity: a same-UID
process can execute another path, manipulate its launch context, or connect straight to the bus.
**The frontend resolves it; the backend is told and never asks** — a backend that resolved its own
peer would resolve xdg-desktop-portal. The frontend distinguishes three honesty levels, forwards
which one it got as `app_id_kind`, and this backend renders the difference:

| Kind | Source | Status |
|---|---|---|
| Sandboxed | Flatpak/Snap, through the containment framework's mediation | authenticated metadata |
| Cgroup-derived | the systemd/cgroup unit | a useful **label**, not a security principal |
| Unverified | an unsandboxed peer | unique bus name and UID are reliable; publisher is not established |

A caller-supplied app id is only a claim, and is never treated as more.

Consequences, enforced rather than advised:

- **Every result is bound to the initiating unique D-Bus connection.** If that connection goes
  away, the transaction is cancelled and nobody else receives the completion.
- The chrome displays what the frontend verified, and says plainly when it could not — from the
  `app_id` and `app_id_kind` it was given, never from anything it re-derived.
- An unverified label is **never** the sole key for a storage partition. An unverifiable host caller
  gets `shared` or `ephemeral` — never a partition it named. This is a large part of why there is no
  per-application persistent mode in version 1.
- First use by an unidentified host caller may warrant an explicit confirmation, particularly before
  a certificate request is made on its behalf. Note that the Certificate portal makes its own
  decision here too, and the backend must pass through enough about the *original* caller for that
  decision to be made honestly — presenting every request as its own would launder the caller's
  identity, which is the opposite of what either project is for.
- **Requests are rate-limited.** Repeated background requests from one connection are the cheapest
  way to turn this portal into a phishing launcher. **This is not implemented anywhere.** It is on
  the frontend branch's own open-items list, and until it is there is nothing between a hostile
  caller and a window every time it asks.

## URI rules

Each rule with the reason. The full definition is in [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md).

- **`start_uri` must be absolute `https` with a host.** The frontend will not forward, and the backend
  will not open, `file:`, `data:`, `javascript:` or a scheme handler; a portal that opens arbitrary
  URIs on request is a
  general-purpose way to make a desktop open anything.
- **`completion_uri` must be absolute, with a host, no userinfo and no wildcard.** Note that the
  branch's XML says exactly that and no more: the earlier "`https` or an exactly named custom
  scheme" carve-out is not a documented category any more.
- **The frontend validates before forwarding, and re-checks what comes back.** A malformed request
  is a D-Bus error before any backend is woken and any window is opened; a returned `completion_uri`
  that is not the one the application asked for becomes response `2` with reason
  `backend_completion_mismatch` and is discarded. The second check is what makes the answer a
  statement the *frontend* makes on the bus name the application trusts, rather than a relay of
  whatever backend a distribution installed.
- **Matching is exact, on parsed URIs, with no prefix mode.** Scheme and host case-insensitive (host
  IDNA-normalised), ports normalised, **path exactly equal**, userinfo forbidden, query and fragment
  carrying the result and taking no part in matching. A string prefix would accept
  `/callback.evil` and `/callbacker` for `/callback`; a naive comparison would accept
  `https://example.com@evil.invalid/`.
- **Malformed input is rejected, not normalised.** Control characters, malformed percent escapes,
  `%00`, backslashes in authority-sensitive positions, and URIs that parse differently between
  libraries. The `%00` case specifically: a decoder that produces an embedded NUL lets a component
  carry more than the C string it decodes to shows, every later comparison stops at the NUL, and a
  different host or path passes as the expected one.
- **Custom schemes are matched structurally and never dispatched externally.** Naming a scheme does
  not prove owning it; the navigation is intercepted before any attempt at external protocol
  handling, and scheme ownership stays a matter for client registration and caller-side validation.
- **A match in any frame ends the flow, and is never loaded.** WebKitGTK 6.0 exposes no frame
  identity on a navigation policy decision, so no backend on this engine can tell a top-level
  navigation from a subframe one; the public XML now promises what is enforceable instead of
  promising that subframes cannot complete a flow. The half that is a security property holds
  everywhere: nothing fetches the completion URI, in any frame, so the authorization code never
  leaves for a server with no part in the exchange. The half that is not: embedded content that can
  point a frame at the completion URI can end the transaction with a URI it chose. The compensating
  controls are the frontend's re-check and the application's own `state`. See
  [IMPL-INTERFACE.md](IMPL-INTERFACE.md).
- **Complete before load.** The matched navigation completes the transaction and destroys the window
  *before it is loaded*. The completion URI routinely carries the credential the flow was for;
  letting the engine fetch it sends that credential to a remote server with no part in the exchange.

## Fixed behaviour the caller cannot influence

A caller may not control: whether TLS errors are ignored; whether security chrome is shown; which
storage partition is used; arbitrary certificate trust; PIN persistence; an unlimited timeout; or
the verified application label shown to the user. An implementation that makes any of these an
option has changed the interface, not configured it.

The `title` option is a **hint**, rendered beneath the portal's own text and marked as
application-supplied. Otherwise a malicious caller labels itself "Microsoft Login", which is the
entire phishing attack in one string.

## Sessions and storage

- **Two modes, `shared` and `ephemeral`.** `shared` is the default and means shared among *this
  backend's* transactions. The documentation and the UI must say so plainly: it is **not** the
  user's Firefox or Chrome profile, and it inherits none of their accounts, enterprise policies,
  extensions, device registration or browser-bound credentials.
- **`ephemeral` must use an ephemeral website data manager from creation.** "Use the persistent
  store and clear it afterwards" is not equivalent and is race-prone. A caller may request
  ephemeral; a caller may never defeat a policy that forces it.
- **Unknown values for a known option are an error**, never a fallback. Falling back from
  `ephemeral` to `shared` on a typo is a security decision made by a typo.
- **A partition is not a cookie jar.** It covers cookies, local and session storage, IndexedDB, HTTP
  and disk caches, service workers, HSTS and related network state, HTTP authentication credentials,
  permission decisions, and client-certificate selection memory. Downloads and autofill are
  disabled rather than partitioned.
- **The frontend chooses the partition; the backend obeys.** A backend that derived its own
  partition would be a second place where the isolation rule lives — and it would have to judge how
  much an app id can be believed, which is frontend knowledge that does not survive the hop as
  anything but a label. A backend may refuse a mode it cannot honour; it may never downgrade one.
- **Shared state amplifies a malicious caller.** A hostile application can start a flow riding a
  session the user already established. OAuth `state` protects transaction correlation; it does
  nothing for the user's understanding of *which native application* asked. The mitigation is the
  chrome, not the protocol.
- Stores live under `$XDG_DATA_HOME/xdg-desktop-portal-webauth/<app id>/`, mode `0700`, with the
  cookie jar at `data/cookies.sqlite` and the HTTP cache under `cache/`. They are treated as
  sensitive: a session cookie is a credential. The `<app id>` component is sanitised to
  `[A-Za-z0-9._-]` with leading dots stripped, so no caller-derived string can escape the directory
  or hide inside it.

## Client certificates

The backend satisfies a client-certificate challenge through an adapter with two providers,
`portal` and `pkcs11`, and the security position differs between them. See
[decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md), which the S2 result
substantially amended.

**There is no in-process chooser and no in-process PIN prompt, and there is not going to be one.**
This process never enumerates tokens, never draws a certificate chooser, and never asks a user for a
PIN. A chooser and a PIN prompt inside a web browser process is the design the Certificate portal
exists to replace; building one here would make this backend a second place where card handling
lives, and the two would drift. What this process does is name a certificate by URI and let GnuTLS
resolve it.

### Under the `portal` provider — preferred, and not usable yet

**What changed.** Spike [S2](SPIKES.md) established that WebKit carries a certificate to its network
process **as a PKCS#11 URI** and asks for the token PIN itself; there is no external-signer seam a
brokered `Sign` could be plugged into, and no `GTlsInteraction` on a `WebKitNetworkSession`. So this
provider is not "call `Sign` for every operation": it names a token that the Certificate portal's own
client-side PKCS#11 module presents, and the module is what calls that portal.
[`backend/src/tls/portal-token.h`](../backend/src/tls/portal-token.h) is the agreement — the token's
label, manufacturer and model, and the requirement that it declare
`CKF_PROTECTED_AUTHENTICATION_PATH`. **That module does not exist yet**, so the provider reports
itself unavailable and `auto` falls through to `pkcs11`.

**Read this second: the delegation gap, and what has changed about it.** Over D-Bus the backend
calls the Certificate portal as an ordinary client of *its public interface*, so the portal derives
**this backend's** app id and not the application's. Its consent window names the wrong thing, and
the original app id can only be passed as untrusted text — `reason`, since there is no `context`
option either. Presenting a passed-through app id as an established identity would launder a
caller's identity through someone else's trusted window, which is the opposite of what either
project is for.

**What changed: both portals now live in one frontend process.** xdg-desktop-portal derived the app
id for the `WebAuthentication.Start` call and still holds it when it calls its own certificate side,
so it can pass the *original* app id along in-process, with nothing untrusted in between and no
attestation crossing a bus. That is the "shared frontend" fix both projects described as arriving at
acceptance, and it has arrived early. **It is not written yet**, on that branch.

**The caveat is permanent.** The fix works *only* in-process. Across a process boundary, passing an
app id along is an unattested assertion of someone else's identity, there is no cross-process
attestation protocol here, none is being built, and doing it as a stopgap is not a smaller version
of the in-process fix — it is the thing the in-process fix exists to avoid needing. See
[decisions/0010](decisions/0010-backend-only-frontend-lives-upstream.md).

- **The PIN never reaches this process.** It is entered in the Certificate portal backend's window, against
  another process's memory. There is no buffer here to scrub and no bug here that can leak one.
- **The grant is bounded**: a certificate, a set of permitted operations and mechanisms, and an
  expiry. Brokered signing gives precise accounting, revocation and per-operation consent — though
  note honestly that no generic `Sign()` can prove its input came from a TLS handshake, so what it
  buys is accounting rather than attestation.
- **A forwarded module is still not the mechanism.** `OpenPkcs11Endpoint` is on neither Certificate
  interface, and the reasons it was deferred are the reasons it was always risky: stock `p11-kit
  server` forwards a whole *token* rather than a scoped object, carries no login state across the
  boundary, and a PKCS#11 URI cannot name a socket. What S2 found instead is that a **permanently
  registered** module — installed by the certificate portal, configured in p11-kit like any other,
  present before WebKit starts — needs none of that. It is the "one permanently registered broker
  module exposing synthetic grant-bound slots" that was the fallback plan, arrived at as the primary
  one.
- **Whatever the adapter held is released on every exit path** — completion, failure, timeout,
  cancellation. A finished transaction must not leave a live grant or endpoint behind. This is the
  one card-related discipline that is entirely the backend's responsibility either way, and the
  split adds one exit path to it: the frontend's connection dropping.
- **Consent UI belongs to the other portal**, which names the requesting application, origin,
  certificate identity and purpose. The backend's contribution is that the caller and origin it has
  been displaying all along are the same ones that window restates: if the backend's chrome can be
  made to lie, the other portal's window inherits the lie. It must therefore pass through enough
  about the *original* caller for that project's own consent decision to be made honestly, rather
  than presenting every request as its own — subject to the delegation gap above, which means
  "honestly" currently includes "and this is a claim we cannot attest". Note that the only field
  available for it is `reason`: there is no `context` option on the interface.

### Under the `pkcs11` provider — the one that works today

A token named by `--client-cert-uri` on this backend's command line: the operator's decision, made
once, outside any request. It is what the end-to-end tests use against SoftHSM, and what an operator
with a card and no certificate portal uses.

- **No chooser.** The certificate is the one the URI names. A backend that offered a choice would be
  drawing the window the Certificate portal exists to own.
- **No PIN prompt, and the PIN is never on a command line.** Where a token needs one, it is read
  from the file named by `--client-cert-pin-file`, held only until the transaction ends, and then
  overwritten. `/proc/*/cmdline` is world-readable, which is why a `pin-value` inside
  `--client-cert-uri` is **refused** rather than accepted with a warning.
- **The PIN is answered to WebKit, not to the DOM.** It goes to
  `webkit_credential_new_for_certificate_pin()` in reply to the engine's own
  `CLIENT_CERTIFICATE_PIN_REQUESTED`; it never enters a page, a form or the engine's credential
  storage, and it is never logged, not even redacted.
- **Retries are not this process's to make.** It answers the challenges the engine raises; it does
  not retry a rejected PIN, because automatically re-answering is how a card gets locked.
- **This provider does not attempt a PKCS#11 logout**, because it never logged in: the module and
  GnuTLS own the session. What that means for a real card — several cache authentication internally —
  is unchanged and is the card's behaviour, not this backend's.

### Under either

- **Challenges are refused when they come from an unrelated host.** Only the verified host the engine
  has loaded, and its `certauth.` subdomain where the flow requires it, may cause a certificate
  request. A page able to provoke a request naming an arbitrary origin would be phishing through a
  trusted window — and through *someone else's* trusted window under the `portal` provider, which is
  worse. `unrelated_certificate_challenge` is one of the `reason` symbols the impl XML names for
  exactly this.
- **A declined challenge is never a silent downgrade.** Neither provider falls back to "continue
  without a certificate" and then reports success: the handshake fails, and the reason recorded is
  the one that describes why the certificate was not supplied.
- **Nothing about a certificate is logged**: not the PKCS#11 URI, not the label, not the serial, not
  the subject. Only counts.
- **When no provider can run**, the challenge is declined; if the server required one, the load then
  fails and the transaction ends `2` with reason `no_certificate_adapter` — one of the XML's symbols.
  A flow that did not actually need a certificate is not failed by a challenge it ignored.
- **The hardening yields, for one interval, on the `portal` provider only.** `PR_SET_DUMPABLE(0)`
  makes this process's `/proc` entries root-owned, and xdg-desktop-portal identifies a caller by
  opening `/proc/<pid>/root` — so while it is set, every call this process *makes* to the portal is
  refused with `AccessDenied`. That is harmless while this backend only answers the portal, and
  fatal on the `portal` provider, where the certificate portal's PKCS#11 module runs inside this
  process and calls `CreateSession` and `AcquireCredential` as an ordinary application.
  [`../backend/src/harden.h`](../backend/src/harden.h) opens a counted window around that one
  constructor and closes it the moment it returns. In that interval this process holds **no PIN**
  — the `portal` provider never has one — and **no authorization code**, because the challenge is
  answered before the flow has redirected anywhere; giving the flag up for the life of the process
  would expose the same `/proc` entries while the completion URI is in memory, which is the moment
  that matters. Both edges are logged at message level as
  `process-hardening outcome=identifiable-begin` / `-end`, so an operator can see every interval in
  which the process was readable without having asked for breadcrumbs. **The `pkcs11` provider opens
  no window at all**: it calls no portal, and the PIN it holds is exactly what the flag protects.
- **The residual risk delegation does not remove:** the backend can still provoke a certificate
  prompt, repeatedly, on behalf of a caller it may be unable to identify. Rate limiting in the
  frontend and honest caller display in the backend stand between that and a nuisance — and the
  rate limiting is not implemented, so at present only the honest display does. Under the `portal`
  provider, that project's own consent policy is the backstop.

## Transactions

The specification below did not change when the design was split; the number of places it can be got
wrong did. Which side enforces each part is in [IMPL-INTERFACE.md](IMPL-INTERFACE.md), and three
obligations are new:

- **The frontend owes an answer when the backend dies.** One `Response(2, { reason:
  "backend_disappeared" })`, not silence. An application waiting forever on a dead backend is a
  denial of service the single-process design could not produce, because there was nobody left to
  wait on. *(Provided by xdg-desktop-portal.)*
- **The backend cancels when the frontend dies.** Its connection dropping destroys the window at
  once. A window belonging to no request is the leaked-window failure this interface promises not to
  have, and it would be a window with security chrome and no request behind it. Note that "the
  frontend" is now xdg-desktop-portal, whose restart is a more ordinary event than a bespoke
  service's death — which is a reason to get this right rather than to assume it away.
- **The deadline exists once, and it is the backend's.** This was written as "the deadline exists
  twice"; reading the branch shows it does not. The frontend forwards a clamped `timeout` and then
  awaits the impl call with a D-Bus timeout of `G_MAXINT`, so a backend that never answers is a
  request that never ends. `src/transaction.c` is the only clock, and it starts when the window
  opens. A backstop in the frontend would be a real improvement and belongs upstream.

- Exactly one terminal result and exactly one `Response` per transaction. A second matching
  navigation, a window closed after a match, a timeout expiring after a close — all ignored.
- A **committed completion wins** over a simultaneous `Close()`. Every other late event is discarded.
- Caller bus disconnection cancels immediately.
- Every transaction has a deadline: 300 seconds by default, 900 as the hard ceiling, a caller may
  only shorten. Short fixed timeouts are hostile to smart-card and MFA flows.
- Browser-session termination produces one defined failure, with no leaked window and no orphaned
  partition.
- `Close()` on an already-answered request is a no-op, not an error.
- Maximum URI and response sizes are enforced.

## Logging

**Never logged, at any level:** `start_uri`, `completion_uri`, any query string, any page content,
any cookie, any `Authorization` header, any PKCS#11 URI, certificate label, serial or subject, and
any PIN. A URI may be logged in exactly one shape — scheme, host, port and the **length** of the
path (`WEBAUTH_FIELD_URI_SHAPE`) — and no field kind renders a query at all.

**Structural, not textual, redaction.** The logging interface takes typed fields and the kind
decides what may be printed. There is deliberately no `log_uri()` a later edit can point at a
completion, and no caller-supplied format string. A field whose kind is not loggable renders as its
kind and its length — `<uri:212>` — never its value.

**What a DEBUG log may contain:** outcome symbols (`matched`, `unrelated`, `cancelled`, `timeout`);
the host, scheme and port of a certificate challenge and whether it was accepted; which certificate
provider ran; the resolved caller identity and its honesty level; counts; stable reason codes for every rejection and cancellation, so a user can report *why* without
reporting *what*; phase timings; and loader or TLS error text **cut before any embedded URI**.

## Accessibility as a security property

Listed here as well as in [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md) because it belongs in both:
the chrome carries a security decision, and a user who cannot perceive it cannot make that decision.
AT-SPI exposure for every backend-owned control, meaningful focus order including across any
hand-off to another portal's windows, screen-reader announcement of the verified caller and the
current origin, no meaning conveyed by colour alone, accessible error and cancellation states, and
focus restored to the calling application on close. There is no chooser and no PIN prompt here any
more; under the `portal` provider those windows are the Certificate portal's and carry the same
obligation on that project's side.

**What is actually done:** the window and its controls carry accessible labels and descriptions, the
lock indicator is a label as well as an icon, and Escape cancels. **Nothing has been tested with a
screen reader**, and the headless smoke test runs with `GTK_A11Y=none`, so this row of the table
above says "partly" rather than "implemented".

---

# Part 2 — the Entra client

## Threat model

**In scope.**

- A same-UID process asking the client for a token for a different client id, a different resource,
  or against an attacker-chosen authority.
- Credential material — codes, tokens, verifiers — reaching a log, a terminal scrollback, or a bug
  report.
- A hostile parent process manipulating the client through argv or environment.
- A response that is not the response to this transaction.

**Out of scope.** As above: same-UID debuggers, a compromised keyring daemon, and the tenant's own
Conditional Access policy.

## Access control

**Same UID only**, by construction: the client is a one-shot process spawned by, and returning to, a
process of the same user. When it grows an IPC transport, the socket lives in `$XDG_RUNTIME_DIR`
with mode `0700` and peer credentials are checked on every connection.

**Allowlists by default.**

- Client id: the AVD public client `a85cf173-4192-42f8-81fa-777a763e6e2c`.
- Authority: `login.microsoftonline.com`, `login.microsoftonline.us`.
- Completion URI: `https://login.microsoftonline.com/common/oauth2/nativeclient`.

A caller may not extend these. A *user* may, through an explicit configuration-file override naming
each addition individually — never a wildcard, never an environment variable, never a command-line
flag a hostile parent could set. The reason is not that arbitrary OAuth is dangerous in itself; it
is that a client willing to sign in to anything, on behalf of anything, and print the result on
stdout, is a phishing primitive with a keyring attached.

**Never accept an endpoint from a caller.** The request carries an *authority host*; the client
derives the authorization endpoint, the token endpoint and the completion URI from its own tables,
optionally refined by OpenID discovery *against that same authority*. A request containing any URL
is a usage error, not a configuration. This is the one rule that stops a same-UID caller pointing a
credential-bearing exchange at a server it controls — and it is why the **client**, not its caller,
chooses the `completion_uri` it hands to the portal.

## OAuth rules

- **`state` on every authorization request**, cryptographically random, compared in **constant
  time**. `state` is the secret a response has to know, and no part of the portal ever sees it.
- **PKCE S256 on every authorization request**, verifier sent only with the token request. A public
  client cannot keep a secret; PKCE is what binds the code to the process that asked.
- **Exact redirect matching** on the returned URI: scheme, host, port and path equal to the
  transaction's redirect (empty path and `/` are the same resource). Not redundant with the
  portal's checks: the backend and the frontend each answered a question about URIs, the client
  answers a question about OAuth, using a secret no part of the portal ever held. A `strstr` for `code=` — what FreeRDP's existing
  fallback does — accepts a redirect to an entirely different host.
- **Reject userinfo and fragment.**
- **Exactly one of `code` or `error`, each occurring exactly once.** A parameter present without a
  value counts as an occurrence, so a second `code` cannot be smuggled in as a bare `code`.
- **Strict percent-decoding**, on the same terms as the portal's and for the same reason.
- **Single-use transactions.** One terminal result. A second response is a replay or an answer to a
  request this process did not make.
- **Bounded transactions**, with a deadline passed to the portal as `timeout` and enforced locally
  as well.

## Secrets

| Artifact | Where it lives | Why |
|---|---|---|
| **Refresh token** | Secret Service keyring only. Never on stdout, never in the JSON response, never in a log, never in a file, never returned to a caller under any option. | Months of standing access. A caller handed one has been handed the user's identity, not a token for one connection. The keyring is the only store on a Linux desktop with a plausible claim to protect it at rest. |
| **Access token** | In memory for the process lifetime; printed once on stdout. | Short-lived and scoped, and the caller needs it. Still a bearer credential, so: stdout and nowhere else. |
| **PoP token** | As above, additionally keyed by the `req_cnf` binding. | A PoP token bound to one `kid` is useless for another; a cache key ignoring the binding would return a token the caller cannot use. |
| **Authorization code** | In memory for the seconds between the completion and the token request. Scrubbed. | Exchangeable for a refresh token by anyone holding it plus the public client id. PKCE is what stops that, and PKCE is not a reason to be careless with the code. |
| **PKCE verifier, `state`** | In memory for the transaction. Scrubbed. | The verifier binds the code to this process; `state` binds the response to this request. |
| **PIN** | Never seen by the Entra client. Under the portal backend's `portal` provider, never seen there either — the token declares a protected authentication path and the Certificate portal prompts. Under `pkcs11`, read from a file the operator named, held only until the transaction ends, then overwritten; never in argv, never in a URI, never logged. | A component having no path to a secret is better than a component being careful with one, which is the argument for finishing the `portal` provider. |
| **Private key on the card** | Never leaves the card. | That is the point of the card. |
| **PoP key** | Never seen by the client. FreeRDP generates and retains it; the client receives only `req_cnf`. | The client cannot leak what it never has. |
| **Account records** (account id, authority, tenant, client id) | Keyring, beside the refresh token. | Not secret in the same sense, but they name a person and a tenant. |

**No persistent cache mode.** When the Secret Service is unavailable the client does **not** fall
back to a file. It runs with no persistence, or exits `40` so a dispatcher can fall through. A
silent downgrade from "keyring" to "file in the home directory" is the kind of thing nobody notices
until it is in a backup.

**Logout must be complete.** `logout` removes the refresh token, the account record, *and* asks the
portal to discard the web session that account established. A logout leaving the Entra session
cookie behind has not logged anybody out.

## Logging

**Never logged, at any level, redacted or not:** access tokens, refresh tokens, ID tokens, any JWT;
authorization codes; PKCE verifiers and challenges; `state` values; the completion URI or any URI
carrying a query string from the authorization server; the authorization server's
`error_description`, which routinely names the account, the tenant and the policy that failed; HTTP
response bodies from the token endpoint; cookies and `Authorization` headers.

**Structural redaction**, as on the portal side.

**What a DEBUG log may contain:** outcome symbols from the callback classifier (`CODE`, `ERROR`,
`UNRELATED`, `INVALID`); the authority host — a public constant, and the single most useful field
when diagnosing a wrong-cloud failure; OAuth error *codes* without their descriptions
(`invalid_grant`, `interaction_required`, `AADSTS50011`); cache hit and miss counts; the portal's
response code; and phase timings.

**What a DEBUG log must not become:** a way to reproduce the sign-in. If a support bundle containing
a DEBUG log would let its reader connect as the user, the redaction is wrong. There is no "trace"
level that relaxes these rules, because a level that relaxes them will be enabled by somebody in
production.

---

## Independent review

Before either interface is frozen, five things want an independent pair of eyes:

1. The completion matcher and its percent-decoder — **both copies**, against the same fixtures. Two
   implementations of one rule is a cost of the split and this is where it is paid. One copy is
   written and tested upstream (`completion_uri_matches()`); the other is not written at all.
2. The frontend's peer check and app-id derivation, and everything downstream that trusts the
   answer — the chrome, the partition choice, the result binding, and what is passed to the
   Certificate portal about the original caller. *(The derivation is upstream's now; what to review
   here is everything that consumes it.)*
2a. The impl boundary itself: that the backend refuses senders that do not own
   `org.freedesktop.portal.Desktop`, that the frontend's option filter drops what it should, and
   that the frontend's re-check of the returned `completion_uri` cannot be skipped on any path.
3. The lifetime of whatever the certificate adapter holds — a grant, an endpoint, a PKCS#11 session
   — especially on the cancellation and timeout paths.
4. The client's callback classifier.
5. The client's cache key — anything missing from it is a token returned to the wrong requester.
6. Cancellation and timeout races on both sides, including `Close()`-during-completion and
   browser-session death.

Those are the places where a subtle mistake is not visible in testing.

## The exit criterion

Recorded here as well as in [decisions/0005-service-shape.md](decisions/0005-service-shape.md),
because it is a security judgement: **if caller identity, displayed origin and storage partitioning cannot be made convincing,
collapse the browser layer back into the Entra client.** A narrowly scoped Entra/AVD helper is better than a generic authentication portal with an
ill-defined trust model, and this is a real outcome to plan for rather than a formality.

## Reporting

This is a design sketch with no users and no releases. Problems in the *design* belong in an issue.
There is nothing deployed to report a vulnerability against yet; when there is, this section will
name a contact and a disclosure window.
