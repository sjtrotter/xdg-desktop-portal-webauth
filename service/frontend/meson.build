# SPDX-License-Identifier: GPL-2.0-or-later

# webauth-portal-frontend: the portal FRONTEND.
#
# This is a COMPLETE, STANDALONE meson project. It can be built on its own:
#
#     meson setup build-frontend service/frontend && ninja -C build-frontend
#
# That is deliberate, and it is now doing a second job. The frontend and the
# backend are separately buildable because they are separately DEPLOYABLE: a
# desktop may ship its own backend and none of this one, which is the property
# the frontend/backend split exists to have. And this directory is the one that
# disappears at upstream acceptance -- its contents become
# xdg-desktop-portal/desktop-portal/webauthentication.c -- so it must not
# accumulate anything the backend needs. See docs/UPSTREAMING.md and
# docs/decisions/0008-build-to-the-upstream-shape.md.

project(
  'webauth-portal-frontend',
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

# The frontend needs GLib and GIO and NOTHING ELSE, now and at any point in the
# future. No GTK, no WebKitGTK, no p11-kit: it draws nothing, it browses
# nothing, and it never touches a card. If a toolkit dependency ever appears
# here, the split has been violated -- the work belongs in a backend.
glib_dep = dependency('glib-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

executable(
  'webauth-portal-frontend',
  sources: ['src/main.c'],
  dependencies: [glib_dep, gio_dep],
  install: true,
  install_dir: get_option('libexecdir'),
)

# The incubating PUBLIC interface description, installed where D-Bus interface
# XML lives so it can be introspected and reviewed. This is NOT an accepted
# freedesktop interface.
install_data(
  'data/io.github.sjtrotter.portal.WebAuthentication1.xml',
  install_dir: get_option('datadir') / 'dbus-1' / 'interfaces',
)

service_conf = configuration_data()
service_conf.set('libexecdir', get_option('prefix') / get_option('libexecdir'))

configure_file(
  input: 'data/io.github.sjtrotter.portal.Desktop.service.in',
  output: 'io.github.sjtrotter.portal.Desktop.service',
  configuration: service_conf,
  install: true,
  install_dir: get_option('datadir') / 'dbus-1' / 'services',
)

summary(
  {
    'glib-2.0 (required)': glib_dep.version(),
    'gio-2.0 (required)': gio_dep.version(),
    'toolkit dependencies': 'none, by design (the backend has them)',
  },
  section: 'frontend dependencies',
)

summary(
  {
    'bus name': 'io.github.sjtrotter.portal.Desktop',
    'object path': '/io/github/sjtrotter/portal/desktop',
    'public interface': 'io.github.sjtrotter.portal.WebAuthentication1',
    'backend interface': 'io.github.sjtrotter.impl.portal.WebAuthentication1',
    'backend discovery': '$XDG_DATA_DIRS/webauth-portal/portals/*.portal',
    'backend preference': 'webauth-portal/portals.conf, [preferred]',
    'interface version': '1 (incubating; not an xdg-desktop-portal interface)',
    'upstream shape': 'xdg-desktop-portal frontend (desktop-portal/*.c)',
  },
  section: 'frontend interface',
)

summary(
  {
    'implemented': false,
    'bus name is a singleton stand-in for org.freedesktop.portal.Desktop': true,
  },
  section: 'frontend status',
  bool_yn: true,
)
