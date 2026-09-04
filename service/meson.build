# SPDX-License-Identifier: GPL-2.0-or-later

# webauth-service: layer 1, the interactive web authentication service.
#
# This is a COMPLETE, STANDALONE meson project. It can be built on its own:
#
#     meson setup build-service service && ninja -C build-service
#
# That is deliberate. This directory is intended to become its own repository at the
# first tagged interface release (docs/decisions/0006-two-repositories.md), and being
# separately buildable today is how that claim stays honest.

project(
  'webauth-service',
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

# The stub needs only GLib and GIO. The rest is declared so the dependency picture is
# visible in the summary, and is unused: none of the components those libraries would
# serve exist yet.
glib_dep = dependency('glib-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

webkit_dep = dependency('webkitgtk-6.0', required: false)  # src/webkit_session.h
gtk_dep = dependency('gtk4', required: false)              # src/chrome.h
# Needed by the in-process certificate adapter (enumeration, and resolving PKCS#11 URIs
# against the system configuration) and, if that path is ever proven, by the portal
# adapter's module transport. See src/tls/client_cert.h and
# docs/decisions/0007-certificate-adapter.md.
p11kit_dep = dependency('p11-kit-1', required: false)      # src/tls/

have_webkit_session = webkit_dep.found() and gtk_dep.found()

executable(
  'webauth-service',
  sources: ['src/main.c'],
  dependencies: [glib_dep, gio_dep],
  install: true,
)

# The incubating interface description, installed where D-Bus interface XML lives so
# it can be introspected and reviewed. This is NOT an accepted freedesktop interface.
install_data(
  'data/io.github.sjtrotter.WebAuthentication1.xml',
  install_dir: get_option('datadir') / 'dbus-1' / 'interfaces',
)

summary(
  {
    'glib-2.0 (required)': glib_dep.version(),
    'gio-2.0 (required)': gio_dep.version(),
    'webkitgtk-6.0 (browser session)': webkit_dep.found() ? webkit_dep.version() : 'not found',
    'gtk4 (window and security chrome)': gtk_dep.found() ? gtk_dep.version() : 'not found',
    'p11-kit-1 (certificate adapters)': p11kit_dep.found() ? p11kit_dep.version() : 'not found',
  },
  section: 'webauth-service dependencies',
)

summary(
  {
    'bus name': 'io.github.sjtrotter.WebAuthentication1',
    'interface version': '1 (incubating, not a freedesktop portal)',
    'client certificates': 'adapter: portal (io.github.sjtrotter.Smartcard1) or in-process',
  },
  section: 'webauth-service interface',
)

summary(
  {
    'WebKitGTK browser session possible': have_webkit_session,
    'implemented': false,
  },
  section: 'webauth-service status',
  bool_yn: true,
)
