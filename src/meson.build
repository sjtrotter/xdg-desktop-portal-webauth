# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

impl_dbus_sources = gnome.gdbus_codegen(
  'xdp-impl-dbus',
  sources: [
    '../data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml',
    '../data/org.freedesktop.impl.portal.Request.xml',
  ],
  interface_prefix: 'org.freedesktop.impl.portal.',
  namespace: 'XdpImpl',
)

# Core tests require neither a display nor a session bus.
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

# A separate log domain lets verbose mode enable only backend messages.
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

# The service library lets integration tests exercise the impl object on a private bus.
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
  # Expose the generated header dependency so consumers compile after code generation.
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
