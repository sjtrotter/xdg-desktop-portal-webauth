/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 *
 * The pkcs11 provider: a token named by URI, with no chooser and no prompt.
 */

#include <string.h>

#include "../redact.h"
#include "client_cert.h"

static char* pin = NULL;

static gboolean pkcs11_available(GError** error)
{
	if (webauth_cert_adapter_cert_uri() == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "no --client-cert-uri was given");
		return FALSE;
	}

	return TRUE;
}

static GTlsCertificate* pkcs11_acquire(const WebAuthCertChallenge* challenge, GError** error)
{
	GTlsCertificate* certificate = NULL;
	g_autoptr(GError) inner = NULL;

	certificate = g_tls_certificate_new_from_pkcs11_uris(webauth_cert_adapter_cert_uri(),
	                                                     webauth_cert_adapter_key_uri(), &inner);
	if (certificate == NULL)
	{
		/* Library error text names the token and can carry a URI; it is cut
		 * before either reaches a log or a D-Bus error. */
		g_autofree char* safe = webauth_redact_error_text(inner->message);

		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", safe);
		return NULL;
	}

	return certificate;
}

/* The PIN is read from a file the operator named, once, and is wiped when the
 * transaction ends. It is never in argv, never in the environment, and never
 * logged - not even as a length. */
static const char* pkcs11_pin(void)
{
	const char* path = webauth_cert_adapter_pin_file();
	g_autoptr(GError) error = NULL;
	g_autofree char* contents = NULL;
	gsize length = 0;

	if (pin != NULL)
		return pin;

	if (path == NULL)
		return NULL;

	if (!g_file_get_contents(path, &contents, &length, &error))
	{
		webauth_log_event(G_LOG_LEVEL_WARNING, WEBAUTH_EVENT_CERT_DECLINED, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "pin-file-unreadable", NULL);
		return NULL;
	}

	g_strchomp(contents);
	pin = g_steal_pointer(&contents);

	return pin;
}

static void pkcs11_release(void)
{
	if (pin == NULL)
		return;

	memset(pin, 0, strlen(pin));
	g_clear_pointer(&pin, g_free);
}

static const WebAuthCertAdapter pkcs11_adapter = {
	.name = "pkcs11",
	.available = pkcs11_available,
	.acquire = pkcs11_acquire,
	.pin = pkcs11_pin,
	.release = pkcs11_release,
};

const WebAuthCertAdapter* webauth_cert_adapter_pkcs11(void)
{
	return &pkcs11_adapter;
}
