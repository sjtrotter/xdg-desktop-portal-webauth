/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_TLS_CLIENT_CERT_PORTAL_H
#define WEBAUTH_TLS_CLIENT_CERT_PORTAL_H

#include <gio/gio.h>

/** @file
 *  The portal adapter: let the smart card service own the chooser and the PIN.
 *
 *  Calls `AcquireCredential` on `io.github.sjtrotter.Smartcard1` — not "RequestCertificate",
 *  because it grants private key use and the name should say so — with `purpose:
 *  "client_auth"` and `context` set to the destination host the certificate is for
 *  (untrusted from the service's point of view: it sees a D-Bus peer, not a TLS
 *  connection, and displays it as the *requested* destination, not a verified one). The
 *  grant that comes back carries the certificate and its chain, the key type, the
 *  operations and mechanisms it permits, and an expiry. The user chose and unlocked in
 *  that service's windows, and the PIN never reaches this process.
 *
 *  Two ways to use the grant, and they are not equally proven:
 *
 *    brokered Sign     one `Sign` call per operation, behind a GnuTLS external-signer
 *                      path — IF WebKitGTK/glib-networking expose one to build a
 *                      GTlsCertificate around. UNPROVEN: no such path is known to exist
 *                      yet, only that it would be tighter than the endpoint if it did.
 *                      Accounting-wise it is precise, revocable and auditable per
 *                      operation, but no generic Sign() can prove its input came from a
 *                      TLS handshake, so what it buys is accounting, not attestation.
 *    PKCS#11 endpoint  `OpenPkcs11Endpoint(grant_id)` — EXPERIMENTAL, opt-in, never
 *                      returned automatically. It returns a Unix socket fd speaking the
 *                      p11-kit RPC protocol, plus `certificate_uri` and
 *                      `private_key_uri` valid only on that endpoint, backed by a
 *                      broker-controlled SYNTHETIC facade — not the card forwarded, and
 *                      not the whole-token export stock `p11-kit server` would give:
 *                      that exports a TOKEN, not an object, and carries no login state
 *                      across the boundary. Two things are open: a PKCS#11 URI cannot
 *                      name a socket, and `g_tls_certificate_new_from_pkcs11_uris()` has
 *                      no module parameter, so making the fd/URIs resolvable to GLib at
 *                      all is unproven; that is spike S2.
 *
 *  The likely resolution if per-grant module registration turns out not to work is one
 *  permanently registered broker module exposing synthetic grant-bound slots — which
 *  turns the contract from "return a new module" into "return a URI an already-registered
 *  module resolves", and is a change to the other project's interface, not to this
 *  adapter's shape.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_SMARTCARD_BUS_NAME "io.github.sjtrotter.Smartcard1"

typedef enum
{
	WEBAUTH_GRANT_SIGN = 1 << 0,     /**< brokered Sign is in permitted_operations */
	WEBAUTH_GRANT_DECRYPT = 1 << 1,  /**< brokered Decrypt is in permitted_operations */
	WEBAUTH_GRANT_ENDPOINT = 1 << 2  /**< GetCapabilities advertised pkcs11_endpoint;
	                                   *   does not mean one has been opened */
} WebAuthGrantCapability;

/** What `AcquireCredential` granted, plus whatever `OpenPkcs11Endpoint` has since added.
 *  Corresponds field-for-field to the D-Bus results documented in
 *  smartcard-portal's docs/INTERFACE.md, not to a p11-kit forwarding handle: there is no
 *  module path and no module socket here, because the service never hands this process
 *  either. */
typedef struct
{
	char* grant_id;          /**< names the grant in Sign/Decrypt/RenewGrant/ReleaseGrant;
	                           *   not itself a capability */
	GBytes* certificate_der; /**< chosen leaf certificate; carries its own length */
	GPtrArray* chain_der;    /**< ordered intermediates as GBytes*, best effort */
	guint capabilities;      /**< a mask of WebAuthGrantCapability */
	gint64 expires_at;       /**< real expiry; may be sooner than requested */

	/* Populated only after a successful webauth_cert_portal_open_endpoint() call;
	 * -1 and NULL/NULL until then, and reset to that on release. */
	gint endpoint_fd;           /**< OpenPkcs11Endpoint's Unix socket fd, or -1 */
	char* certificate_uri;      /**< RFC 7512 URI, valid only on endpoint_fd */
	char* private_key_uri;      /**< RFC 7512 URI, valid only on endpoint_fd */
	guint endpoint_version;     /**< wire/behaviour version of the open endpoint */
} WebAuthPortalCredential;

/** Whether the smart card service answers and advertises a capability we can use. */
gboolean webauth_cert_portal_available(guint* capabilities, GError** error);

/** Open a PKCS#11 endpoint for @credential's grant and fill in its endpoint_fd,
 *  certificate_uri, private_key_uri and endpoint_version. Only when
 *  WEBAUTH_GRANT_ENDPOINT was advertised, and only after S2 has shown the resulting fd
 *  can actually satisfy a WebKit handshake — which consumer, if any, can load a
 *  broker-issued fd/URI pair at all is exactly what is unproven. */
gboolean webauth_cert_portal_open_endpoint(WebAuthPortalCredential* credential, GError** error);

/** Release the grant, and with it any endpoint or PKCS#11 session behind it. Resets
 *  endpoint_fd to -1. */
void webauth_cert_portal_release_grant(WebAuthPortalCredential* credential);

#endif /* WEBAUTH_TLS_CLIENT_CERT_PORTAL_H */
