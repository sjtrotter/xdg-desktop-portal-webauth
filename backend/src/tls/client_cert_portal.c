/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "client_cert_portal.h"

#include "../redact.h"
#include "client_cert.h"
#include "portal-token.h"

#define CERTIFICATE_PUBLIC_INTERFACE "org.freedesktop.portal.experimental.Certificate"
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
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no session bus");
		return FALSE;
	}

	reply = g_dbus_connection_call_sync(
	    bus, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, "org.freedesktop.DBus.Properties", "Get",
	    g_variant_new("(ss)", CERTIFICATE_PUBLIC_INTERFACE, "version"), G_VARIANT_TYPE("(v)"),
	    G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, NULL, &inner);

	if (reply == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "the Certificate portal interface is not exported");
		return FALSE;
	}

	return TRUE;
}

static gboolean portal_available(GError** error)
{
	if (!webauth_cert_portal_interface_present(error))
		return FALSE;

	/* THE HALF THAT DOES NOT EXIST. The interface being exported says the
	 * certificate portal is running; it does not say its PKCS#11 module is
	 * installed, and until that module exists this provider cannot produce a
	 * certificate. Reporting unavailable here is what makes "auto" fall through
	 * to the pkcs11 provider on a machine that has the portal but not the
	 * module, instead of failing in the middle of a handshake. */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	                    "the Certificate portal's PKCS#11 module (" XDG_PORTAL_CERTIFICATE_MODULE_CONFIG
	                    ") is not implemented yet");
	return FALSE;
}

static GTlsCertificate* portal_acquire(const WebAuthCertChallenge* challenge, GError** error)
{
	GTlsCertificate* certificate = NULL;
	g_autoptr(GError) inner = NULL;

	webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_CERT_CHALLENGE, "provider",
	                  WEBAUTH_FIELD_OUTCOME, "portal", "host", WEBAUTH_FIELD_HOST,
	                  challenge->origin, NULL);

	certificate = g_tls_certificate_new_from_pkcs11_uris(XDG_PORTAL_CERTIFICATE_CERT_URI,
	                                                     XDG_PORTAL_CERTIFICATE_KEY_URI, &inner);
	if (certificate == NULL)
	{
		g_autofree char* safe = webauth_redact_error_text(inner->message);

		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", safe);
		return NULL;
	}

	return certificate;
}

/* The portal's token declares a protected authentication path: the PIN is typed
 * in the certificate portal's own window and never enters this process. */
static const char* portal_pin(void)
{
	return NULL;
}

static void portal_release(void)
{
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
