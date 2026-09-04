# Upstreaming: what changes at acceptance, and what does not

Status: design sketch, and this document is a **plan for a patch nobody has been asked to accept**.
Nothing here has been proposed to xdg-desktop-portal, no maintainer has been contacted, and the
interface may well be argued down. See [ROADMAP.md](ROADMAP.md) phase 2 for the acceptance path and
its preconditions — a working implementation, a second unrelated consumer, and testing on two
desktops — none of which have been met.

The point of this document is that it is **short**. This repository is built in
xdg-desktop-portal's own shape
([decisions/0008](decisions/0008-build-to-the-upstream-shape.md)) precisely so that acceptance is a
rename and a move rather than a rewrite. If this file ever grows a section called "and then
restructure X", the decision has stopped paying for itself.

## Sources this shape was copied from

Read these before changing anything here; every mapping below is checked against them.

- Docs index — <https://flatpak.github.io/xdg-desktop-portal/docs/>
- Writing a new backend — <https://flatpak.github.io/xdg-desktop-portal/docs/writing-a-new-backend.html>
- `.portal` files and `portals.conf` — <https://flatpak.github.io/xdg-desktop-portal/docs/portals.conf.html>
- The Request interface — <https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html>
- The Session interface — <https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Session.html>
- Window identifiers — <https://flatpak.github.io/xdg-desktop-portal/docs/window-identifiers.html>
- Public and impl XML — <https://github.com/flatpak/xdg-desktop-portal/tree/main/data>
- Frontend sources — <https://github.com/flatpak/xdg-desktop-portal/tree/main/desktop-portal>
  and <https://github.com/flatpak/xdg-desktop-portal/tree/main/shared>
- Reference backend — <https://github.com/flatpak/xdg-desktop-portal-gtk>

**Note the frontend path.** Upstream's frontend sources were in `src/` and are now in
`desktop-portal/`, with the app-info code in `shared/`; `request.c` is `xdp-request.c`,
`session.c` is `xdp-session.c`, and `portal-impl.c` is `xdp-portal-config.c`. Documents written
against the old layout — including the design review this project started from — name paths that
no longer exist. Verified against the tree on 2026-09-03.

## Name mapping

| Incubating (here) | At acceptance | Notes |
|---|---|---|
| `io.github.sjtrotter.portal.WebAuthentication1` | `org.freedesktop.portal.WebAuthentication` | Public interface. Upstream drops the trailing major version from the interface name and carries it in the `version` property instead, as `org.freedesktop.portal.Account` does. |
| `io.github.sjtrotter.impl.portal.WebAuthentication1` | `org.freedesktop.impl.portal.WebAuthentication` | Backend interface. Same. |
| `io.github.sjtrotter.portal.WebAuthentication` (bus name) | `org.freedesktop.portal.Desktop` | This project's own incubating bus name, not a shared stand-in — see [decisions/0008](decisions/0008-build-to-the-upstream-shape.md), "Per-project bus names during incubation". At acceptance the frontend *is* xdg-desktop-portal, which already owns the real name. |
| `/io/github/sjtrotter/portal/WebAuthentication` | `/org/freedesktop/portal/desktop` | Both the frontend's and every backend's object path. |
| `io.github.sjtrotter.portal.Request` | `org.freedesktop.portal.Request` | Shared. Our XML node is **deleted**; upstream's existing interface is used unchanged. |
| `io.github.sjtrotter.impl.portal.Request` | `org.freedesktop.impl.portal.Request` | Same. |
| `io.github.sjtrotter.portal.Session` | `org.freedesktop.portal.Session` | Documented but unused: version 1 creates no Session. |
| `io.github.sjtrotter.impl.portal.WebAuthentication.gtk` | `org.freedesktop.impl.portal.desktop.<backend>` | The backend bus name. Uncontested either way — every backend has its own. |
| `io.github.sjtrotter.portal.Certificate1` | `org.freedesktop.portal.<TBD>` | **The sibling project's** name, and not this project's to map. Its own documents argue the eventual home may be a credential type under `credentialsd`'s proposed interface rather than a device-named portal. |

## File mapping

| Here | At acceptance | What happens |
|---|---|---|
| `service/frontend/src/webauthentication.h` | `xdg-desktop-portal/desktop-portal/webauthentication.c` (+`.h`) | **Moves.** This is the patch. |
| `service/frontend/data/io.github.sjtrotter.portal.WebAuthentication1.xml` | `xdg-desktop-portal/data/org.freedesktop.portal.WebAuthentication.xml` | Moves, renamed; the `Request` node is dropped. |
| `service/backends/gtk/data/io.github.sjtrotter.impl.portal.WebAuthentication1.xml` | `xdg-desktop-portal/data/org.freedesktop.impl.portal.WebAuthentication.xml` | Moves to the **frontend** repository, where upstream keeps all impl XML; backends consume it from `desktop_portal_interfaces_dir`. |
| `service/frontend/src/request.h` | — | **Deleted.** Upstream has `desktop-portal/xdp-request.c`. |
| `service/frontend/src/session.h` | — | **Deleted.** Upstream has `desktop-portal/xdp-session.c`. |
| `service/frontend/src/app-info.h` | — | **Deleted.** Upstream has `shared/xdp-app-info*.c`. |
| `service/frontend/src/portal-impl.h` | — | **Deleted.** Upstream has `desktop-portal/xdp-portal-config.c`. |
| `service/frontend/data/…portal.WebAuthentication.service.in` | — | **Deleted.** Upstream ships it. |
| `service/frontend/src/main.c`, `service/frontend/meson.build` | — | **Deleted.** Upstream has `xdp-main.c`; the new portal is one entry in `desktop-portal/meson.build` and one `init_webauthentication()` call. |
| `service/backends/gtk/**` | `xdg-desktop-portal-gtk/src/webauthentication.c` — or stays as its own backend project | **Stays.** Either it is contributed to xdg-desktop-portal-gtk as one more file in `src/`, or it remains a standalone backend like `xdg-desktop-portal-gnome`. That is a conversation with a desktop, not a precondition. |
| `service/backends/gtk/data/webauth-gtk.portal.in` | `data/<backend>.portal.in`, installed to `$datadir/xdg-desktop-portal/portals` | Directory and interface name change; the file's three keys do not. |
| `service/backends/gtk/src/completion.h` + the frontend's re-check | `xdg-desktop-portal/shared/` | **Merges.** One rule with two implementations becomes one rule with one implementation linked by both halves. This is the only genuine code merge in the list. |
| `clients/entra/**` | — | **Never moves.** It is a consumer. See [decisions/0006](decisions/0006-two-repositories.md). |
| `docs/**` | the proposal's text | The threat model, the matching rules, the caller-identity model and the chrome requirements are what a portal proposal is *asked* for; they move into the discussion, not the tree. |

## What actually changes at acceptance

Two things, and this list is deliberately exhaustive:

1. **The names.** Every `io.github.sjtrotter.portal.*` becomes `org.freedesktop.portal.*` and every
   `io.github.sjtrotter.impl.portal.*` becomes `org.freedesktop.impl.portal.*`, along with the
   object path and the two configuration directories.
2. **The frontend's home.** `service/frontend/` stops existing here and its one interesting file
   becomes one file in `xdg-desktop-portal/desktop-portal/`. Four of its five headers are deleted
   rather than moved, because upstream already has all four.

Nothing else. Not the impl method signature, not the response codes, not the option vocabulary, not
the matching rule, not the backend's structure, not the threat model, not the client. That is the
whole return on [decisions/0008](decisions/0008-build-to-the-upstream-shape.md), and it is why the
costs listed there were accepted.

## What acceptance would still require, that a rename does not give

Being *shaped* correctly is not being *accepted*, and the difference is most of the work:

- Answering "what protected host resource is being mediated?" — portals traditionally mediate
  access sandboxed applications lack. The strongest available answer is "a trusted system
  authentication user-agent, persistent sign-in state, and client-certificate capability", and it
  has to be argued rather than assumed.
- A second, unrelated consumer. [ROADMAP.md](ROADMAP.md) phase 1 makes this a precondition, and no
  amount of correct plumbing substitutes for it.
- Agreement on the public interface, and on **what the frontend enforces** — which is precisely the
  content of [IMPL-INTERFACE.md](IMPL-INTERFACE.md), and precisely the thing this repository has
  taken a position on early so that there is something concrete to disagree with.
- A second backend, and interest from another desktop.
- Conformance tests and documentation before the incubating interface is declared obsolete.

## Retiring the incubating names

When and if the freedesktop names ship, the incubating ones are **retired, not aliased**. A
compatibility period in which both bus names work would double the surface being reasoned about at
exactly the moment the security argument matters most, and this project has no deployed users to
protect. The `.portal` file, the D-Bus service files and the client's constants change in one
commit, and the old names are never claimed again.
