/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_WEBAUTHENTICATION_H
#define WEBAUTH_GTK_WEBAUTHENTICATION_H

#include <gio/gio.h>

/** @file
 *  The WebAuthentication1 backend: the impl skeleton and its one handler.
 *
 *  This is xdg-desktop-portal-gtk's src/account.c under another name, and it
 *  does what that file does:
 *
 *    webauth_backend_init(bus)
 *      -> create the impl skeleton
 *      -> connect "handle-start"
 *      -> export it at WEBAUTH_BACKEND_OBJECT_PATH, the same path the frontend
 *         uses, on THIS process's own bus name
 *
 *    handle_start(invocation, handle, app_id, parent_window, start_uri,
 *                 completion_uri, options)
 *      -> export an impl Request object at @handle (request.h) and connect
 *         "handle-close" to the transaction
 *      -> build the transaction (transaction.h): deadline, partition, chrome
 *      -> parse @parent_window (externalwindow.h) and open the window
 *      -> ... the user authenticates ...
 *      -> unexport the request and RETURN FROM THE METHOD with
 *         (response, results)
 *
 *  Note the asymmetry, which is upstream's and is deliberate: the frontend
 *  answers its caller with a SIGNAL on a Request object, and the backend answers
 *  the frontend by RETURNING from a method call whose D-Bus timeout the frontend
 *  has set to G_MAXINT. A backend therefore has no way to emit a result twice,
 *  and the "exactly one Response" guarantee lives entirely in the frontend.
 *
 *  WHAT THIS BACKEND MUST NOT DO, each of which would be a real bug in a
 *  process that has a D-Bus connection and a window:
 *
 *  - Resolve its own peer to identify the application. Its peer is the
 *    frontend. @app_id is a fact it is told; if it is empty the caller is
 *    unidentified and the chrome says so.
 *  - Accept a call from anyone but the frontend. See docs/SECURITY.md: the impl
 *    interface is not for applications, and the backend both checks the sender
 *    and is protected by D-Bus policy.
 *  - Re-decide policy the frontend already decided: session_mode arrives as a
 *    decision, the timeout arrives clamped, and the option vardict has already
 *    had unknown keys dropped.
 *  - Trust the frontend's validation instead of doing its own. It validates the
 *    URIs again (completion.h). A backend that assumed a correct frontend would
 *    be a backend whose safety depended on a process it does not ship with.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_BACKEND_BUS_NAME "io.github.sjtrotter.impl.portal.desktop.gtk"
#define WEBAUTH_BACKEND_OBJECT_PATH "/io/github/sjtrotter/portal/desktop"
#define WEBAUTH_IMPL_INTERFACE "io.github.sjtrotter.impl.portal.WebAuthentication1"
#define WEBAUTH_IMPL_INTERFACE_VERSION 1u

/** Export the impl interface on @bus. Fails if no web engine and no display can
 *  be found: a backend that cannot show a page should not claim the interface,
 *  so that the frontend can fall through to another one. */
gboolean webauth_backend_init(GDBusConnection* bus, GError** error);

/** Whether @sender is the portal frontend this backend serves. A call from
 *  anything else is refused with an error and logged as an outcome symbol. */
gboolean webauth_backend_sender_is_frontend(const char* sender);

#endif /* WEBAUTH_GTK_WEBAUTHENTICATION_H */
