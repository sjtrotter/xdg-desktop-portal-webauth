# SPDX-License-Identifier: GPL-2.0-or-later

# entra-token-client: the Entra ID / AVD token client -- a CONSUMER of the portal.
#
# This is a COMPLETE, STANDALONE meson project. It can be built on its own:
#
#     meson setup build-entra clients/entra && ninja -C build-entra
#
# It depends on the web authentication portal only over D-Bus, at run time, and
# only for interactive sign-in. There is no build-time dependency in either
# direction, and there must never be one: the whole claim of the design is that
# the portal is protocol-independent.
#
# It calls xdg-desktop-portal's public interface. It has no knowledge of the
# impl interface, of .portal files, or of which backend runs -- exactly as a
# Flatpak application calling org.freedesktop.portal.FileChooser has none.

project(
  'entra-token-client',
  'c',
  version: '0.0.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=false',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments([
    '-Wshadow',
    '-Wpointer-arith',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wwrite-strings',
    '-Wformat=2',
  ]),
  language: 'c',
)

glib_dep = dependency('glib-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

secret_dep = dependency('libsecret-1', required: false)  # src/cache/keyring.h

executable(
  'entra-token-helper',
  sources: ['src/main.c'],
  dependencies: [glib_dep, gio_dep],
  install: true,
)

summary(
  {
    'glib-2.0 (required)': glib_dep.version(),
    'gio-2.0 (required)': gio_dep.version(),
    'libsecret-1 (refresh token storage)': secret_dep.found() ? secret_dep.version() : 'not found',
  },
  section: 'entra-token-client dependencies',
)

summary(
  {
    'portal (run time, D-Bus)': 'org.freedesktop.portal.experimental.WebAuthentication',
    'on bus name': 'org.freedesktop.portal.Desktop',
    'needs the portal started with': 'XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication',
    'if the gate is off': 'the interface is absent; the client exits 40 (unavailable)',
    'backend interface used': 'none: applications never call impl interfaces',
    'implemented': false,
  },
  section: 'entra-token-client status',
)
