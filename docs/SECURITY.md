# Security model

Status: design sketch. This document states the rules the implementation must satisfy. None of them
are enforced yet, because there is no implementation.

There are three boundaries. Two are in this repository.

The **web authentication service** boundary protects the browser session: whoever can drive it can
show the user a page of their choosing, cause a request for a card signature, and learn the URI a
flow ended at. The **Entra client** boundary protects the identity: whoever can drive it can mint
tokens for a cloud account. The third — the **smart card service**, a separate project — protects
the card itself: it owns the certificate chooser, the PIN prompt, and the PIN.

They are separated so that none has to be trusted with another's job. The web authentication service
never sees a token and never sees a PIN; the Entra client never touches a card and never opens a
window; the smart card service never learns what protocol any of it is for.

The standard for both is set by the client side. A refresh token for an AVD tenant is in practice
**months of standing access** to whatever that account can reach, redeemable without the smart card
that originally produced it. A design that is merely "as safe as the RDP client" is not safe enough,
because the RDP client never held anything that durable.

---

# Part 1 — the web authentication service

## What is being protected

Naming the assets first, because "it just shows a web page" understates every one of them:

- **Persistent authenticated web sessions.** The shared store holds live sign-in sessions for
  whatever has been signed into through this service.
- **The ability to induce smart-card operations.** A flow here can end with a hardware token
  authenticating. It cannot happen silently — a chooser and a PIN prompt stand in the way, whichever
  adapter shows them — but the ability to *provoke* that prompt, naming an origin of the caller's
  choosing, is a capability and not a rendering feature.
- **The engine's remembered client-certificate selections**, which live in the storage partition.
- **The user's trust in service-controlled UI.** If the chrome can be made to lie, everything above
  it is worthless.
- **Returned authorization codes and assertions.** A completion URI routinely carries the
  credential the entire flow was for.

## Threat model

**In scope.**

- A process running as *another* user attempting to start a transaction, read a completion, or
  reach another user's storage.
- A same-UID process starting a transaction with a hostile `start_uri`, or with a `completion_uri`
  chosen to capture a completion from a flow it did not start.
- A same-UID process using the service as a **phishing launcher** — showing a convincing corporate
  sign-in page under an application name that is not its own.
- A hostile application starting a flow that rides an already-authenticated shared session.
- A hostile or compromised page inside the web view attempting to complete the transaction early,
  to provoke a certificate request naming a host of its choosing, to reach persistent state it
  should not, or to escape into the rest of the desktop.
- Credential-bearing URIs reaching a log, a crash dump, or a bug report.

**Out of scope.**

- An attacker already running as the same UID with a debugger attached. Same-UID isolation is what
  the OS gives us; no user-space process defends against `ptrace` from its own user. **This service
  materially helps sandboxed applications; it cannot claim strong separation between mutually
  hostile unsandboxed ones**, and it must not be described as though it can.
- The security of WebKit's own sandbox, beyond using it correctly and keeping up with it.
- A compromised session bus.

## Access control

**Same UID only.** The service serves one user's session bus. The peer's UID is checked against the
service's own *before* the request is parsed, not after. No cross-user mode, no root mode. Same-UID
is a necessary check and not a complete authorization policy — which is why everything in "Caller
identity" exists.

**Caller identity is resolved, never asserted.** An executable path is not an application identity:
a same-UID process can execute another path, manipulate its launch context, or connect straight to
the bus. So the service distinguishes three honesty levels and says which one it has:

| Kind | Source | Status |
|---|---|---|
| Sandboxed | Flatpak/Snap, through the containment framework's mediation | authenticated metadata |
| Cgroup-derived | the systemd/cgroup unit | a useful **label**, not a security principal |
| Unverified | an unsandboxed peer | unique bus name and UID are reliable; publisher is not established |

A caller-supplied app id is only a claim, and is never treated as more.

Consequences, enforced rather than advised:

- **Every result is bound to the initiating unique D-Bus connection.** If that connection goes
  away, the transaction is cancelled and nobody else receives the completion.
- The chrome displays what was verified, and says plainly when it was not.
- An unverified label is **never** the sole key for a storage partition. An unverifiable host caller
  gets `shared` or `ephemeral` — never a partition it named. This is a large part of why there is no
  per-application persistent mode in version 1.
- First use by an unidentified host caller may warrant an explicit confirmation, particularly before
  a certificate request is made on its behalf. Note that the smart card service makes its own
  decision here too, and this service must pass through enough about the *original* caller for that
  decision to be made honestly — presenting every request as its own would launder the caller's
  identity, which is the opposite of what either service is for.
- **Requests are rate-limited.** Repeated background requests from one connection are the cheapest
  way to turn this service into a phishing launcher.

## URI rules

Each rule with the reason. The full definition is in [SERVICE-INTERFACE.md](SERVICE-INTERFACE.md).

- **`start_uri` must be absolute `https` with a host.** The service will not open `file:`, `data:`,
  `javascript:` or a scheme handler; a service that opens arbitrary URIs on request is a
  general-purpose way to make a desktop open anything.
- **`completion_uri` must be absolute, `https` or an exactly named custom scheme, with no userinfo
  and no wildcard.**
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
- **Top-level navigations only.** A subframe navigating to the completion URI is not the end of the
  flow, and treating it as one would let embedded content end the transaction.
- **Complete before load.** The matched navigation completes the transaction and destroys the window
  *before it is loaded*. The completion URI routinely carries the credential the flow was for;
  letting the engine fetch it sends that credential to a remote server with no part in the exchange.

## Fixed behaviour the caller cannot influence

A caller may not control: whether TLS errors are ignored; whether security chrome is shown; which
storage partition is used; arbitrary certificate trust; PIN persistence; an unlimited timeout; or
the verified application label shown to the user. An implementation that makes any of these an
option has changed the interface, not configured it.

The `title` option is a **hint**, rendered beneath the service's own text and marked as
application-supplied. Otherwise a malicious caller labels itself "Microsoft Login", which is the
entire phishing attack in one string.

## Sessions and storage

- **Two modes, `shared` and `ephemeral`.** `shared` is the default and means shared among *this
  service's* transactions. The documentation and the UI must say so plainly: it is **not** the
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
- **The transaction layer chooses the partition; the browser session obeys.** A session that derived
  its own partition would be a second place where the isolation rule lives.
- **Shared state amplifies a malicious caller.** A hostile application can start a flow riding a
  session the user already established. OAuth `state` protects transaction correlation; it does
  nothing for the user's understanding of *which native application* asked. The mitigation is the
  chrome, not the protocol.
- Stores live under `$XDG_CACHE_HOME`, mode `0700`, and are treated as sensitive: a session cookie
  is a credential.

## Client certificates

The service satisfies a client-certificate challenge through an adapter with two implementations,
and the security position differs between them. Both are documented because both will exist for a
while; see [decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md).

### Under the `portal` adapter — preferred

- **The PIN never reaches this process.** It is entered in the smart card service's window, against
  another process's memory. There is no buffer here to scrub and no bug here that can leak one.
- **The grant is bounded**: a certificate, a set of permitted operations and mechanisms, and an
  expiry. Brokered signing gives precise accounting, revocation and per-operation consent — though
  note honestly that no generic `Sign()` can prove its input came from a TLS handshake, so what it
  buys is accounting rather than attestation.
- **The module-endpoint variant (`OpenPkcs11Endpoint`) is experimental, opt-in, and its isolation is
  weaker than it sounds.** Stock `p11-kit server` forwards a whole *token*, not a scoped object, and
  carries no login state across the boundary; what this endpoint returns instead is a Unix socket fd
  backed by the smart card service's own broker-controlled synthetic facade — one slot, the granted
  objects only, read-only sessions. Two things about it are unresolved and must not be described as
  solved: a PKCS#11 URI cannot name a socket, and `g_tls_certificate_new_from_pkcs11_uris()` has no
  module parameter, so whether this process can make the returned fd and URIs resolvable to GLib at
  all — as opposed to merely receiving them — is unproven ([S2](SPIKES.md)); the likely resolution is
  one permanently registered broker module exposing synthetic grant-bound slots, not a module handed
  over per grant.
- **Whatever the adapter held is released on every exit path** — completion, failure, timeout,
  cancellation. A finished transaction must not leave a live grant or endpoint behind. This is the
  one card-related discipline that is entirely this service's responsibility either way.
- **Consent UI belongs to the other service**, which names the requesting application, origin,
  certificate identity and purpose. This service's contribution is that the caller and origin it has
  been displaying all along are the same ones that window restates: if this service's chrome can be
  made to lie, the other service's window inherits the lie. It must therefore pass through enough
  about the *original* caller for that service's own consent decision to be made honestly, rather
  than presenting every request as its own.

### Under the `inproc` adapter — the fallback

Everything the portal adapter moves out of this process is back inside it, and the rules are the
ones the proven implementation already follows:

- **The chooser names the requesting application, the target origin, the certificate identity and
  the purpose — before any PIN is asked for.** A chooser that does not say who wants the certificate
  is teaching the user to click through.
- **The PIN is never stored.** Never in the DOM, never in the engine's credential storage, never
  across the bus, buffer cleared on completion, failure, timeout and cancellation alike. Never
  logged, not even redacted: a redacted PIN still says one was entered and how long it was.
- **A challenge is answered once, plus at most one retry the TLS stack itself initiated.**
  Automatically re-answering is how a card gets locked, and burning a user's last PIN attempt is not
  a bug this service is allowed to have. Retry exhaustion is reported in plain language.
- **The PKCS#11 login is ended where practical**, with no pretence that a card, middleware daemon or
  token firmware can be made to forget authentication on demand — several cache it internally.

### Under either

- **Challenges are refused when they come from an unrelated host.** Only the verified host the engine
  has loaded, and its `certauth.` subdomain where the flow requires it, may cause a certificate
  request. A page able to provoke a request naming an arbitrary origin would be phishing through a
  trusted window — and through *someone else's* trusted window under the portal adapter, which is
  worse.
- **Cancelling anywhere cancels the transaction**, producing response `1`. Neither adapter falls back
  to "continue without a certificate".
- **Nothing about a certificate is logged**: not the PKCS#11 URI, not the label, not the serial, not
  the subject. Only counts.
- **When neither adapter can run**, the challenge is declined and the transaction ends `2` with
  reason `no_certificate_adapter`.
- **The residual risk delegation does not remove:** this service can still provoke a certificate
  prompt, repeatedly, on behalf of a caller it may be unable to identify. Rate limiting and honest
  caller display stand between that and a nuisance; under the portal adapter, that service's own
  consent policy is the backstop.

## Transactions

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
any PIN.

**Structural, not textual, redaction.** The logging interface takes typed fields and the kind
decides what may be printed. There is deliberately no `log_uri()` a later edit can point at a
completion, and no caller-supplied format string. A field whose kind is not loggable renders as its
kind and its length — `<uri:212>` — never its value.

**What a DEBUG log may contain:** outcome symbols (`MATCHED`, `UNRELATED`, `CANCELLED`, `TIMEOUT`);
the host, scheme and port of a certificate challenge and whether it was accepted; which certificate
adapter ran; the resolved caller identity and its honesty level; counts (tokens found, certificates
found, chosen index); stable reason codes for every rejection and cancellation, so a user can report *why* without
reporting *what*; phase timings; and loader or TLS error text **cut before any embedded URI**.

## Accessibility as a security property

Listed here as well as in [SERVICE-INTERFACE.md](SERVICE-INTERFACE.md) because it belongs in both:
the chrome carries a security decision, and a user who cannot perceive it cannot make that decision.
AT-SPI exposure for every service-owned control including the in-process chooser and PIN prompt,
keyboard-only certificate selection and PIN entry, meaningful focus order including across any
hand-off to another service's windows, screen-reader announcement of the verified caller and the
current origin, no meaning conveyed by colour alone, accessible error and cancellation states, and
focus restored to the calling application on close. Under the portal adapter the chooser and PIN
prompt carry the same obligation on that service's side.

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
chooses the `completion_uri` it hands to the service.

## OAuth rules

- **`state` on every authorization request**, cryptographically random, compared in **constant
  time**. `state` is the secret a response has to know, and the service never sees it.
- **PKCE S256 on every authorization request**, verifier sent only with the token request. A public
  client cannot keep a secret; PKCE is what binds the code to the process that asked.
- **Exact redirect matching** on the returned URI: scheme, host, port and path equal to the
  transaction's redirect (empty path and `/` are the same resource). Not redundant with the
  service's check: the service answered a question about URIs, the client answers a question about
  OAuth, using a secret the service never held. A `strstr` for `code=` — what FreeRDP's existing
  fallback does — accepts a redirect to an entirely different host.
- **Reject userinfo and fragment.**
- **Exactly one of `code` or `error`, each occurring exactly once.** A parameter present without a
  value counts as an occurrence, so a second `code` cannot be smuggled in as a bare `code`.
- **Strict percent-decoding**, on the same terms as the service's and for the same reason.
- **Single-use transactions.** One terminal result. A second response is a replay or an answer to a
  request this process did not make.
- **Bounded transactions**, with a deadline passed to the service as `timeout` and enforced locally
  as well.

## Secrets

| Artifact | Where it lives | Why |
|---|---|---|
| **Refresh token** | Secret Service keyring only. Never on stdout, never in the JSON response, never in a log, never in a file, never returned to a caller under any option. | Months of standing access. A caller handed one has been handed the user's identity, not a token for one connection. The keyring is the only store on a Linux desktop with a plausible claim to protect it at rest. |
| **Access token** | In memory for the process lifetime; printed once on stdout. | Short-lived and scoped, and the caller needs it. Still a bearer credential, so: stdout and nowhere else. |
| **PoP token** | As above, additionally keyed by the `req_cnf` binding. | A PoP token bound to one `kid` is useless for another; a cache key ignoring the binding would return a token the caller cannot use. |
| **Authorization code** | In memory for the seconds between the completion and the token request. Scrubbed. | Exchangeable for a refresh token by anyone holding it plus the public client id. PKCE is what stops that, and PKCE is not a reason to be careless with the code. |
| **PKCE verifier, `state`** | In memory for the transaction. Scrubbed. | The verifier binds the code to this process; `state` binds the response to this request. |
| **PIN** | Never seen by the Entra client. Under the web authentication service's `portal` adapter, never seen there either; under `inproc`, held in that service only long enough to answer the challenge and then scrubbed. | A component having no path to a secret is better than a component being careful with one — which is the strongest argument for finishing the portal path and retiring the fallback. |
| **Private key on the card** | Never leaves the card. | That is the point of the card. |
| **PoP key** | Never seen by the client. FreeRDP generates and retains it; the client receives only `req_cnf`. | The client cannot leak what it never has. |
| **Account records** (account id, authority, tenant, client id) | Keyring, beside the refresh token. | Not secret in the same sense, but they name a person and a tenant. |

**No persistent cache mode.** When the Secret Service is unavailable the client does **not** fall
back to a file. It runs with no persistence, or exits `40` so a dispatcher can fall through. A
silent downgrade from "keyring" to "file in the home directory" is the kind of thing nobody notices
until it is in a backup.

**Logout must be complete.** `logout` removes the refresh token, the account record, *and* asks the
service to discard the web session that account established. A logout leaving the Entra session
cookie behind has not logged anybody out.

## Logging

**Never logged, at any level, redacted or not:** access tokens, refresh tokens, ID tokens, any JWT;
authorization codes; PKCE verifiers and challenges; `state` values; the completion URI or any URI
carrying a query string from the authorization server; the authorization server's
`error_description`, which routinely names the account, the tenant and the policy that failed; HTTP
response bodies from the token endpoint; cookies and `Authorization` headers.

**Structural redaction**, as on the service side.

**What a DEBUG log may contain:** outcome symbols from the callback classifier (`CODE`, `ERROR`,
`UNRELATED`, `INVALID`); the authority host — a public constant, and the single most useful field
when diagnosing a wrong-cloud failure; OAuth error *codes* without their descriptions
(`invalid_grant`, `interaction_required`, `AADSTS50011`); cache hit and miss counts; the service's
response code; and phase timings.

**What a DEBUG log must not become:** a way to reproduce the sign-in. If a support bundle containing
a DEBUG log would let its reader connect as the user, the redaction is wrong. There is no "trace"
level that relaxes these rules, because a level that relaxes them will be enabled by somebody in
production.

---

## Independent review

Before either interface is frozen, five things want an independent pair of eyes:

1. The service's completion matcher and its percent-decoder.
2. The service's peer check and caller-identity resolution, and everything downstream that trusts
   the answer — the chrome, the partition choice, the result binding, and what is passed to the
   smart card service about the original caller.
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
collapse the browser layer back into the Entra client.** A narrowly scoped Entra/AVD helper is better than a generic authentication service with an
ill-defined trust model, and this is a real outcome to plan for rather than a formality.

## Reporting

This is a design sketch with no users and no releases. Problems in the *design* belong in an issue.
There is nothing deployed to report a vulnerability against yet; when there is, this section will
name a contact and a disclosure window.
