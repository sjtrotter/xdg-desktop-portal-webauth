# The backend (impl) interface

Status: **incubating, version 1, nothing implemented.** The machine-readable description is
[`service/backends/gtk/data/io.github.sjtrotter.impl.portal.WebAuthentication1.xml`](../service/backends/gtk/data/io.github.sjtrotter.impl.portal.WebAuthentication1.xml);
this document explains what it means, and — more importantly — **which side of the boundary each
rule is enforced on and why**.

The public half is [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md). Applications should read that one and
stop there.

> **This interface is not for applications.** It is the contract between a portal frontend and a
> desktop's backend. An application calling it directly bypasses every check the frontend exists to
> perform, and gets in exchange an interface with no stability promise at all. How that is
> prevented, and how far the prevention goes, is in [SECURITY.md](SECURITY.md).

```
bus name       io.github.sjtrotter.impl.portal.WebAuthentication.gtk   (this backend)
object path    /io/github/sjtrotter/portal/WebAuthentication           (same path as the frontend's)
interface      io.github.sjtrotter.impl.portal.WebAuthentication1
request objects the frontend's handle path, exported on the BACKEND's bus name
declared in    $datadir/webauth-portal/portals/webauth-gtk.portal
```

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

with two things prepended and one thing changed:

- **`handle`** — the object path of the public Request. The backend exports its own
  `io.github.sjtrotter.impl.portal.Request` there, on its own bus name, so the frontend can forward
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
| `session_mode` | `s` | A **decision**, already validated and already narrowed by policy. Not a request. |
| `timeout` | `u` | Already clamped to the ceiling. |
| `title` | `s` | Untrusted application text, already length-limited. |
| `app_id_kind` | `s` | `sandboxed`, `cgroup` or `host`. The honesty level of `app_id`, so the chrome can say "an unidentified application" rather than showing an empty name. |

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
| Top-level navigations only | No | **Enforces** | Frame knowledge is engine knowledge. |
| TLS client certificates, PIN, card | Never sees them | **Enforces** every rule in [SECURITY.md](SECURITY.md) | The handshake is the backend's. |
| `parent_window` parsing | No, forwards opaquely | **Enforces** | Only the backend has a display connection and a window. |
| Exactly one `Response` | **Enforces** | Returns once | See above. |
| Answer when the backend dies | **Enforces** | Cannot | Nobody else is left to answer. |
| Cancel when the frontend dies | Cannot | **Enforces** | A window belonging to no request is a leak. |

## Completion matching: one rule, two enforcement points

The rule is defined once, in [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md) under "Completion matching".
It is enforced twice, and the two checks answer different questions:

1. **The backend**, against every top-level navigation, deciding *when to stop the browser*. This is
   the security-critical interception: match, commit, destroy the window, and never load the page.
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

**The cost, stated plainly:** one rule now has two implementations, and they can drift. The mitigation
is the shared fixture table in [../tests/README.md](../tests/README.md), which both must pass; the
cure is upstream, where the matcher belongs in shared code linked by both halves
([UPSTREAMING.md](UPSTREAMING.md)).

**And a third check that is not this one at all:** the application's own validation of the URI as a
*protocol* response — OAuth `state`, exactly one `code` — using a secret nothing in the portal ever
sees. None of the three substitutes for another.

## Failure modes the split introduced

Each is a real obligation, and each is the price of
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md).

| Situation | Who answers | What the application sees |
|---|---|---|
| No backend implements the interface | Frontend, at startup | The interface is **not exported**. The application sees "no such interface" and reports *unavailable*, exactly as if no portal were installed. |
| The backend cannot start (no display, no engine) | Frontend | `2`, reason `no_backend` |
| The backend dies mid-transaction | Frontend | `2`, reason `backend_disappeared` |
| The backend returns a URI that was not requested | Frontend | `2`, reason `backend_completion_mismatch` |
| The backend returns a malformed vardict | Frontend | `2`, reason `backend_protocol_error` |
| The frontend dies mid-transaction | Nobody — the application's connection sees the name vanish | The backend destroys its window at once |
| The backend is slow (a user with a card) | Nobody; this is normal | Nothing. The frontend's proxy timeout is `G_MAXINT`; the deadline is the transaction's, not D-Bus's |

## Versioning

The two interfaces version **independently**, and that is a new obligation the single-process design
did not have. Within impl version 1: the `Start` argument list does not change, response codes keep
their meanings, and new option and result keys may be added — a backend must ignore option keys it
does not recognise, and a frontend must ignore result keys it does not recognise.

A frontend paired with an older backend is a supported configuration only to the extent that the
older backend implements all of version 1. Version 1 has **no capability negotiation**, deliberately,
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

1. Implement `io.github.sjtrotter.impl.portal.WebAuthentication1` in a D-Bus-activatable executable
   that owns `io.github.sjtrotter.impl.portal.WebAuthentication.<name>` and exports the interface at
   `/io/github/sjtrotter/portal/WebAuthentication`.
2. Install `<name>.portal` into `$datadir/webauth-portal/portals/`:

   ```
   [portal]
   DBusName=io.github.sjtrotter.impl.portal.WebAuthentication.<name>
   Interfaces=io.github.sjtrotter.impl.portal.WebAuthentication1;
   UseIn=<desktop>
   ```

3. Select it in `portals.conf`:

   ```
   [preferred]
   io.github.sjtrotter.impl.portal.WebAuthentication1=<name>
   ```

A backend must satisfy **everything** in [SECURITY.md](SECURITY.md) that the table above marks as
backend-enforced. A backend that shows a page without the security chrome, or that loads the
completion URI before matching it, has not implemented this interface — it has implemented something
that looks like it and is a phishing launcher with a credential leak.
