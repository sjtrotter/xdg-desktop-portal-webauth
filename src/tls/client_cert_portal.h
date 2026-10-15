/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_TLS_CLIENT_CERT_PORTAL_H
#define WEBAUTH_TLS_CLIENT_CERT_PORTAL_H

#include <gio/gio.h>

/** The portal exposes credentials through PKCS#11; see docs/decisions/0007-certificate-adapter.md. */

/** Checks the Certificate portal interface without D-Bus activation. */
gboolean webauth_cert_portal_interface_present(GError** error);

/** Returns a newly allocated config path from the user, /etc, or p11-kit module directory, or NULL. */
char* webauth_cert_portal_module_config_path(void);

#endif /* WEBAUTH_TLS_CLIENT_CERT_PORTAL_H */
