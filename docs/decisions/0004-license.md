# 4. GPL-2.0-or-later

Date: 2026-09-03
Status: accepted (for the sketch)

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
[0001](0001-standalone-helper.md). Nothing links FreeRDP into either of them and neither is linked
into FreeRDP.

## Decision

License this project **GPL-2.0-or-later**. Ship the full GPLv2 text as `LICENSE`, and carry
`SPDX-License-Identifier: GPL-2.0-or-later` headers in every source file.

The Remmina-derived chooser and PIN code, when lifted, keeps its Remmina copyright attribution
alongside the SPDX header. Note where it lands: `service/backends/gtk/src/tls/`, the in-process certificate
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
