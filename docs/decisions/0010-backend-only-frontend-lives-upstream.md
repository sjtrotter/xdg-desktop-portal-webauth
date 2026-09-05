# 10. Be a backend only: the frontend lives in xdg-desktop-portal

Date: 2026-09-04
Status: accepted (for the sketch); supersedes the *packaging* half of
[0008](0008-build-to-the-upstream-shape.md) and retires the incubating frontend

## Context

[0008](0008-build-to-the-upstream-shape.md) decided to build a portal frontend and a
portal backend now, in xdg-desktop-portal's shape but under project-controlled names, so
that acceptance upstream would be "a rename and a move rather than a rewrite". That
decision was right about the *shape* and wrong about the *route*, and the reason it was
wrong is simply that the author did not yet know how new portals actually get developed.

The evidence is in the xdg-desktop-portal tree and in
[PR #1889](https://github.com/flatpak/xdg-desktop-portal/pull/1889), "Introduce
Credentials portal (experimental)". A new portal is not incubated in a separate
repository under a separate namespace and then proposed as a finished thing. It is
developed **in the frontend's own tree**, under an `experimental` namespace, gated off by
default. Sebastian Wick, on that PR, 2026-01-28:

> As for the interface name, let's call it something like
> `org.freedesktop.portal.experimental.Credentials`. It should also not be exposed by
> default and have a environment variable to turn it on (e.g.
> `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=credentials`).

So there is a sanctioned way to write an unfinished portal frontend, it is inside
xdg-desktop-portal, and the names it uses are `org.freedesktop.portal.experimental.*` and
`org.freedesktop.impl.portal.experimental.*` — not because acceptance has been granted,
but because that is the namespace upstream set aside for exactly this state.

Against that, a separately-namespaced frontend in this repository was worse in every
direction. It reimplemented `Request`, app-id derivation and `.portal` discovery that
xdg-desktop-portal already has and that [UPSTREAMING.md](../UPSTREAMING.md) always said
would be deleted. It could never be reviewed by the people whose review matters, because
it was not in their tree. And it made this repository claim a public bus name no
application had any reason to trust.

## Decision

**Move the frontend into a branch of xdg-desktop-portal, and delete it from here. This
repository is an out-of-tree backend plus an application, and nothing else.**

- The frontend is `xdg-desktop-portal`, branch
  `experimental/certificate-webauthentication`, commits `3f46e3c..661e441`, with
  `3a32e9b web-authentication: Add an experimental WebAuthentication portal` as the one
  that matters here. It defines both
  `org.freedesktop.portal.experimental.WebAuthentication` (public) and
  `org.freedesktop.impl.portal.experimental.WebAuthentication` (impl), implements the URI
  validation, the option filter, the completion re-check and the
  `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` gate, and ships a python-dbusmock backend and
  a pytest suite (38 cases, passing).
- `backend/` builds **one binary**, `xdg-desktop-portal-webauth`, owning
  `org.freedesktop.impl.portal.desktop.webauth` and exporting
  `/org/freedesktop/portal/desktop`.
- `clients/entra/` is unchanged in substance. It calls
  `org.freedesktop.portal.experimental.WebAuthentication` on
  `org.freedesktop.portal.Desktop` instead of a project-controlled name, and it now has to
  say something specific when that interface is absent — see "The gate is a normal
  outcome" below.
- `backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml` is a
  **verbatim copy** of the branch's file and must track it. The interface is not this
  repository's to change.
- `backend/data/webauth.portal` installs into the **real**
  `${datadir}/xdg-desktop-portal/portals`, because that is where the frontend looks and
  there is nowhere else it could find it.
- `service/frontend/` is deleted; `service/backends/gtk/` becomes `backend/`, mirroring
  the sibling repository's top-level `src/` + `data/` as closely as a second component
  allows.

## What this changes about the old "never advertise" rule

The previous layout installed its `.portal` file into `${datadir}/webauth-portal/portals`
and said, in the file itself, that an unaccepted prototype must not advertise itself to
the real xdg-desktop-portal, "which would neither know the interface nor be able to route
it". That rule was correct **for a frontend** shipping its own portals.conf search path.

We no longer ship a frontend, so the rule no longer has a subject. What is left is a
backend, and an out-of-tree backend that does not install into
`${datadir}/xdg-desktop-portal/portals` is a backend that can never be selected. Both
reference points do exactly this: `xdg-desktop-portal-gtk` installs `gtk.portal` there,
and `xdg-desktop-portal-termfilechooser` installs `termfilechooser.portal` there and tells
the user to name it in `portals.conf`.

The honest caveat, kept rather than dropped: **the interface named in that file is
experimental and gated upstream.** A stock xdg-desktop-portal has never heard of
`org.freedesktop.impl.portal.experimental.WebAuthentication`, will not match this file
against any interface it knows, and will ignore it. A frontend that does know it still
exports nothing unless it was started with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`. Installing the file is
therefore inert on a machine without the branch — which is what makes it safe, and is not
the same claim as "this is a supported portal".

## The gate is a normal outcome, and the client must say so

`entra-token-helper` already reported *unavailable* (exit `40`) when nothing implemented
the interface, so that a dispatcher could fall through to another provider. That path is
now the **common** one rather than the exotic one: a correctly installed portal, a
correctly installed backend and a correctly built client still produce "no such
interface" unless somebody set the environment variable.

So the exit-40 diagnostic must name `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` rather than
saying "portal unavailable". The three causes — gate off, no backend configured, no portal
at all — remain indistinguishable to a client, deliberately, because the frontend does not
export the interface in any of them.

## Consequences

- **[0008](0008-build-to-the-upstream-shape.md)'s split is preserved, not reversed.**
  Everything it argued for — that the app id is derived by a process the application
  cannot talk to and handed to the process that draws the window naming it, that
  alternative browsing mechanisms are alternative *backends* selected in `portals.conf`
  rather than a capability mask, that the frontend validates and the backend enforces —
  is exactly what this repository is now built against. What changed is who ships the
  other half.
- **0008's "Per-project bus names during incubation" section is moot**, and is marked as
  such there in one line rather than rewritten away. There is no incubating frontend to
  give a bus name to.
- **[0005](0005-service-shape.md) is amended in one line.** Everything it is about — URL
  in, completion out, no tokens, no accounts, no caching, the nine objections, the exit
  criterion — is untouched. Only its "Naming" section, which chose
  `io.github.sjtrotter.portal.WebAuthentication1`, is overtaken: the name is now the
  frontend branch's to choose.
- **[0007](0007-certificate-adapter.md) is amended.** The certificate adapter is now a
  client of the *public* `org.freedesktop.portal.experimental.Certificate` on
  `org.freedesktop.portal.Desktop` — and the interface it calls no longer has
  `OpenPkcs11Endpoint`, which changes what the `portal` adapter can actually do.
- **The delegation gap gets a cheap answer in-process.** 0007 and 0008 both said this
  backend naming the wrong application in the certificate portal's chooser is solved when
  both interfaces live in one trusted frontend process. They do now — the same
  xdg-desktop-portal, the same branch — so the frontend can hand the original app id to
  its own certificate side without anything crossing a bus. It does not do so yet; that is
  unwritten work on the branch.

  **An earlier version of this line said "and only in-process". That is wrong.** The rule
  is narrower and more useful: **never believe a caller about a third party's identity**.
  An app id read out of a message from a peer that could have put anything there is
  identity laundering and is not to be built. Delegation across a boundary is not itself
  forbidden — authenticated IPC, where the frontend derives each peer's identity itself,
  would satisfy the rule, and so would a capability the frontend issues to a named peer and
  later recognises. Neither is built. In-process is the cheapest way to satisfy the rule,
  not the only one.
- **The branch is ours until it is accepted.** Moving the frontend into xdg-desktop-portal's
  tree buys review in the right place and the reuse of `Request`, `Session`, app-id
  derivation and `.portal` discovery. It does **not** transfer maintenance: an unmerged
  branch is this author's to rebase, to keep green and to redesign when upstream asks, and
  upstream may redesign the interface rather than rename it. Any sentence in this
  repository that reads as though the frontend became somebody else's problem is describing
  the intended end state and not today.
- **One rule, two implementations, and one of them is now tested.** The completion matcher
  still exists twice — here, against live navigations, and in the frontend, re-checking
  what this backend returns. The frontend's copy has tests
  (`test_completion_mismatch_rejected`, `test_completion_normalisation_accepted`); this
  one has none, because it has no implementation. The shared fixture table in
  `tests/README.md` is still the mitigation.
- **The interface changed shape in the move.** `Start`'s signature did not, on either
  side — which is the strongest evidence 0008's shape argument was right. What did change
  is around it: `completion_uri` is specified as "absolute, with a host, no userinfo"
  rather than "https or an exactly named custom scheme"; the `reason` symbols are fixed by
  the XML; and the timeout ceiling and default are settled in code.
- **This repository is smaller and its claims are narrower.**
  [PUBLIC-INTERFACE.md](../PUBLIC-INTERFACE.md) is a pointer to the branch's XML with a
  summary, and the authority is the XML.
- **What remains before any of it could be a pull request** is in
  [UPSTREAMING.md](../UPSTREAMING.md): the "new portals" issue upstream asks for, the
  `Assisted-by:` trailer convention the branch write-up notes, a second unrelated
  consumer, and a second backend.
