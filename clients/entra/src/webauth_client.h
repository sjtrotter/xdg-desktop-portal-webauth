/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ENTRA_WEBAUTH_CLIENT_H
#define ENTRA_WEBAUTH_CLIENT_H

#include <glib.h>

/** @file
 *  Calling the web authentication portal: the client's only interactive
 *  dependency.
 *
 *      bus name      io.github.sjtrotter.portal.Desktop
 *      object path   /io/github/sjtrotter/portal/desktop
 *      interface     io.github.sjtrotter.portal.WebAuthentication1
 *      method        Start(s parent_window, s start_uri, s completion_uri,
 *                          a{sv} options) -> o request_handle
 *      result        io.github.sjtrotter.portal.Request::Response(u, a{sv})
 *      request path  /io/github/sjtrotter/portal/desktop/request/<sender>/<token>
 *
 *  Everything interactive this client does is one Start call and one Response.
 *  It hands over a URI to open and the exact URI whose navigation ends the flow,
 *  and gets back the URI the flow ended at. The portal knows nothing about
 *  OAuth, and this client owns no windows, no web view, no certificate chooser
 *  and no PIN prompt.
 *
 *  IT TALKS TO THE FRONTEND AND TO NOTHING ELSE. The portal is a frontend that
 *  routes to a backend, exactly as xdg-desktop-portal does, and none of that is
 *  visible here: this client never names a backend, never reads a .portal file,
 *  never calls io.github.sjtrotter.impl.portal.* - which it could not be
 *  permitted to do anyway - and cannot tell which backend served it. A machine
 *  that installs a different backend changes nothing in this file. That
 *  invisibility is the property the split exists to have, and it is why the
 *  restructuring changed the names here and nothing else.
 *
 *  If no portal is reachable - no session bus, nothing owning the bus name, no
 *  backend configured for the interface, no display - this reports unavailable
 *  rather than failing, so the caller can fall through to another provider such
 *  as FreeRDP's terminal paste flow. Note the middle one: a frontend that finds
 *  no backend does not export the interface at all, so "unavailable" now covers
 *  one more case than it did, and it is not distinguishable from the others by
 *  design.
 *
 *  The completion URI this client passes is always the one from its own cloud
 *  table. It is never taken from the client's caller. See
 *  docs/PUBLIC-INTERFACE.md.
 *
 *  Sketch only; nothing here is implemented.
 */

#define ENTRA_PORTAL_BUS_NAME "io.github.sjtrotter.portal.Desktop"
#define ENTRA_PORTAL_OBJECT_PATH "/io/github/sjtrotter/portal/desktop"
#define ENTRA_PORTAL_INTERFACE "io.github.sjtrotter.portal.WebAuthentication1"
#define ENTRA_PORTAL_REQUEST_INTERFACE "io.github.sjtrotter.portal.Request"

typedef enum
{
	ENTRA_WEBAUTH_COMPLETED = 0,  /**< @completion_uri is set */
	ENTRA_WEBAUTH_CANCELLED = 1,  /**< the user closed the window, or we called Close() */
	ENTRA_WEBAUTH_OTHER = 2,      /**< timeout, or the transaction ended some other way */
	ENTRA_WEBAUTH_UNAVAILABLE     /**< nothing implementing the interface to call */
} EntraWebAuthResult;

/** Whether the portal can be reached, without starting a transaction. */
gboolean entra_webauth_available(GError** error);

/** Run one interactive transaction and return the completion URI. Subscribes to
 *  Response on the handle derived from a fresh handle_token BEFORE calling Start,
 *  so a fast completion cannot race the subscription. Blocks until the portal
 *  responds, the timeout expires, or @cancellable fires - in which case it calls
 *  Close() and waits for the response it is still owed. */
EntraWebAuthResult entra_webauth_start(const char* parent_window, const char* activation_token,
                                       const char* start_uri, const char* completion_uri,
                                       const char* session_mode, guint timeout_seconds,
                                       GCancellable* cancellable, char** completion_uri_out,
                                       GError** error);

#endif /* ENTRA_WEBAUTH_CLIENT_H */
