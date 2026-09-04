# Contributing

This is a design sketch. The most useful contribution right now is an argument, not a patch.

## What is most wanted

- **Reasons this is wrong.** [docs/decisions/0005-service-shape.md](docs/decisions/0005-service-shape.md)
  lists nine ways it could be, including an explicit exit criterion for abandoning the whole middle
  layer. A tenth is worth more than a feature.
- **A second consumer.** Something that is not AVD and not FreeRDP, that needs an interactive web
  sign-in. Until one exists, the generic layer is a generalisation from one example — see
  [docs/ROADMAP.md](docs/ROADMAP.md) phase 1.
- **Spike results.** [docs/SPIKES.md](docs/SPIKES.md) has two go/no-go questions and an optional
  third. Answers change the design; opinions about them do not.
- **Corrections to facts.** The AVD client id, the registered redirect, the sovereign-cloud
  constants and the `AADSTS50011` behaviour were verified by hand. If any of them has changed, that
  matters more than anything else here.

## Ground rules

- **Never commit a real tenant id, hostname, account, token, or certificate.** Use `<tenant-id>`,
  `<user>@<tenant-domain>` and similar placeholders. The only real identifiers in this repository are
  the AVD public client id, Microsoft's authority/scope/redirect constants, and the error code
  `AADSTS50011`.
- **Keep the layers apart.** `service/` must contain no Entra, Azure, OAuth or RDP identifier — that
  is what makes "the portal is protocol-independent" a testable claim rather than a slogan. There is
  no build-time dependency between `service/` and `clients/entra/` in either direction, and there must
  never be one. See [docs/decisions/0006-two-repositories.md](docs/decisions/0006-two-repositories.md).
- **Keep the frontend and the backend apart, and keep the frontend thin.** `service/frontend/`
  depends on GLib and GIO and nothing else, ever: it is the directory that moves into
  xdg-desktop-portal at acceptance, and a toolkit dependency there is a defect, not a convenience.
  Anything that draws, browses or touches a card belongs in a backend. There is no build-time
  dependency between the two halves either — they speak D-Bus. See
  [docs/decisions/0008-build-to-the-upstream-shape.md](docs/decisions/0008-build-to-the-upstream-shape.md).
- **Mirror upstream rather than inventing.** Where xdg-desktop-portal has already answered a
  question — object paths, option filtering, `.portal` files, who gets told the `app_id` — copy the
  answer and cite it. Where this project must differ, say why in the file that differs. The measure
  of success is that [docs/UPSTREAMING.md](docs/UPSTREAMING.md) stays short.
- **Every source file carries `SPDX-License-Identifier: GPL-2.0-or-later`.** See
  [docs/decisions/0004-license.md](docs/decisions/0004-license.md).
- **Documents state reasons, not just decisions.** Every rule in
  [docs/SECURITY.md](docs/SECURITY.md) has the attack it prevents written next to it, and that is
  deliberate: a rule without a reason gets removed by the next person who finds it inconvenient.
- **Say what is unproven.** Several things here are assumptions — the brokered certificate path most
  of all. Mark them, do not smooth them over.
- **Disclose AI assistance** in commits where it was used, as the existing commits do.

## Building

Each of the three components is a standalone meson project:

```console
$ meson setup build-frontend service/frontend      && ninja -C build-frontend
$ meson setup build-gtk      service/backends/gtk  && ninja -C build-gtk
$ meson setup build-entra    clients/entra         && ninja -C build-entra
```

Or all three, through the umbrella:

```console
$ meson setup build && ninja -C build
```

Only GLib and GIO are needed for the stubs. Nothing is implemented: every verb exits `70`.

## Style

C11, tabs, 100 columns, `SPDX` header — see [`.editorconfig`](.editorconfig). Headers carry the doc
comments; there are no implementations yet, and a header that explains *why* an interface is shaped
the way it is is worth more here than one that restates its own signatures.
