/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_WEBKIT_SESSION_H
#define WEBAUTH_WEBKIT_SESSION_H

#include <glib.h>

#include "transaction.h"

/** @file
 *  The web view: a GTK4 window hosting a WebKitGTK 6.0 engine.
 *
 *  This is what the backend IS. Note what is no longer here: the
 *  browser_session.h vtable that version 0 used as an in-process seam between a
 *  transaction layer and "whatever shows the page". That seam is now the impl
 *  D-Bus interface, and the alternatives it was going to select between are now
 *  SEPARATE BACKENDS chosen by portals.conf rather than implementations chosen
 *  by a capability mask:
 *
 *    the system browser, where the completion mechanism lets it securely return
 *      the result (loopback HTTP, claimed https app links, registered custom
 *      schemes) - a future webauth-portal-browser backend, and RFC 8252's
 *      preference wherever it is possible;
 *    a portal-owned WebKitGTK session, where interception or a client
 *      certificate from a hardware token requires it - THIS backend, and the
 *      AVD/PIV case that the project exists for;
 *    manual paste, as the headless fallback - a future backend, and the one a
 *      server or a bare SSH session would install;
 *    a browser extension, only as an experimental, explicitly installed
 *      integration - never the reference.
 *
 *  That reshaping is a genuine improvement and it was forced by mirroring
 *  upstream: a distribution or an administrator now selects the mechanism with a
 *  configuration file, one backend per mechanism, each shipping and updating
 *  independently, exactly as a desktop selects xdg-desktop-portal-gtk or
 *  -gnome. It also costs something honest: an in-process vtable could advertise
 *  a capability mask so a transaction could choose per-request, and the impl
 *  interface has no such negotiation. Version 1's answer is that a backend
 *  implementing the interface implements all of it, and a mechanism that cannot
 *  (paste, loopback-only) is a different backend the user configures - not a
 *  runtime fallback. See docs/decisions/0008-build-to-the-upstream-shape.md.
 *
 *  Responsibilities beyond showing a page: it tests every top level navigation
 *  against the completion URI and finishes the transaction BEFORE the navigation
 *  is loaded, because the completion URI carries the credential the flow was
 *  for; it answers TLS client certificate challenges through the adapter in
 *  tls/client_cert.h, bound to the verified host of the page it is showing; it
 *  uses exactly the storage partition it was given; and it renders the security
 *  chrome, whose contents come from the app id the frontend established and the
 *  engine's own origin, never from anything the application typed.
 *
 *  Fixed, not configurable, and not an option the impl interface exposes: no TLS
 *  error bypass, no caller-controlled certificate trust, downloads and autofill
 *  disabled, no page content or URI logging.
 *
 *  The honest caveat, unchanged by any of this: RFC 8252 prefers an external
 *  user-agent, and although a portal-owned engine is a real improvement over a
 *  web view embedded in the requesting application (the requester cannot reach
 *  the DOM), some identity providers may still classify it as embedded.
 *
 *  Sketch only; nothing here is implemented.
 */

/** Whether a WebKitGTK session can be created at all right now: an engine, a
 *  display, and a usable GTK environment. Checked at startup, before the impl
 *  interface is exported: a backend that cannot show a page must not claim the
 *  interface, so that the frontend can find another one. */
gboolean webauth_webkit_session_available(GError** error);

typedef struct WebAuthWebkitSession WebAuthWebkitSession;

WebAuthWebkitSession* webauth_webkit_session_new(WebAuthTransaction* transaction,
                                                 const char* parent_window,
                                                 const char* activation_token,
                                                 const char* title_hint, GError** error);

/** Show the window and run the transaction. Every terminal outcome goes through
 *  webauth_transaction_finish(). */
gboolean webauth_webkit_session_present(WebAuthWebkitSession* self, GError** error);

/** Cancel: destroy the window and every dialog belonging to the transaction.
 *  Called when a Close() arrives from the frontend, when the deadline expires,
 *  and when the frontend's connection drops. */
void webauth_webkit_session_cancel(WebAuthWebkitSession* self);

/** The host the view has actually loaded and verified, used to bind certificate
 *  challenges and to render the origin in the security chrome. */
const char* webauth_webkit_session_verified_host(WebAuthWebkitSession* self);

void webauth_webkit_session_free(WebAuthWebkitSession* self);

#endif /* WEBAUTH_WEBKIT_SESSION_H */
