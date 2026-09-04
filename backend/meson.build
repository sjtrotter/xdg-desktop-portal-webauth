# SPDX-License-Identifier: GPL-2.0-or-later

# xdg-desktop-portal-webauth: an out-of-tree BACKEND for the experimental
# org.freedesktop.impl.portal.experimental.WebAuthentication portal interface.
#
# This is a COMPLETE, STANDALONE meson project. It can be built on its own:
#
#     meson setup build-backend backend && ninja -C build-backend
#
# It has no build-time dependency on anything else in this repository and must
# never acquire one. The frontend is xdg-desktop-portal itself (branch
# experimental/certificate-webauthentication) and the two speak D-Bus; the Entra
# client in clients/ is an ordinary application that calls the portal.
#
# The layout mirrors every out-of-tree backend: data/ holds the .portal file,
# the D-Bus service file and the interface XML, src/ holds one file per portal
# interface implemented.
#
# There is no frontend here any more; see
# docs/decisions/0010-backend-only-frontend-lives-upstream.md.

project(
  'xdg-desktop-portal-webauth',
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

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
libexecdir = prefix / get_option('libexecdir')

# THE REAL DIRECTORY. Out-of-tree backends install their .portal file into
# ${datadir}/xdg-desktop-portal/portals -- that is where the frontend looks
# (desktop-portal/xdp-portal-config.c:364) and there is nowhere else it would
# find it. See data/webauth.portal.in for why that is safe while the interface
# is experimental and gated.
portal_dir = datadir / 'xdg-desktop-portal' / 'portals'

# The stub needs only GLib and GIO. The rest is declared so the dependency
# picture is visible in the summary, and is unused: none of the components those
# libraries would serve exist yet.
glib_dep = dependency('glib-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

webkit_dep = dependency('webkitgtk-6.0', required: false)  # src/webkit_session.h
gtk_dep = dependency('gtk4', required: false)              # src/chrome.h, src/externalwindow.h
# Needed by the in-process certificate adapter (enumeration, and resolving PKCS#11
# URIs against the system configuration). See src/tls/client_cert.h and
# docs/decisions/0007-certificate-adapter.md.
p11kit_dep = dependency('p11-kit-1', required: false)      # src/tls/

have_webkit_session = webkit_dep.found() and gtk_dep.found()

executable(
  'xdg-desktop-portal-webauth',
  sources: ['src/main.c'],
  dependencies: [glib_dep, gio_dep],
  install: true,
  install_dir: libexecdir,
)

# The impl interface description. It is a VERBATIM COPY of the xdg-desktop-portal
# branch's file and must track it; the header in the file says so. Upstream keeps
# these in xdg-desktop-portal and backends read them from its pkg-config
# interfaces directory -- which is where this one comes from the day the branch
# lands. See docs/IMPL-INTERFACE.md.
install_data(
  'data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml',
  install_dir: datadir / 'dbus-1' / 'interfaces',
)

# Upstream's gtk.portal.in substitutes optional interfaces into the Interfaces=
# list at configure time; this backend implements exactly one interface
# unconditionally, so the file is copied rather than templated. It keeps the .in
# suffix because the moment a second interface here becomes optional it becomes
# a template, and renaming the file then would be noise in that diff.
configure_file(
  input: 'data/webauth.portal.in',
  output: 'webauth.portal',
  copy: true,
  install: true,
  install_dir: portal_dir,
)

configure_file(
  input: 'data/org.freedesktop.impl.portal.desktop.webauth.service.in',
  output: 'org.freedesktop.impl.portal.desktop.webauth.service',
  configuration: {'libexecdir': libexecdir},
  install: true,
  install_dir: datadir / 'dbus-1' / 'services',
)

summary(
  {
    'glib-2.0 (required)': glib_dep.version(),
    'gio-2.0 (required)': gio_dep.version(),
    'webkitgtk-6.0 (web view)': webkit_dep.found() ? webkit_dep.version() : 'not found',
    'gtk4 (window, chrome, parenting)': gtk_dep.found() ? gtk_dep.version() : 'not found',
    'p11-kit-1 (certificate adapters)': p11kit_dep.found() ? p11kit_dep.version() : 'not found',
  },
  section: 'backend dependencies',
)

summary(
  {
    'backend bus name': 'org.freedesktop.impl.portal.desktop.webauth',
    'impl interface': 'org.freedesktop.impl.portal.experimental.WebAuthentication',
    'object path': '/org/freedesktop/portal/desktop',
    'portal file': portal_dir / 'webauth.portal',
    'frontend': 'xdg-desktop-portal, branch experimental/certificate-webauthentication',
    'public interface (frontend)': 'org.freedesktop.portal.experimental.WebAuthentication',
    'frontend gate': 'XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication',
    'called by': 'xdg-desktop-portal only, never by applications',
    'client certificates': 'adapter: portal (org.freedesktop.portal.experimental.Certificate) or in-process',
  },
  section: 'backend interface',
)

summary(
  {
    'WebKitGTK browser session possible': have_webkit_session,
    'implemented': false,
    'frontend branch merged upstream': false,
  },
  section: 'backend status',
  bool_yn: true,
)
