/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_WEBKIT_SESSION_H
#define WEBAUTH_WEBKIT_SESSION_H

#include <glib.h>

#include "browser_session.h"

/** @file
 *  The WebKitGTK browser session: a GTK4 window hosting a WebKitGTK 6.0 web view.
 *
 *  Chosen when the completion mechanism needs interception the system browser cannot
 *  safely return, or when the flow needs a client certificate the browser cannot
 *  supply, as with a PIV card behind a PKCS#11 token. Note the honest caveat: RFC 8252
 *  prefers an external user-agent, and although a service-owned engine is a real
 *  improvement over a web view embedded in the requesting application (the requester
 *  cannot reach the DOM), some identity providers may still classify it as embedded.
 *
 *  Responsibilities beyond showing a page: it tests every top level navigation
 *  against the completion URI and finishes the transaction BEFORE the navigation is
 *  loaded, because the completion URI carries the credential the flow was for; it
 *  answers TLS client certificate challenges through the adapter in tls/client_cert.h,
 *  bound to the verified host of the page it is showing; it uses exactly the storage partition it was given; and it renders
 *  the security chrome, whose contents come from the resolved caller identity and the
 *  engine's own origin, never from anything the caller typed.
 *
 *  Fixed, not configurable: no TLS error bypass, no caller-controlled certificate
 *  trust, downloads and autofill disabled, no page content or URI logging.
 *
 *  Sketch only; nothing here is implemented.
 */

/** The vtable this implementation registers with browser_session.h. */
const WebAuthBrowserSessionVtable* webauth_webkit_session_vtable(void);

/** Whether a WebKitGTK session can be created at all right now: an engine, a display,
 *  and a usable GTK environment. */
gboolean webauth_webkit_session_available(GError** error);

/** The host the view has actually loaded and verified, used to bind certificate
 *  challenges and to render the origin in the security chrome. */
const char* webauth_webkit_session_verified_host(WebAuthBrowserSession* session);

#endif /* WEBAUTH_WEBKIT_SESSION_H */
