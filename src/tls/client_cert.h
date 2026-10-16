/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_TLS_CLIENT_CERT_H
#define WEBAUTH_TLS_CLIENT_CERT_H

#include <gio/gio.h>

/** Certificate provider design and WebKit PKCS#11 transport are described in docs/decisions/0007-certificate-adapter.md. */

typedef struct
{
	const char* origin;      /**< The verified host that raised the challenge. */
	const char* app_id;      /**< Frontend application id; empty if unidentified. */
	const char* app_identity_level; /**< "sandboxed", "host" or "unidentified". */
	gpointer parent;         /**< Transaction window for dialog parenting. */
} WebAuthCertChallenge;

typedef struct
{
	const char* name;

	gboolean (*available)(GError** error);

	/** Returns NULL and sets @error when no credential is available. */
	GTlsCertificate* (*acquire)(const WebAuthCertChallenge* challenge, GError** error);

	/** Returns NULL for the portal provider, whose protected authentication path prompts in the portal. */
	const char* (*pin)(void);

	/** Releases transaction resources and wipes the PIN buffer on every exit path. */
	void (*release)(void);
} WebAuthCertAdapter;

/** @key_uri defaults to the private-key URI derived from @cert_uri; @pin_file keeps the PIN out of argv. */
gboolean webauth_cert_adapter_configure(const char* name, const char* cert_uri,
                                        const char* key_uri, const char* pin_file,
                                        GError** error);

/** Returns NULL when no provider is available; certificate challenges then end with credential_unavailable. */
const WebAuthCertAdapter* webauth_cert_adapter_select(GError** error);

const char* webauth_cert_adapter_configured_name(void);

const WebAuthCertAdapter* webauth_cert_adapter_portal(void);
const WebAuthCertAdapter* webauth_cert_adapter_pkcs11(void);

/** Configuration used only by the pkcs11 provider. */
const char* webauth_cert_adapter_cert_uri(void);
const char* webauth_cert_adapter_key_uri(void);
const char* webauth_cert_adapter_pin_file(void);

#endif /* WEBAUTH_TLS_CLIENT_CERT_H */
