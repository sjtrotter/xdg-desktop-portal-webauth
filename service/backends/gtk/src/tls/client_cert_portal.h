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
 *  Names, and their status. Both projects have now landed their frontend/backend
 *  restructuring and mirror xdg-desktop-portal, and each incubating frontend now
 *  owns its own bus name rather than a shared stand-in, so the call goes to:
 *
 *      bus name      io.github.sjtrotter.portal.Certificate
 *      object path   /io/github/sjtrotter/portal/Certificate
 *      interface     io.github.sjtrotter.portal.Certificate1
 *      method        CreateSession(a{sv} options) -> o session_handle
 *                    AcquireCredential(o session_handle, s parent_window,
 *                                      a{sv} options) -> o request_handle
 *      result        io.github.sjtrotter.portal.Request::Response(u, a{sv})
 *
 *  THE GRANT IS THE SESSION. `CreateSession` returns a `Session` object path,
 *  and that path - not a bare `grant_id` string - is the handle this adapter
 *  holds, watches, and closes. `AcquireCredential`'s response still carries a
 *  `grant_id`, but it is a LOG IDENTIFIER only, for correlating this project's
 *  journal with the smart card portal's; it is never passed back to that portal
 *  as an argument. `Sign` and `Decrypt` are themselves `Request`-shaped calls,
 *  because upstream's convention is that anything which can prompt - a lazy
 *  login, per-operation consent - returns a `Request` the caller can `Close()`,
 *  not an ordinary method call.
 *
 *  NOTE THE BUS NAME IT IS ON. Under the portal shape the certificate interface
 *  is hosted by its OWN frontend on its OWN incubating bus name,
 *  io.github.sjtrotter.portal.Certificate - a separate name from this project's
 *  own frontend's io.github.sjtrotter.portal.WebAuthentication. There is no
 *  collision to work around: this adapter simply calls that bus name as an
 *  ordinary D-Bus client, exactly as it would call any other installed portal.
 *  At acceptance both interfaces move onto the real
 *  org.freedesktop.portal.Desktop and this call's bus name changes with them.
 *  See docs/decisions/0008-build-to-the-upstream-shape.md, "Per-project bus
 *  names during incubation".
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
 *  fix alone. A single frontend hosting both interfaces would SOLVE this, but
 *  only there: inside one frontend the derived app id is already in hand and can
 *  be passed to the certificate side IN-PROCESS, with no attestation crossing a
 *  bus at all. A shared incubating frontend along those lines is one option
 *  available to explore, not a required next step - the fix arrives for free at
 *  acceptance regardless. That fix does not generalise past the process
 *  boundary it lives inside. It works only because the two portals then run in
 *  one trusted process sharing one address space; across two separate frontend
 *  processes, passing an app id across the boundary would be an unattested
 *  assertion of someone else's identity, which is exactly what the paragraph
 *  above forbids. Doing it that way is NOT a smaller version of the
 *  shared-frontend fix - it is the thing a shared frontend exists to avoid
 *  needing, and it is not to be built as a stopgap.
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
 *    PKCS#11 endpoint  `OpenPkcs11Endpoint(session_handle)` - EXPERIMENTAL, opt-in,
 *                      never returned automatically. It is created by the smart card
 *                      portal's BACKEND and relayed by its frontend: what this adapter
 *                      receives is a Unix socket fd speaking the p11-kit RPC protocol,
 *                      plus `certificate_uri` and `private_key_uri` valid only on that
 *                      endpoint, backed by a broker-controlled SYNTHETIC facade - not the
 *                      card forwarded, and not the whole-token export stock
 *                      `p11-kit server` would give: that exports a TOKEN, not an object,
 *                      and carries no login state across the boundary. Two things are
 *                      open: a PKCS#11 URI cannot name a socket, and
 *                      `g_tls_certificate_new_from_pkcs11_uris()` has no module
 *                      parameter, so making the fd/URIs resolvable to GLib at all is
 *                      unproven; that is spike S2.
 *
 *  The likely resolution if per-grant module registration turns out not to work is one
 *  permanently registered broker module exposing synthetic grant-bound slots - which
 *  turns the contract from "return a new module" into "return a URI an already-registered
 *  module resolves", and is a change to the other project's interface, not to this
 *  adapter's shape.
 *
 *  BACKEND_GONE. The smart card portal's `GrantInvalidated` signal can arrive with reason
 *  `backend_gone` - its own backend died while this adapter's session was live - separately
 *  from anything this adapter did. It is not a `Sign`/`Decrypt` failure to retry: the
 *  session is dead, the endpoint fd (if any) is poisoned, and the only correct response is
 *  to treat the grant as released, fail the in-flight operation, and let
 *  `webauth_cert_adapter_select()` re-run `available()` before trying the portal path
 *  again. See webauth_cert_portal_release_grant() below.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_CERTIFICATE_BUS_NAME "io.github.sjtrotter.portal.Certificate"
#define WEBAUTH_CERTIFICATE_OBJECT_PATH "/io/github/sjtrotter/portal/Certificate"
#define WEBAUTH_CERTIFICATE_INTERFACE "io.github.sjtrotter.portal.Certificate1"

typedef enum
{
	WEBAUTH_GRANT_SIGN = 1 << 0,     /**< brokered Sign is in permitted_operations */
	WEBAUTH_GRANT_DECRYPT = 1 << 1,  /**< brokered Decrypt is in permitted_operations */
	WEBAUTH_GRANT_ENDPOINT = 1 << 2  /**< GetCapabilities advertised pkcs11_endpoint;
	                                   *   does not mean one has been opened */
} WebAuthGrantCapability;

/** What `AcquireCredential` granted, plus whatever `OpenPkcs11Endpoint` has since added.
 *  Corresponds field-for-field to the D-Bus results documented in
 *  the certificate portal's own interface document, not to a p11-kit forwarding handle: there
 *  is no module path and no module socket here, because that project never hands this
 *  process either. */
typedef struct
{
	char* session_object_path; /**< the `Session` object path `CreateSession` returned;
	                              *  THIS is the grant handle - passed to `Sign`, `Decrypt`,
	                              *  `RenewGrant` and `ReleaseGrant`/`Session.Close()`, and
	                              *  the path this adapter watches for `Session.Closed` and
	                              *  `GrantInvalidated` (including reason `backend_gone`) */
	char* grant_id;             /**< a LOG IDENTIFIER only, from the AcquireCredential
	                              *  response; correlates this project's journal with the
	                              *  smart card portal's, and is never itself passed back to
	                              *  that portal as an argument */
	GBytes* certificate_der;    /**< chosen leaf certificate; carries its own length */
	GPtrArray* chain_der;       /**< ordered intermediates as GBytes*, best effort */
	guint capabilities;         /**< a mask of WebAuthGrantCapability */
	gint64 expires_at;          /**< real expiry; may be sooner than requested */

	/* Populated only after a successful webauth_cert_portal_open_endpoint() call;
	 * -1 and NULL/NULL until then, and reset to that on release. */
	gint endpoint_fd;           /**< OpenPkcs11Endpoint's Unix socket fd, created by the
	                               *  smart card portal's BACKEND and RELAYED by its
	                               *  frontend - or -1 */
	char* certificate_uri;      /**< RFC 7512 URI, valid only on endpoint_fd */
	char* private_key_uri;      /**< RFC 7512 URI, valid only on endpoint_fd */
	guint endpoint_version;     /**< wire/behaviour version of the open endpoint */
} WebAuthPortalCredential;

/** Whether the certificate portal answers on its own bus name and advertises a
 *  capability we can use. Missing is not an error: the inproc adapter runs. */
gboolean webauth_cert_portal_available(guint* capabilities, GError** error);

/** Open a PKCS#11 endpoint for @credential's session and fill in its endpoint_fd,
 *  certificate_uri, private_key_uri and endpoint_version. Only when
 *  WEBAUTH_GRANT_ENDPOINT was advertised, and only after S2 has shown the resulting fd
 *  can actually satisfy a WebKit handshake - which consumer, if any, can load a
 *  broker-issued fd/URI pair at all is exactly what is unproven. */
gboolean webauth_cert_portal_open_endpoint(WebAuthPortalCredential* credential, GError** error);

/** Release the grant - i.e. close the Session at @credential's session_object_path - and
 *  with it any endpoint or PKCS#11 session behind it. Resets endpoint_fd to -1. Called on
 *  every exit path of the transaction, AND when a `GrantInvalidated` signal (reason
 *  `backend_gone` or otherwise) arrives for this session out of band: in that case the
 *  Session is already dead on the portal side, and this only needs to drop the local
 *  handle and fail the caller, not send another Close(). */
void webauth_cert_portal_release_grant(WebAuthPortalCredential* credential);

#endif /* WEBAUTH_GTK_TLS_CLIENT_CERT_PORTAL_H */
