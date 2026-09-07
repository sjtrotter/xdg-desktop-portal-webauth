# Architecture

Status: the backend is implemented and has been run end to end, against a fixture identity provider
and against a real Entra ID tenant on 2026-09-05; what was run is in [TESTING.md](TESTING.md).
Where this document says "would", it still means it.

Web authentication is **a portal frontend and a portal backend**, plumbed exactly as
xdg-desktop-portal plumbs every portal it has — because the frontend *is* xdg-desktop-portal.
Applications call it on `org.freedesktop.portal.Desktop`; it derives who is asking, validates what
it was given, finds a backend through a `.portal` file, and forwards the call over an `impl`
interface applications must never reach; the backend owns the window. That shape is deliberate and
early, and its costs are recorded in
[decisions/0008-build-to-the-upstream-shape.md](decisions/0008-build-to-the-upstream-shape.md).
**This repository is the backend and nothing else** — the frontend is a branch of
xdg-desktop-portal, `experimental/certificate-webauthentication`, commit `a6b06d4`; see
[decisions/0010-backend-only-frontend-lives-upstream.md](decisions/0010-backend-only-frontend-lives-upstream.md).

```
  FreeRDP ──GetCommonAccessToken──▶ entra-token-helper           SEPARATE REPOSITORY
                                      │  OAuth, PKCE, cache, sovereign clouds
                                      │
                                      │  D-Bus: org.freedesktop.portal.Desktop
                                      ▼
                    ┌─────────────────────────────────────────┐
                    │  xdg-desktop-portal                     │  ANOTHER REPOSITORY,
                    │  branch experimental/                   │  on a branch
                    │    certificate-webauthentication        │
                    │  org.freedesktop.portal.experimental.   │  no window, no engine,
                    │      WebAuthentication   [GATED]        │  no toolkit, no card
                    │  app id · validation · options filter   │
                    │  Request objects · one Response         │
                    └─────────────────────────────────────────┘
                                      │
                                      │  D-Bus: org.freedesktop.impl.portal.
                                      │         experimental.WebAuthentication
                                      │         (backend interface; NOT for applications)
                                      ▼
                    ┌─────────────────────────────────────────┐
                    │  xdg-desktop-portal-webauth             │  THIS REPOSITORY
                    │  GTK4 + WebKitGTK 6.0 web view          │
                    │  security chrome · storage partition    │
                    │  navigation interception                │
                    │  certificate adapter ───┐               │
                    └─────────────────────────┼───────────────┘
                                              │
       portal (the only client-cert   ┴──▶ the Certificate portal's OWN PKCS#11
        path; preferred)                    module, named by URI: portal-token.h
                                              ▲ chooser, consent and PIN stay there
       pkcs11 (--client-cert-uri) ───────────▶ any p11-kit token. No chooser, no
                                              PIN prompt, no card handling here
```

**`[GATED]`** is load-bearing: the public interface is not exported unless the portal was started
with `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`. With the gate off, an application
sees "no such interface" and this backend is never activated.

The Certificate portal knows nothing about the web. The web authentication portal knows nothing
about OAuth, and knows as little about cards as the chosen adapter allows. The Entra client owns no
windows. None of them knows anything about RDP.

**The Certificate portal is an external component and is NOT a hard dependency.** Its backend is a
separate project in its own repository (`xdg-desktop-portal-certificate`), sketched in parallel;
its frontend is the *same* xdg-desktop-portal branch as ours. It is the *preferred* way to satisfy
a certificate challenge, and the mechanism connecting it to a WebKit handshake is what spike
[S2](SPIKES.md) settled: WebKit carries a certificate to its network process as a **PKCS#11 URI**
and resolves it there, so the seam between the two projects is a p11-kit module the Certificate
portal publishes, not a brokered `Sign`. That module exists and is tested (2026-09-06, Firefox and
this backend), so the `portal` provider is the working, preferred path; the `pkcs11` provider — a
token named on this backend's command line — remains available for a machine with no certificate
portal installed. See
[decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md).

Note what the arrow into the backend **is**: a D-Bus interface, not the in-process vtable version 0
had. And note what the arrow out of the certificate adapter is: a call to another portal's
**public** interface, made as an ordinary client. A backend never calls another backend.

## Responsibility split: frontend and backend

This is the table the rest of the document explains. Every row is upstream's own split, checked
against `xdg-desktop-portal/desktop-portal/account.c` and
`xdg-desktop-portal-gtk/src/account.c` — the smallest complete example of a UI-dialog portal pair.

| Concern | Frontend (xdg-desktop-portal, branch) | Backend (this repository) | Upstream this mirrors |
|---|---|---|---|
| **Owns the bus name applications call** | Yes: `org.freedesktop.portal.Desktop` | No; owns `org.freedesktop.impl.portal.desktop.webauth`, which applications must not call | it *is* `org.freedesktop.portal.Desktop` vs `org.freedesktop.impl.portal.desktop.<backend>` |
| **App id derivation** | **Only here.** `xdp_invocation_get_app_info()`: Flatpak/Snap mediation (`sandboxed`), a cgroup label (`host`), or nothing (`unidentified`) | Never. Its D-Bus peer is the portal, not the application; it is *told* `app_id` and `app_identity_level` | `shared/xdp-app-info*.c`; `app_id` is an impl argument |
| **Same-UID peer check** | Yes, before the request is parsed | Refuses any sender that is not its frontend | frontend-side in every portal |
| **Argument validation** | Yes: `start_uri` must be absolute `https` with a host; `completion_uri` absolute, with a host, no userinfo; both rejected for control characters, backslashes and over-length. A malformed request is a D-Bus error, no backend is woken | Again, independently. Its safety must not depend on a frontend having been correct | `validate_reason()` + `xdp_filter_options()` in `account.c` |
| **Option filtering** | Yes: known keys only, unknown keys dropped, unknown values rejected (`session_mode` must be exactly `shared` or `ephemeral`), `timeout` clamped to 900 s and always forwarded (default 300), `title` ≤ 256 chars and single-line, `handle_token` not forwarded, `app_identity_level` added | Receives an already-filtered vardict | `XdpOptionKey` tables |
| **Session-mode / storage policy** | Decides it, and forwards it as a decision | Obeys it. Never derives a partition from an app id | `xdp-permissions.c`, portal-side policy |
| **Rate limiting** | Yes, per connection | No | frontend-side |
| **Request object the app holds** | Yes: `/org/freedesktop/portal/desktop/request/<sender>/<token>`, exported before the backend is called | Exports its own impl Request at the same path on its own bus name, for `Close()` only | `xdp-request-dex.c` vs gtk's `src/request.c` |
| **Exactly one `Response`** | Yes; also owes one when the backend dies | Answers once, by returning from the method | frontend emits, backend returns |
| **`parent_window` parsing** | No; forwards the string opaquely | Yes: `x11:`/`wayland:` and `xdg_foreign`, because only it has a display | gtk's `src/externalwindow.c` |
| **The window, the web engine, the chrome** | Never. No toolkit dependency, ever | Yes, all of it | gtk's dialogs |
| **Completion matching** | Re-checks the returned URI against the requested one | **Enforces it against live navigations** and stops before load | see [IMPL-INTERFACE.md](IMPL-INTERFACE.md) |
| **TLS client certificates, PIN** | Never sees either | Yes, behind the adapter | backend-side |
| **Backend discovery** | Yes: `.portal` files and `portals.conf` | Declares itself in one `.portal` file | `xdp-portal-config.c`; gtk's `data/gtk.portal` |
| **Deadline** | Yes: races `dex_timeout_new_seconds(timeout)` against the impl call and closes the impl request on expiry | Also started when the window opens | see [IMPL-INTERFACE.md](IMPL-INTERFACE.md), noted in [SECURITY.md](SECURITY.md) |
| **Remembered decisions (permission store)** | None in version 1 — see below | None | `xdp-permissions.c` / `org.freedesktop.impl.portal.PermissionStore` |

**On the permission store.** Upstream frontends remember per-application decisions for portals
whose consent is repeatable — Location, Camera, Background. Version 1 of this interface stores
**nothing**, and that is a decision rather than an omission: the two things a user could be asked
here are "may this application open a sign-in window" and "may it use your card", the second of
which belongs to the Certificate portal's own policy, and remembering the first for an *unverified*
host caller would key a persistent grant on a label that is not a principal. When a permission
store is added it will be for sandboxed callers only, and it will be a separately reviewed
decision. There is deliberately no `permission-store.h` in this sketch.

## The frontend — not in this repository

`desktop-portal/web-authentication.c` on the branch. It is upstream's `account.c` in shape: find
the impl config, refuse to export the interface if nothing implements it, proxy the backend with a
`G_MAXINT` timeout because a human with a smart card is not a stalled call, export the Request
before calling the backend (note `xdp_request_dex_export()` is a separate call from
`xdp_request_dex_new()`, and forgetting it silently swallows the Response), filter the options,
forward, and turn the reply into a `Response`.

It also does the one thing upstream does that is easy to miss: it **re-processes the results**
rather than trusting them. Account re-registers the returned avatar URI as a document; FileChooser
validates the URIs a backend returns. Here, `completion_uri_matches()` re-checks the
`completion_uri` against the one the application asked for — scheme and host case-insensitively,
effective ports with defaults normalised, paths exactly, no userinfo, query and fragment ignored —
and answers `2` with reason `backend_completion_mismatch` if they differ.

Version 1 creates **no** `Session` object: a transaction is a Request, like
`Account.GetUserInformation`. `session_mode` is a website data store and not a portal `Session`,
despite the word.

The files this repository used to hold for all of that — `request.h`, `session.h`, `app-info.h`,
`portal-impl.h`, `webauthentication.h`, `main.c` — are **deleted**, not moved: upstream has an
equivalent of every one. See [UPSTREAMING.md](UPSTREAMING.md).

## The backend — this repository

An out-of-tree backend project, laid out like every other one: `data/` holds the `.portal` file,
the D-Bus service file and the interface XML, `src/` holds one file per portal interface
implemented.

### `webauthentication-impl` — [`src/webauthentication-impl.h`](../src/webauthentication-impl.h)

The impl skeleton and its one handler. What it must *not* do is the interesting half: never resolve
its own peer to identify the application, never accept a call from anything but its frontend, never
re-decide policy the frontend decided — and never trust the frontend's validation instead of doing
its own.

### `transaction` — [`src/transaction.h`](../src/transaction.h)

One transaction and the only object allowed to complete it: the URIs, the deadline, the partition,
the window, whatever the certificate adapter holds, and exactly one terminal result. The races are
specified rather than discovered, and two of them are new: the frontend can vanish (cancel at once;
a window belonging to no request is the leaked-window failure the interface promises not to have),
and this process can vanish (the frontend owes the answer).

### `webkit-session` — [`src/webkit-session.h`](../src/webkit-session.h)

The GTK4 + WebKitGTK 6.0 web view. It tests every navigation and finishes the transaction
*before the navigation is loaded*, because the completion URI carries the credential the flow was
for; it answers TLS client-certificate challenges bound to the verified host of the page it is
showing; it uses exactly the partition it was given; it renders the security chrome. Fixed, not
configurable: no TLS-error bypass, no caller-controlled certificate trust, downloads and autofill
disabled, no URI or page-content logging.

**Where `browser_session.h` went.** Version 0 had an in-process vtable with a capability mask,
selecting between a system-browser session, a WebKit session, manual paste and a browser extension.
That seam is now the impl interface, and those alternatives are now **separate backends** chosen by
`portals.conf` — each with its own `.portal` file naming
`org.freedesktop.impl.portal.experimental.WebAuthentication` — exactly as a desktop chooses
`xdg-desktop-portal-gtk` or `-gnome`. The preference order is unchanged in substance (system
browser wherever the completion can be returned safely, per RFC 8252; WebKitGTK where interception
or a card requires it, which is the AVD/PIV case; paste as the headless fallback; an extension only
as an experimental integration). What is lost is per-request capability negotiation, which the impl
interface does not have because upstream's does not.

### `chrome` — [`src/chrome.h`](../src/chrome.h)

The part of the window nobody outside this process can influence: the app id **the frontend
established**, the engine's own current origin, and the caller's `title` hint rendered beneath and
marked as application-supplied. Accessibility lives here as acceptance criteria, listed in
[PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md).

### `external-window` — [`src/external-window.h`](../src/external-window.h)

Parsing `x11:<xid>` and `wayland:<handle>` and parenting the window. Backend work, because the
frontend has no display connection. An invalid identifier degrades to an unparented window and
never aborts authentication; parenting is not activation.

### `completion` — [`src/completion.h`](../src/completion.h)

Exact matching on parsed URIs, with no prefix mode. One rule, two enforcement points, and they must
agree — see [IMPL-INTERFACE.md](IMPL-INTERFACE.md).

### `storage` — [`src/storage.h`](../src/storage.h)

Which website data store a transaction runs in, and what a store covers — every piece of engine
state, not just cookies. Two modes: `shared` (all of this backend's transactions, and emphatically
*not* the user's real browser) and `ephemeral` (created with the transaction, destroyed with it).
The mode arrives as a decision; this process may refuse a mode it cannot honour, and may not
silently downgrade one.

### `tls/client_cert` — [`src/tls/`](../src/tls/)

Answering a TLS client-certificate challenge, behind an adapter with two providers. Both end in the
same call — `g_tls_certificate_new_from_pkcs11_uris()` — because [S2](SPIKES.md) established that
this is the seam WebKit actually has:

- **`portal`** — a URI naming the token that the Certificate portal's own client-side PKCS#11 module
  presents ([`src/tls/portal-token.h`](../src/tls/portal-token.h)). The card, the chooser,
  the consent and the PIN stay in that service; the token declares
  `CKF_PROTECTED_AUTHENTICATION_PATH`, so this backend answers no PIN challenge at all.
  **Preferred**, and the only client-certificate path; tested 2026-09-04/05/06.
- **`pkcs11`** — any p11-kit token, named by `--client-cert-uri` on this backend's command line,
  with a PIN read from the file `--client-cert-pin-file` names. No chooser, no prompt, no
  enumeration.

**There is no `inproc` provider and no in-process card handling**, which is a change from the
sketch: a chooser and a PIN prompt inside a browser process is the design the Certificate portal
exists to replace. What S2 left open — dynamic registration, concurrency, card removal, the version
matrix — is in [SPIKES.md](SPIKES.md).

**What the split did not fix, and made visible — and what has since changed.** Over D-Bus, under
the `portal` adapter, this backend is the Certificate portal's *caller*, so that portal derives
**this backend's** identity, not the application's, and its consent window names this backend. That
is still true of anything crossing the bus.

What changed is that **both portals now live in one frontend process**. That process derived the
app id of the application that called `WebAuthentication.Start` and still holds it when it calls
its own certificate side, so it can pass the original app id along **in-process**, with no
attestation crossing a bus — which is exactly the "shared frontend" fix both projects described as
arriving at acceptance. The frontend does not do this yet; it is unwritten work on that branch.

**The caveat is permanent, and it is about trust rather than about processes:** never believe a
caller about a third party's identity. What that forbids, and what would satisfy it, is in
[SECURITY.md](SECURITY.md) and
[decisions/0010](decisions/0010-backend-only-frontend-lives-upstream.md).

**Rules this path must keep even once in-process app id derivation is built** — today each portal
derives and grants consent separately, which is why the same WebKitGTK handshake raises two
choosers and one PIN, not one:
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

### `redact` — [`src/redact.h`](../src/redact.h)

Structural, not textual: the logging interface takes typed fields and the kind decides what may be
printed. There is deliberately no "log this URI" entry point, and URI and response sizes are capped
so a hostile page cannot make the log the problem instead. The frontend is under the same obligation
and has no copy of the file; at acceptance both belong in shared code.

## The consumer — a separate repository

The first consumer is the Entra ID / Azure Virtual Desktop token client `entra-token-helper`, in
[github.com/sjtrotter/entra-token-helper](https://github.com/sjtrotter/entra-token-helper). It owns
the OAuth half — PKCE, code exchange, refresh, the proof-of-possession grant, the sovereign-cloud
table and a Secret Service account store — and its CLI contract is `docs/CLI.md` there. It moved out
of this repository on 2026-09-07
([decisions/0006-two-repositories.md](decisions/0006-two-repositories.md)); nothing here depends on
it, and nothing there is built from here.

What matters on this side is the shape of the call it makes, which is the shape any consumer makes:
one `Start` and one `Response` on `org.freedesktop.portal.Desktop`, with the `Response` subscription
taken on the handle derived from its own `handle_token` *before* `Start` is called, so a fast
completion cannot race it. It never names a backend, never reads a `.portal` file, and cannot tell
which backend served it. When the interface is not exported it reports *unavailable* rather than
failing, so a dispatcher can fall through to another provider. **That is the common case**, not the
exotic one: the interface is experimental and absent unless the portal was started with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`. Three causes — the gate off, no backend
configured, no portal at all — are indistinguishable by design, because the frontend exports nothing
in any of them.

**There are three completion checks in the chain, and none is redundant.** The backend answers "is
this navigation the URI the request named" — a question about URIs, asked against a live browser.
The frontend answers "is what the backend handed me the URI the application asked for" — a question
about whether a backend behaved, implemented and tested upstream (`completion_uri_matches()`,
`test_completion_mismatch_rejected`). The consumer answers "is this a valid authorization response
to the request I made" — a question about OAuth, involving a secret nothing in the portal ever saw.
Putting the third one in the portal is what would make the portal protocol-specific.

## One AVD connection, end to end

FreeRDP needs two tokens for one connection, in this order. The steps below that belong to the
client belong to its repository; they are kept here because they are the only end-to-end use of
this portal that has run against a real tenant.

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
   on `org.freedesktop.portal.experimental.WebAuthentication`. If that interface is absent — the
   experimental gate is off, or no backend is configured — the client exits `40` here instead,
   naming the environment variable.
5. **The frontend** (xdg-desktop-portal) derives the app id, validates both URIs, filters the
   options, decides the storage mode, mints and exports the Request, finds the backend named by
   `portals.conf`, and calls
   `Start(handle, app_id, parent_window, start_uri, completion_uri, options)` on
   `org.freedesktop.impl.portal.experimental.WebAuthentication`.
6. **The backend** exports its impl Request at the same handle, parses `parent_window`, opens the
   window with chrome naming the app id it was given and the origin the engine reports; the user
   authenticates; `certauth.login.microsoftonline.us` challenges for a client certificate; **the
   certificate adapter runs** — under the `portal` provider the Certificate portal's own chooser and
   PIN window, naming application, origin, certificate and purpose; under `pkcs11` the token the
   operator named; the handshake completes; the authority redirects to the `nativeclient` URL; the
   navigation policy matches it exactly, commits the completion, destroys the window before it
   renders, releases the grant, unexports the impl Request, and returns
   `(0, { completion_uri })`. A non-zero response carries a `reason` from the XML's list —
   `timeout`, `no_display`, `no_engine`, `user_cancelled`, `session_terminated`,
   `credential_unavailable` — or one of this backend's own additions.
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
12. **Silent path — settled by the 2026-09-05 chain.** Under this Government tenant's Conditional
    Access policy, redeeming the refresh token for a *new* PoP token bound to a *new* key was not
    silent: it needed one interactive re-auth through the portal window. The `shared` session store
    earned its keep as designed — the Entra session cookie from step 6 was still there, so the
    second window was a click rather than a card. Spike S1 in [SPIKES.md](SPIKES.md) is answered.
13. The client prints the PoP token. FreeRDP requests the RDS nonce and completes the RDS-AAD
    handshake with the key from step 10.

On the 2026-09-05 run an interactive window appeared twice: once for the initial sign-in, and once,
a click rather than a card, for the RDS proof-of-possession token.

## Process model

**Three processes on the session bus, one of which the desktop was running anyway.**

```
application (entra-token-helper, or anything else)
    │ org.freedesktop.portal.experimental.WebAuthentication
    │ on org.freedesktop.portal.Desktop                  [GATED]
    ▼
xdg-desktop-portal           already running; the branch adds two portals to it
    │ org.freedesktop.impl.portal.experimental.WebAuthentication
    ▼
xdg-desktop-portal-webauth   D-Bus activated, GTK4 + WebKitGTK      THIS REPOSITORY
    │ org.freedesktop.portal.experimental.Certificate   (as a client, preferred adapter)
    ▼                         ...which is the SAME xdg-desktop-portal, which routes to
xdg-desktop-portal-certificate   separate repository (xdg-desktop-portal-certificate), optional
```

Note the shape of the certificate call: it goes back out to the portal and in again to a different
backend. That is the correct direction and the only allowed one — a backend never calls another
backend — and it is why both portals sharing one frontend process is what closes the delegation gap.

The frontend is deliberately the boring process, and it is now in somebody else's tree — which is
where it can be reviewed, and is not the same as being somebody else's to maintain: until the branch
is accepted it is this author's. It can be restarted, it holds no window, and every desktop already
has one. The backend is where the
security-critical infrastructure lives — a web engine, forever — and it is separately replaceable,
which is what makes "a KDE backend" or "a system-browser backend" a packaging decision rather than a
fork.

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
request is exactly the leak the interface promises not to have. That second case is now a
*xdg-desktop-portal* restart, which is a more ordinary event than a bespoke service dying, and is a
reason to get it right rather than to assume it away.

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
