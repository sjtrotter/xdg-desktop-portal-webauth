# Contributing

This backend is implemented. Its first consumer, the Entra ID / AVD token client
[entra-token-helper](https://github.com/sjtrotter/entra-token-helper), signed in against a real
Entra ID tenant and ran the full FreeRDP-to-AVD chain through it on 2026-09-05; see
[docs/TESTING.md](docs/TESTING.md) before changing anything, because most of its rules have a test.

## What is most wanted

- **Reasons this is wrong.** [docs/decisions/0005-service-shape.md](docs/decisions/0005-service-shape.md)
  lists nine ways it could be, including an explicit exit criterion for abandoning the whole middle
  layer. A tenth is worth more than a feature.
- **A second consumer.** Something that is not AVD and not FreeRDP, that needs an interactive web
  sign-in. Until one exists, the generic layer is a generalisation from one example — see
  [docs/ROADMAP.md](docs/ROADMAP.md) phase 1.
- **Spike results.** [docs/SPIKES.md](docs/SPIKES.md) tracked two go/no-go questions and an optional
  third; S2 was answered 2026-09-04 and S1 by the 2026-09-05 chain (the RDS proof-of-possession
  token needed one interactive re-auth). Answers change the design; opinions about them do not.
- **Corrections to facts.** The AVD client id, the registered redirect, the sovereign-cloud
  constants and the `AADSTS50011` behaviour were verified by hand. If any of them has changed, that
  matters more than anything else here.

## Ground rules

- **Never commit a real tenant id, hostname, account, token, or certificate.** Use `<tenant-id>`,
  `<user>@<tenant-domain>` and similar placeholders. The only real identifiers in this repository are
  the AVD public client id, Microsoft's authority/scope/redirect constants, and the error code
  `AADSTS50011`.
- **Keep the layers apart.** This repository must contain no Entra, Azure, OAuth or RDP identifier —
  that is what makes "the portal is protocol-independent" a testable claim rather than a slogan. The
  client lives in its own repository and there is no build-time dependency in either direction,
  which is the point of
  [docs/decisions/0006-two-repositories.md](docs/decisions/0006-two-repositories.md).
- **The frontend is not here, and the interface is not ours.** The frontend is a branch of
  xdg-desktop-portal ([docs/decisions/0010](docs/decisions/0010-backend-only-frontend-lives-upstream.md)),
  and `data/org.freedesktop.impl.portal.WebAuthentication.X1.xml` is a verbatim
  copy of that branch's file. A change to the interface is a change to that branch, followed by
  re-copying the file; a hand-edit here produces a backend that no longer implements what it claims.
  Anything about who is calling, what may be asked, or what a caller is told belongs upstream;
  anything that draws, browses or touches a card belongs here.
- **Mirror upstream rather than inventing.** Where xdg-desktop-portal has already answered a
  question — object paths, option filtering, `.portal` files, who gets told the `app_id` — copy the
  answer and cite it. Where this project must differ, say why in the file that differs. The measure
  of success is that [docs/UPSTREAMING.md](docs/UPSTREAMING.md) stays short.
- **Every source, test, tool and meson file carries `SPDX-License-Identifier: LGPL-2.1-or-later`
  and `SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>`.** See
  [docs/decisions/0004-license.md](docs/decisions/0004-license.md).
- **Documents state reasons, not just decisions.** Every rule in
  [docs/SECURITY.md](docs/SECURITY.md) has the attack it prevents written next to it, and that is
  deliberate: a rule without a reason gets removed by the next person who finds it inconvenient.
- **Say what is unproven.** The brokered certificate path was proven live on 2026-09-04/05. Mark what
  is still an assumption, do not smooth it over.
- **Disclose AI assistance** in commits where it was used, as the existing commits do.

## Building

One standalone meson project:

```console
$ meson setup build && ninja -C build
```

It needs GLib, GIO, GTK 4, libadwaita and WebKitGTK 6.0, all required.

## Style

C11, tabs, 100 columns, `SPDX` header — see [`.editorconfig`](.editorconfig). Headers carry the doc
comments: a header that explains *why* an interface is shaped the way it is is worth more here than
one that restates its own signatures, and a rule that has a reason should be readable next to the
code that enforces it.

## Running the tests

`meson test -C build` is the whole no-display suite; the end-to-end runs, which open a window, are
`tools/ui-smoke.sh` and `tools/portal-stack.sh`. What each tier can and cannot tell you is
[docs/TESTING.md](docs/TESTING.md).

## Sign-off and licence

A `Signed-off-by` trailer (`git commit -s`) is welcome but not required. By sending a change you
agree it is licensed under this project's licence, LGPL-2.1-or-later.
