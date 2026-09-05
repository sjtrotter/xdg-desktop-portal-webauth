/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_WEBAUTHENTICATION_IMPL_H
#define WEBAUTH_WEBAUTHENTICATION_IMPL_H

#include <gio/gio.h>

/** @file
 *  This backend's implementation of
 *  org.freedesktop.impl.portal.experimental.WebAuthentication: the impl
 *  skeleton and its one handler.
 *
 *  The interface is defined by the xdg-desktop-portal branch
 *  experimental/certificate-webauthentication (commit 3a32e9b), and the copy of
 *  the XML in data/ tracks that branch verbatim. It is not this repository's to
 *  change. See docs/IMPL-INTERFACE.md.
 *
 *  This is xdg-desktop-portal-gtk's src/account.c under another name:
 *
 *    webauth_backend_init(bus)
 *      -> create the impl skeleton and export it at
 *         WEBAUTH_BACKEND_OBJECT_PATH on THIS process's own bus name
 *
 *    handle_start(invocation, handle, app_id, parent_window, start_uri,
 *                 completion_uri, options)   -- the argument order is the
 *                 branch XML's: app_id comes BEFORE parent_window
 *      -> export an impl Request object at @handle and connect "handle-close"
 *      -> build the transaction: deadline, storage mode, chrome
 *      -> parse @parent_window and open the window
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
 *  - Resolve its own peer to identify the application. Its peer is
 *    xdg-desktop-portal. @app_id is a fact it is told; if it is empty the
 *    caller is unidentified and the chrome must say that rather than show an
 *    empty name or anything the application supplied.
 *  - Accept a call from anyone but xdg-desktop-portal.
 *  - Re-decide policy the frontend already decided: session_mode arrives as a
 *    decision, the timeout arrives clamped, and the option vardict has already
 *    had unknown keys and handle_token dropped.
 *  - Trust the frontend's validation instead of doing its own. It validates the
 *    URIs again (completion.h), because its safety must not depend on a process
 *    it does not ship with -- and note that the frontend re-checks the
 *    completion_uri THIS backend returns, for the mirror-image reason.
 */

#define WEBAUTH_BACKEND_BUS_NAME "org.freedesktop.impl.portal.desktop.webauth"
#define WEBAUTH_BACKEND_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define WEBAUTH_IMPL_INTERFACE "org.freedesktop.impl.portal.experimental.WebAuthentication"
#define WEBAUTH_IMPL_INTERFACE_VERSION 1u

/** The only bus name whose owner may call this backend. */
#define WEBAUTH_FRONTEND_BUS_NAME "org.freedesktop.portal.Desktop"

/** The frontend's own ceiling and default, repeated here because a backend that
 *  trusted them would be a backend whose deadline depended on a process it does
 *  not ship with. */
#define WEBAUTH_DEFAULT_TIMEOUT 300u
#define WEBAUTH_MAX_TIMEOUT 900u

typedef struct WebAuthBackend WebAuthBackend;

/** Export the impl interface on @bus. */
WebAuthBackend* webauth_backend_new(GDBusConnection* bus, GError** error);

/** Answer every transaction still running, tear their windows down and stop
 *  accepting new ones. */
void webauth_backend_shutdown(WebAuthBackend* backend);

void webauth_backend_free(WebAuthBackend* backend);

/** Whether @sender currently owns WEBAUTH_FRONTEND_BUS_NAME. A call from
 *  anything else is refused with AccessDenied and logged as an outcome symbol. */
gboolean webauth_backend_sender_is_frontend(WebAuthBackend* backend, const char* sender);

/** The same question asked from the Request skeleton, which has no backend
 *  pointer of its own. There is one backend per process. */
gboolean webauth_backend_sender_is_frontend_default(const char* sender);

#endif /* WEBAUTH_WEBAUTHENTICATION_IMPL_H */
