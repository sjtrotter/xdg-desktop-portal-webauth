/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_WEBAUTH_CLIENT_H
#define ENTRA_WEBAUTH_CLIENT_H

#include <glib.h>

/** @file
 *  Calling the web authentication portal: the client's only interactive
 *  dependency.
 *
 *      bus name      org.freedesktop.portal.Desktop
 *      object path   /org/freedesktop/portal/desktop
 *      interface     org.freedesktop.portal.experimental.WebAuthentication
 *      method        Start(s parent_window, s start_uri, s completion_uri,
 *                          a{sv} options) -> o request_handle
 *      result        org.freedesktop.portal.Request::Response(u, a{sv})
 *      request path  /org/freedesktop/portal/desktop/request/<sender>/<token>
 *
 *  Everything interactive this client does is one Start call and one Response.
 *  It hands over a URI to open and the exact URI whose navigation ends the flow,
 *  and gets back the URI the flow ended at. The portal knows nothing about
 *  OAuth, and this client owns no windows, no web view, no certificate chooser
 *  and no PIN prompt.
 *
 *  IT TALKS TO xdg-desktop-portal AND TO NOTHING ELSE. The portal routes to a
 *  backend, and none of that is visible here: this client never names a backend,
 *  A WORKING REFERENCE FOR THIS CALL EXISTS: tools/webauth-e2e.py makes exactly
 *  it, in python, and is what the backend's end-to-end tests drive the portal
 *  with. Read it before writing this in C; docs/TESTING.md says what it proved.
 *
 *  never reads a .portal file, never calls org.freedesktop.impl.portal.* - which
 *  it could not be permitted to do anyway - and cannot tell which backend served
 *  it. A machine that installs a different backend changes nothing in this file.
 *
 *  THE INTERFACE IS EXPERIMENTAL AND GATED, AND THAT IS THE COMMON FAILURE.
 *  org.freedesktop.portal.experimental.WebAuthentication is not exported unless
 *  xdg-desktop-portal was started with XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL
 *  containing "web-authentication" (or "all"). With the gate off the interface is
 *  absent from introspection entirely - Properties.Get on it fails, and Start()
 *  gets UnknownMethod. That is not an error to report as a failure: it is
 *  ENTRA_WEBAUTH_UNAVAILABLE, the CLI maps it to exit 40, and the diagnostic
 *  should NAME THE ENVIRONMENT VARIABLE, because on a developer's machine that is
 *  almost always what is wrong.
 *
 *  The same "absent interface" also means: no session bus, nothing owning
 *  org.freedesktop.portal.Desktop, or a portal with the gate on but no backend
 *  configured for the interface - the frontend returns early when it finds no
 *  impl, so it exports nothing. Those cases are indistinguishable from the gate
 *  being off, by design, and all of them are "unavailable" so that a dispatcher
 *  can fall through to another provider such as FreeRDP's terminal paste flow.
 *
 *  The completion URI this client passes is always the one from its own cloud
 *  table. It is never taken from the client's caller. See
 *  docs/PUBLIC-INTERFACE.md.
 *
 *  Sketch only; nothing here is implemented.
 */

#define ENTRA_PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define ENTRA_PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define ENTRA_PORTAL_INTERFACE "org.freedesktop.portal.experimental.WebAuthentication"
#define ENTRA_PORTAL_REQUEST_INTERFACE "org.freedesktop.portal.Request"

/** What XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL must contain for
 *  ENTRA_PORTAL_INTERFACE to exist at all. This client cannot set it - it is read
 *  by xdg-desktop-portal at startup - and quotes it in the exit-40 diagnostic. */
#define ENTRA_PORTAL_EXPERIMENTAL_FLAG "web-authentication"

typedef enum
{
	ENTRA_WEBAUTH_COMPLETED = 0,  /**< @completion_uri is set */
	ENTRA_WEBAUTH_CANCELLED = 1,  /**< the user closed the window, or we called Close() */
	ENTRA_WEBAUTH_OTHER = 2,      /**< timeout, or the transaction ended some other way */
	ENTRA_WEBAUTH_UNAVAILABLE     /**< the interface is not exported: no bus, no portal,
	                                *  no backend, or the experimental gate is off.
	                                *  The CLI maps this to exit 40 and the message
	                                *  names ENTRA_PORTAL_EXPERIMENTAL_FLAG. */
} EntraWebAuthResult;

/** Whether the interface is exported, without starting a transaction. Checked by
 *  reading its `version` property, which fails cleanly when the gate is off. */
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
