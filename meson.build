# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>


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
libexecdir = prefix / get_option('libexecdir')
dbus_service_dir = datadir / 'dbus-1' / 'services'
dbus_interfaces_dir = datadir / 'dbus-1' / 'interfaces'

# The frontend discovers backend declarations in this directory.
portal_dir = datadir / 'xdg-desktop-portal' / 'portals'

glib_dep = dependency('glib-2.0', version: '>= 2.66')
gobject_dep = dependency('gobject-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

webkit_dep = dependency('webkitgtk-6.0', version: '>= 2.40')
gtk_dep = dependency('gtk4', version: '>= 4.10')
adw_dep = dependency('libadwaita-1', version: '>= 1.4')

# Unavailable parenting support falls back to an unparented window.
gtk_wayland_dep = dependency('gtk4-wayland', required: false)
gtk_x11_dep = dependency('gtk4-x11', required: false)
x11_dep = dependency('x11', required: false)

# p11-kit supplies only its module configuration directory; PKCS#11 access uses GLib.
# See docs/decisions/0007-certificate-adapter.md.
p11kit_dep = dependency('p11-kit-1', version: '>= 0.23', required: false)
p11kit_module_configs = datadir / 'p11-kit' / 'modules'
if p11kit_dep.found()
  p11kit_module_configs = p11kit_dep.get_variable(
    pkgconfig: 'p11_module_configs',
    default_value: p11kit_module_configs,
  )
endif

config = configuration_data()
config.set_quoted('PACKAGE_VERSION', meson.project_version())
config.set('HAVE_GTK_WAYLAND', gtk_wayland_dep.found() ? 1 : 0)
config.set('HAVE_GTK_X11', (gtk_x11_dep.found() and x11_dep.found()) ? 1 : 0)
config.set_quoted('P11_KIT_MODULE_CONFIGS', p11kit_module_configs)
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
    'impl interface': 'org.freedesktop.impl.portal.experimental.WebAuthentication1',
    'object path': '/org/freedesktop/portal/desktop',
    'portal file': portal_dir / 'webauth.portal',
    'frontend': 'xdg-desktop-portal, branch experimental/integration',
    'public interface (frontend)': 'org.freedesktop.portal.experimental.WebAuthentication1',
    'public object path (frontend)': '/org/freedesktop/portal/desktop',
    'frontend environment': 'XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=WebAuthentication1',
    'certificate frontend opt-in': 'append Certificate1 to XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL',
    'called by': 'xdg-desktop-portal only, never by applications',
    'client certificates': 'providers: portal (primary) and pkcs11 (fallback)',
    'p11-kit module configuration read from': p11kit_module_configs,
  },
  section: 'interfaces',
)

summary(
  {
    'web view, chrome and interception': true,
    'client certificate from a PKCS#11 token': true,
    'certificate portal provider usable': true,
    'in-process certificate chooser or PIN prompt': false,
    'frontend branch merged upstream': false,
    'safe to depend on': false,
  },
  section: 'status',
  bool_yn: true,
)
