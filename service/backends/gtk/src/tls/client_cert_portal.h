/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_TLS_CLIENT_CERT_PORTAL_H
#define WEBAUTH_GTK_TLS_CLIENT_CERT_PORTAL_H

#include <gio/gio.h>

/** @file
 *  The portal adapter: let the smart card portal own the chooser and the PIN.
 *
 *  This backend calls the smart card portal AS A CLIENT, over its PUBLIC
 *  interface - the one applications use - and not over any impl interface. That
 *  is the correct direction and the only allowed one: impl interfaces are for a
 *  frontend to call, and a backend that called another project's backend
 *  directly would be bypassing that project's frontend and every check it
 *  performs, which is exactly what this project asks other people not to do to
 *  it.
 *
 *  Names, and their status. In the restructured shape both projects mirror
 *  xdg-desktop-portal, so the call goes to:
 *
 *      bus name      io.github.sjtrotter.portal.Desktop
 *      object path   /io/github/sjtrotter/portal/desktop
 *      interface     io.github.sjtrotter.portal.Smartcard1
 *      method        AcquireCredential(s parent_window, a{sv} options) -> o handle
 *      result        io.github.sjtrotter.portal.Request::Response(u, a{sv})
 *
 *  UNAGREED, AND SAY SO. The sibling sketch currently ships
 *  io.github.sjtrotter.Smartcard1 on its own bus name at
 *  /io/github/sjtrotter/Smartcard1, and argues in its own documents that a
 *  frontend/backend split is premature. Its restructuring is being done in
 *  parallel and has not landed. This adapter therefore treats the name above as
 *  a proposal to that project, not as a fact about it, and an implementation
 *  must probe rather than assume: the portal name first, the standalone
 *  io.github.sjtrotter.Smartcard1 name second, neither being an error to be
 *  missing (the inproc adapter is what runs then). Neither name may be frozen
 *  before S2 has said whether the module transport works at all.
 *
 *  NOTE THE BUS NAME IT IS ON. Under the portal shape the smart card interface
 *  is hosted by a FRONTEND on the shared Desktop bus name - the same singleton
 *  name this project's own frontend claims. Two incubating frontends cannot both
 *  hold it, so on a machine where both are installed the certificate adapter and
 *  the web authentication portal are talking to the same process or to nothing.
 *  That is not a bug in this file; it is the argument for one incubating
 *  frontend process hosting both interfaces, exactly as xdg-desktop-portal hosts
 *  all portals. See docs/decisions/0008-build-to-the-upstream-shape.md.
 *
 *  THE IDENTITY THIS CALL CARRIES, and the honest limitation. The smart card
 *  portal's frontend derives the app id of ITS caller, which here is
 *  webauth-portal-gtk - not Remmina, not the application that started the
 *  transaction. Its consent dialog will name this backend. The original app id
 *  can only be passed as untrusted text (the "reason" hint, with the challenging
 *  origin in "context"), and must be presented as such: presenting it as an
 *  established identity would launder a caller's identity through a trusted
 *  window, which is the opposite of what either project is for.
 *
 *  Attested delegation - "this request is on behalf of an application whose id I
 *  established" - is a protocol neither project has, and it is one hop that
 *  crosses a trust boundary in the wrong direction for anything either side can
 *  fix alone. It is a further argument for a single frontend hosting both
 *  interfaces: inside one frontend the derived app id is already in hand, and no
 *  attestation would have to cross a bus at all.
 *
 *  Two ways to use the grant, and they are not equally proven:
 *
 *    brokered Sign     one `Sign` call per operation, behind a GnuTLS external-signer
 *                      path - IF WebKitGTK/glib-networking expose one to build a
 *                      GTlsCertificate around. UNPROVEN: no such path is known to exist
 *                      yet, only that it would be tighter than the endpoint if it did.
 *                      Accounting-wise it is precise, revocable and auditable per
 *                      operation, but no generic Sign() can prove its input came from a
 *                      TLS handshake, so what it buys is accounting, not attestation.
 *    PKCS#11 endpoint  `OpenPkcs11Endpoint(grant_id)` - EXPERIMENTAL, opt-in, never
 *                      returned automatically. It returns a Unix socket fd speaking the
 *                      p11-kit RPC protocol, plus `certificate_uri` and
 *                      `private_key_uri` valid only on that endpoint, backed by a
 *                      broker-controlled SYNTHETIC facade - not the card forwarded, and
 *                      not the whole-token export stock `p11-kit server` would give:
 *                      that exports a TOKEN, not an object, and carries no login state
 *                      across the boundary. Two things are open: a PKCS#11 URI cannot
 *                      name a socket, and `g_tls_certificate_new_from_pkcs11_uris()` has
 *                      no module parameter, so making the fd/URIs resolvable to GLib at
 *                      all is unproven; that is spike S2.
 *
 *  The likely resolution if per-grant module registration turns out not to work is one
 *  permanently registered broker module exposing synthetic grant-bound slots - which
 *  turns the contract from "return a new module" into "return a URI an already-registered
 *  module resolves", and is a change to the other project's interface, not to this
 *  adapter's shape.
 *
 *  Sketch only; nothing here is implemented.
 */

/** The smart card portal under the restructured, portal-shaped names. */
#define WEBAUTH_SMARTCARD_BUS_NAME "io.github.sjtrotter.portal.Desktop"
#define WEBAUTH_SMARTCARD_OBJECT_PATH "/io/github/sjtrotter/portal/desktop"
#define WEBAUTH_SMARTCARD_INTERFACE "io.github.sjtrotter.portal.Smartcard1"

/** The name that sketch ships today, probed second. Not a compatibility promise:
 *  neither name is frozen, and both projects say so. */
#define WEBAUTH_SMARTCARD_LEGACY_BUS_NAME "io.github.sjtrotter.Smartcard1"

typedef enum
{
	WEBAUTH_GRANT_SIGN = 1 << 0,     /**< brokered Sign is in permitted_operations */
	WEBAUTH_GRANT_DECRYPT = 1 << 1,  /**< brokered Decrypt is in permitted_operations */
	WEBAUTH_GRANT_ENDPOINT = 1 << 2  /**< GetCapabilities advertised pkcs11_endpoint;
	                                   *   does not mean one has been opened */
} WebAuthGrantCapability;

/** What `AcquireCredential` granted, plus whatever `OpenPkcs11Endpoint` has since added.
 *  Corresponds field-for-field to the D-Bus results documented in
 *  smartcard-portal's own interface document, not to a p11-kit forwarding handle: there
 *  is no module path and no module socket here, because that project never hands this
 *  process either. */
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

/** Whether a smart card portal answers - under either name - and advertises a
 *  capability we can use. Missing is not an error: the inproc adapter runs. */
gboolean webauth_cert_portal_available(guint* capabilities, GError** error);

/** Open a PKCS#11 endpoint for @credential's grant and fill in its endpoint_fd,
 *  certificate_uri, private_key_uri and endpoint_version. Only when
 *  WEBAUTH_GRANT_ENDPOINT was advertised, and only after S2 has shown the resulting fd
 *  can actually satisfy a WebKit handshake - which consumer, if any, can load a
 *  broker-issued fd/URI pair at all is exactly what is unproven. */
gboolean webauth_cert_portal_open_endpoint(WebAuthPortalCredential* credential, GError** error);

/** Release the grant, and with it any endpoint or PKCS#11 session behind it. Resets
 *  endpoint_fd to -1. Called on every exit path of the transaction. */
void webauth_cert_portal_release_grant(WebAuthPortalCredential* credential);

#endif /* WEBAUTH_GTK_TLS_CLIENT_CERT_PORTAL_H */
