# 6. One repository now, two repositories at the first tagged interface release

Date: 2026-09-03
Status: accepted (for the sketch)

## Context

The two layers in this repository have almost nothing in common except the author and the moment
they were written:

| | `service/` | `clients/entra/` |
|---|---|---|
| Security boundary | a browser and a smart card | a cloud identity and its refresh tokens |
| Dependencies | GTK, WebKitGTK, p11-kit, GLib | libsecret, an HTTP client, GLib |
| Likely upstream | freedesktop / a desktop project | FreeRDP's orbit, or standalone |
| Consumers | any application needing an interactive sign-in | RDP clients |
| Acceptance criteria | a portal interface argued about in public | "does an AVD connection work" |
| Release cadence | slow, interface-bound | fast, provider-bound |

Publishing them as one combined project would undermine the central claim of the design — that
layer 1 is protocol-independent. A reader who finds Entra constants in the same repository as the
generic service has every right to conclude the generic service is Entra's.

Against that: during initial extraction, a single repository genuinely reduces friction. The
interface is going to change repeatedly as the first consumer exercises it, and a coordinated
change across two repositories with a version bump between them, before the interface has ever run,
is ceremony without benefit.

## Decision

Keep **one repository for the sketch**, with two clearly independent top-level components:

```
service/          webauth-service      layer 1 — a complete, standalone meson project
clients/entra/    entra-token-client   layer 2 — a complete, standalone meson project
```

Each is separately configurable and buildable today (`meson setup build-service service`), and
there is **no build-time dependency between them in either direction**. The top-level `meson.build`
is a convenience umbrella that includes both as meson subprojects and does nothing else; it
disappears at the split.

**Split into two repositories at the first tagged interface release**, with tagged interface
versions and CI integration tests between them.

**The Entra client never moves into the eventual portal repository.** It is a consumer. If layer 1
is ever accepted upstream, what moves is the interface contract and the implementation — not the
first thing that happened to use them.

## Consequences

- Until the split, every change must be reviewed with the split in mind: a shared header, a shared
  build flag, or a helper reached across the boundary is a defect, not a convenience. The absence
  of build-time coupling is what makes that reviewable rather than aspirational.
- The claim "layer 1 is protocol-independent" stays testable: it is true only while `service/`
  contains no Entra, Azure, OAuth or RDP identifier. That is a grep, and it should be one in CI.
- The repository name is currently `entra-token-helper`, which is the *old* name and now describes
  only the smaller half. The working title is `webauth-service`; the rename waits until the
  interface name is settled, because renaming twice is worse than renaming late.
- A second consumer, which
  [0005](0005-service-shape.md) makes a precondition for pursuing standardisation, is also the
  natural trigger to check the split has actually happened. If a second consumer would find it
  awkward to depend on this repository, the split is overdue.
