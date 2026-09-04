# SPDX-License-Identifier: GPL-2.0-or-later

# Umbrella build for the sketch.
#
# This repository holds two components that are deliberately independent:
#
#   service/        webauth-service     layer 2, the web authentication service
#   clients/entra/  entra-token-client  layer 3, the Entra ID / AVD token client
#
# Layer 1, the smart card service, is a separate project in its own repository and
# is optional; see docs/decisions/0007-certificate-adapter.md.
#
# Each is a complete meson project in its own right and can be configured on its own
# (see the comments at the top of each). This top-level file exists only so that one
# `meson setup build && ninja -C build` builds both while they share a repository. It
# includes them through subprojects/, whose two entries are symlinks to the real
# directories, so nothing here is a build-time dependency between the two.
#
# This file disappears when the components are split into separate repositories at the
# first tagged interface release; see docs/decisions/0006-two-repositories.md.

project(
  'webauth-service-sketch',
  'c',
  version: '0.0.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.60.0',
)

subproject('webauth-service')
subproject('entra-token-client')

summary(
  {
    'layer 2': 'service/ (webauth-service)',
    'layer 3': 'clients/entra/ (entra-token-client)',
    'layer 1': 'smartcard-portal (separate repository, optional)',
    'build-time coupling': 'none, by design',
    'status': 'design sketch; neither component is implemented',
  },
  section: 'Repository',
)
