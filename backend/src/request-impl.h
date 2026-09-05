/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_REQUEST_IMPL_H
#define WEBAUTH_REQUEST_IMPL_H

#include <gio/gio.h>

#include "xdp-impl-dbus.h"

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
 */

#define WEBAUTH_TYPE_IMPL_REQUEST (webauth_impl_request_get_type())
G_DECLARE_FINAL_TYPE(WebAuthImplRequest, webauth_impl_request, WEBAUTH, IMPL_REQUEST,
                     XdpImplRequestSkeleton)

/** @sender is xdg-desktop-portal's unique name; @app_id is the application
 *  identity the frontend derived, carried here only so the chrome can name it;
 *  @handle is the object path the frontend chose. */
WebAuthImplRequest* webauth_impl_request_new(const char* sender, const char* app_id,
                                             const char* handle);

gboolean webauth_impl_request_export(WebAuthImplRequest* request, GDBusConnection* connection,
                                     GError** error);
void webauth_impl_request_unexport(WebAuthImplRequest* request);
gboolean webauth_impl_request_is_exported(WebAuthImplRequest* request);

GCancellable* webauth_impl_request_get_cancellable(WebAuthImplRequest* request);
const char* webauth_impl_request_get_app_id(WebAuthImplRequest* request);

#endif /* WEBAUTH_REQUEST_IMPL_H */
