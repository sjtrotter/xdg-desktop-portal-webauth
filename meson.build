# SPDX-License-Identifier: GPL-2.0-or-later

# Umbrella build for the sketch.
#
# This repository holds three components that are deliberately independent:
#
#   service/frontend/      webauth-portal-frontend  the portal FRONTEND
#   service/backends/gtk/  webauth-portal-gtk       the reference BACKEND
#   clients/entra/         entra-token-client       a CONSUMER of the frontend
#
# The first two are the two halves of xdg-desktop-portal's frontend/backend
# architecture, built to that shape now so that acceptance upstream is a rename
# rather than a rewrite; see docs/decisions/0008-build-to-the-upstream-shape.md
# and docs/UPSTREAMING.md. The third is an application, and it talks only to the
# frontend.
#
# The smart card portal is a separate project in its own repository and is
# optional; see docs/decisions/0007-certificate-adapter.md.
#
# Each of the three is a complete meson project in its own right and can be
# configured on its own (see the comments at the top of each). This top-level
# file exists only so that one `meson setup build && ninja -C build` builds all
# three while they share a repository. It includes them through subprojects/,
# whose three entries are symlinks to the real directories, so nothing here is a
# build-time dependency between any of them.
#
# This file disappears when the components are split into separate repositories
# at the first tagged interface release; see docs/decisions/0006-two-repositories.md.

project(
  'webauth-portal-sketch',
  'c',
  version: '0.0.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.60.0',
)

subproject('webauth-portal-frontend')
subproject('webauth-portal-gtk')
subproject('entra-token-client')

summary(
  {
    'frontend': 'service/frontend/ (webauth-portal-frontend)',
    'backend': 'service/backends/gtk/ (webauth-portal-gtk)',
    'consumer': 'clients/entra/ (entra-token-helper)',
    'smart card portal': 'separate repository, optional',
    'build-time coupling': 'none, by design',
    'runtime coupling': 'D-Bus only: app -> frontend -> backend',
    'status': 'design sketch; nothing is implemented',
  },
  section: 'Repository',
)
