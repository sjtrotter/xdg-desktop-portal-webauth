# Architecture

Status: design sketch. Nothing described here is implemented.

The web authentication service is **a portal frontend and a portal backend**, plumbed exactly as
xdg-desktop-portal plumbs every portal it has: applications call a frontend on a shared `Desktop`
bus name; the frontend derives who is asking, validates what it was given, finds a backend through
a `.portal` file, and forwards the call over an `impl` interface applications must never reach; the
backend owns the window. That shape is deliberate and early, and its costs are recorded in
[decisions/0008-build-to-the-upstream-shape.md](decisions/0008-build-to-the-upstream-shape.md).

```
  FreeRDP ──GetCommonAccessToken──▶ entra-token-helper           clients/entra/
                                      │  OAuth, PKCE, cache, sovereign clouds
                                      │
                                      │  D-Bus: io.github.sjtrotter.portal.Desktop
                                      ▼
                    ┌─────────────────────────────────────────┐
                    │  webauth-portal-frontend                │  service/frontend/
                    │  io.github.sjtrotter.portal.            │
                    │      WebAuthentication1                 │  no window, no engine,
                    │  app id · validation · options filter   │  no toolkit, no card
                    │  Request objects · one Response         │
                    └─────────────────────────────────────────┘
                                      │
                                      │  D-Bus: io.github.sjtrotter.impl.portal.
                                      │         WebAuthentication1
                                      │         (backend interface; NOT for applications)
                                      ▼
                    ┌─────────────────────────────────────────┐
                    │  webauth-portal-gtk                     │  service/backends/gtk/
                    │  GTK4 + WebKitGTK 6.0 web view          │
                    │  security chrome · storage partition    │
                    │  navigation interception                │
                    │  certificate adapter ───┐               │
                    └─────────────────────────┼───────────────┘
                                              │
                       portal (preferred) ────┴──▶ io.github.sjtrotter.portal.Smartcard1
                       inproc (fallback) ──▶ p11-kit + own chooser and PIN    SEPARATE REPO
```

The smart card portal knows nothing about the web. The web authentication portal knows nothing
about OAuth, and knows as little about cards as the chosen adapter allows. The Entra client owns no
windows. None of them knows anything about RDP.

**The smart card portal is an external component and is NOT a hard dependency.** It is a separate
project in its own repository, sketched in parallel, and it is the *preferred* way to satisfy a
certificate challenge — but the mechanism connecting it to a WebKit handshake is unproven, so the
certificate path is an adapter with an in-process fallback. A machine with no smart card portal
installed still signs in. See
[decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md).

Note what the arrow into the backend now **is**: a D-Bus interface, not the in-process vtable
version 0 had. And note what the arrow out of the certificate adapter is: a call to another
portal's **public** interface, made as an ordinary client. A backend never calls another backend.

## Responsibility split: frontend and backend

This is the table the rest of the document explains. Every row is upstream's own split, checked
against `xdg-desktop-portal/desktop-portal/account.c` and
`xdg-desktop-portal-gtk/src/account.c` — the smallest complete example of a UI-dialog portal pair.

| Concern | Frontend (`service/frontend/`) | Backend (`service/backends/gtk/`) | Upstream this mirrors |
|---|---|---|---|
| **Owns the bus name applications call** | Yes: `io.github.sjtrotter.portal.Desktop` | No; owns `io.github.sjtrotter.impl.portal.desktop.gtk`, which applications must not call | `org.freedesktop.portal.Desktop` vs `org.freedesktop.impl.portal.desktop.gtk` |
| **App id derivation** | **Only here.** Flatpak/Snap mediation, cgroup label, or host (unidentified) | Never. Its D-Bus peer is the frontend, not the application; it is *told* `app_id` | `shared/xdp-app-info*.c`; `app_id` is an impl argument |
| **Same-UID peer check** | Yes, before the request is parsed | Refuses any sender that is not its frontend | frontend-side in every portal |
| **Argument validation** | Yes: both URIs, before anything is forwarded. A malformed request is a D-Bus error, no backend is woken | Again, independently. Its safety must not depend on a frontend having been correct | `validate_reason()` + `xdp_filter_options()` in `account.c` |
| **Option filtering** | Yes: known keys only, unknown keys dropped, unknown values rejected, `timeout` clamped, `title` length-limited | Receives an already-filtered vardict | `XdpOptionKey` tables |
| **Session-mode / storage policy** | Decides it, and forwards it as a decision | Obeys it. Never derives a partition from an app id | `xdp-permissions.c`, portal-side policy |
| **Rate limiting** | Yes, per connection | No | frontend-side |
| **Request object the app holds** | Yes: `/io/github/sjtrotter/portal/desktop/request/<sender>/<token>`, exported before the backend is called | Exports its own impl Request at the same path on its own bus name, for `Close()` only | `xdp-request.c` vs gtk's `src/request.c` |
| **Exactly one `Response`** | Yes; also owes one when the backend dies | Answers once, by returning from the method | frontend emits, backend returns |
| **`parent_window` parsing** | No; forwards the string opaquely | Yes: `x11:`/`wayland:` and `xdg_foreign`, because only it has a display | gtk's `src/externalwindow.c` |
| **The window, the web engine, the chrome** | Never. No toolkit dependency, ever | Yes, all of it | gtk's dialogs |
| **Completion matching** | Re-checks the returned URI against the requested one | **Enforces it against live navigations** and stops before load | see [IMPL-INTERFACE.md](IMPL-INTERFACE.md) |
| **TLS client certificates, PIN** | Never sees either | Yes, behind the adapter | backend-side |
| **Backend discovery** | Yes: `.portal` files and `portals.conf` | Declares itself in one `.portal` file | `xdp-portal-config.c`; gtk's `data/gtk.portal` |
| **Deadline** | Backstop, slightly longer | Authoritative | frontend proxy timeout is `G_MAXINT`; backend runs the real one |
| **Remembered decisions (permission store)** | None in version 1 — see below | None | `xdp-permissions.c` / `org.freedesktop.impl.portal.PermissionStore` |

**On the permission store.** Upstream frontends remember per-application decisions for portals
whose consent is repeatable — Location, Camera, Background. Version 1 of this interface stores
**nothing**, and that is a decision rather than an omission: the two things a user could be asked
here are "may this application open a sign-in window" and "may it use your card", the second of
which belongs to the smart card portal's own policy, and remembering the first for an *unverified*
host caller would key a persistent grant on a label that is not a principal. When a permission
store is added it will be for sandboxed callers only, and it will be a separately reviewed
decision. There is deliberately no `permission-store.h` in this sketch.

## The frontend — `service/frontend/`

The directory that **moves into xdg-desktop-portal at acceptance** and is deleted from here. That
is why it holds only files upstream already has an equivalent of. See
[UPSTREAMING.md](UPSTREAMING.md).

### `webauthentication` — [`src/webauthentication.h`](../service/frontend/src/webauthentication.h)

The portal itself: `init`, one method handler, one completion callback. Upstream's `account.c`
under another name, in the same order — find the impl config, refuse to export the interface if
nothing implements it, proxy the backend with a `G_MAXINT` timeout because a human with a smart
card is not a stalled call, export the Request before calling the backend, filter the options,
forward, and turn the reply into a `Response`.

It also does the one thing upstream does that is easy to miss: it **re-processes the results**
rather than trusting them. Account re-registers the returned avatar URI as a document; FileChooser
validates the URIs a backend returns. Here, the frontend re-checks the `completion_uri` against the
one the application asked for, and answers `2` with reason `backend_completion_mismatch` if they
differ.

### `request` — [`src/request.h`](../service/frontend/src/request.h)

The Request object applications hold, its path convention, and the "exactly one Response" guarantee
— including the response the frontend owes when the backend disappears, which is a failure mode a
single process could not have.

### `session` — [`src/session.h`](../service/frontend/src/session.h)

The portal Session pattern, and the recorded decision that version 1 creates **no** Session object:
a transaction is a Request, like `Account.GetUserInformation`. Also the warning that `session_mode`
is a website data store and not a portal Session, despite the word.

### `app-info` — [`src/app-info.h`](../service/frontend/src/app-info.h)

Who is asking, and how much of it can be believed, as a type rather than a comment: sandboxed
(authenticated), cgroup-derived (a label), host (unidentified). The public interface has no `app_id`
argument, precisely so that there is nothing for a caller to claim. This is the largest single thing
the split buys: the derivation happens where the application cannot reach it, and its result reaches
the window as a fact.

### `portal-impl` — [`src/portal-impl.h`](../service/frontend/src/portal-impl.h)

Backend discovery: `.portal` files (`DBusName`, `Interfaces`, the deprecated `UseIn`) and a
`portals.conf`-shaped `[preferred]` list with `default=`, `none` and `*`. It reads **this project's
own directories**, not xdg-desktop-portal's, because an unaccepted prototype must not parse — or be
parsed by — the real portal's configuration.

## The backend — `service/backends/gtk/`

The directory that **stays** and becomes an ordinary desktop backend project. Layout mirrors
xdg-desktop-portal-gtk: `data/` holds the `.portal` file and the D-Bus service file, `src/` holds
one file per portal interface implemented.

### `webauthentication` — [`src/webauthentication.h`](../service/backends/gtk/src/webauthentication.h)

The impl skeleton and its one handler. What it must *not* do is the interesting half: never resolve
its own peer to identify the application, never accept a call from anything but its frontend, never
re-decide policy the frontend decided — and never trust the frontend's validation instead of doing
its own.

### `transaction` — [`src/transaction.h`](../service/backends/gtk/src/transaction.h)

One transaction and the only object allowed to complete it: the URIs, the deadline, the partition,
the window, whatever the certificate adapter holds, and exactly one terminal result. The races are
specified rather than discovered, and two of them are new: the frontend can vanish (cancel at once;
a window belonging to no request is the leaked-window failure the interface promises not to have),
and this process can vanish (the frontend owes the answer).

### `webkit_session` — [`src/webkit_session.h`](../service/backends/gtk/src/webkit_session.h)

The GTK4 + WebKitGTK 6.0 web view. It tests every top-level navigation and finishes the transaction
*before the navigation is loaded*, because the completion URI carries the credential the flow was
for; it answers TLS client-certificate challenges bound to the verified host of the page it is
showing; it uses exactly the partition it was given; it renders the security chrome. Fixed, not
configurable: no TLS-error bypass, no caller-controlled certificate trust, downloads and autofill
disabled, no URI or page-content logging.

**Where `browser_session.h` went.** Version 0 had an in-process vtable with a capability mask,
selecting between a system-browser session, a WebKit session, manual paste and a browser extension.
That seam is now the impl interface, and those alternatives are now **separate backends** chosen by
`portals.conf` — `webauth-portal-browser`, `webauth-portal-paste` — exactly as a desktop chooses
`xdg-desktop-portal-gtk` or `-gnome`. The preference order is unchanged in substance (system
browser wherever the completion can be returned safely, per RFC 8252; WebKitGTK where interception
or a card requires it, which is the AVD/PIV case; paste as the headless fallback; an extension only
as an experimental integration). What is lost is per-request capability negotiation, which the impl
interface does not have because upstream's does not.

### `chrome` — [`src/chrome.h`](../service/backends/gtk/src/chrome.h)

The part of the window nobody outside this process can influence: the app id **the frontend
established**, the engine's own current origin, and the caller's `title` hint rendered beneath and
marked as application-supplied. Accessibility lives here as acceptance criteria — AT-SPI exposure
for every backend-owned control, meaningful focus order, screen-reader announcement of caller and
origin, no meaning carried by colour alone, focus restored to the calling application on close.

### `externalwindow` — [`src/externalwindow.h`](../service/backends/gtk/src/externalwindow.h)

Parsing `x11:<xid>` and `wayland:<handle>` and parenting the window. Backend work, because the
frontend has no display connection. An invalid identifier degrades to an unparented window and
never aborts authentication; parenting is not activation.

### `completion` — [`src/completion.h`](../service/backends/gtk/src/completion.h)

Exact matching on parsed URIs, with no prefix mode. One rule, two enforcement points, and they must
agree — see [IMPL-INTERFACE.md](IMPL-INTERFACE.md).

### `storage` — [`src/storage.h`](../service/backends/gtk/src/storage.h)

Which website data store a transaction runs in, and what a store covers — every piece of engine
state, not just cookies. Two modes: `shared` (all of this backend's transactions, and emphatically
*not* the user's real browser) and `ephemeral` (created with the transaction, destroyed with it).
The mode arrives as a decision; this process may refuse a mode it cannot honour, and may not
silently downgrade one.

### `tls/client_cert` — [`src/tls/`](../service/backends/gtk/src/tls/)

Answering a TLS client-certificate challenge, behind an adapter with two implementations:

- **`portal`** — call the smart card portal's **public** interface as an ordinary client:
  `AcquireCredential` with `purpose: "client_auth"` and `context` set to the destination host, then
  satisfy the operation either by brokered `Sign` behind a GnuTLS external-signer path — unproven —
  or by the experimental `OpenPkcs11Endpoint` compatibility endpoint. **Preferred when available**,
  because the chooser and the PIN then belong to one trusted service shared by every application,
  and the PIN never reaches this process.
- **`inproc`** — enumerate with p11-kit, show this backend's own chooser and PIN prompt, and build
  the certificate with `g_tls_certificate_new_from_pkcs11_uris()` against the **system** p11-kit
  configuration. **Retained as the fallback**, and there is no release in which the AVD case is
  blocked on another project shipping.

**Why the fallback is not scaffolding.** The ends of the chain are documented and fine; the middle
is not: a PKCS#11 URI cannot name a socket, p11-kit remoting needs the client module registered in
*configuration*, GLib's constructor has no module parameter, and WebKit's network process may not
see a module registered after it started. That is spike [S2](SPIKES.md).

**What the split did not fix, and made visible.** Under the `portal` adapter this backend is the
smart card portal's *caller*, so that portal derives **this backend's** identity, not the
application's. Its consent window names `webauth-portal-gtk`. The original app id can only be
passed as untrusted text. Attested delegation across one portal hop is a protocol neither project
has — and is a further argument for one incubating frontend hosting both interfaces, where the
derived app id would already be in hand. See [SECURITY.md](SECURITY.md) and
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md).

**Rules the in-process path must keep**, because it is a trusted dialog when it is the one running:
the chooser names the requesting application, the origin, the certificate identity and the purpose
*before* any PIN; the PIN is never stored, never logged, never in the DOM, and its buffer is cleared
on every exit path; a challenge is answered once plus at most one retry the TLS stack itself
initiated, because automatically re-answering is how a card gets locked; p11-kit's own trust tokens
are skipped; a token holding no certificate is *empty*, not an error; and cancelling anywhere
cancels the transaction rather than continuing without a certificate.

**What the backend owns under either adapter:** recognising the challenge, determining and
displaying the origin that raised it, refusing challenges from hosts unrelated to the page being
shown, binding it to one cancellable transaction, and releasing whatever the adapter held on every
exit path. That last one is small in code and disproportionate in risk.

### `redact` — [`src/redact.h`](../service/backends/gtk/src/redact.h)

Structural, not textual: the logging interface takes typed fields and the kind decides what may be
printed. There is deliberately no "log this URI" entry point, and URI and response sizes are capped
so a hostile page cannot make the log the problem instead. The frontend is under the same obligation
and has no copy of the file; at acceptance both belong in shared code.

## The consumer — `clients/entra/`

Components under [`clients/entra/src/`](../clients/entra/src/). The CLI contract is
[ENTRA-CLIENT-CLI.md](ENTRA-CLIENT-CLI.md). **Nothing about this component changed in the
restructuring except the D-Bus names it calls** — which is the point: an application sees a portal,
not an architecture.

### `cli` — [`src/main.c`](../clients/entra/src/main.c)

Argument parsing for the four verbs, option validation, exit-code mapping, plain-vs-JSON output.
No network, no windows: it builds a request object and prints a response object.

### `request schema` — [`ipc/request.h`](../clients/entra/src/ipc/request.h)

The versioned request/response objects (`"schema": 1`). Built from `argv` today, shaped as messages
from the start so that putting the client behind a socket later is a transport change rather than a
redesign — and so the same shape can become a typed FreeRDP provider request.

### `webauth client` — [`webauth_client.h`](../clients/entra/src/webauth_client.h)

The client's only interactive dependency: one `Start`, one `Response`, on the **frontend**. It
subscribes to `Response` on the handle derived from its own `handle_token` *before* calling `Start`,
so a fast completion cannot race the subscription. It never names a backend, never reads a `.portal`
file, and cannot tell which backend served it. If the portal is unreachable — including when a
frontend is running but no backend implements the interface, in which case the interface is not
exported at all — this reports *unavailable* rather than failing, so a dispatcher can fall through
to another provider.

### `transaction` — [`oauth/transaction.h`](../clients/entra/src/oauth/transaction.h)

The `state`, the PKCE verifier and its S256 challenge, the expected redirect, a deadline, and a
single-use flag. Note what it does not contain: a window, a web view, a reference count shared with
UI callbacks.

### `oauth` — [`callback.h`](../clients/entra/src/oauth/callback.h), [`discovery.h`](../clients/entra/src/oauth/discovery.h), [`clouds.h`](../clients/entra/src/oauth/clouds.h)

Classification of the URI the portal returned, the code/refresh/PoP token requests, endpoint
derivation, and the sovereign-cloud table. The classifier is the security-critical piece on this
side and is modelled on FreeRDP's `freerdp_client_aad_parse_callback` from the `aad/oauth-hardening`
branch: exact redirect match, no userinfo or fragment, `state` present exactly once and compared in
constant time, exactly one of `code` or `error` where a bare parameter still counts as an
occurrence, strict percent-decoding with `%00` rejected.

**There are now three checks, and none is redundant.** The backend answered "is this navigation the
URI the request named" — a question about URIs, asked against a live browser. The frontend answered
"is what the backend handed me the URI the application asked for" — a question about whether a
backend behaved. The client answers "is this a valid authorization response to the request I made" —
a question about OAuth, involving a secret nothing in the portal ever saw. Putting the third one in
the portal is what would make the portal protocol-specific.

Discovery is **optional**: the request always carries a configured authority, and discovered
endpoints only refine it.

### `cache` / `keyring` — [`cache/keyring.h`](../clients/entra/src/cache/keyring.h)

Refresh tokens and account records in the Secret Service keyring; access tokens in memory only,
keyed by `(account, authority, tenant, client id, sorted scopes, token kind, PoP binding)`. An
explicit "no persistent cache" mode stores nothing — the correct behaviour when no keyring is
available, not a silent fallback to a file.

### `log` / `redact` — [`log/redact.h`](../clients/entra/src/log/redact.h)

The same structural principle as the backend's, over the artifacts the portal never sees:
authorization codes, tokens, verifiers, `state`, and the authorization server's `error_description`.

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
   the token and exits `0`. **No portal call and no window.**
4. **Interactive path.** Otherwise, if `--prompt` allows it, the client creates a transaction
   (fresh `state`, fresh PKCE verifier and S256 challenge), builds the authorization URL, subscribes
   to `Response`, and calls
   `Start(parent_window, start_uri, "https://login.microsoftonline.com/common/oauth2/nativeclient", { handle_token, session_mode: "shared", timeout: 300, title })`
   on `io.github.sjtrotter.portal.WebAuthentication1`.
5. **The frontend** checks the peer, derives the app id, validates both URIs, filters the options,
   decides the storage mode, mints and exports the Request, finds the backend named by
   `portals.conf`, and calls
   `Start(handle, app_id, parent_window, start_uri, completion_uri, options)` on
   `io.github.sjtrotter.impl.portal.WebAuthentication1`.
6. **The backend** exports its impl Request at the same handle, parses `parent_window`, opens the
   window with chrome naming the app id it was given and the origin the engine reports; the user
   authenticates; `certauth.login.microsoftonline.us` challenges for a client certificate; **the
   certificate adapter runs** — the smart card portal's chooser and PIN window under the `portal`
   adapter, this backend's own under `inproc`, both naming application, origin, certificate and
   purpose; the handshake completes; the authority redirects to the `nativeclient` URL; the
   navigation policy matches it exactly, commits the completion, destroys the window before it
   renders, releases the grant, unexports the impl Request, and returns
   `(0, { completion_uri })`.
7. **The frontend** re-checks that URI against the one it forwarded, emits
   `Response(0, { completion_uri })` on the public Request, and unexports it.
8. The client classifies `completion_uri` against its transaction, extracts `code`, exchanges it
   with the verifier at the authority's token endpoint, stores the refresh token in the keyring,
   caches the access token in memory, prints it, exits `0`. With `--prompt never` it would have
   exited `10` at step 4 instead, printing nothing on stdout.
9. FreeRDP puts the token in `FreeRDP_GatewayHttpExtAuthBearer` and calls ARM, which returns the
   connection details for the session host.

### 2. Session-host proof-of-possession token

10. FreeRDP generates the RDS PoP key — it, not the client and certainly not the portal, owns that
    key — formats `req_cnf` as base64url of `{"kid": "<key-id>"}`, builds the RDS resource scope from
    the target hostname, and calls `GetCommonAccessToken` with `ACCESS_TOKEN_TYPE_AAD`, the scope,
    and `req_cnf`.
11. The callback runs the client again with `--req-cnf <value>` and the RDS scope.
12. **Silent path — the one that decides whether this is worth building.** The client should be able
    to redeem the refresh token it already holds for a *new* PoP token bound to a *new* key, with no
    portal call and no second card interaction. Whether that works under Government tenant policy
    and Conditional Access is spike S1 in [SPIKES.md](SPIKES.md). If it does not, this becomes a
    second transaction — and this is where the `shared` session store earns its keep: the Entra
    session cookie from step 6 is still there, so the user should see a window but not a second card
    prompt.
13. The client prints the PoP token. FreeRDP requests the RDS nonce and completes the RDS-AAD
    handshake with the key from step 10.

An interactive window appears at most once per connection if S1 passes; at most twice if it does
not, and the second should be a click rather than a card.

## Process model

**Three processes on the session bus, plus a fourth in another repository.**

```
application (entra-token-helper, or anything else)
    │ io.github.sjtrotter.portal.Desktop
    ▼
webauth-portal-frontend      D-Bus activated, no display, no toolkit
    │ io.github.sjtrotter.impl.portal.desktop.gtk
    ▼
webauth-portal-gtk           D-Bus activated, GTK4 + WebKitGTK
    │ io.github.sjtrotter.portal.Smartcard1   (as a client, preferred adapter)
    ▼
smart card portal            separate repository, optional
```

The frontend is deliberately the boring process: it can be restarted, it holds no window, and it
depends on nothing a distribution would hesitate to install. The backend is where the security-
critical infrastructure lives — a web engine, forever — and it is separately replaceable, which is
what makes "a KDE backend" or "a system-browser backend" a packaging decision rather than a fork.

**The Entra client is a one-shot CLI.** Spawned per request, does one thing, writes to stdout,
exits. The smallest thing that integrates and the easiest to reason about as a credential boundary:
no long-lived process holds refresh tokens in memory.

**Concurrency.** Client requests are serialized **per account**, with a lock in `$XDG_RUNTIME_DIR`.
Two connections opened at once must not open two transactions for the same account, and must not
race to redeem the same refresh token: some authorities rotate refresh tokens on redemption, so a
lost race can invalidate the winner's token.

Neither the frontend nor the backend serializes anything. Concurrent transactions from different
callers are normal, each with its own Request, its own impl call and its own window; the frontend
rate-limits per connection, which is a different concern.

**Activation and death.** Both halves are D-Bus activated. A backend that dies mid-transaction is a
new failure mode the single-process design did not have, and the frontend answers it with one
`Response(2, { reason: "backend_disappeared" })`. A frontend that dies takes the transaction with
it: the backend sees its connection drop and destroys the window, because a window belonging to no
request is exactly the leak the interface promises not to have.

## Explicitly not owned

**The portal does not own:** any protocol meaning (OAuth, OIDC, SAML); token exchange; credential
storage beyond web session state; the choice of identity provider, tenant or cloud; policy about who
may sign in to what. It opens a URI and returns a completion. It owns the card only as far as the
chosen adapter forces it to.

**The frontend does not own:** a window, a web engine, a toolkit, a display connection, a
`parent_window` parse, a card, a PIN, or a storage partition's contents.

**The backend does not own:** the application's identity, the decision to accept a request, the
option vocabulary, the storage-mode policy, the guarantee of exactly one response, or any relationship
with the application at all.

**The Entra client does not own:** the RDP session; any window, web view, certificate chooser or PIN
prompt; cloud *selection* (FreeRDP resolves that from the `.rdp`/`.rdpw` file and passes the
authority in — the client's cloud table exists to *allowlist* what it will talk to, not to decide);
the PoP key (FreeRDP generates it, retains it, and completes the handshake with it); and device
enrollment, PRT or Conditional Access device claims, which are `sso-mib`'s and the Microsoft
broker's territory.
