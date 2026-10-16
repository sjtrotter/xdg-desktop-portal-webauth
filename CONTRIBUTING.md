# Contributing

[Testing](docs/TESTING.md) provides build and test commands;
[Security](docs/SECURITY.md) defines the backend's obligations.

## Useful contributions

- Review the objections and exit criterion in
  [ADR 0005](docs/decisions/0005-service-shape.md).
- Exercise an unrelated consumer or implement another backend; see
  [Roadmap](docs/ROADMAP.md).
- Extend the hardware and distribution checks in [Feasibility checks](docs/SPIKES.md).
  Use the recorded S1 and S2 evidence to identify missing coverage.
- Correct factual claims, including client-side AVD identifiers, registered redirects,
  sovereign-cloud constants and `AADSTS50011` behavior, with evidence.

## Ground rules

Use placeholders for tenant IDs, hostnames, accounts, tokens and certificates. Public AVD
client IDs, Microsoft's authority/scope/redirect constants and `AADSTS50011` may be recorded
as reference facts.

Keep protocol-specific Entra, Azure, OAuth and RDP logic in the client repository; see
[ADR 0006](docs/decisions/0006-two-repositories.md).

Follow [Upstreaming](docs/UPSTREAMING.md) for frontend integration and
[the XML source rule](docs/IMPL-INTERFACE.md#xml-source) for interface changes.
[Architecture](docs/ARCHITECTURE.md#ownership) defines component ownership.

Keep license and SPDX headers exactly. Source, test, tool and Meson files carry
`SPDX-License-Identifier: LGPL-2.1-or-later` and
`SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>`.
See [ADR 0004](docs/decisions/0004-license.md).

State which security rules are implemented and which remain requirements. Record measured
limits and disclose AI assistance in commits where it applies.

## Style

C11, tabs and 100 columns; see [`.editorconfig`](.editorconfig). Comments state non-obvious
invariants in one factual line, two at most. Keep ownership rules, protocol constraints and
security facts; omit code narration and editing history. Reference docs describe current behavior;
ADRs retain decisions and history.

## Sign-off and licence

A `Signed-off-by` trailer (`git commit -s`) is welcome but optional. Contributions are licensed
under LGPL-2.1-or-later.
