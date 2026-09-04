/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_TLS_CLIENT_CERT_PORTAL_H
#define WEBAUTH_TLS_CLIENT_CERT_PORTAL_H

#include <gio/gio.h>

/** @file
 *  The portal adapter: let the Certificate portal own the chooser and the PIN.
 *
 *  This backend calls the Certificate portal AS AN ORDINARY CLIENT, over its
 *  PUBLIC interface - the one applications use - and not over any impl
 *  interface. That is the correct direction and the only allowed one: impl
 *  interfaces are for a frontend to call, and a backend that called another
 *  backend directly would be bypassing the frontend and every check it
 *  performs, which is exactly what this project asks other people not to do to
 *  it.
 *
 *  Names. Both portals are now hosted by the SAME frontend - xdg-desktop-portal,
 *  branch experimental/certificate-webauthentication - on the real
 *  org.freedesktop.portal.Desktop, so the call goes to:
 *
 *      bus name      org.freedesktop.portal.Desktop
 *      object path   /org/freedesktop/portal/desktop
 *      interface     org.freedesktop.portal.experimental.Certificate
 *      method        CreateSession(a{sv} options) -> o handle        [A REQUEST]
 *                    AcquireCredential(o session_handle, s parent_window,
 *                                      a{sv} options) -> o handle    [A REQUEST]
 *      result        org.freedesktop.portal.Request::Response(u, a{sv})
 *
 *  NOTE WHAT CreateSession RETURNS. It is a Request, not a method that hands
 *  back a session path: the returned object path is the REQUEST, and the
 *  `session_handle` arrives in that Request's Response results. This adapter
 *  must subscribe to the Response before calling, exactly as it does for
 *  AcquireCredential, and must not treat the return value as a session. That is
 *  what every session-creating portal upstream does (GlobalShortcuts,
 *  InputCapture, ScreenCast, RemoteDesktop) and it is a change from what this
 *  file used to describe.
 *
 *  THE GRANT IS THE SESSION. Once `session_handle` has arrived, that path - not
 *  a bare `grant_id` string - is the handle this adapter holds, watches, and
 *  closes. `AcquireCredential`'s response still carries a `grant_id`, but it is
 *  a LOG IDENTIFIER only, for correlating this project's journal with the
 *  portal's; it is never passed back as an argument. `Sign` and `Decrypt` are
 *  themselves `Request`-shaped calls, because upstream's convention is that
 *  anything which can prompt - a lazy login, per-operation consent - returns a
 *  `Request` the caller can `Close()`, not an ordinary method call.
 *
 *  BOTH ENDS OF THE CALL ARE GATED. The Certificate interface is experimental
 *  and is not exported unless xdg-desktop-portal was started with
 *  XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL containing "certificate". So
 *  webauth_cert_portal_available() failing is the NORMAL case on a machine that
 *  has not opted in, and it is not an error: the inproc adapter runs instead.
 *  Note also that a portal with the gate on but no Certificate BACKEND
 *  installed does not export the interface either - the frontend returns early
 *  when it finds no impl - so "absent" covers both, indistinguishably and by
 *  design.
 *
 *  THE IDENTITY THIS CALL CARRIES, and what changed about it. The Certificate
 *  portal's frontend derives the app id of ITS caller, which over D-Bus is this
 *  backend - not Remmina, not the application that started the transaction.
 *
 *  BUT BOTH PORTALS NOW LIVE IN ONE FRONTEND PROCESS. That process already
 *  derived, and still holds, the app id of the application that called
 *  WebAuthentication.Start; it can hand that ORIGINAL app id to its own
 *  certificate side IN-PROCESS, with no attestation crossing a bus, because
 *  nothing untrusted touches it in between. This is precisely the
 *  "shared frontend" fix both projects described as arriving at acceptance, and
 *  it has arrived early. The frontend does not do it yet - it is unwritten work
 *  on that branch, not something this adapter can supply.
 *
 *  THE CAVEAT DOES NOT GO AWAY AND MUST NOT BE DROPPED: that fix works ONLY
 *  in-process. Across a process boundary, passing an app id would be an
 *  unattested assertion of someone else's identity - identity-laundering
 *  through a trusted window, which is the opposite of what either portal is
 *  for. There is no cross-process attestation protocol here, none is being
 *  built, and passing the original app id over the bus as a stopgap is NOT a
 *  smaller version of the in-process fix. It is the thing the in-process fix
 *  exists to avoid needing. Until the frontend forwards it internally, the
 *  original app id can only travel as untrusted text in `reason`, and must be
 *  presented as such.
 *
 *  NOTE ALSO: the interface has NO `context` OPTION. The earlier sketch put the
 *  challenging origin there. The branch's XML does not have it, so the origin
 *  can only go in `reason` - the same untrusted text under a different name -
 *  and the chooser labels it as application-supplied.
 *
 *  ONE WAY TO USE THE GRANT, NOT TWO:
 *
 *    brokered Sign     one `Sign` call per operation, behind a GnuTLS external-signer
 *                      path - IF WebKitGTK/glib-networking expose one to build a
 *                      GTlsCertificate around. UNPROVEN: no such path is known to exist
 *                      yet, only that it would be tighter than an endpoint if it did.
 *                      Accounting-wise it is precise, revocable and auditable per
 *                      operation, but no generic Sign() can prove its input came from a
 *                      TLS handshake, so what it buys is accounting, not attestation.
 *
 *    PKCS#11 endpoint  GONE. `OpenPkcs11Endpoint` is NOT on the interface - not on the
 *                      public side and not on the impl side. The frontend branch left it
 *                      out deliberately: an fd-returning method needs its own review, and
 *                      the python-dbusmock backend the frontend is tested against cannot
 *                      hand back a usable fd, so a first version with it would have had no
 *                      test. It is a follow-up. Until it lands there is no compatibility
 *                      transport at all, which means the portal adapter is viable ONLY if
 *                      the external-signer path above turns out to exist. If it does not,
 *                      the inproc adapter is not a fallback, it is the only implementation.
 *
 *  The open questions the endpoint would have faced are unchanged and are the
 *  reason it needs its own review: a PKCS#11 URI cannot name a socket, and
 *  `g_tls_certificate_new_from_pkcs11_uris()` has no module parameter, so making
 *  a broker-issued fd and URI pair resolvable to GLib at all is unproven - that
 *  is spike S2. The likely resolution is one permanently registered broker
 *  module exposing synthetic grant-bound slots, which turns the contract from
 *  "return a new module" into "return a URI an already-registered module
 *  resolves", and is a change to the other project's interface rather than to
 *  this adapter's shape.
 *
 *  BACKEND_GONE. The Certificate portal's `GrantInvalidated` signal can arrive with
 *  reason `backend_gone` - the certificate BACKEND died while this adapter's session was
 *  live - separately from anything this adapter did. It is not a `Sign` failure to retry:
 *  the session is dead and the only correct response is to treat the grant as released,
 *  fail the in-flight operation, and let `webauth_cert_adapter_select()` re-run
 *  `available()` before trying the portal path again. The full reason list the frontend
 *  can send is `released`, `expired`, `token_removed`, `owner_gone`, `policy`,
 *  `service_shutdown`, `backend_gone`, `error`, and a consumer must tolerate values it
 *  does not know. See webauth_cert_portal_release_grant() below.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_CERTIFICATE_BUS_NAME "org.freedesktop.portal.Desktop"
#define WEBAUTH_CERTIFICATE_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define WEBAUTH_CERTIFICATE_INTERFACE "org.freedesktop.portal.experimental.Certificate"

/** The value XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL must contain for the
 *  interface above to be exported at all. This backend cannot set it: it is read
 *  by xdg-desktop-portal at startup. It is here so a diagnostic can say what is
 *  missing rather than "not available". */
#define WEBAUTH_CERTIFICATE_EXPERIMENTAL_FLAG "certificate"

typedef enum
{
	WEBAUTH_GRANT_SIGN = 1 << 0,    /**< brokered Sign is in permitted_operations */
	WEBAUTH_GRANT_DECRYPT = 1 << 1  /**< brokered Decrypt is in permitted_operations */
	/* There is no ENDPOINT bit any more: GetCapabilities has no pkcs11_endpoint
	 * key, because there is no OpenPkcs11Endpoint to advertise. */
} WebAuthGrantCapability;

/** What `AcquireCredential` granted. Corresponds field-for-field to the D-Bus results
 *  documented in the Certificate portal's own public XML, not to a p11-kit forwarding
 *  handle: there is no module path and no module socket here, because that portal never
 *  hands this process either. */
typedef struct
{
	char* session_object_path; /**< the `Session` object path that arrived in
	                              *  CreateSession's RESPONSE (not its return value);
	                              *  THIS is the grant handle - passed to `Sign`,
	                              *  `Decrypt`, `RenewGrant` and
	                              *  `ReleaseGrant`/`Session.Close()`, and the path this
	                              *  adapter watches for `Session.Closed` and
	                              *  `GrantInvalidated` */
	char* grant_id;             /**< a LOG IDENTIFIER only, from the AcquireCredential
	                              *  response; correlates this project's journal with the
	                              *  Certificate portal's, and is never itself passed back
	                              *  as an argument */
	GBytes* certificate_der;    /**< chosen leaf certificate; carries its own length */
	GPtrArray* chain_der;       /**< ordered intermediates as GBytes*, best effort */
	guint capabilities;         /**< a mask of WebAuthGrantCapability */
	gint64 expires_at;          /**< real expiry; frontend-generated, and may be sooner
	                              *  than requested_lifetime asked for */
} WebAuthPortalCredential;

/** Whether the Certificate portal interface is exported on
 *  org.freedesktop.portal.Desktop and advertises a capability we can use. Missing is
 *  not an error and is the normal case: the experimental gate may be off, or no
 *  certificate backend may be installed. The inproc adapter runs instead. */
gboolean webauth_cert_portal_available(guint* capabilities, GError** error);

/** Release the grant - i.e. close the Session at @credential's session_object_path -
 *  and with it any PKCS#11 session behind it. Called on every exit path of the
 *  transaction, AND when a `GrantInvalidated` signal arrives for this session out of
 *  band: in that case the Session is already dead on the portal side, and this only
 *  needs to drop the local handle and fail the caller, not send another Close(). */
void webauth_cert_portal_release_grant(WebAuthPortalCredential* credential);

#endif /* WEBAUTH_TLS_CLIENT_CERT_PORTAL_H */
