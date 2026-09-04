/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ENTRA_WEBAUTH_CLIENT_H
#define ENTRA_WEBAUTH_CLIENT_H

#include <glib.h>

/** @file
 *  Calling io.github.sjtrotter.WebAuthentication1: the client's only interactive
 *  dependency.
 *
 *  Everything interactive this client does is one Start call and one Response. It
 *  hands over a URI to open and the exact URI whose navigation ends the flow, and
 *  gets back the URI the flow ended at. The service knows nothing about OAuth, and
 *  the client owns no windows, no web view, no certificate chooser and no PIN prompt.
 *  That division is the point of the two-layer design: an RDP client should not
 *  contain a browser, and a browser should not contain an OAuth stack.
 *
 *  If no service is reachable — no session bus, nothing implementing the interface,
 *  no display — this reports unavailable rather than failing, so the caller can fall
 *  through to another provider such as FreeRDP's terminal paste flow.
 *
 *  The completion URI this client passes is always the one from its own cloud table.
 *  It is never taken from the client's caller. See docs/SERVICE-INTERFACE.md.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef enum
{
	ENTRA_WEBAUTH_COMPLETED = 0,  /**< @completion_uri is set */
	ENTRA_WEBAUTH_CANCELLED = 1,  /**< the user closed the window, or we called Close() */
	ENTRA_WEBAUTH_OTHER = 2,      /**< timeout, or the transaction ended some other way */
	ENTRA_WEBAUTH_UNAVAILABLE     /**< nothing implementing the interface to call */
} EntraWebAuthResult;

/** Whether the service can be reached, without starting a transaction. */
gboolean entra_webauth_available(GError** error);

/** Run one interactive transaction and return the completion URI. Subscribes to
 *  Response on the handle derived from a fresh handle_token BEFORE calling Start, so
 *  a fast completion cannot race the subscription. Blocks until the service responds,
 *  the timeout expires, or @cancellable fires — in which case it calls Close() and
 *  waits for the response it is still owed. */
EntraWebAuthResult entra_webauth_start(const char* parent_window, const char* activation_token,
                                       const char* start_uri, const char* completion_uri,
                                       const char* session_mode, guint timeout_seconds,
                                       GCancellable* cancellable, char** completion_uri_out,
                                       GError** error);

#endif /* ENTRA_WEBAUTH_CLIENT_H */
