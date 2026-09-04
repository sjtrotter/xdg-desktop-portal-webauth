/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_PORTAL_REQUEST_H
#define WEBAUTH_PORTAL_REQUEST_H

#include <gio/gio.h>

#include "app-info.h"

/** @file
 *  The public Request object: one pending portal request, in the frontend.
 *
 *  This is xdg-desktop-portal's request.c pattern under this project's names.
 *  Upstream the same object lives in desktop-portal/xdp-request.[ch] (it was
 *  src/request.c before the tree was reorganised) and carries exactly these
 *  fields: exported flag, id (the object path), sender, a mutex, the caller's
 *  app info, and a proxy for the BACKEND's own impl Request object so that a
 *  Close() from the application can be forwarded to the backend.
 *
 *  Path convention, copied exactly:
 *
 *    /io/github/sjtrotter/portal/desktop/request/<SENDER>/<TOKEN>
 *
 *  where <SENDER> is the caller's unique bus name with the leading ':' removed
 *  and every '.' replaced by '_', and <TOKEN> is the caller's own handle_token
 *  option. The caller can therefore compute the path and subscribe to Response
 *  BEFORE calling Start, which is the whole point of the convention: a fast
 *  completion cannot race the subscription.
 *  https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html
 *
 *  What lives here and not in the backend: the object the APPLICATION holds,
 *  the binding of the result to the initiating unique connection, and the
 *  guarantee of exactly one Response. What lives in the backend and not here:
 *  the window, the deadline it enforces itself, and the certificate adapter's
 *  lifetime (backends/gtk/src/transaction.h). The two halves must agree about
 *  the races, and the races are specified rather than discovered:
 *
 *    - a committed completion wins over a simultaneous Close();
 *    - Close() never yields a later success;
 *    - caller bus disconnection cancels immediately, and nobody else is told;
 *    - a timeout answers 2;
 *    - a backend that disappears answers 2 exactly once, with reason
 *      "backend_disappeared" - the frontend owns this, because the application
 *      is owed a response even when the process that was going to produce it
 *      has died. This obligation does not exist in a single-process design and
 *      is one of the costs decisions/0008 accepts;
 *    - every late event after the terminal result is discarded.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_PORTAL_BUS_NAME "io.github.sjtrotter.portal.Desktop"
#define WEBAUTH_PORTAL_OBJECT_PATH "/io/github/sjtrotter/portal/desktop"
#define WEBAUTH_PORTAL_REQUEST_INTERFACE "io.github.sjtrotter.portal.Request"

typedef enum
{
	WEBAUTH_RESPONSE_COMPLETED = 0, /**< @results carries "completion_uri" */
	WEBAUTH_RESPONSE_CANCELLED = 1, /**< the user closed the window, or Close() was called */
	WEBAUTH_RESPONSE_OTHER = 2      /**< timeout, no backend, no display, backend died */
} WebAuthResponse;

typedef struct WebAuthRequest WebAuthRequest;

/** Mint the request for @invocation: derive the object path from the sender and
 *  the handle_token, and attach the resolved caller identity. Rejects a
 *  handle_token that is not a valid object path element. */
WebAuthRequest* webauth_request_new(GDBusMethodInvocation* invocation, const WebAuthAppInfo* app_info,
                                    GError** error);

const char* webauth_request_object_path(WebAuthRequest* self);

/** Export the request before the backend is called, so a backend that responds
 *  immediately cannot respond to an object that does not exist yet. */
void webauth_request_export(WebAuthRequest* self, GDBusConnection* connection);
void webauth_request_unexport(WebAuthRequest* self);

/** Remember the backend's impl Request proxy, so Close() can be forwarded. The
 *  backend exports its own object at the SAME path on its own bus name. */
void webauth_request_set_impl_request(WebAuthRequest* self, GDBusProxy* impl_request);

/** Emit Response exactly once and unexport. @return TRUE if this call is the one
 *  that completed the request; every later call is a discarded late event. */
gboolean webauth_request_respond(WebAuthRequest* self, WebAuthResponse response,
                                 const char* completion_uri, const char* reason);

WebAuthRequest* webauth_request_ref(WebAuthRequest* self);
void webauth_request_unref(WebAuthRequest* self);

#endif /* WEBAUTH_PORTAL_REQUEST_H */
