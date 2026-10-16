# Upstreaming

The WebAuthentication frontend is on
[`experimental/integration`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration)
in the author's [xdg-desktop-portal fork](https://github.com/sjtrotter/xdg-desktop-portal).
Its remotes are [upstream](https://github.com/flatpak/xdg-desktop-portal.git) and
[origin](https://github.com/sjtrotter/xdg-desktop-portal.git).

The upstream experimental-portal mechanism includes PR #2129 and its fix, #2173.
WebAuthentication and Certificate remain experimental interfaces on this branch, without a
claim of acceptance or stability. Their XML is in
`data/org.freedesktop.portal.experimental.WebAuthentication1.xml` and
`data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml`.
The branch's `doc/experimental-portals.rst` describes the convention. The
[public guide](PUBLIC-INTERFACE.md) summarizes naming, export and versioning.

The recorded upstream contact consists of comments from 2026-09-05,
flatpak/xdg-desktop-portal#662 and FreeRDP#13328. No WebAuthentication design issue or pull
request is recorded locally. The author maintains the frontend branch until upstream acceptance.

[ADR 0010](decisions/0010-backend-only-frontend-lives-upstream.md) records frontend ownership;
[ADR 0008](decisions/0008-build-to-the-upstream-shape.md) records the split.

## Repository integration

This repository supplies a D-Bus-activated backend, its `.portal` declaration and impl XML copy.
The [architecture](ARCHITECTURE.md) lists source components and the
[impl guide](IMPL-INTERFACE.md) covers installation and backend selection.
Use xdg-desktop-portal's pkg-config interfaces directory when a release provides the XML.

[The enforcement table](IMPL-INTERFACE.md#enforcement) assigns frontend and backend duties.
[Shared completion fixtures](../tests/README.md#completion-fixtures) must agree between projects.

Certificate CreateSession returns a Request; its Response carries the session handle.
GetCapabilities has no `pkcs11_endpoint` key. Coordinate the
[module contract](SECURITY.md#portal-provider) and [WebKit transport](SPIKES.md#pin-and-module-constraints)
between the backends.

## Proposal requirements

The [proposal requirements](ROADMAP.md#consumers-and-proposal) include independent consumers
and backends, a design discussion, conformance tests and review. The proposed protected resource
is a trusted desktop authentication user-agent, persistent sign-in state and client-certificate
capability. Test across the GLib/WebKit/GnuTLS support matrix.

The branch requires `Assisted-by:` trailers for coding-agent contributions, following its
`.gitlint.conf/co-authored-by-coding-agent.py` policy. Validate its own test suites, gitlint and
Black checks in that checkout; this backend's local tests do not establish frontend conformance.
From the configured frontend checkout:

```console
$ meson test -C build --suite integration --suite unit
$ gitlint --commits upstream/main..HEAD
$ black --check tests/test_webauthentication.py tests/templates/webauthentication.py
```

The recorded frontend integration/unit run excluded a failing USB test because umockdev-run
was unavailable. That result does not establish a passing full frontend suite.

## Stable interface transition

If a stable `org.freedesktop.portal.WebAuthentication` is accepted, retire the experimental
interface without an alias. Coordinate the `.portal` declaration, activation metadata and client
constants. This project has no released deployment requiring a compatibility period.
