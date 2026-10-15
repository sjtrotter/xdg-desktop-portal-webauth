/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "config.h"

#include "client_cert_portal.h"

#include "../harden.h"
#include "../redact.h"
#include "client_cert.h"
#include "portal-token.h"

#ifndef P11_KIT_MODULE_CONFIGS
#define P11_KIT_MODULE_CONFIGS "/usr/share/p11-kit/modules"
#endif

#define CERTIFICATE_PUBLIC_INTERFACE "org.freedesktop.portal.experimental.Certificate1"
#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"

gboolean webauth_cert_portal_interface_present(GError** error)
{
	g_autoptr(GDBusConnection) bus = NULL;
	g_autoptr(GVariant) reply = NULL;
	g_autoptr(GError) inner = NULL;

	bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &inner);
	if (bus == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Session bus unavailable");
		return FALSE;
	}

	reply = g_dbus_connection_call_sync(
	    bus, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, "org.freedesktop.DBus.Properties", "Get",
	    g_variant_new("(ss)", CERTIFICATE_PUBLIC_INTERFACE, "version"), G_VARIANT_TYPE("(v)"),
	    G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL, &inner);

	if (reply == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "Certificate portal interface unavailable; check "
		                    "XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL includes Certificate1 "
		                    "in the frontend environment");
		return FALSE;
	}

	return TRUE;
}

/* WebKit's network process inherits XDG_CONFIG_HOME and resolves the same p11-kit configuration. */
char* webauth_cert_portal_module_config_path(void)
{
	const char* config_home = g_get_user_config_dir();
	const char* dirs[] = { NULL, "/etc/pkcs11/modules", P11_KIT_MODULE_CONFIGS };
	g_autofree char* user_dir = g_build_filename(config_home, "pkcs11", "modules", NULL);
	gsize i;

	dirs[0] = user_dir;

	for (i = 0; i < G_N_ELEMENTS(dirs); i++)
	{
		char* candidate =
		    g_build_filename(dirs[i], XDG_PORTAL_CERTIFICATE_MODULE_CONFIG, NULL);

		if (g_file_test(candidate, G_FILE_TEST_EXISTS))
			return candidate;

		g_free(candidate);
	}

	return NULL;
}

static gboolean portal_available(GError** error)
{
	g_autofree char* module_config = NULL;

	if (!webauth_cert_portal_interface_present(error))
		return FALSE;

	/* Availability checks module configuration presence; load errors surface during TLS. */
	module_config = webauth_cert_portal_module_config_path();
	if (module_config == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "Certificate portal module configuration missing: " XDG_PORTAL_CERTIFICATE_MODULE_CONFIG);
		return FALSE;
	}

	return TRUE;
}

static GTlsCertificate* portal_acquire(const WebAuthCertChallenge* challenge, GError** error)
{
	GTlsCertificate* certificate = NULL;
	g_autoptr(GError) inner = NULL;

	webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_CERT_CHALLENGE, "provider",
	                  WEBAUTH_FIELD_OUTCOME, "portal", "host", WEBAUTH_FIELD_HOST,
	                  challenge->origin, NULL);

	/* The UI and network processes acquire separate portal grants; this constructor needs /proc identification access; see docs/SECURITY.md. */
	webauth_harden_identifiable_begin();

	/* GnuTLS single-object import requires an object selector; see portal-token.h. */
	certificate = g_tls_certificate_new_from_pkcs11_uris(XDG_PORTAL_CERTIFICATE_CERT_URI,
	                                                     XDG_PORTAL_CERTIFICATE_KEY_URI, &inner);

	webauth_harden_identifiable_end();

	if (certificate == NULL)
	{
		g_autofree char* safe = webauth_redact_error_text(inner->message);

		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", safe);
		return NULL;
	}

	return certificate;
}

/* The protected authentication path prompts for the PIN in the certificate portal. */
static const char* portal_pin(void)
{
	return NULL;
}

/* Module grants and pooled TLS connections can outlive the transaction; see docs/SECURITY.md. */
static void portal_release(void)
{
	webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_CERT_RELEASED, "provider",
	                  WEBAUTH_FIELD_OUTCOME, "portal", "grant", WEBAUTH_FIELD_OUTCOME,
	                  "retained_until_expiry", NULL);
}

static const WebAuthCertAdapter portal_adapter = {
	.name = "portal",
	.available = portal_available,
	.acquire = portal_acquire,
	.pin = portal_pin,
	.release = portal_release,
};

const WebAuthCertAdapter* webauth_cert_adapter_portal(void)
{
	return &portal_adapter;
}
