# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

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
# The layout mirrors every out-of-tree backend: data/ holds the .portal file, the
# D-Bus service file and the interface XML, src/ holds one file per portal
# interface implemented.
#
# LIBDEX IS DELIBERATELY NOT A DEPENDENCY. The frontend branch uses it for its
# fibers; backends do not, and no released distribution ships it as a backend
# dependency. Everything here is plain GLib/GIO async on one thread, which is
# what xdg-desktop-portal-gtk does.

project(
  'xdg-desktop-portal-webauth',
  'c',
  version: '0.1.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')
gnome = import('gnome')

add_project_arguments(
  cc.get_supported_arguments([
    '-Wshadow',
    '-Wpointer-arith',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wwrite-strings',
    '-Wformat=2',
    '-Wno-unused-parameter',
    '-Wno-declaration-after-statement',
  ]),
  language: 'c',
)

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
# The backend is D-Bus activated and is not meant to be run from a shell, so it
# belongs in libexecdir -- as xdg-desktop-portal-gtk's backend does.
libexecdir = prefix / get_option('libexecdir')
dbus_service_dir = datadir / 'dbus-1' / 'services'
dbus_interfaces_dir = datadir / 'dbus-1' / 'interfaces'

# THE REAL DIRECTORY. Out-of-tree backends install their .portal file into
# ${datadir}/xdg-desktop-portal/portals -- that is where the frontend looks
# (desktop-portal/xdp-portal-config.c:364) and there is nowhere else it would
# find it. See data/webauth.portal.in for why that is safe while the interface
# is experimental and gated.
portal_dir = datadir / 'xdg-desktop-portal' / 'portals'

glib_dep = dependency('glib-2.0', version: '>= 2.66')
gobject_dep = dependency('gobject-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

# The engine, the window and the chrome. All three are REQUIRED: every method
# this backend implements opens a web view, and a backend that cannot must not
# claim the interface.
webkit_dep = dependency('webkitgtk-6.0', version: '>= 2.40')
gtk_dep = dependency('gtk4', version: '>= 4.10')
adw_dep = dependency('libadwaita-1', version: '>= 1.4')

# Parenting the window to the requesting application's. Both optional: a backend
# on a display server it was not built for draws an unparented window, which it
# has to be able to do anyway for an invalid handle.
gtk_wayland_dep = dependency('gtk4-wayland', required: false)
gtk_x11_dep = dependency('gtk4-x11', required: false)
x11_dep = dependency('x11', required: false)

# PKCS#11 is reached through GLib -- g_tls_certificate_new_from_pkcs11_uris()
# and the p11-kit configuration the process can see -- and this backend links
# against no PKCS#11 library of its own. It enumerates nothing, loads no module
# and never calls C_Login: that is the certificate portal's work, and on the
# pkcs11 provider it is GnuTLS's. See docs/decisions/0007-certificate-adapter.md.

config = configuration_data()
config.set_quoted('PACKAGE_VERSION', meson.project_version())
config.set('HAVE_GTK_WAYLAND', gtk_wayland_dep.found() ? 1 : 0)
config.set('HAVE_GTK_X11', (gtk_x11_dep.found() and x11_dep.found()) ? 1 : 0)
configure_file(output: 'config.h', configuration: config)

config_inc = include_directories('.')

subdir('data')
subdir('src')
subdir('tests')

summary(
  {
    'glib-2.0': glib_dep.version(),
    'gio-2.0': gio_dep.version(),
    'webkitgtk-6.0 (the web view)': webkit_dep.version(),
    'gtk4 (window and chrome)': gtk_dep.version(),
    'libadwaita-1': adw_dep.version(),
    'gtk4-wayland (xdg_foreign parenting)': gtk_wayland_dep.found() ? gtk_wayland_dep.version() : 'not found',
    'gtk4-x11 (XID parenting)': (gtk_x11_dep.found() and x11_dep.found()) ? gtk_x11_dep.version() : 'not found',
  },
  section: 'dependencies',
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
    'client certificates': 'providers: portal (not yet usable) and pkcs11',
  },
  section: 'interfaces',
)

summary(
  {
    'web view, chrome and interception': true,
    'client certificate from a PKCS#11 token': true,
    'certificate portal provider usable': false,
    'in-process certificate chooser or PIN prompt': false,
    'frontend branch merged upstream': false,
    'safe to depend on': false,
  },
  section: 'status',
  bool_yn: true,
)
