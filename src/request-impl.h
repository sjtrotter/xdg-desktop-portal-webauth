/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_REQUEST_IMPL_H
#define WEBAUTH_REQUEST_IMPL_H

#include <gio/gio.h>

#include "xdp-impl-dbus.h"

/** The Request is exported on the backend bus name and must be unexported before Start returns. */

#define WEBAUTH_TYPE_IMPL_REQUEST (webauth_impl_request_get_type())
G_DECLARE_FINAL_TYPE(WebAuthImplRequest, webauth_impl_request, WEBAUTH, IMPL_REQUEST,
                     XdpImplRequestSkeleton)

/** @sender is the frontend's unique name; @app_id and @handle are supplied by the frontend. */
WebAuthImplRequest* webauth_impl_request_new(const char* sender, const char* app_id,
                                             const char* handle);

gboolean webauth_impl_request_export(WebAuthImplRequest* request, GDBusConnection* connection,
                                     GError** error);
void webauth_impl_request_unexport(WebAuthImplRequest* request);
gboolean webauth_impl_request_is_exported(WebAuthImplRequest* request);

GCancellable* webauth_impl_request_get_cancellable(WebAuthImplRequest* request);
const char* webauth_impl_request_get_app_id(WebAuthImplRequest* request);

#endif /* WEBAUTH_REQUEST_IMPL_H */
