# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# Interface XML must track experimental/integration verbatim; see docs/IMPL-INTERFACE.md.
install_data(
  [
    'org.freedesktop.impl.portal.experimental.WebAuthentication1.xml',
    'org.freedesktop.impl.portal.Request.xml',
  ],
  install_dir: dbus_interfaces_dir,
)

configure_file(
  input: 'webauth.portal.in',
  output: 'webauth.portal',
  copy: true,
  install: true,
  install_dir: portal_dir,
)

configure_file(
  input: 'org.freedesktop.impl.portal.desktop.webauth.service.in',
  output: 'org.freedesktop.impl.portal.desktop.webauth.service',
  configuration: {'libexecdir': libexecdir},
  install: true,
  install_dir: dbus_service_dir,
)
