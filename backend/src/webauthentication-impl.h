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
 *  change. See docs/IMPL-INTERFACE.md and
 *  docs/decisions/0010-backend-only-frontend-lives-upstream.md.
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
 *                 completion_uri, options)   -- the argument order is the
 *                 branch XML's: app_id comes BEFORE parent_window
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
 *  - Resolve its own peer to identify the application. Its peer is
 *    xdg-desktop-portal. @app_id is a fact it is told; if it is empty the
 *    caller is unidentified and the chrome must say that rather than show an
 *    empty name or anything the application supplied. @app_id_kind
 *    ("sandboxed", "cgroup" or "host") says how much it can be trusted, and the
 *    backend renders the difference; it never re-derives it.
 *  - Accept a call from anyone but xdg-desktop-portal. See docs/SECURITY.md:
 *    the impl interface is not for applications, and the backend both checks
 *    the sender and is protected by D-Bus policy.
 *  - Re-decide policy the frontend already decided: session_mode arrives as a
 *    decision (a backend that cannot honour "ephemeral" must FAIL rather than
 *    quietly use the shared store), the timeout arrives clamped to 900 s, and
 *    the option vardict has already had unknown keys and handle_token dropped.
 *  - Trust the frontend's validation instead of doing its own. It validates the
 *    URIs again (completion.h). A backend that assumed a correct frontend would
 *    be a backend whose safety depended on a process it does not ship with --
 *    and note that the frontend re-checks the completion_uri THIS backend
 *    returns, before any application sees it, for the mirror-image reason.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_BACKEND_BUS_NAME "org.freedesktop.impl.portal.desktop.webauth"
#define WEBAUTH_BACKEND_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define WEBAUTH_IMPL_INTERFACE "org.freedesktop.impl.portal.experimental.WebAuthentication"
#define WEBAUTH_IMPL_INTERFACE_VERSION 1u

/** The only bus name whose owner may call this backend. */
#define WEBAUTH_FRONTEND_BUS_NAME "org.freedesktop.portal.Desktop"

/** Export the impl interface on @bus. Fails if no web engine and no display can
 *  be found: a backend that cannot show a page should not claim the interface,
 *  so that the frontend can fall through to another one. */
gboolean webauth_backend_init(GDBusConnection* bus, GError** error);

/** Whether @sender currently owns WEBAUTH_FRONTEND_BUS_NAME. A call from
 *  anything else is refused with an error and logged as an outcome symbol. */
gboolean webauth_backend_sender_is_frontend(const char* sender);

#endif /* WEBAUTH_WEBAUTHENTICATION_IMPL_H */
