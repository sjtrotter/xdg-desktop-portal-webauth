/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "client_cert.h"

#include <string.h>

#include "../redact.h"

static char* configured_name = NULL;
static char* cert_uri = NULL;
static char* key_uri = NULL;
static char* pin_file = NULL;

/* The private key object of a pair written to a token under one CKA_ID: the
 * certificate URI with its object class swapped. Deriving it rather than asking
 * for it is what makes --client-cert-uri alone enough. */
static char* derive_key_uri(const char* certificate_uri)
{
	const char* type = NULL;

	if (certificate_uri == NULL)
		return NULL;

	type = strstr(certificate_uri, ";type=");
	if (type == NULL)
		return g_strconcat(certificate_uri, ";type=private", NULL);

	{
		g_autofree char* head = g_strndup(certificate_uri, (size_t) (type - certificate_uri));

		return g_strconcat(head, ";type=private", NULL);
	}
}

gboolean webauth_cert_adapter_configure(const char* name, const char* certificate_uri,
                                        const char* private_key_uri, const char* pin_path,
                                        GError** error)
{
	if (name == NULL)
		name = "auto";

	if (g_strcmp0(name, "auto") != 0 && g_strcmp0(name, "portal") != 0 &&
	    g_strcmp0(name, "pkcs11") != 0 && g_strcmp0(name, "none") != 0)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "--cert-adapter takes auto, portal, pkcs11 or none");
		return FALSE;
	}

	if (g_strcmp0(name, "pkcs11") == 0 && certificate_uri == NULL)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "--cert-adapter pkcs11 needs --client-cert-uri");
		return FALSE;
	}

	if (certificate_uri != NULL && !g_str_has_prefix(certificate_uri, "pkcs11:"))
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "--client-cert-uri must be a pkcs11: URI");
		return FALSE;
	}

	/* A pin-value in a URI is a PIN in this process's argv, and argv is
	 * world-readable through /proc. The file is the way in. */
	if (certificate_uri != NULL && strstr(certificate_uri, "pin-value") != NULL)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "--client-cert-uri must not carry pin-value; use --client-cert-pin-file");
		return FALSE;
	}

	g_clear_pointer(&configured_name, g_free);
	g_clear_pointer(&cert_uri, g_free);
	g_clear_pointer(&key_uri, g_free);
	g_clear_pointer(&pin_file, g_free);

	configured_name = g_strdup(name);
	cert_uri = g_strdup(certificate_uri);
	key_uri = private_key_uri != NULL ? g_strdup(private_key_uri) : derive_key_uri(certificate_uri);
	pin_file = g_strdup(pin_path);

	return TRUE;
}

const char* webauth_cert_adapter_configured_name(void)
{
	return configured_name != NULL ? configured_name : "auto";
}

const char* webauth_cert_adapter_cert_uri(void)
{
	return cert_uri;
}

const char* webauth_cert_adapter_key_uri(void)
{
	return key_uri;
}

const char* webauth_cert_adapter_pin_file(void)
{
	return pin_file;
}

const WebAuthCertAdapter* webauth_cert_adapter_select(GError** error)
{
	const char* name = webauth_cert_adapter_configured_name();
	g_autoptr(GError) portal_error = NULL;
	g_autoptr(GError) pkcs11_error = NULL;

	if (g_strcmp0(name, "none") == 0)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                    "Client certificates are disabled");
		return NULL;
	}

	if (g_strcmp0(name, "portal") == 0)
	{
		if (webauth_cert_adapter_portal()->available(error))
			return webauth_cert_adapter_portal();
		return NULL;
	}

	if (g_strcmp0(name, "pkcs11") == 0)
	{
		if (webauth_cert_adapter_pkcs11()->available(error))
			return webauth_cert_adapter_pkcs11();
		return NULL;
	}

	/* auto: the portal first, because it is the one that keeps the PIN out of
	 * this process, and the token URI second. */
	if (webauth_cert_adapter_portal()->available(&portal_error))
		return webauth_cert_adapter_portal();

	if (webauth_cert_adapter_pkcs11()->available(&pkcs11_error))
		return webauth_cert_adapter_pkcs11();

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	            "No certificate provider is available: portal: %s; pkcs11: %s",
	            portal_error->message, pkcs11_error->message);

	return NULL;
}
