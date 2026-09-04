/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_REQUEST_IMPL_H
#define WEBAUTH_REQUEST_IMPL_H

#include <gio/gio.h>

/** @file
 *  The backend side of a pending request: an object whose only job is Close().
 *
 *  xdg-desktop-portal-gtk's src/request.h is four fields - exported, sender,
 *  app_id, id - and two functions, request_export() and request_unexport(). So
 *  is this. The backend exports one of these at the @handle path
 *  xdg-desktop-portal passed to Start, on the BACKEND's own bus name, and
 *  connects "handle-close" to the transaction. When the application calls
 *  Close() on the public Request, the portal calls Close() on this one, and the
 *  window goes away. The interface is upstream's own and is not redefined here.
 *
 *  It carries no Response signal. The backend answers by returning from Start.
 *
 *  The lifetime rule, copied from upstream and easy to get wrong: unexport the
 *  request BEFORE completing the method call, so that a Close() arriving in the
 *  same instant finds nothing to close rather than reaching a transaction that
 *  has already answered.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_IMPL_REQUEST_INTERFACE "org.freedesktop.impl.portal.Request"

typedef struct WebAuthImplRequest WebAuthImplRequest;

/** @sender is xdg-desktop-portal's unique name; @app_id is the application
 *  identity the frontend derived, carried here only so the chrome and any
 *  certificate dialog can name it; @id is the object path the frontend chose. */
WebAuthImplRequest* webauth_impl_request_new(const char* sender, const char* app_id,
                                             const char* id);

void webauth_impl_request_export(WebAuthImplRequest* self, GDBusConnection* connection);
void webauth_impl_request_unexport(WebAuthImplRequest* self);

void webauth_impl_request_free(WebAuthImplRequest* self);

#endif /* WEBAUTH_REQUEST_IMPL_H */
