# SPDX-License-Identifier: GPL-2.0-or-later

# entra-token-client: layer 2, the Entra ID / AVD token client.
#
# This is a COMPLETE, STANDALONE meson project. It can be built on its own:
#
#     meson setup build-entra clients/entra && ninja -C build-entra
#
# It depends on layer 1 only over D-Bus, at run time, and only for interactive
# sign-in. There is no build-time dependency between the two in either direction, and
# there must never be one: the whole claim of the two-layer design is that the
# authentication service is protocol-independent.

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
    'web auth service (run time, D-Bus)': 'io.github.sjtrotter.WebAuthentication1',
    'implemented': false,
  },
  section: 'entra-token-client status',
)
