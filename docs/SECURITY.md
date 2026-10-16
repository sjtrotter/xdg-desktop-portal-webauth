# Security model

[Testing](TESTING.md) records fixture coverage and live Entra ID evidence from 2026-09-05.
The frontend is xdg-desktop-portal on `experimental/integration`; the backend is this repository.
[ADR 0010](decisions/0010-backend-only-frontend-lives-upstream.md) records that ownership.

The frontend establishes caller identity, validates requests and owns public results. The
backend protects the browser session and completion URI. The Certificate backend protects
card operations and owns its chooser and PIN prompt. The consumer,
[entra-token-helper](https://github.com/sjtrotter/entra-token-helper), protects account tokens.
Refresh tokens can provide standing access without another card operation, so their lifetime
and storage need independent controls in the consumer.

The [public guide](PUBLIC-INTERFACE.md) defines experimental export conditions. A frontend
without support for this interface cannot discover and route it. This does not constitute
access control on the backend; the peer checks below remain required.
[ADR 0008](decisions/0008-build-to-the-upstream-shape.md) records the process split, and
[the impl guide](IMPL-INTERFACE.md) assigns enforcement responsibilities.

## Implemented controls and gaps

| Control | Where | State |
|---|---|---|
| Only the owner of `org.freedesktop.portal.Desktop` may call the impl interface, re-resolved from the bus on every accept | `src/webauthentication-impl.c` | Implemented |
| The same check on `Request.Close()` | `src/request-impl.c` | Implemented |
| `start_uri` re-validated: absolute, https, host, no userinfo, no control characters, no backslash, bounded length | `src/completion.c` | Implemented |
| `completion_uri` re-validated: absolute, no userinfo or wildcard host; HTTP(S) requires a host, private-use schemes accept a path | `src/completion.c` | Implemented |
| Exact completion matching on parsed URIs, ports normalised, query and fragment ignored | `src/completion.c`, unit-tested against the frontend's fixtures | Implemented |
| A completion match in any frame ends the flow before load | `src/webkit-session.c` | Implemented. See [completion checks](IMPL-INTERFACE.md#completion-checks) and [the access-log limitation](TESTING.md#recorded-runs). |
| `session_mode` obeyed as a decision; an unknown value is an error | `src/storage.c` | Implemented |
| `ephemeral` uses an ephemeral network session from creation | `src/webkit-session.c` | Implemented |
| An unidentified caller is narrowed to `ephemeral` | `src/storage.c` | Implemented |
| The persistent store is per application, 0700, and is not named by the caller | `src/storage.c` | Implemented |
| The deadline, starting when the window opens | `src/transaction.c` | Implemented |
| Exactly one terminal result; late events discarded | `src/transaction.c` | Implemented |
| The window is destroyed on every exit path, and adapter release is invoked | `src/transaction.c`, `src/webkit-session.c` | Implemented; see [grant and connection lifetimes](#certificate-grant-and-connection-lifetimes). |
| Credentials, client-certificate decisions included, are never written to a persistent store | `src/webkit-session.c`, `webkit_network_session_set_persistent_credential_storage_enabled(FALSE)` | Implemented |
| A finished transaction revokes the certificate grants it caused | Unimplemented | See [grant and connection lifetimes](#certificate-grant-and-connection-lifetimes). |
| A finished transaction closes the connections it authenticated | Unimplemented | Not implemented. WebKitGTK offers no way to; ephemeral storage can reuse authenticated connections |
| The frontend vanishing cancels every transaction | `src/webauthentication-impl.c` | Implemented |
| TLS errors fail closed, with no bypass reachable from the bus | `src/webkit-session.c` | Implemented |
| Downloads, popups, JavaScript-opened windows and every permission request refused | `src/webkit-session.c` | Implemented |
| Certificate challenges from a host unrelated to the page are declined | `src/webkit-session.c` | Implemented |
| `pin-value` rejection | `src/tls/client_cert.c` | Implemented for both `--client-cert-uri` and `--client-key-uri`. |
| Structural redaction: no entry point can log a URI, a query, a certificate URI or a PIN | `src/redact.c`, unit-tested | Implemented |
| `PR_SET_DUMPABLE(0)` and `RLIMIT_CORE 0` | `src/main.c` | Implemented for this process. `execve` resets dumpable, so WebKit's network and web processes are not covered |
| Chrome states the frontend-established caller, its identity level, and the engine's origin | `src/chrome.c` | Implemented. Designer and user review outstanding |
| App id derivation, option filtering, the returned-URI re-check, exactly one `Response` | Frontend | Implemented there |
| Rate limiting | Frontend | Not implemented anywhere |
| The `portal` certificate provider | `src/tls/client_cert_portal.c` | Implemented; preferred provider: tested against the Certificate portal's module 2026-09-04/05/06 |
| Accessibility: AT-SPI labels, keyboard operation, focus return | `src/chrome.c` | Partly: labels and Escape are there; nothing has been tested with a screen reader |


## Assets and threats

Assets include persistent authenticated web sessions, remembered certificate selections,
card-operation authority, trust in portal-owned UI and credentials carried in completion URIs.
The portal can provoke a signature for a caller-selected origin. Under `portal`, the Certificate
backend mediates it; under `pkcs11`, the operator chooses the token outside any request.

In scope are cross-user access attempts, same-UID callers selecting hostile start or completion
URIs, phishing under a misleading application label, shared-session reuse by hostile callers,
compromised page content, induced certificate requests, browser escape and credential leakage
through logs or crash reports.

Strong isolation between hostile unsandboxed processes of one UID is outside this design's
guarantees. An unrestricted same-UID debugger, a compromised session bus and WebKit sandbox
vulnerabilities are outside the threat model. The backend must still use the engine sandbox
correctly and receive security updates. Sandboxed callers benefit from mediated identity and
D-Bus access restrictions; host labels have weaker guarantees.

## Access control and caller identity

The frontend serves one user's session bus and checks caller UID before request parsing.
There is no root or cross-user mode. The backend accepts Start and impl Request.Close only from
the owner of `org.freedesktop.portal.Desktop`, re-resolving ownership on every accepted call.
Refusal returns an error without opening a window. Flatpak portal proxy policy does not
expose impl names. Distribution D-Bus policy can add defense in depth, but unrestricted host
processes can address session-bus names and must not be treated as strongly isolated.

`xdp_invocation_get_app_info()` derives caller identity at the frontend. Resolving the backend's
peer would identify xdg-desktop-portal itself. The backend consumes `app_id` and
`app_identity_level` and renders their confidence separately from application-supplied text.

| Level | Source | Guarantee |
|---|---|---|
| `sandboxed` | Flatpak/Snap mediation | Authenticated containment metadata |
| `host` | Host systemd/cgroup unit | Useful label; insufficient as a security principal |
| `unidentified` | No established app ID | Unique bus name and UID identify the connection |

Results are bound to the initiating unique connection; caller disconnection cancels the
request. The chrome shows the frontend-established identity and engine origin. An empty app ID
forces ephemeral storage. The implementation keys persistent storage on sanitized nonempty app
IDs, including host labels; this cannot guarantee isolation between hostile unsandboxed apps.

First-use confirmation for unidentified callers remains a policy question. Per-connection rate
limiting belongs to the frontend and is unimplemented. A hostile caller can repeatedly open
windows and provoke certificate prompts; honest chrome and the Certificate portal's consent
policy are the available controls.

## URI constraints

[The public contract](PUBLIC-INTERFACE.md#completion-matching) defines validation and matching.
The frontend rejects invalid input before activation and rechecks the returned URI. The backend
validates independently and intercepts every matching navigation before load. Prefix matching
would accept `/callback.evil` or `/callbacker` for `/callback`; userinfo can disguise a destination
such as `https://example.com@evil.invalid/`.

Coverage for malformed percent escapes, `%00` in every component and cross-parser disagreements still
needs expansion; encoded NULs must not create truncated comparisons in downstream decoders.

Scheme registration belongs to the caller.
[Application response validation](PUBLIC-INTERFACE.md#completion-matching) is required to detect
injected authorization responses, including those from subframes; frontend matching alone
cannot establish authenticity.

## Fixed behavior and storage

D-Bus callers cannot bypass TLS errors or security chrome, name arbitrary storage paths, install
certificate trust, persist a PIN, request an unlimited timeout or set the verified application
label. `title` is an application-supplied hint displayed separately. The command-line development
trust pin is absent from the installed service's arguments.

`shared` is the default requested mode. This backend stores data per nonempty app ID under
`$XDG_DATA_HOME/xdg-desktop-portal-webauth/<app id>/`, with `data/` and `cache/` created mode
0700. The ID is limited to 128 characters, replaces characters outside `[A-Za-z0-9._-]`, and
strips leading dots. The cookie jar is `data/cookies.sqlite`. A host label's sanitization is
path safety, and does not authenticate the label.

`ephemeral` creates an ephemeral website store from the start for each transaction. Clearing a
persistent store afterwards would race other flows. An unknown mode fails; the backend must
honor a forced ephemeral decision and must never downgrade it to shared. An empty app ID forces
ephemeral mode. The backend derives persistent paths from the frontend-supplied ID.

Storage isolation applies to cookies, local/session storage, IndexedDB, HTTP and disk caches,
service workers, HSTS, HTTP authentication, permissions and certificate-selection memory.
Downloads and autofill are disabled. The fixture measures cookie isolation; full engine-state
coverage remains outstanding. Persistent credential storage is disabled on every
WebKitNetworkSession. Network connections may still survive store teardown.

The store is separate from the user's Firefox or Chrome profile. It inherits none of their SSO,
enterprise policies, extensions, enrollment or browser-bound credentials. A hostile caller can
reuse state available to its partition; the chrome must identify which native application asked.
[ADR 0005](decisions/0005-service-shape.md) records this risk.

## Client certificates

[ADR 0007](decisions/0007-certificate-adapter.md) records two providers. This process has no
certificate chooser, token enumeration or PIN prompt. Both providers construct a certificate
from PKCS#11 URIs; [S2](SPIKES.md) records the WebKit transport and support-matrix gaps.

### Portal provider

The Certificate portal's permanently registered client-side PKCS#11 module presents the token
specified by [`portal-token.h`](../src/tls/portal-token.h). Its label, manufacturer, model,
object and `CKF_PROTECTED_AUTHENTICATION_PATH` form the shared contract. The module must be
visible in p11-kit configuration before WebKit starts. The card, chooser and PIN remain in the
Certificate backend. This backend holds no PIN under this provider.

WebKit passes the certificate URI to its network process. There is no external-signer seam or
WebKitNetworkSession TLS-interaction setter. `OpenPkcs11Endpoint` is absent from both Certificate
interfaces; `context` is also absent, so origin or original-caller hints can travel only as
untrusted `reason` text. Stock `p11-kit server` forwards a token, does not carry login state,
and cannot be named by a PKCS#11 socket URI.

`auto` falls through to `pkcs11` only when the Certificate interface is absent or no module
configuration file exists. The check does not load the module. A configured module that fails
to load or acquire a credential ends the challenge without fallback. A grant bounds certificate,
operations, mechanisms and expiry. Brokered Sign provides accounting and revocation; it cannot
attest that arbitrary signing input came from TLS.

### Attribution and consent

The module calls the Certificate public interface as an ordinary client, so its chooser attributes
the request to this backend. Both portals share a frontend process; forwarding the original
application identity there is unimplemented. A caller-provided third-party app ID is untrusted.
Authenticated IPC with independently derived peer identity, or a frontend-issued capability,
could support delegation; neither is built. [ADR 0010](decisions/0010-backend-only-frontend-lives-upstream.md)
records the identity constraint.

[Testing](TESTING.md#two-grants-per-fresh-handshake) records the separate UI-process and
network-process grants and the resulting chooser and PIN counts. Consent UI should identify
requester, origin, certificate and purpose before PIN entry; the attribution gap must remain visible.

Process-tree delegation is excluded. The archived frontend branch is
`experimental/certificate-webauthentication+delegation`. It derived ancestry through pidfds and
`/proc`, but Flatpak app-info pidfds identify a sandbox instance, and host descendants can cross
application boundaries, for example through `flatpak run com.other.App`. Grants bound to app-info
identity could address sandbox callers; host processes have no equivalent instance identity.
This backend sets no `PKCS11_PORTAL_CERTIFICATE_DELEGATE_TO_CHILDREN`.

### Certificate grant and connection lifetimes

Transaction teardown invokes adapter release on completion, failure, timeout, cancellation and
frontend loss. For `portal`, release only logs retention: the adapter holds no session handle for
ReleaseGrant, cannot reach the other process's module and cannot finalize just one module through
p11-kit's proxy without affecting the others.

The two grants expire independently, are invalidated by the portal or end when their holding
process exits and C_Finalize calls ReleaseGrant. Invalidation includes card removal, policy,
Certificate backend restart and frontend loss; wire reasons include `token_removed`, `policy`,
`backend_gone` and `error`. The module requests no lifetime of its own and receives the portal
default. Releasing one grant does not release the other.

TLS authenticates a connection at handshake. Grant revocation does not remove that authentication
from an open connection. The [second-Start measurement](TESTING.md#repeated-start) demonstrates
connection reuse across separate ephemeral WebKitNetworkSessions. These sessions isolate website data but share
a network process and can reuse its connections.

WebKitGTK exposes no idle-connection closure API or website-data type for client-certificate
decisions. This backend sets persistent credential storage to FALSE, which limits storage beyond
the window without closing those connections. A run requiring a fresh authenticated transport
must restart the backend. Possible improvements include a transaction-bounded requested_lifetime,
a WebKit API accepting PKCS#11 URIs without UI-process import, and transport-level connection
termination. None is implemented.

### PKCS#11 provider

The operator names a token with `--client-cert-uri`; an optional `--client-key-uri` names its key.
This provider supplies no chooser. It reads a required PIN from `--client-cert-pin-file`, holds
it for the transaction and overwrites it on release. Both URI options reject `pin-value` and
report an invalid argument directing the operator to `--client-cert-pin-file`.

The PIN answers WebKit's CLIENT_CERTIFICATE_PIN_REQUESTED through
`webkit_credential_new_for_certificate_pin()`. It never enters the DOM, logs or persistent engine
credential storage. The backend answers each engine-raised PIN challenge with the cached PIN
without checking retry state. Engine-driven retries are unverified; a wrong PIN could cause
repeated attempts and lock the card. GnuTLS and the module own login state, so this provider makes
no PKCS#11 logout attempt. Real cards can cache authentication internally.

### Shared constraints

Certificate requests must be bound to one cancellable transaction and a related origin. The
backend accepts the engine's current host or pending navigation host; unrelated hosts produce
`unrelated_certificate_challenge`,
a backend addition to the XML's open reason vocabulary. A declined challenge cannot silently
become successful certificate authentication. If no provider can supply a required certificate,
the flow ends `2` / `credential_unavailable`; an optional certificate challenge need not fail a
flow that can continue without it. Chooser cancellation is indistinguishable from other unavailable
credential failures at this boundary.

`PR_SET_DUMPABLE(0)` makes `/proc` entries inaccessible to the frontend's caller-identification
path. [`harden.h`](../src/harden.h) temporarily yields that protection in a counted interval around
the portal provider's certificate constructor, then restores it. That interval holds no PIN and
precedes completion-code receipt. Message logs mark `process-hardening outcome=identifiable-begin`
and `-end`. The pkcs11 provider opens no such interval. RLIMIT_CORE remains zero. WebKit's spawned
processes are not covered by this process's dumpable flag because execve resets it.

## Transactions

The frontend enforces its deadline independently of the backend. It closes the impl Request and
answers `2` / `timeout` on expiry. The backend starts its timer when the window opens. Both use
the [public timeout option](PUBLIC-INTERFACE.md#options). Startup and scheduling determine which
timer fires first; both must tear down the window and discard late events.

Backend death produces `2` / `backend_disappeared`. Frontend death destroys backend windows;
caller disconnection cancels the public Request. Completion is terminal once committed and late
navigation, Close and timeout events are discarded. Public Close unexports the Request and
suppresses later Response signals. Closing an already-completed Request has nothing left to
cancel. Browser termination must produce a defined failure and tear down the window and store.
Maximum URI and result sizes are interface requirements; malformed success results are rejected
by the frontend.

## Logging

Never log full start or completion URIs, query strings, page content, cookies, Authorization
headers, PKCS#11 URIs, certificate labels/serials/subjects or PINs. Typed fields determine what
can be rendered. Non-loggable values render kind and length, for example `<uri:212>`, except
secret fields, which render exactly `<secret>` without length. There is no arbitrary URI
logging entry point or caller-supplied format string.

WEBAUTH_FIELD_URI_SHAPE contains only scheme, host, port and path length. Debug logs may contain
outcome and reason symbols, accepted challenge origin, provider, caller identity and confidence,
counts and timings. Loader and TLS error text is truncated before embedded URIs. URI and result
sizes must stay bounded to prevent log amplification.

## Accessibility and review

The [public accessibility criteria](PUBLIC-INTERFACE.md#accessibility-requirements) are backend
requirements. Labels, descriptions, a textual lock indicator and Escape cancellation exist.
Screen-reader use remains untested; headless smoke tests use `GTK_A11Y=none`. The Certificate
backend has the same obligations for its chooser and PIN controls.

Independent review is required before interface freeze for both matchers and shared fixtures,
frontend peer checks and app-ID derivation, impl sender checks and option filtering, downstream
identity display and partition use, certificate authority lifetime, and completion/Close/timeout
races on both sides. [Testing](TESTING.md) describes current evidence.

If caller identity, displayed origin and storage partitioning cannot provide adequate protection,
the exit criterion is to put the browser layer back in the Entra client.
[ADR 0005](decisions/0005-service-shape.md) records that decision.

## Reporting

[The reporting policy](../SECURITY.md) covers issues and disclosure arrangements.
