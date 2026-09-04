/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_TLS_CLIENT_CERT_PORTAL_H
#define WEBAUTH_TLS_CLIENT_CERT_PORTAL_H

#include <gio/gio.h>

/** @file
 *  The portal adapter: let the smart card service own the chooser and the PIN.
 *
 *  `AcquireCredential` — not "RequestCertificate", because it grants private key use and
 *  the name should say so — returns a grant: the certificate and its chain, the key
 *  type, the operations and mechanisms the grant permits, and an expiry. The user chose
 *  and unlocked in that service's windows, and the PIN never reaches this process.
 *
 *  Two ways to use the grant, and they are not equally proven:
 *
 *    brokered signing  a Sign/Decrypt call per operation, behind a GnuTLS external
 *                      signer. Tighter: the grant is accounted for, revocable, and
 *                      auditable per operation. But no generic Sign() can prove its
 *                      input came from a TLS handshake, so it buys accounting rather
 *                      than semantic attestation, and it needs a GLib/GnuTLS path that
 *                      lets an external signer back a GTlsCertificate.
 *    PKCS#11 endpoint  a module the ordinary TLS stack consumes unchanged. EXPERIMENTAL:
 *                      stock p11-kit forwarding scopes to a TOKEN, not to an object, so
 *                      "scoped to the chosen certificate" needs a restricted facade that
 *                      does not exist yet; and dynamic registration of a new module
 *                      after WebKit's processes have started is exactly what spike S2
 *                      has to prove.
 *
 *  The likely resolution if dynamic registration fails is one permanently registered
 *  broker module exposing synthetic grant-bound slots — which turns the contract from
 *  "return a new module" into "return a URI an already-registered module resolves", and
 *  is a change to the other project's interface, not to this adapter's shape.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_SMARTCARD_BUS_NAME "io.github.sjtrotter.Smartcard1"

typedef enum
{
	WEBAUTH_GRANT_SIGN = 1 << 0,     /**< brokered Sign is available for this grant */
	WEBAUTH_GRANT_DECRYPT = 1 << 1,  /**< brokered Decrypt is available */
	WEBAUTH_GRANT_ENDPOINT = 1 << 2  /**< a PKCS#11 endpoint can be opened for it */
} WebAuthGrantCapability;

typedef struct
{
	char* grant_id;
	GBytes* certificate_der;
	GPtrArray* chain_der;
	guint capabilities; /**< a mask of WebAuthGrantCapability */
	gint64 expires_at;
} WebAuthCredentialGrant;

/** Whether the smart card service answers and advertises a capability we can use. */
gboolean webauth_cert_portal_available(guint* capabilities, GError** error);

/** Open a PKCS#11 endpoint for @grant. Only when the grant advertises it, and only
 *  after S2 has shown the resulting module can actually satisfy a WebKit handshake. */
gboolean webauth_cert_portal_open_endpoint(const WebAuthCredentialGrant* grant,
                                           char** module_path, GError** error);

/** Release the grant, and with it any endpoint or PKCS#11 session behind it. */
void webauth_cert_portal_release_grant(WebAuthCredentialGrant* grant);

#endif /* WEBAUTH_TLS_CLIENT_CERT_PORTAL_H */
