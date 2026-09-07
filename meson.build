# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# Umbrella build for the sketch.
#
# This repository holds two components that are deliberately independent:
#
#   backend/         xdg-desktop-portal-webauth   an out-of-tree portal BACKEND
#   clients/entra/   entra-token-helper           a CONSUMER of the portal
#
# There is no frontend here. The portal frontend -- app id derivation, URI
# validation, option filtering, policy, the Request object and the guarantee of
# exactly one Response, and the re-check of the completion URI the backend
# returns -- is a branch of xdg-desktop-portal itself:
# experimental/certificate-webauthentication, 10 commits on upstream 86bd3e2
# (22818e6..1aaffaf). See docs/decisions/0010-backend-only-frontend-lives-upstream.md and
# docs/UPSTREAMING.md.
#
# The certificate portal's backend is a separate project in its own repository
# and is optional; see docs/decisions/0007-certificate-adapter.md.
#
# Each of the two is a complete meson project in its own right and can be
# configured on its own (see the comments at the top of each). This top-level
# file exists only so that one `meson setup build && ninja -C build` builds both
# while they share a repository. It includes them through subprojects/, whose
# entries are symlinks to the real directories, so nothing here is a build-time
# dependency between them.
#
# This file disappears when the components are split into separate repositories
# at the first tagged interface release; see docs/decisions/0006-two-repositories.md.

project(
  'xdg-desktop-portal-webauth',
  'c',
  version: '0.0.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.60.0',
)

subproject('xdg-desktop-portal-webauth')
subproject('entra-token-client')

summary(
  {
    'backend': 'backend/ (xdg-desktop-portal-webauth)',
    'consumer': 'clients/entra/ (entra-token-helper)',
    'frontend': 'xdg-desktop-portal, branch experimental/certificate-webauthentication',
    'certificate portal backend': 'separate repository, optional',
    'build-time coupling': 'none, by design',
    'runtime coupling': 'D-Bus only: app -> xdg-desktop-portal -> backend',
    'status': 'experimental; live-tested 2026-09-05',
  },
  section: 'Repository',
)
