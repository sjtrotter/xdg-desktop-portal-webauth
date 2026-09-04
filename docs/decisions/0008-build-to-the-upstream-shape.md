# 8. Build to the upstream shape now: a portal frontend and a portal backend

Date: 2026-09-03
Status: accepted (for the sketch); overrides the "premature" advice in
[0005](0005-service-shape.md), the review, and this repository's own previous
[ROADMAP.md](../ROADMAP.md) "Deferred" entry

## Context

Every earlier document in this repository said the same thing, and said it well:

> **Version 0 is ONE D-Bus-activated service.** Not a frontend and a backend.
> — [ARCHITECTURE.md](../ARCHITECTURE.md), before this decision

> **A frontend/backend D-Bus split** (`org.freedesktop.impl.portal.WebAuthentication`). Premature
> for version 0: imitating the names does not confer the properties, and it would double the D-Bus
> surface, activation and crash handling, versioning obligations, packaging, capability
> negotiation, error translation and transaction-lifetime bugs.
> — [ROADMAP.md](../ROADMAP.md) "Deferred", before this decision

The design review that shaped the sketch
(`FreeRDP-plan/DESIGN-portal-shape-codex.md`) is more explicit still:

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
                         owns io.github.sjtrotter.portal.Desktop
                         exports io.github.sjtrotter.portal.WebAuthentication1
service/backends/gtk/    webauth-portal-gtk        the reference BACKEND
                         owns io.github.sjtrotter.impl.portal.desktop.gtk
                         implements io.github.sjtrotter.impl.portal.WebAuthentication1
```

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

## The Desktop bus name

**The problem.** `io.github.sjtrotter.portal.Desktop` is a singleton, and it is the incubating
stand-in for `org.freedesktop.portal.Desktop`. The sibling `smartcard-portal` sketch is being
restructured in parallel into the same shape, and its frontend would claim the same name. Two
incubating frontends cannot both hold it: the second to start fails to acquire the name.

That is not a packaging accident to be worked around. It is the architecture stating a fact: **the
real xdg-desktop-portal hosts every portal interface in one process.** `desktop-portal/xdp-main.c`
calls `init_account()`, `init_file_chooser()`, `init_screenshot()` and the rest against one
`XdpContext`, all exported on one bus name at one object path. A per-project frontend is not a
smaller version of that; it is a different, incompatible thing.

**The resolution, in three parts.**

1. **At acceptance, the question disappears.** Both interfaces would be hosted by
   xdg-desktop-portal itself, on `org.freedesktop.portal.Desktop`, and neither repository would
   ship a frontend at all. This is the only end state, and it is why building a frontend now is
   explicitly building something to be deleted.

2. **Before acceptance, the answer is one shared incubating frontend.** A separate
   `incubating-portal-frontend` project — one process, one bus name, one object path,
   `init_webauthentication()` and `init_smartcard()` side by side, each routing to its own impl
   interface — is exactly xdg-desktop-portal's own structure and is the honest way for two
   incubating portals to coexist on one machine. It is proposed here rather than built here,
   because it needs agreement from both projects and neither interface is settled.

3. **For now, each repository ships its own frontend stub, and the documents say so.** This
   repository's frontend claims the name; so would the sibling's. Only one can be installed. Both
   `README.md` and the D-Bus service file say this in as many words, and the constraint is left
   visible rather than papered over with a per-project bus name, because a per-project bus name
   would quietly make the two prototypes *look* compatible while removing the one property the
   shape exists to have.

There is a second, sharper reason this matters here rather than being a packaging footnote. The
GTK backend's preferred certificate adapter calls the smart card portal's **public** interface,
`io.github.sjtrotter.portal.Smartcard1` — which, under the portal shape, is hosted on the same
`…portal.Desktop` bus name this project's frontend claims. So on a machine with both, the
certificate adapter and the web authentication portal are talking to the same process or to
nothing. One shared frontend is not a tidiness argument; it is what makes the preferred certificate
path reachable at all.

And it would fix something else neither project can fix alone. The smart card portal derives the
app id of *its* caller, which under the portal adapter is `webauth-portal-gtk`, not the application
that started the sign-in — so its consent dialog names the wrong thing, and the original app id can
only be passed as untrusted text. Inside one frontend, both interfaces would already hold the same
derived app id, and no attestation would have to cross a bus at all. See
[SECURITY.md](../SECURITY.md) and `service/backends/gtk/src/tls/client_cert_portal.h`.

**Status of the sibling's names.** As of this writing the smart card sketch ships
`io.github.sjtrotter.Smartcard1` — no `portal` component, no shared Desktop bus name — on its own
bus name at `/io/github/sjtrotter/Smartcard1`, and argues in its own documents that a
frontend/backend split is premature — the same position this decision overrides. Its restructuring
has not landed. The names this repository uses for it are therefore a **proposal to that project**,
not a fact about it, and the certificate adapter probes for both.
