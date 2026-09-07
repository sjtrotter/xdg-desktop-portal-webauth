# 4. LGPL-2.1-or-later

Date: 2026-09-04
Status: accepted, supersedes the GPL-2.0-or-later decision below

## Decision

License this repository **LGPL-2.1-or-later**. Ship the LGPL-2.1 text as `LICENSE`, add a
REUSE-style `LICENSES/LGPL-2.1-or-later.txt`, and carry `SPDX-License-Identifier:
LGPL-2.1-or-later` plus `SPDX-FileCopyrightText: 2026 Stephen J. Trotter
<stephen.j.trotter@gmail.com>` in every source, test, tool and meson file.

## Why

**No Remmina code was ever copied.** The decision below chose GPL-2.0-or-later in anticipation of
lifting roughly 900 lines of certificate chooser and PIN-prompt code out of Remmina's RDP plugin
into `src/tls/`. That lift never happened, and it now never will: there is no chooser and
no PIN prompt in this repository at all. [0007](0007-certificate-adapter.md) put both in the
certificate portal, and `src/tls/` is two providers that build a `GTlsCertificate` from a
PKCS#11 URI and nothing else — a hundred lines with no card handling in them. The reason the
original decision gave no longer has a component to apply to.

**It matches the code's actual destination.** `xdg-desktop-portal`,
`xdg-desktop-portal-gtk` and `xdg-desktop-portal-gnome` are all LGPL-2.1-or-later, and so is the
frontend branch this backend is written against. This repository already carries files derived
from those projects under that licence — `src/request-impl.c`,
`src/external-window.c`, `src/completion.c`'s rule, the two verbatim XML copies —
and [UPSTREAMING.md](../UPSTREAMING.md) describes this backend's own eventual path alongside them.
Matching their licence removes the relicensing step the superseded decision below called out as a
cost.

**No constraint on D-Bus consumers.** This was already true under GPL-2.0-or-later — consumers
talk to this service over D-Bus, never by linking — and LGPL-2.1-or-later keeps it true while
removing any ambiguity about whether the *implementation* could later be linked into another
project's process.

**The sibling made the same choice, for the same reasons.**
`xdg-desktop-portal-certificate` relicensed from GPL-2.0-or-later to LGPL-2.1-or-later on
2026-09-04 (its `docs/decisions/0004-license.md`). The two repositories share a header —
`src/tls/portal-token.h` and that project's `src/module/portal-token.h` — which had to
carry two different licence lines while the licences differed, and which is now byte-identical.
[0006](0006-two-repositories.md) said the two halves need not stay on the same licence forever;
they did not have to, and they chose to.

**Apache-2.0 was not revisited.** The superseded decision's alternative was Apache-2.0 *with a
clean-room rewrite of the chooser*, and it was rejected because of what the rewrite would cost.
There is no chooser here to rewrite any more, so that trade no longer exists in the form it was
argued in; what LGPL-2.1-or-later buys over Apache-2.0 is matching the projects this code is
written to join, which is the property that is actually wanted.

## Superseded

The decision below, dated 2026-09-03, is retained as the record of the original reasoning. It no
longer reflects this repository's licence.

---

# 4. GPL-2.0-or-later (superseded)

Date: 2026-09-03
Status: superseded by the decision above, 2026-09-04

## Context

Two licences were in play, because two bodies of prior art are.

The **PKCS#11 certificate chooser and PIN prompt** — the part of this project that is genuinely
hard and genuinely proven — is derived from Remmina's RDP plugin, which is **GPL-2.0-or-later**.
It is roughly 900 lines that handle certificate enumeration under a cancellable, time-bounded
subprocess, asynchronous certificate loading off the GTK thread, per-challenge PIN binding with a
single WebKit-initiated retry, challenge-host verification against the sign-in authority, and a
long list of card and p11-kit edge cases that were found by running it against real hardware.

The **OAuth transaction, redirect classifier and redaction rules** are informed by work on
FreeRDP, which is **Apache-2.0**. Apache-2.0 is one-way compatible with GPL-3.0 but not with
GPL-2.0, so a project that wanted to take Apache-2.0 *code* into a GPL-2.0-only work would have a
problem.

That combination is only a problem if both bodies of code end up linked into one binary. They do
not: both components are separate processes, spoken to over CLI and D-Bus boundaries, per
[0001](https://github.com/sjtrotter/entra-token-helper/blob/main/docs/decisions/0001-standalone-helper.md).
Nothing links FreeRDP into either of them and neither is linked into FreeRDP.

## Decision

License this project **GPL-2.0-or-later**. Ship the full GPLv2 text as `LICENSE`, and carry
`SPDX-License-Identifier: GPL-2.0-or-later` headers in every source file.

The Remmina-derived chooser and PIN code, when lifted, keeps its Remmina copyright attribution
alongside the SPDX header. Note where it lands: `src/tls/`, the in-process certificate
adapter ([0007](0007-certificate-adapter.md)). So the licence question travels with the components
at the repository split ([0006](0006-two-repositories.md)), the two halves need not stay on the same
licence forever, and if the portal adapter is ever proven and the in-process one retired, this
decision's central premise leaves this repository with it. The FreeRDP-derived material is design and rules, not code — the
strict-decoding requirements, the classification outcomes, the redaction list — and where any
Apache-2.0 code is actually reused it must carry its own notice and the `NOTICE` obligations that
come with it. The "or later" makes GPL-3.0 available if a future dependency ever needs it, which
also resolves the Apache-2.0 direction should real code ever need to be taken.

## Consequences

**What this buys.** The proven card-handling code can be used directly. That code is the reason
this project is a few months of work rather than a research exercise; a licence choice that
forbade using it would be choosing convenience over the only battle-tested component in the
design.

**What it costs.**

- Neither component can be linked into a permissively licensed program. That is not a limitation in
  practice, since both are explicitly separate processes — but it does mean the "just link it as a
  library" shortcut is closed, which is arguably a feature.
- Downstream projects that need a permissive licence cannot vendor this code. They can still call
  the binary, which is the supported integration anyway.

**The alternative that was not chosen: Apache-2.0 with a clean-room rewrite of the chooser.**

Apache-2.0 would match FreeRDP, would let any client vendor this code, and would leave every future
licensing door open. It would also matter more for layer 1 than for layer 2: a component hoping to
be adopted by desktop projects has a stronger case for a permissive licence than one consumed over a
CLI. It requires writing the certificate chooser, PIN prompt and card
edge-case handling again from scratch, without reference to the Remmina implementation. That is
not primarily a coding cost: it is a *re-discovery* cost. The value in the existing code is the
enumerated list of things that go wrong with real cards — trust tokens that must be skipped,
empty tokens that exit non-zero, PIN retries that must not be spent automatically, tokens removed
mid-flow — and a clean-room rewrite would have to find them all again on hardware.

For a design sketch, spending that budget to buy a licence property nobody has asked for is the
wrong trade. If a desktop project, or a downstream consumer, ever genuinely needs a permissive
implementation, the rewrite can
be done then, deliberately, with the edge-case list already written down in
[SPIKES.md](../SPIKES.md) as the specification — which is a far cheaper clean room than the one
available today.
