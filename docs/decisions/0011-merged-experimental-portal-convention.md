# 11. Follow the merged experimental-portal convention

Date: 2026-10-15
Status: accepted; supersedes the experimental naming and object-path decisions in
[0005](0005-service-shape.md), [0007](0007-certificate-adapter.md) and
[0010](0010-backend-only-frontend-lives-upstream.md)

## Context

xdg-desktop-portal merged experimental-portal support in PR #2129, fixed by #2173
(upstream main `8eba842`). The `experimental/integration` frontend branch follows that
convention. Its `doc/experimental-portals.rst` and impl XML define the contract for this backend.

## Decision

Implement `org.freedesktop.impl.portal.experimental.WebAuthentication1` at
`/org/freedesktop/portal/desktop`, using a copy of the frontend branch's impl
XML that is byte for byte apart from its provenance comment block. Applications call `org.freedesktop.portal.experimental.WebAuthentication1` on
`org.freedesktop.portal.Desktop` at the same path.

The certificate adapter follows the Certificate portal rename and calls
`org.freedesktop.portal.experimental.Certificate1` at the standard portal path.
Request and Session objects retain their handle paths.

## Consequences

The frontend must start with `WebAuthentication1` in
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` and a configured backend. Runs using the Certificate
backend also need `Certificate1`. The variable belongs in the frontend's environment and
contains comma-separated names with no spaces. Development launchers preserve caller-supplied
names and append only missing names.

A breaking change bumps the trailing interface number; a non-breaking change increments the
`version` property. The interfaces remain experimental.

The `entra-token-helper` client follows in step in its own repository. This change does not edit
that repository or the frontend checkout.
