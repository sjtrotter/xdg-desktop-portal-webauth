# Architecture

Status: design sketch. Nothing described here is implemented.

Three layers, and the boundaries between them are the whole design. Only two of them are in this
repository:

```
  FreeRDP ──GetCommonAccessToken──▶ entra-token-helper        layer 3, clients/entra/
                                      │  OAuth, PKCE, cache, sovereign clouds
                                      │
                                      ├─ D-Bus ─▶ io.github.sjtrotter.WebAuthentication1
                                      │             │                   layer 2, service/
                                      │             │  transaction layer: identity, policy,
                                      │             │  completion matching, one response
                                      │             │
                                      │             ├─▶ browser session  (in-process vtable)
                                      │             │     WebKitGTK 6.0 + GTK4:
                                      │             │     web view, security chrome
                                      │             │
                                      │             ├─▶ certificate adapter (tls/client_cert.h)
                                      │             │     portal ─ D-Bus ─▶ Smartcard1  (layer 1,
                                      │             │       preferred            ANOTHER REPO)
                                      │             │     inproc ─ p11-kit + own chooser and PIN
                                      │             │       fallback, and the path known to work
                                      │             ◀── completion_uri
                                      │
                                      └─ Secret Service keyring (refresh tokens)
```

Layer 1 knows nothing about the web. Layer 2 knows nothing about OAuth, and as little about cards as
the chosen adapter allows. Layer 3 owns no windows. None of them knows anything about RDP.

**The smart card service is an external component and is NOT a hard dependency for v0.** It is a
separate project in its own repository (working name `smartcard-portal`), sketched in parallel, and
it is the *preferred* way to satisfy a certificate challenge — but the mechanism connecting it to a
WebKit handshake is unproven, so the certificate path is an adapter with an in-process fallback. A
machine with no smart card service installed still signs in. See
[decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md).

Note what the arrow into the browser session is **not**: it is not a D-Bus interface. Version 0 is
one service with an in-process abstraction, deliberately; see "Process model" below. The arrow out
of the certificate adapter *may* be D-Bus, depending on which implementation is in use — which is
exactly why it is behind an adapter.

## Layer 2 — the web authentication service

Components under [`service/src/`](../service/src/). The interface itself is
[SERVICE-INTERFACE.md](SERVICE-INTERFACE.md).

### `service` — [`service/src/service.h`](../service/src/service.h)

The D-Bus transaction layer: the object that owns `io.github.sjtrotter.WebAuthentication1`. It
draws nothing and knows nothing about web engines. It checks that the peer is the same UID,
resolves the caller's identity, applies the policy a caller may not influence (session mode,
timeout ceiling, storage partition), validates both URIs before anything is opened, mints the
`Request` object, rate-limits, and drives one browser session to exactly one terminal response.

### `transaction` — [`service/src/transaction.h`](../service/src/transaction.h)

One transaction, and the only object allowed to complete it. Owns the URIs, the deadline, the
assigned partition, the resolved caller identity, and exactly one terminal result. Reference
counted, because the browser session, each UI-thread idle, the bus method invocation and the
timeout source all hold references and outlive one another in unpredictable orders.

The races are *specified*, not discovered: a committed completion wins over a simultaneous
`Close()`; `Close()` never yields a later success; caller bus disconnection cancels immediately; a
timeout answers `2`; browser-session termination answers `2` exactly once; every late event after
the terminal result is discarded; and the certificate and PIN dialogs belong to the transaction and
close with it.

The model comes from the working Remmina implementation, which replaced a design that accepted any
redirect the web view happened to see and polled a borrowed pointer every 500 ms without a bound.

### `identity` — [`service/src/identity.h`](../service/src/identity.h)

Who is asking, and how much of that answer can be believed — as a **type**, not a comment.
Sandboxed (Flatpak/Snap, through the containment framework's mediation) is authenticated;
cgroup-derived is a label; an unsandboxed peer is unverified, and a caller-supplied app id is only
a claim. The unique D-Bus name and UID identify the connection and the user reliably, and the
application publisher not at all.

This distinction has teeth elsewhere: results are bound to the initiating unique connection; an
unverified label is never a partition key on its own; and an unidentified caller may need an
explicit confirmation before the card is used.

### `completion` — [`service/src/completion.h`](../service/src/completion.h)

Exact matching, on parsed URIs, with no prefix mode. Scheme and host case-insensitive (host IDNA
normalised), ports normalised, path exact, userinfo forbidden, query and fragment carrying the
result and taking no part in matching, and malformed input rejected rather than normalised. This is
the routine that decides which URI is handed to the caller, so it is the one that gets an
independent review.

### `storage` — [`service/src/storage.h`](../service/src/storage.h)

Which website data store a transaction runs in, and what a store covers — which is every piece of
engine state, not just cookies. Two modes in version 1: `shared` (all of this service's
transactions, and emphatically *not* the user's real browser) and `ephemeral` (created with the
transaction, destroyed with it, from an ephemeral data manager rather than by clearing afterwards).

### `browser_session` — [`service/src/browser_session.h`](../service/src/browser_session.h)

The in-process seam to whatever shows the page: a C vtable with a capability mask, not a bus
interface. Intended implementations in preference order: the system browser where the completion
mechanism lets it securely return the result; a service-owned WebKitGTK session where interception
or PKCS#11 handling requires it (the AVD/PIV case); manual paste as a headless fallback; a browser
extension only as an experimental integration.

### `webkit_session` — [`service/src/webkit_session.h`](../service/src/webkit_session.h)

The GTK4 + WebKitGTK 6.0 implementation. Beyond showing a page: it tests every top-level navigation
and finishes the transaction *before the navigation is loaded*, because the completion URI carries
the credential the flow was for; it answers TLS client-certificate challenges bound to the verified
host of the page it is showing; it uses exactly the partition it was given; and it renders the
security chrome. Fixed, not configurable: no TLS-error bypass, no caller-controlled certificate
trust, downloads and autofill disabled, no URI or page-content logging.

The honest caveat, recorded in the header: RFC 8252 prefers an external user-agent, and while a
service-owned engine is a real improvement on a web view embedded in the requesting application —
the requester cannot reach the DOM — some identity providers may still classify it as embedded.

### `chrome` — [`service/src/chrome.h`](../service/src/chrome.h)

The part of the window the caller cannot influence: the resolved caller identity (and a plain
statement when it could not be verified), the engine's own current origin, and the caller's `title`
hint rendered visibly beneath and marked as application-supplied. Any same-UID application can ask
this service to show a convincing corporate sign-in page; this is the only thing standing between
that and a phishing launcher.

Whether it includes the certificate chooser and the PIN prompt depends on the adapter: under
`portal` those are the smart card service's windows and this service's job is to have already
established, and to still be displaying, the caller and origin that the other window will restate;
under `inproc` they are this service's own. Either way both must state the same four things —
requesting application, origin, certificate identity, purpose — so what a user learns from one
transfers to the other.

Accessibility lives here too, as acceptance criteria rather than refinement: AT-SPI exposure for
every service-owned control, meaningful focus order, screen-reader announcement of caller and
origin, no meaning carried by colour alone, and focus restored to the calling application on close.
WebKit covers the accessibility of web *content*; it covers none of these, and these are the parts
carrying the security decisions.

### `tls/client_cert` — [`service/src/tls/`](../service/src/tls/)

Answering a TLS client-certificate challenge, behind an adapter with two implementations:

- **`portal`** ([`client_cert_portal.h`](../service/src/tls/client_cert_portal.h)) — call the smart
  card service's `AcquireCredential` (named for what it grants: private-key use, not just a
  certificate) with `purpose: "client_auth"` and `context` set to the destination host, then satisfy
  the operation either by brokered `Sign` behind a GnuTLS external-signer path — unproven, only
  worth using if WebKitGTK/glib-networking expose one — or by the experimental
  `OpenPkcs11Endpoint` compatibility endpoint. **Preferred when available**, because the chooser
  and the PIN then belong to one trusted service shared by every application, and the PIN never
  reaches this process.
- **`inproc`** ([`client_cert_inproc.h`](../service/src/tls/client_cert_inproc.h)) — enumerate with
  [`pkcs11.h`](../service/src/tls/pkcs11.h), show this service's own
  [`chooser.h`](../service/src/tls/chooser.h) and [`pin.h`](../service/src/tls/pin.h), and build the
  certificate with `g_tls_certificate_new_from_pkcs11_uris()` against the **system** p11-kit
  configuration. **Retained as the fallback**, and there is no release in which the AVD case is
  blocked on another project shipping.

**Why the fallback is not scaffolding.** The ends of the chain are documented and fine —
`g_tls_certificate_new_from_pkcs11_uris()` takes URIs with the key used only later, and
`webkit_credential_new_for_certificate()` takes a `GTlsCertificate`. The middle is not: a PKCS#11
URI cannot name a socket, p11-kit remoting needs the client module registered in *configuration*,
GLib's constructor has no module parameter, and WebKit's network process may not see a module
registered after it started. That is spike [S2](SPIKES.md), and until it passes this service must
not hard-depend on the portal path.

**Rules the in-process path must keep**, because it is a trusted dialog when it is the one running:
the chooser names the requesting application, the origin, the certificate identity and the purpose
*before* any PIN; the PIN is never stored, never logged, never in the DOM, and its buffer is cleared
on every exit path; a challenge is answered once plus at most one retry the TLS stack itself
initiated, because automatically re-answering is how a card gets locked; p11-kit's own trust tokens
are skipped; a token holding no certificate is *empty*, not an error; and cancelling anywhere
cancels the transaction rather than continuing without a certificate.

**What this service owns under either adapter:** recognising the challenge, determining and
displaying the origin that raised it, refusing challenges from hosts unrelated to the page being
shown, binding it to one cancellable transaction, and releasing whatever the adapter held on every
exit path. That last one is small in code and disproportionate in risk.

### `redact` — [`service/src/redact.h`](../service/src/redact.h)

Structural, not textual: the logging interface takes typed fields and the kind decides what may be
printed. There is deliberately no "log this URI" entry point, and URI and response sizes are capped
so a hostile page cannot make the log the problem instead.

## Layer 3 — the Entra ID / AVD token client

Components under [`clients/entra/src/`](../clients/entra/src/). The CLI contract is
[ENTRA-CLIENT-CLI.md](ENTRA-CLIENT-CLI.md).

### `cli` — [`clients/entra/src/main.c`](../clients/entra/src/main.c)

Argument parsing for the four verbs, option validation, exit-code mapping, plain-vs-JSON output.
No network, no windows: it builds a request object and prints a response object.

### `request schema` — [`ipc/request.h`](../clients/entra/src/ipc/request.h)

The versioned request/response objects (`"schema": 1`). Built from `argv` today, shaped as messages
from the start so that putting the client behind a socket later is a transport change rather than a
redesign — and so the same shape can become a typed FreeRDP provider request.

### `webauth client` — [`webauth_client.h`](../clients/entra/src/webauth_client.h)

The client's only interactive dependency: one `Start`, one `Response`. It subscribes to `Response`
on the handle derived from its own `handle_token` *before* calling `Start`, so a fast completion
cannot race the subscription. If the service is unreachable this reports *unavailable* rather than
failing, so a dispatcher can fall through to another provider.

### `transaction` — [`oauth/transaction.h`](../clients/entra/src/oauth/transaction.h)

The `state`, the PKCE verifier and its S256 challenge, the expected redirect, a deadline, and a
single-use flag. Note what it no longer contains: a window, a web view, a reference count shared
with UI callbacks. Moving the browser into layer 1 made this a plain object.

### `oauth` — [`callback.h`](../clients/entra/src/oauth/callback.h), [`discovery.h`](../clients/entra/src/oauth/discovery.h), [`clouds.h`](../clients/entra/src/oauth/clouds.h)

Classification of the URI the service returned, the code/refresh/PoP token requests, endpoint
derivation, and the sovereign-cloud table. The classifier is the security-critical piece on this
side and is modelled on FreeRDP's `freerdp_client_aad_parse_callback` from the
`aad/oauth-hardening` branch: exact redirect match, no userinfo or fragment, `state` present
exactly once and compared in constant time, exactly one of `code` or `error` where a bare parameter
still counts as an occurrence, strict percent-decoding with `%00` rejected.

**The two checks are not redundant.** The service answered "does this URI equal the completion URI
the caller gave me" — a question about URIs. The client answers "is this a valid authorization
response to the request I made" — a question about OAuth, involving a secret the service never saw.
Neither substitutes for the other, and putting the second one in the service is what would make the
service protocol-specific.

Discovery is **optional**: the request always carries a configured authority, and discovered
endpoints only refine it. (FreeRDP has the opposite coupling today — see [ROADMAP.md](ROADMAP.md).)

### `cache` / `keyring` — [`cache/keyring.h`](../clients/entra/src/cache/keyring.h)

Refresh tokens and account records in the Secret Service keyring; access tokens in memory only,
keyed by `(account, authority, tenant, client id, sorted scopes, token kind, PoP binding)`. The
binding is in the key because a PoP token bound to one `kid` is useless for another. An explicit
"no persistent cache" mode stores nothing — the correct behaviour when no keyring is available, not
a silent fallback to a file.

### `log` / `redact` — [`log/redact.h`](../clients/entra/src/log/redact.h)

The same structural principle as layer 1's, over the artifacts layer 1 never sees: authorization
codes, tokens, verifiers, `state`, and the authorization server's `error_description`.

## One AVD connection, end to end

FreeRDP needs two tokens for one connection, in this order.

### 1. ARM gateway bearer token

1. FreeRDP resolves the cloud from the `.rdp`/`.rdpw` file and calls `GetCommonAccessToken` with
   `ACCESS_TOKEN_TYPE_AVD`; the provider derives client id, AAD host, tenant and scope from
   settings.
2. The installed callback runs `entra-token-helper token --authority login.microsoftonline.us
   --tenant <tenant-id> --scope 'https://www.wvd.azure.us/.default' …`.
3. **Silent path.** The client looks for a live access token under that cache key; failing that,
   for a refresh token for the account in the keyring, and redeems it. If either succeeds it prints
   the token and exits `0`. **Layer 1 is never called and no window appears.**
4. **Interactive path.** Otherwise, if `--prompt` allows it, the client creates a transaction
   (fresh `state`, fresh PKCE verifier and S256 challenge), builds the authorization URL, subscribes
   to `Response`, and calls
   `Start(parent_window, start_uri, "https://login.microsoftonline.com/common/oauth2/nativeclient", { handle_token, session_mode: "shared", timeout: 300, title })`.
5. The service checks the peer, resolves the caller identity, validates both URIs, decides the
   storage partition, and creates a browser session. The window opens with chrome naming the caller
   and the origin; the user authenticates; `certauth.login.microsoftonline.us` challenges for a
   client certificate; **the certificate adapter runs** — the smart card service's chooser and PIN
   window under the `portal` adapter, this service's own under `inproc`, both naming application,
   origin, certificate and purpose; the handshake completes; the authority redirects to the
   `nativeclient` URL; the navigation policy matches it exactly, commits
   the completion, and destroys the window before it renders. `Response(0, { completion_uri })`.
6. The client classifies `completion_uri` against its transaction, extracts `code`, exchanges it
   with the verifier at the authority's token endpoint, stores the refresh token in the keyring,
   caches the access token in memory, prints it, exits `0`. With `--prompt never` it would have
   exited `10` at step 4 instead, printing nothing on stdout.
7. FreeRDP puts the token in `FreeRDP_GatewayHttpExtAuthBearer` and calls ARM, which returns the
   connection details for the session host.

### 2. Session-host proof-of-possession token

8. FreeRDP generates the RDS PoP key — it, not the client and certainly not the service, owns that
   key — formats `req_cnf` as base64url of `{"kid": "<key-id>"}`, builds the RDS resource scope from
   the target hostname, and calls `GetCommonAccessToken` with `ACCESS_TOKEN_TYPE_AAD`, the scope,
   and `req_cnf`.
9. The callback runs the client again with `--req-cnf <value>` and the RDS scope.
10. **Silent path — the one that decides whether this is worth building.** The client should be able
    to redeem the refresh token it already holds for a *new* PoP token bound to a *new* key, with no
    service call and no second card interaction. Whether that works under Government tenant policy
    and Conditional Access is spike S1 in [SPIKES.md](SPIKES.md). If it does not, step 10 becomes a
    second transaction — and this is where the `shared` session store earns its keep: the Entra
    session cookie from step 5 is still there, so the user should see a window but not a second card
    prompt.
11. The client prints the PoP token. FreeRDP requests the RDS nonce and completes the RDS-AAD
    handshake with the key from step 8.

An interactive window appears at most once per connection if S1 passes; at most twice if it does
not, and the second should be a click rather than a card.

## Process model

**Version 0 is ONE D-Bus-activated service.** Not a frontend and a backend. Internally:

```
D-Bus transaction layer          service.h, transaction.h, identity.h, storage.h, completion.h
    → browser session interface  browser_session.h   (a C vtable, in-process)
        → WebKitGTK session      webkit_session.h, chrome.h
            → cert adapter       tls/client_cert.h
                → portal         tls/client_cert_portal.h   (D-Bus, out to Smartcard1)
                → inproc         tls/client_cert_inproc.h, pkcs11.h, chooser.h, pin.h
```

The abstraction is there so a second implementation can be added without touching the transaction
layer, and so the eventual `org.freedesktop.impl.portal.*` split has an obvious place to happen.
But **no backend D-Bus ABI is published**, because an independent prototype gains none of a real
portal's properties by imitating its names, and doubles its D-Bus surface, activation and crash
handling, versioning obligations, packaging, capability negotiation, error translation and
transaction-lifetime bugs. See [ROADMAP.md](ROADMAP.md) phase 2.

**The Entra client is a one-shot CLI.** Spawned per request, does one thing, writes to stdout,
exits. The smallest thing that integrates (a client installs one callback that runs a subprocess)
and the easiest to reason about as a credential boundary: no long-lived process holds refresh
tokens in memory.

The two-layer split removed most of the pressure to make the *client* a daemon: the warm browser
session a client daemon would have bought is now the service's shared store, and the service is
already long-lived. What a client daemon would still buy is one place to serialize concurrent
requests and a cancellation channel — real, but not yet asked for.

**Concurrency.** Client requests are serialized **per account**, with a lock in `$XDG_RUNTIME_DIR`.
Two connections opened at once must not open two transactions for the same account, and must not
race to redeem the same refresh token: some authorities rotate refresh tokens on redemption, so a
lost race can invalidate the winner's token.

The service serializes nothing. Concurrent transactions from different callers are normal, each
with its own window and its own transaction object; it does rate-limit per connection, which is a
different concern.

## Explicitly not owned

**The service does not own:** any protocol meaning (OAuth, OIDC, SAML); token exchange; credential
storage beyond web session state; the choice of identity provider, tenant or cloud; policy about who
may sign in to what. It opens a URI and returns a completion. It owns the card only as far as the
chosen adapter forces it to, and prefers not to: the `portal` adapter moves enumeration, the
chooser and the PIN out of this process entirely, and retiring the in-process fallback once that
path is proven is the next decision after
[0007](decisions/0007-certificate-adapter.md).

**The Entra client does not own:** the RDP session; any window, web view, certificate chooser or PIN
prompt; cloud *selection* (FreeRDP resolves that from the `.rdp`/`.rdpw` file and passes the
authority in — the client's cloud table exists to *allowlist* what it will talk to, not to decide);
the PoP key (FreeRDP generates it, retains it, and completes the handshake with it — a provider that
generates its own key returns a token FreeRDP cannot use); and device enrollment, PRT or Conditional
Access device claims, which are `sso-mib`'s and the Microsoft broker's territory.
