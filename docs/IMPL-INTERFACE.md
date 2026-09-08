# The backend (impl) interface

Status: **experimental, version 1, implemented.** The backend in this repository answers `Start`,
opens the window, intercepts the completion navigation and answers a client-certificate challenge;
what it has been run against is [TESTING.md](TESTING.md). This document explains what the interface
means, and — more importantly — **which side of the boundary each rule is enforced on and why**.

The public half is [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md), which is itself a pointer to the
branch. Applications should read that one and stop there.

> **This interface is not for applications.** It is the contract between xdg-desktop-portal and a
> desktop's backend. An application calling it directly bypasses every check the frontend exists to
> perform, and gets in exchange an interface with no stability promise at all. How that is
> prevented, and how far the prevention goes, is in [SECURITY.md](SECURITY.md).

```
bus name       org.freedesktop.impl.portal.desktop.webauth   (this backend)
object path    /org/freedesktop/portal/desktop               (same path as the frontend's)
interface      org.freedesktop.impl.portal.WebAuthentication.X1
request objects the frontend's handle path, exported on the BACKEND's bus name
declared in    $datadir/xdg-desktop-portal/portals/webauth.portal
```

## The XML this repository ships is a copy, and it must track its source

[`../data/org.freedesktop.impl.portal.WebAuthentication.X1.xml`](../data/org.freedesktop.impl.portal.WebAuthentication.X1.xml)
is a **verbatim copy**, apart from a header comment saying so, of

```
xdg-desktop-portal, branch experimental/integration, commit 357e4d7
data/org.freedesktop.impl.portal.WebAuthentication.X1.xml
```

The interface belongs to the frontend. This repository does not get to change it, and a divergence
between the two files is not a difference of opinion — it is a backend that no longer implements
the interface it claims in `data/webauth.portal`. To update: copy the branch's file again
and change the commit id in the header.

Upstream keeps `org.freedesktop.impl.portal.*.xml` in xdg-desktop-portal itself and backends
consume it from that project's pkg-config interfaces directory. This copy exists only because the
branch is unmerged and no released xdg-desktop-portal ships the file. When the branch lands, the
copy is deleted and the file comes from the interfaces directory like every other backend's.

**The `.X1` suffix is not a claim of acceptance.** It is the naming upstream set aside for portals
that are not finished — see [UPSTREAMING.md](UPSTREAMING.md) — and the public side of such an
interface is exported on `/org/freedesktop/portal/desktop/experimental`, and only when a backend
for it is configured.

## The signature, and how it is derived from the public one

Upstream's convention, followed exactly. The public method

```
Start(s parent_window, s start_uri, s completion_uri, a{sv} options) → o handle
```

becomes the backend method

```
Start(o handle, s app_id, s parent_window, s start_uri, s completion_uri, a{sv} options)
    → (u response, a{sv} results)
```

That signature survived the move upstream **unchanged**, which is the strongest single piece of
evidence that [decisions/0008](decisions/0008-build-to-the-upstream-shape.md)'s shape argument was
right. Note the argument order: `app_id` comes before `parent_window`.

with two things prepended and one thing changed:

- **`handle`** — the object path of the public Request. The backend exports its own
  `org.freedesktop.impl.portal.Request` there, on its own bus name, so the frontend can forward
  a `Close()`.
- **`app_id`** — the application identity **the frontend derived**. Empty means the frontend could
  not establish one.
- **the reply** — the result comes back as the method's own return value, not as a signal. The
  frontend sets the proxy timeout to `G_MAXINT`, because a human hunting for a card reader is not a
  stalled call.

Compare `org.freedesktop.impl.portal.Account`:

```
GetUserInformation(o handle, s app_id, s window, a{sv} options) → (u response, a{sv} results)
```

The consequence worth stating: **a backend cannot answer twice.** It returns once, or its connection
drops. "Exactly one `Response`" is therefore a frontend guarantee, and a backend that has already
returned has nothing left to get wrong.

## Options the frontend forwards

| Key | Type | Status when it arrives |
|---|---|---|
| `activation_token` | `s` | Passed through unchanged; the backend decides whether to use it. |
| `session_mode` | `s` | A **decision**, already validated. Not a request. A backend that cannot honour `ephemeral` must **fail** rather than quietly use the shared store. |
| `timeout` | `u` | Already clamped to the 900 s ceiling, and always forwarded (default 300). **The frontend runs this deadline too**: when it passes it calls `Close()` on the `handle` and answers the application itself, so a backend that never answers is no longer a request that never ends. This backend still owns the window and still ends the flow on its own deadline, which starts when the window opens rather than when `Start` arrives, so it is normally the one that is reached first. |
| `title` | `s` | Untrusted application text, already length-limited. |
| `app_identity_level` | `s` | `sandboxed`, `host` or `unidentified`. The honesty level of `app_id`, so the chrome can say "an unidentified application" rather than showing an empty name. |

`handle_token` is **not** forwarded: it has already done its job, which was to let the application
predict the Request path. Unknown keys are **not** forwarded either — the frontend drops them, so a
backend can never be steered by a key its frontend does not know about.

## Who enforces what

This is the table that matters, and it is the thing an upstream discussion would argue about first.

| Rule | Frontend | Backend | Why there |
|---|---|---|---|
| App id derivation | **Enforces** | Never | The backend's D-Bus peer is the frontend. Only the frontend can see the application's connection. |
| Same-UID check | **Enforces** | Refuses non-frontend senders | The frontend is the one with an application peer to check. |
| `start_uri` / `completion_uri` well-formedness | **Enforces**, before forwarding | **Enforces again** | The frontend so that a malformed request costs nothing and wakes nothing; the backend so that its safety never depends on a frontend having been correct. |
| Option vocabulary and values | **Enforces** | Assumes filtered, validates cheaply | Upstream's `xdp_filter_options()` pattern. |
| Storage-mode policy | **Decides** | **Obeys**, or fails | The judgement needs the app id's honesty level, which is frontend knowledge. A backend that derived its own partition would be a second place the isolation rule lived. |
| Rate limiting | **Enforces** | No | Per-connection, and the connection is the frontend's. |
| **Completion matching against live navigations** | No | **Enforces** | Only the backend has navigations. This is the interception, and it must stop the load. |
| **Completion re-check of the returned URI** | **Enforces** | No | The application trusts the frontend's bus name, not whichever backend a distribution installed. |
| Intercept *before load* | No | **Enforces** | The completion URI carries the credential; fetching it would send that credential to a server with no part in the exchange. |
| Completion match in **any** frame | No | **Enforces** | The XML asks for exactly this, and it is what an engine can deliver: a navigation policy decision carries no frame identity. |
| TLS client certificates, PIN, card | Never sees them | **Enforces** every rule in [SECURITY.md](SECURITY.md) | The handshake is the backend's. |
| `parent_window` parsing | No, forwards opaquely | **Enforces** | Only the backend has a display connection and a window. |
| Exactly one `Response` | **Enforces** | Returns once | See above. |
| Answer when the backend dies | **Enforces** | Cannot | Nobody else is left to answer. |
| Cancel when the frontend dies | Cannot | **Enforces** | A window belonging to no request is a leak. |

## Completion matching: one rule, two enforcement points

The rule is defined once, in [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md) under "Completion matching".
It is enforced twice, and the two checks answer different questions:

1. **The backend**, against every navigation the view is asked to make in any frame, deciding
   *when to stop the browser*. This is the security-critical interception: match, commit, destroy
   the window, and never load the page.
2. **The frontend**, against the single URI the backend returned, deciding *what the application is
   told*. If the returned URI is not the one the application asked for — different host, different
   path, anything — the response becomes `2` with reason `backend_completion_mismatch` and the URI is
   discarded.

**Why both, when the backend is presumably not hostile?** Because the frontend's `Response` is a
statement the *frontend* makes, on the bus name the application trusts, and a backend is a separate
package a distribution chose. The failure this prevents is not a conspiracy; it is a bug in a
backend delivering an attacker-chosen URI — carrying, in the AVD case, an authorization code — to an
application that asked for a different one. The frontend is where "you get what you asked for" can
be promised, so that is where it is promised.

**The cost, stated plainly:** one rule has two implementations, and they can drift. Both are now
written and both are tested: `web-authentication.c:completion_uri_matches()` by
`test_completion_mismatch_rejected` and its negative control, and this repository's
[`../src/completion.c`](../src/completion.c) by
[`../tests/test-completion.c`](../tests/test-completion.c), whose table carries the
frontend's own cases marked `FRONTEND`. The mitigation for drift is that table, not good intentions.

One shared behaviour worth writing down because neither implementation asked for it: **GLib
normalises an unreserved percent escape even under `G_URI_FLAGS_ENCODED`**, so
`https://example.com/call%62ack` and `https://example.com/callback` are the same path to both
sides. They agree because they are the same library, not because either decodes on purpose.

**And a third check that is not this one at all:** the application's own validation of the URI as a
*protocol* response — OAuth `state`, exactly one `code` — using a secret nothing in the portal ever
sees. None of the three substitutes for another.

## Where this backend deviates from the XML, and what it adds

The XML is the contract and this backend does not get to change it. Three places where the
implementation is nevertheless not a literal reading of it, each recorded here rather than in a
comment nobody reads:

**1. The reason vocabulary is extended.** The XML names `timeout`, `no_display`, `no_engine`,
`user_cancelled`, `session_terminated` and `credential_unavailable`, introduced with "for
instance" — an open list. This backend emits those and six more, defined in
[`../src/transaction.h`](../src/transaction.h). `unrelated_certificate_challenge`
is one of them: it names a certificate concept, which is this backend's business and not a generic
web sign-in portal's, so it is an addition here rather than a word in the interface.

| Symbol | When |
|---|---|
| `request_closed` | `Close()` arrived from the frontend. Response `1`. |
| `unrelated_certificate_challenge` | A client-certificate challenge arrived from a host unrelated to this flow. Response `2`. |
| `tls_error` | The server's certificate did not verify. There is no bypass. Response `2`. |
| `load_failed` | The engine could not load the page and no better reason applies. Response `2`. |
| `invalid_request` | The backend's own re-validation of the arguments failed. Response `2`. |
| `no_storage` | The website data store the mode requires could not be created. Response `2`. |

**`credential_unavailable` also means "the user refused", and cannot say so.** When the `portal`
provider's chooser is cancelled, what reaches this backend is GnuTLS reporting that the PKCS#11
object was not available — the same thing it reports when the module is not installed, when the
portal is not running, and when the certificate portal declined for a policy reason. This backend
cannot tell those apart and does not guess: it emits `credential_unavailable` and response `2` for
all of them. A caller that needs to know why must ask the certificate portal, which does know.
(Measured in [TESTING.md](TESTING.md) tier 2b: Escape at the chooser, response `2`, reason
`credential_unavailable`, 1.9 seconds.)

A frontend must tolerate a reason it does not know, which the branch's frontend does: it forwards
the string unchanged. If any of these earn their place, they belong in the XML.

**2. A malformed call is answered, not refused.** The XML does not say what a backend does when the
frontend forwards something the backend's own validation rejects — which should never happen, since
the frontend validates first. This backend answers `(2, {"reason": "invalid_request"})` rather than
returning a D-Bus error, because a D-Bus error out of `Start` reaches the application as
`backend_disappeared`, which is a false statement about what happened.

**3. Frames cannot be told apart, and the interface no longer pretends otherwise.** WebKitGTK
6.0's `WebKitNavigationPolicyDecision` exposes **no frame identity**: there is no `frame_info` and
no `is_main_frame`, `webkit_navigation_action_get_frame_name()` names a link's *target* frame and
is NULL for an ordinary navigation, and `WebKitFrame` lives in the web-process extension API and
not here. So this backend cannot distinguish a top-level navigation from a subframe one at the
moment it must decide, and no backend on this engine can.

The public XML used to promise "Subframe navigations do not end the flow", which was a promise
about the frontend that only a backend could keep and no backend could. **It now promises what is
enforced**: a navigation matching the completion URI, *in any frame*, ends the flow and is never
loaded. This backend implements exactly that, in `on_decide_policy()` for the navigation decision
and again for the response decision.

Blocking the load is the half that matters, and it holds in every frame: nothing fetches the
completion URI, so nothing carries the authorization code to a server with no part in the exchange.
The residual is the other half — a page that can create a frame pointing at the completion URI can
end the flow with a URI it chose, which is authorization-code injection. The compensating controls
are unchanged and are named where they live: the frontend's re-check, which forces the returned URI
to be the one the application asked for, and the application's own `state`, which is the check that
actually detects an injected code and uses a secret no part of this design ever sees.

**4. A transaction ends; the certificate authority it caused does not.** The interface has no way
to say "and revoke what this transaction acquired", and this backend could not act on one if it had.
The grants belong to two PKCS#11 module instances — this process's and WebKit's network process's,
each separately consented to (delegation is out of this proposal) — and the adapter has no session
handle, no route to the other process, and no per-module `C_Finalize` that would not finalize every
module GnuTLS loaded through p11-kit's proxy.
`portal_release()` therefore logs `certificate-released grant=retained_until_expiry` and returns.
What ends a grant is its own expiry, the portal invalidating it, or the holding process exiting.

Two consequences follow that an interface change alone would not fix. First, **revoking a grant
would not unauthenticate an established connection**: TLS authenticates a connection once, and a
connection that has presented the card's certificate stays authenticated while it is open. Second,
**an ephemeral session is not a fresh connection pool**: `ephemeral` already makes a new
`WebKitNetworkSession` per `Start`, and the measured second `Start` still reached the provider on
the first one's authenticated connection. Per-transaction isolation is a transport property, and
this engine exposes no lever for it beyond
`webkit_network_session_set_persistent_credential_storage_enabled()`, which this backend sets to
`FALSE`. [SECURITY.md](SECURITY.md), "What closing a transaction does NOT do", has the whole of it.

**Two deadlines, and they are not the same deadline.** The frontend races the impl call against
`dex_timeout_new_seconds(timeout)` (`web-authentication.c`) and, when the timeout wins, calls
`Close()` on the impl `Request` and answers `2` with `reason` `timeout`. The deadline in
[`../src/transaction.c`](../src/transaction.c) starts when the window opens rather
than when `Start` arrives, so it is normally reached first and the frontend's is the backstop for a
backend that never answers at all. Either way the application gets one answer and the window goes
away.

## Failure modes the split introduced

Each is a real obligation, and each is the price of
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md).

| Situation | Who answers | What the application sees |
|---|---|---|
| No backend implements the interface | Frontend, at startup | The interface is **not exported**. This is the default state of every machine: the application sees "no such interface" and reports *unavailable*, exactly as if no portal were installed. |
| The backend cannot start (no display, no engine) | Frontend | `2`, reason `no_backend` |
| The backend dies mid-transaction | Frontend | `2`, reason `backend_disappeared` |
| The backend returns a URI that was not requested | Frontend | `2`, reason `backend_completion_mismatch` |
| The backend returns a malformed vardict | Frontend | `2`, reason `backend_protocol_error` |
| The frontend dies mid-transaction | Nobody — the application's connection sees the name vanish | The backend destroys its window at once |
| The backend is slow (a user with a card) | Nobody; this is normal | Nothing, until `timeout`. The frontend's proxy timeout is `G_MAXINT`, so D-Bus never gives up; the deadline is the transaction's, and the frontend's own timer behind it |
| The backend never answers at all | Frontend, at `timeout` | `2`, reason `timeout`, and the impl `Request` is closed so the window goes away |

## Versioning

The two interfaces version **independently**, and that is a new obligation the single-process design
did not have. Within impl version 1: the `Start` argument list does not change, response codes keep
their meanings, and new option and result keys may be added — a backend must ignore option keys it
does not recognise, and a frontend must ignore result keys it does not recognise.

A frontend paired with an older backend is a supported configuration only to the extent that the
older backend implements all of version 1 — and note that "version 1" of an **experimental**
interface carries no promise at all: the branch's own XML says it can change or be removed without
a version bump, so in practice a backend tracks a commit, not a version. Version 1 has **no capability negotiation**, deliberately,
because upstream's impl interfaces have none: a backend implements the whole interface or does not
claim it in its `.portal` file. A mechanism that cannot implement all of it — a paste-only fallback,
a loopback-only system-browser session — is a *different backend* an administrator selects, not a
runtime downgrade.

That is the sharpest thing lost when the old `browser_session.h` capability mask became a D-Bus
boundary, it is recorded as a cost in
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md), and if experience shows it was the
wrong call, the fix is a `GetCapabilities`-style addition argued upstream rather than invented here.

## Writing another backend

The interface exists so this is possible; the steps are upstream's, unchanged:

1. Implement `org.freedesktop.impl.portal.WebAuthentication.X1` in a D-Bus-activatable
   executable that owns `org.freedesktop.impl.portal.desktop.<name>` and exports the interface at
   `/org/freedesktop/portal/desktop`.
2. Install `<name>.portal` into `$datadir/xdg-desktop-portal/portals/` — the real directory, which
   is where the frontend looks:

   ```
   [portal]
   DBusName=org.freedesktop.impl.portal.desktop.<name>
   Interfaces=org.freedesktop.impl.portal.WebAuthentication.X1;
   UseIn=<desktop>
   ```

3. Select it in `portals.conf`:

   ```
   [preferred]
   org.freedesktop.impl.portal.WebAuthentication.X1=<name>
   ```

4. Restart xdg-desktop-portal, which exports the public interface once it finds your backend
   configured. [`../tools/dev-stack.sh`](../tools/dev-stack.sh) does all four on a private bus.

A backend must satisfy **everything** in [SECURITY.md](SECURITY.md) that the table above marks as
backend-enforced, and must not hand-edit the interface XML: it is a tracking copy of the frontend
branch's file. A backend that shows a page without the security chrome, or that loads the
completion URI before matching it, has not implemented this interface — it has implemented something
that looks like it and is a phishing launcher with a credential leak.
