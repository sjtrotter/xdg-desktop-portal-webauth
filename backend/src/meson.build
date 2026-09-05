# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# ONE gdbus-codegen run over both impl interfaces, with the common prefix
# stripped, so the generated symbols read the way upstream's do:
#
#   org.freedesktop.impl.portal.experimental.WebAuthentication
#       -> XdpImplExperimentalWebAuthentication
#   org.freedesktop.impl.portal.Request
#       -> XdpImplRequest
impl_dbus_sources = gnome.gdbus_codegen(
  'xdp-impl-dbus',
  sources: [
    '../data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml',
    '../data/org.freedesktop.impl.portal.Request.xml',
  ],
  interface_prefix: 'org.freedesktop.impl.portal.',
  namespace: 'XdpImpl',
)

# The parts with no window, no engine and no bus in them, so that the unit tests
# need neither a display nor a session bus to run. The completion rule is here
# because it is the one piece of this backend that a test can exhaust.
webauth_core_sources = files(
  'completion.c',
  'harden.c',
  'options.c',
  'redact.c',
  'storage.c',
)

webauth_service_sources = files(
  'chrome.c',
  'external-window.c',
  'request-impl.c',
  'transaction.c',
  'webauthentication-impl.c',
  'webkit-session.c',
  'tls/client_cert.c',
  'tls/client_cert_pkcs11.c',
  'tls/client_cert_portal.c',
)

webauth_deps = [glib_dep, gobject_dep, gio_dep]

# One log domain for everything this backend writes, so that --verbose can turn
# ON this backend's breadcrumbs without also turning on GIO's, dconf's and GDK's
# -- which is what "all" did, and it buried the six lines that matter.
webauth_c_args = ['-DG_LOG_DOMAIN="webauth"']

webauth_ui_deps = [gtk_dep, adw_dep, webkit_dep]
if gtk_wayland_dep.found()
  webauth_ui_deps += gtk_wayland_dep
endif
if gtk_x11_dep.found() and x11_dep.found()
  webauth_ui_deps += [gtk_x11_dep, x11_dep]
endif

webauth_core = static_library(
  'webauth-core',
  webauth_core_sources,
  c_args: webauth_c_args,
  dependencies: webauth_deps,
  include_directories: config_inc,
  install: false,
)

webauth_core_dep = declare_dependency(
  link_with: webauth_core,
  include_directories: [config_inc, include_directories('.')],
  dependencies: webauth_deps,
)

# The D-Bus and UI half, as a library rather than only as objects inside the
# binary, so that a test can stand the impl object up on a private bus and drive
# the real code rather than a copy of it.
webauth_service = static_library(
  'webauth-service',
  webauth_service_sources + impl_dbus_sources,
  c_args: webauth_c_args,
  link_with: webauth_core,
  dependencies: webauth_deps + webauth_ui_deps,
  include_directories: config_inc,
  install: false,
)

webauth_service_dep = declare_dependency(
  link_with: [webauth_service, webauth_core],
  # The generated header, so that a dependent compiles after codegen has run.
  sources: impl_dbus_sources[1],
  include_directories: [config_inc, include_directories('.')],
  dependencies: webauth_deps + webauth_ui_deps,
)

executable(
  'xdg-desktop-portal-webauth',
  ['main.c'],
  c_args: webauth_c_args,
  dependencies: [webauth_service_dep],
  include_directories: config_inc,
  install: true,
  install_dir: libexecdir,
)
