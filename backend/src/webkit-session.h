/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_WEBKIT_SESSION_H
#define WEBAUTH_WEBKIT_SESSION_H

#include <glib.h>

#include "transaction.h"

/** @file
 *  The web view: a GTK4 window hosting a WebKitGTK 6.0 engine.
 *
 *  This is what the backend IS. The alternatives a single-process design would
 *  have selected between at run time are SEPARATE BACKENDS chosen by
 *  portals.conf: the system browser where the completion mechanism lets it
 *  securely return the result, manual paste for a headless machine, a browser
 *  extension as an experimental integration. This one is the portal-owned
 *  WebKitGTK session, and it exists for the case the others cannot serve:
 *  interception before load, and a client certificate from a hardware token.
 *
 *  Responsibilities beyond showing a page: it tests every navigation against the
 *  completion URI and finishes the transaction BEFORE the navigation is loaded,
 *  because the completion URI carries the credential the flow was for; it
 *  answers TLS client certificate challenges through the adapter in
 *  tls/client_cert.h, bound to the host of the page it is showing; it uses
 *  exactly the storage mode it was given; and it renders the security chrome,
 *  whose contents come from the app id the frontend established and the engine's
 *  own origin, never from anything the application typed.
 *
 *  Fixed, not configurable, and not an option the impl interface exposes: no TLS
 *  error bypass, no caller-controlled certificate trust, downloads disabled,
 *  JavaScript-opened windows disabled, every permission request denied, no page
 *  content or URI logging.
 *
 *  The honest caveat: RFC 8252 prefers an external user-agent, and although a
 *  portal-owned engine is a real improvement over a web view embedded in the
 *  requesting application (the requester cannot reach the DOM), some identity
 *  providers may still classify it as embedded.
 */

/** DEVELOPMENT ONLY. Accept @spec, "host=/path/to/certificate.pem", as the
 *  server certificate for that host, for every transaction in this process.
 *
 *  It exists for one reason: an end-to-end test needs an https server, an https
 *  server needs a certificate, and a certificate issued by a fixture CA is
 *  exactly what a correct backend refuses. Nothing about it is reachable from
 *  the bus: it is a command-line option of this process, it names one host and
 *  one certificate, and there is no "continue anyway" anywhere else in this
 *  backend. tools/ui-smoke.sh passes it; the installed .service file does not.
 *  See docs/TESTING.md. */
gboolean webauth_webkit_debug_trust_add(const char* spec, GError** error);

/** Whether a WebKitGTK session can be created at all right now: an engine, a
 *  display, and a usable GTK environment. Checked at startup, before the impl
 *  interface is exported: a backend that cannot show a page must not claim the
 *  interface, so that the frontend can find another one. */
gboolean webauth_webkit_session_available(GError** error);

typedef struct WebAuthWebkitSession WebAuthWebkitSession;

/** Build the window, the network session and the view. Takes a reference on
 *  @transaction and registers itself as its teardown. */
WebAuthWebkitSession* webauth_webkit_session_new(WebAuthTransaction* transaction,
                                                 const char* parent_window,
                                                 const char* activation_token,
                                                 const char* title_hint, GError** error);

/** Show the window and load the start URI. Every terminal outcome from here on
 *  goes through webauth_transaction_finish(). */
void webauth_webkit_session_present(WebAuthWebkitSession* self);

#endif /* WEBAUTH_WEBKIT_SESSION_H */
