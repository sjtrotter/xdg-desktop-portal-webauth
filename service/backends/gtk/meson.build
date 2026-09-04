# SPDX-License-Identifier: GPL-2.0-or-later

# webauth-portal-gtk: the reference BACKEND.
#
# This is a COMPLETE, STANDALONE meson project. It can be built on its own:
#
#     meson setup build-gtk service/backends/gtk && ninja -C build-gtk
#
# It has no build-time dependency on the frontend and must never acquire one:
# the two speak D-Bus, and the whole point of the split is that a desktop can
# ship a different backend, or three, without rebuilding anything. The only
# thing they share is the impl interface description, and a backend for another
# toolkit would copy it rather than link against this directory.
#
# The layout mirrors xdg-desktop-portal-gtk: data/ holds the .portal file and
# the D-Bus service file, src/ holds one file per portal interface implemented.

project(
  'webauth-portal-gtk',
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

# The stub needs only GLib and GIO. The rest is declared so the dependency
# picture is visible in the summary, and is unused: none of the components those
# libraries would serve exist yet. Note that all of them are HERE and none of
# them are in the frontend, which is the split doing its job.
glib_dep = dependency('glib-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

webkit_dep = dependency('webkitgtk-6.0', required: false)  # src/webkit_session.h
gtk_dep = dependency('gtk4', required: false)              # src/chrome.h, src/externalwindow.h
# Needed by the in-process certificate adapter (enumeration, and resolving PKCS#11
# URIs against the system configuration) and, if that path is ever proven, by the
# portal adapter's module transport. See src/tls/client_cert.h and
# docs/decisions/0007-certificate-adapter.md.
p11kit_dep = dependency('p11-kit-1', required: false)      # src/tls/

have_webkit_session = webkit_dep.found() and gtk_dep.found()

executable(
  'webauth-portal-gtk',
  sources: ['src/main.c'],
  dependencies: [glib_dep, gio_dep],
  install: true,
  install_dir: get_option('libexecdir'),
)

# The incubating BACKEND interface description. Installed alongside the public
# one so that both halves of the contract can be introspected and reviewed, and
# so that a second backend has something to implement against.
install_data(
  'data/io.github.sjtrotter.impl.portal.WebAuthentication1.xml',
  install_dir: get_option('datadir') / 'dbus-1' / 'interfaces',
)

# The .portal file goes in THIS project's directory, not xdg-desktop-portal's:
# an unaccepted prototype must not advertise itself to the real portal.
#
# Upstream's gtk.portal.in substitutes the optional interfaces into the
# Interfaces= list at configure time; this backend implements exactly one
# interface unconditionally, so the file is copied rather than templated. It
# keeps the .in suffix because the moment a second interface here becomes
# optional -- which is how a second portal in this backend would arrive -- it
# becomes a template, and renaming the file then would be noise in that diff.
configure_file(
  input: 'data/webauth-gtk.portal.in',
  output: 'webauth-gtk.portal',
  copy: true,
  install: true,
  install_dir: get_option('datadir') / 'webauth-portal' / 'portals',
)

service_conf = configuration_data()
service_conf.set('libexecdir', get_option('prefix') / get_option('libexecdir'))

configure_file(
  input: 'data/io.github.sjtrotter.impl.portal.WebAuthentication.gtk.service.in',
  output: 'io.github.sjtrotter.impl.portal.WebAuthentication.gtk.service',
  configuration: service_conf,
  install: true,
  install_dir: get_option('datadir') / 'dbus-1' / 'services',
)

summary(
  {
    'glib-2.0 (required)': glib_dep.version(),
    'gio-2.0 (required)': gio_dep.version(),
    'webkitgtk-6.0 (web view)': webkit_dep.found() ? webkit_dep.version() : 'not found',
    'gtk4 (window, chrome, parenting)': gtk_dep.found() ? gtk_dep.version() : 'not found',
    'p11-kit-1 (certificate adapters)': p11kit_dep.found() ? p11kit_dep.version() : 'not found',
  },
  section: 'gtk backend dependencies',
)

summary(
  {
    'bus name': 'io.github.sjtrotter.impl.portal.WebAuthentication.gtk',
    'object path': '/io/github/sjtrotter/portal/WebAuthentication',
    'implements': 'io.github.sjtrotter.impl.portal.WebAuthentication1',
    'declared in': 'datadir/webauth-portal/portals/webauth-gtk.portal',
    'called by': 'the portal frontend only, never by applications',
    'client certificates': 'adapter: portal (io.github.sjtrotter.portal.Certificate1) or in-process',
    'upstream shape': 'xdg-desktop-portal-gtk (data/gtk.portal, src/<name>.c)',
  },
  section: 'gtk backend interface',
)

summary(
  {
    'WebKitGTK browser session possible': have_webkit_session,
    'implemented': false,
  },
  section: 'gtk backend status',
  bool_yn: true,
)
