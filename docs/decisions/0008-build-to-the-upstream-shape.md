# 8. Build to the upstream shape now: a portal frontend and a portal backend

Date: 2026-09-03
Status: accepted (for the sketch); overrides the "premature" advice in
[0005](0005-service-shape.md), the review, and this repository's own previous
[ROADMAP.md](../ROADMAP.md) "Deferred" entry; its *packaging* half is superseded by
[0010](0010-backend-only-frontend-lives-upstream.md)

> **Amendment (0010).** The split argued for here is preserved exactly; what changed is that the
> frontend half is no longer in this repository. It is a branch of xdg-desktop-portal
> (`experimental/certificate-webauthentication`), which is how upstream asks new portals to be
> developed, so `service/frontend/` was deleted and `service/backends/gtk/` became `backend/`,
> which in turn became this repository's root at the split of 2026-09-07
> ([0006](0006-two-repositories.md)). Every directory and interface name below is therefore
> historical.

## Context

Every earlier document in this repository said the same thing, and said it well:

> **Version 0 is ONE D-Bus-activated service.** Not a frontend and a backend.
> — [ARCHITECTURE.md](../ARCHITECTURE.md), before this decision

> **A frontend/backend D-Bus split** (`org.freedesktop.impl.portal.WebAuthentication`). Premature
> for version 0: imitating the names does not confer the properties, and it would double the D-Bus
> surface, activation and crash handling, versioning obligations, packaging, capability
> negotiation, error translation and transaction-lifetime bugs.
> — [ROADMAP.md](../ROADMAP.md) "Deferred", before this decision

The design review that shaped the sketch is more explicit still:

> Layer 1 should initially be an independently named, single D-Bus service with an internal backend
> abstraction — not a freedesktop portal frontend/backend ecosystem.

> A real portal needs it: applications call `org.freedesktop.portal.Desktop`, and the frontend
> selects a desktop backend that implements an inaccessible `org.freedesktop.impl.portal.*`
> interface. An independent prototype does not gain those properties by imitating the names. It
> instead doubles: D-Bus surface; activation and crash handling; versioning obligations; packaging;
> capability negotiation; error translation; transaction-lifetime bugs.

> **Do not publish a backend D-Bus ABI yet.**

None of that has been shown to be wrong. It is being **overridden**, deliberately, by the author,
and this file exists so that the override is recorded with the reasons *against* it intact rather
than quietly deleted.

## Decision

Build the sketch in the shape it would have upstream: **a portal frontend and a portal backend,
split across a D-Bus impl interface, mirroring xdg-desktop-portal and
xdg-desktop-portal-gtk file for file.**

```
service/frontend/        webauth-portal-frontend   the FRONTEND
                         owns io.github.sjtrotter.portal.WebAuthentication
                         exports io.github.sjtrotter.portal.WebAuthentication1
service/backends/gtk/    webauth-portal-gtk        the reference BACKEND
                         owns io.github.sjtrotter.impl.portal.WebAuthentication.gtk
                         implements io.github.sjtrotter.impl.portal.WebAuthentication1
```

(The frontend's bus name below is this project's own, per "Per-project bus names during
incubation" further down; it was originally drafted as a shared `…portal.Desktop` stand-in and has
since been corrected here to match.)

**The names stay incubating.** Nothing here ships an `org.freedesktop.*` name, and that part of the
earlier advice is followed exactly: `io.github.sjtrotter.portal.*` and
`io.github.sjtrotter.impl.portal.*` are project-controlled reverse-DNS names with a major version,
chosen so that acceptance is a rename. The rule being broken is "do not publish a backend ABI yet",
not "do not claim the freedesktop namespace".

## Why, in order of weight

1. **To avoid a second rewrite.** The acceptance path in [ROADMAP.md](../ROADMAP.md) phase 2 ends
   with "add xdg-desktop-portal frontend routing and an `org.freedesktop.impl.portal.*` backend
   interface". Building the single service first means writing the transaction layer, the identity
   resolution, the policy and the window ownership once as one process, and then again as two.
   Everything that is easy in one address space — passing a resolved identity struct, sharing a
   matcher, letting a window and a request die together — has to be rediscovered as a protocol.
   The rewrite is not the expensive part; discovering *during* the rewrite which of those things
   the design silently depended on is.

2. **To exercise the impl boundary early, while it is still cheap to change.** The split is where
   the design's hardest questions live, and every one of them is invisible in a single process:
   what exactly does a backend get told about the caller, and what may it never ask? Which side
   validates the URIs? What happens to the application's request when the backend crashes
   mid-transaction? Who owns the deadline? These are questions with answers, and the answers are
   worth arguing about before anyone writes the code — which is only possible if the boundary
   exists on paper. Two of them changed the design as soon as the boundary was drawn; they are
   listed under "What this immediately changed" below.

3. **To make the upstream patch a rename.** With this shape, the eventual proposal is: move
   `service/frontend/src/webauthentication.h` into `xdg-desktop-portal/desktop-portal/`, drop the
   incubating prefixes, delete the frontend's Request/Session/app-info/portal-impl files because
   upstream already has all four, and rename the backend's bus name and `.portal` file. The
   interface argument stays about the interface. [UPSTREAMING.md](../UPSTREAMING.md) is that patch,
   written in advance, and it is short — which is the evidence that this decision did what it was
   for.

4. **Because the split is genuinely better security, not only better paperwork.** The app id is now
   derived by a process the application cannot talk to and handed to the process that draws the
   window naming it. In the single-process design the thing being protected (the chrome) and the
   thing establishing who is asking sat in one address space. This is the one benefit that would
   have been worth having even if upstream never existed.

## The costs, named by the review, accepted here

Each one is real. None is denied.

- **Double the D-Bus surface.** Two interfaces to keep honest instead of one, and the impl
  interface is one that applications must be kept away from — a problem that did not exist
  before. See [SECURITY.md](../SECURITY.md), "The impl interface is not for applications".
- **Activation and crash handling.** Two D-Bus service files, two activation paths, and a whole
  new failure mode: the backend dying mid-transaction, which the frontend must turn into exactly
  one `Response` with reason `backend_disappeared`. A single process could only fail by dying, and
  then there was nobody left who owed anybody an answer.
- **Versioning obligations.** The public interface and the impl interface version independently.
  A backend older than its frontend is now a supported configuration, or must be explicitly
  declared unsupported.
- **Packaging.** Two binaries, a `.portal` file, a `portals.conf` search path, and a documented
  answer to "what happens when no backend is installed" (the interface is not exported at all).
- **Capability negotiation.** The old `browser_session.h` vtable carried a capability mask, so one
  process could pick between a WebKit session, a system browser and a paste fallback per request.
  The impl interface has no such negotiation, because upstream's has none. Version 1's answer is
  that a backend implements the whole interface or does not claim it, and a mechanism that cannot
  (paste-only, loopback-only) is a *different backend* the administrator selects. That is a real
  loss of runtime flexibility in exchange for a real gain in configurability, and it may turn out
  to be the wrong trade.
- **Error translation.** A `GError` from a backend now has to become a response code and a stable
  reason symbol without leaking anything a URI-carrying error message might contain.
- **Transaction-lifetime bugs.** The deadline now exists in two processes, `Close()` travels one
  hop further, and the "committed completion wins over a simultaneous `Close()`" race is now
  distributed. The specification did not change; the number of places it can be got wrong did.

And the review's central point, which stands: **imitating the names confers none of a portal's
properties.** Nothing here is an xdg-desktop-portal interface, nothing has been proposed to anyone,
and no maintainer has been asked. This decision buys a shorter path to *asking*; it buys no
standing whatsoever.

## What this immediately changed in the design

Drawing the boundary settled two questions that the single-process design had left comfortable:

- **Validation is split, and both halves stay.** The frontend validates the URIs it forwards
  (upstream's rule: every portal in `desktop-portal/` validates and filters what it passes to an
  impl), and the backend performs the interception, because only the backend has a navigation to
  intercept. The frontend then re-checks the `completion_uri` the backend returns against the one
  it forwarded. See [IMPL-INTERFACE.md](../IMPL-INTERFACE.md).
- **The browser-session vtable is gone.** Alternative browsing mechanisms are alternative
  *backends*, selected by `portals.conf`, not implementations selected by a capability mask.

## Consequences

- `service/src/` no longer exists. `service/frontend/` is the directory that moves upstream at
  acceptance; `service/backends/gtk/` is the directory that stays here and becomes an ordinary
  desktop backend project.
- The frontend depends on GLib and GIO and must never depend on a toolkit. If GTK ever appears in
  `service/frontend/meson.build`, this decision has been violated.
- The completion matcher now has two implementations of one rule. The shared fixture table in
  [tests/README.md](../../tests/README.md) is mandatory rather than valuable, and merging the two
  into shared code is part of the upstreaming work.
- **This decision is reversible, and the reversal is the cheap direction.** Collapsing a frontend
  and a backend that already agree on a documented interface back into one process is a much
  smaller job than splitting one process into two. If the impl boundary turns out to cost more
  than it is worth before anyone upstream is interested, collapsing it is a decision to record
  here, not a rewrite.

## Per-project bus names during incubation

> **Moot (0010).** This whole section is about two incubating frontends coexisting on one machine.
> Neither exists any more — both interfaces are on one xdg-desktop-portal branch, on the real
> `org.freedesktop.portal.Desktop` — so there are no per-project bus names left to coordinate. It
> is kept as the record of why the shared-name arrangement was dropped, and because its closing
> argument about the delegation gap is the one 0010 reports as resolved in-process.

**The old problem, and why it is gone.** `io.github.sjtrotter.portal.Desktop` used to be a shared
singleton stand-in for `org.freedesktop.portal.Desktop`, claimed by this project's frontend and, in
parallel, by the sibling certificate-portal sketch's frontend. Two incubating frontends could not
both hold it: the second to start would fail to acquire the name. The author has since decided that
was the wrong shape for incubation — a shared name papers over the fact that these are two separate,
unreviewed prototypes, not one project — and each incubating frontend now claims its **own** bus
name and object path instead:

| | Bus name | Object path | Backend impl bus |
|---|---|---|---|
| This project's frontend | `io.github.sjtrotter.portal.WebAuthentication` | `/io/github/sjtrotter/portal/WebAuthentication` | `io.github.sjtrotter.impl.portal.WebAuthentication.gtk` |
| The sibling's frontend | `io.github.sjtrotter.portal.Certificate` | `/io/github/sjtrotter/portal/Certificate` | `io.github.sjtrotter.impl.portal.Certificate.gtk` |

The sibling's interface itself was renamed alongside its bus name: `Smartcard1` is now
`Certificate1`, for reasons that are that project's own to record (its own ADR 0009). Both
incubating frontends install and run side by side now; there is nothing left to coordinate about
bus names between the two projects.

**At acceptance, the question disappears entirely.** Both interfaces would be hosted by
xdg-desktop-portal itself, on the real `org.freedesktop.portal.Desktop` at
`/org/freedesktop/portal/desktop`, and neither repository would ship a frontend at all. Acceptance
is a rename, nothing else, for either project — the per-project incubating names above are deleted
in the same commit that deletes the frontend directory; see [UPSTREAMING.md](../UPSTREAMING.md).

**What per-project names do NOT fix: the delegation gap.** This project's backend calls the
certificate portal's **public** interface, `io.github.sjtrotter.portal.Certificate1`, as an ordinary
client — a backend never calls another project's backend. The certificate portal derives the app id
of *its* caller, which under that call is `webauth-portal-gtk`, not the application that started the
original sign-in — so its consent dialog names the wrong thing, and the original app id can only be
passed as untrusted text (the "reason" and "context" hints). This is a **process-boundary** problem,
not a naming collision, and giving each frontend its own bus name does nothing to change it: it would
be exactly as true if the two frontends had always had separate names.

It is solved only when both interfaces run inside **one trusted frontend process**, sharing one
address space and one derived identity, so that a passed-along app id is exactly as trustworthy as
the frontend's own derivation because nothing untrusted touched it in between. That happens
automatically at acceptance. Before acceptance, **a shared incubating frontend hosting both
interfaces is one option available to explore** — not a required next step, and not something either
project is blocked on the other to build. See [SECURITY.md](../SECURITY.md) and
`service/backends/gtk/src/tls/client_cert_portal.h`.

**What is not to be done.** Passing an unattested app id across the process boundary as a stopgap
ahead of any shared-frontend work is an assertion of someone else's identity with nothing to back
it, which is exactly the identity-laundering [SECURITY.md](../SECURITY.md) forbids. It is not a
smaller version of a shared-frontend fix; it is the thing a shared frontend would exist to avoid
needing, and it is not to be built.
