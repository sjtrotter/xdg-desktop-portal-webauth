/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_TLS_PKCS11_H
#define WEBAUTH_TLS_PKCS11_H

#include <glib.h>

/** @file
 *  Finding the client certificates on the machine's PKCS#11 tokens.
 *
 *  Used by the in-process adapter only; the portal adapter never gets here, because the
 *  smart card service does its own enumeration.
 *
 *  Enumeration is bounded and cancellable, because a card reader can block for a long
 *  time and a user who has changed their mind should not wait for it. The edge cases
 *  were all found on real hardware and each must survive any reimplementation:
 *  p11-kit's own trust tokens (model p11-kit-trust) are skipped rather than searched; a
 *  token holding no certificate is empty, not an error, even though the tool used to
 *  list it exits non-zero; a token removed during enumeration ends it cleanly; and
 *  output is bounded so a hostile module cannot exhaust memory. Nothing about a
 *  certificate is logged: not the PKCS#11 URI, not the label, not the serial, not the
 *  subject. Only counts.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef struct
{
	char* uri;      /**< PKCS#11 URI of the certificate. Never logged. */
	char* key_uri;  /**< PKCS#11 URI of the matching private key. Never logged. */
	char* label;    /**< Display label for the chooser. Never logged. */
	char* token;    /**< Display label of the token it lives on. Never logged. */
	gint64 expires; /**< notAfter, so an expired certificate can be marked. */
} WebAuthCertificate;

/** Enumerate certificates on all usable tokens. Returns a GPtrArray of
 *  WebAuthCertificate, possibly empty, or NULL on error. */
GPtrArray* webauth_pkcs11_enumerate(GCancellable* cancellable, guint timeout_ms, GError** error);

void webauth_certificate_free(WebAuthCertificate* certificate);

#endif /* WEBAUTH_TLS_PKCS11_H */
