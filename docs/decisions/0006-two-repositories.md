# 6. One repository now, two repositories at the first tagged interface release

Date: 2026-09-03
Status: accepted (for the sketch); the split is now **three** components, per
[0008](0008-build-to-the-upstream-shape.md)

> **Amendment (0008).** `service/` is now two independent meson projects rather than one — a portal
> frontend and a reference backend — so this repository holds three separately buildable
> components, not two. The reasoning below is unchanged and applies with more force: the frontend
> and the backend have different dependencies (GLib only, versus GTK/WebKitGTK/p11-kit), different
> release cadences and different eventual homes. Their eventual homes are now *known*: the frontend
> moves into xdg-desktop-portal at acceptance and the backend stays as a desktop backend project.
> See [../UPSTREAMING.md](../UPSTREAMING.md).

## Context

The two layers in this repository have almost nothing in common except the author and the moment
they were written:

| | `service/` (frontend + backend) | `clients/entra/` |
|---|---|---|
| Security boundary | a browser and a smart card | a cloud identity and its refresh tokens |
| Dependencies | GTK, WebKitGTK, p11-kit, GLib | libsecret, an HTTP client, GLib |
| Likely upstream | freedesktop / a desktop project | FreeRDP's orbit, or standalone |
| Consumers | any application needing an interactive sign-in | RDP clients |
| Acceptance criteria | a portal interface argued about in public | "does an AVD connection work" |
| Release cadence | slow, interface-bound | fast, provider-bound |

Publishing them as one combined project would undermine the central claim of the design — that the
portal is protocol-independent. A reader who finds Entra constants in the same repository as the
generic portal has every right to conclude the generic portal is Entra's.

Against that: during initial extraction, a single repository genuinely reduces friction. The
interface is going to change repeatedly as the first consumer exercises it, and a coordinated
change across two repositories with a version bump between them, before the interface has ever run,
is ceremony without benefit.

## Decision

Keep **one repository for the sketch**, with clearly independent top-level components:

```
backend/         xdg-desktop-portal-webauth  a complete, standalone meson project
clients/entra/   entra-token-client          a complete, standalone meson project
```

> **Amendment (0010).** There were three of these; the first,
> `service/frontend/  webauth-portal-frontend`, is gone. The frontend is a branch of
> xdg-desktop-portal, so it is neither a component here nor a repository this decision has to
> split anything into. `service/backends/gtk/` became `backend/`. Two components, one of which is
> a portal backend and one of which is an application, is the same argument this ADR makes with
> one fewer term in it.

Each is separately configurable and buildable today (`meson setup build-backend backend`),
and there is **no build-time dependency between them in either direction**. The top-level
`meson.build` is a convenience umbrella that includes both as meson subprojects and does
nothing else; it disappears at the split.

**Split into two repositories at the first tagged interface release**, with tagged interface
versions and CI integration tests between them.

**The Entra client never moves into the eventual portal repository.** It is a consumer. If the
portal is ever accepted upstream, what moves is the interface contract and the frontend — not the
backend, and certainly not the first thing that happened to use them. [../UPSTREAMING.md](../UPSTREAMING.md)
says exactly which files go where.

## Consequences

- Until the split, every change must be reviewed with the split in mind: a shared header, a shared
  build flag, or a helper reached across the boundary is a defect, not a convenience. The absence
  of build-time coupling is what makes that reviewable rather than aspirational.
- The claim "the portal is protocol-independent" stays testable: it is true only while `backend/`
  contains no Entra, Azure, OAuth or RDP identifier. That is a grep, and it should be one in CI.
  A second grep used to join it — the frontend directory had to contain no toolkit dependency,
  because the frontend that moves upstream cannot bring GTK with it — and it is gone with the
  frontend ([0010](0010-backend-only-frontend-lives-upstream.md)); upstream enforces it now by
  simply not being this repository.
- The repository was renamed from `entra-token-helper` to `xdg-desktop-portal-webauth` on
  2026-09-06; the CLI binary and client are still called `entra-token-helper`, and the meson
  subproject is `entra-token-client`.
- A second consumer, which
  [0005](0005-service-shape.md) makes a precondition for pursuing standardisation, is also the
  natural trigger to check the split has actually happened. If a second consumer would find it
  awkward to depend on this repository, the split is overdue.
