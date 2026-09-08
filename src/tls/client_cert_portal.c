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

#define CERTIFICATE_PUBLIC_INTERFACE "org.freedesktop.portal.Certificate.X1"
#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop/experimental"

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

/* The directories p11-kit reads module configuration from, in its own order.
 * Checking for a FILE is the whole of the check: this backend links no PKCS#11
 * library, loads no module and asks p11-kit nothing. A file that is there and
 * names a module that will not load is a handshake failure and not a start-up
 * one, which is the same trade the pkcs11 provider makes with its URI.
 *
 * THE PROCESS THAT ACTUALLY LOADS THE MODULE IS NOT THIS ONE. The URI is
 * resolved inside WebKit's network process, which is a child of this one and
 * inherits its environment, so $XDG_CONFIG_HOME resolves to the same directory
 * on both sides and this check answers for the process that matters. */
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

	/* THE INTERFACE BEING EXPORTED IS HALF THE ANSWER. It says the certificate
	 * portal is running; it does not say its PKCS#11 module is installed, and
	 * without that module there is nothing for
	 * g_tls_certificate_new_from_pkcs11_uris() to resolve. Reporting unavailable
	 * here is what makes "auto" fall through to the pkcs11 provider on a machine
	 * that has the portal but not the module, instead of failing in the middle
	 * of a handshake. */
	module_config = webauth_cert_portal_module_config_path();
	if (module_config == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "the Certificate portal's PKCS#11 module is not configured (no " XDG_PORTAL_CERTIFICATE_MODULE_CONFIG
		                    " in p11-kit's module directories)");
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

	/* THE ONE CALL IN THIS BACKEND THAT NEEDS THE PORTAL TO SEE WHO WE ARE.
	 * The constructor below loads the certificate portal's PKCS#11 module into
	 * THIS process, and that module then calls CreateSession and
	 * AcquireCredential on the public interface as an ordinary application. The
	 * frontend identifies its callers by reading /proc/<pid>, which this
	 * backend's own hardening deliberately closes; harden.h says what the
	 * window costs and why it is opened here and nowhere else. */
	webauth_harden_identifiable_begin();

	/* THE URIs NAME AN OBJECT AND NOT ONLY THE TOKEN, and portal-token.h says
	 * why at length: the constructor below reaches GnuTLS's single-object
	 * import, which refuses a URI that names no object. */
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

/* The portal's token declares a protected authentication path: the PIN is typed
 * in the certificate portal's own window and never enters this process. */
static const char* portal_pin(void)
{
	return NULL;
}

/* WHAT THIS CANNOT DO, stated once here and at length in docs/SECURITY.md.
 *
 * A grant belongs to the D-Bus peer that acquired it, and the peers that hold
 * one are the two PKCS#11 module instances -- this process's and WebKit's
 * network process's -- not this adapter. It has no session handle to release,
 * no way to reach the network process's module, and no per-module C_Finalize
 * that would not also finalize every other module GnuTLS loaded through
 * p11-kit's proxy. Both grants therefore outlive the transaction, until their
 * own expiry (the portal's default, because the module requests no lifetime),
 * until the portal invalidates them, or until this process exits.
 *
 * Revoking one would not end the authenticated connection either: TLS
 * authenticates a connection once, and WebKit's connection pool outlives a
 * WebKitNetworkSession. Per-transaction isolation is a transport question, not
 * a grant question. */
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
