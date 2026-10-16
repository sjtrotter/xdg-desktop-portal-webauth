/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 *
 * Derived from xdg-desktop-portal-gtk's src/request.c, LGPL-2.1-or-later,
 * Copyright (C) 2016 Red Hat, Inc, by Alexander Larsson and Matthias Clasen, and
 * from the sibling backend xdg-desktop-portal-certificate's src/request-impl.c.
 * See docs/decisions/0004-license.md.
 */

#include "request-impl.h"

#include "redact.h"
#include "webauthentication-impl.h"

struct _WebAuthImplRequest
{
	XdpImplRequestSkeleton parent_instance;

	gboolean exported;
	char* sender;
	char* app_id;
	char* id;
	GCancellable* cancellable;
};

static void webauth_impl_request_iface_init(XdpImplRequestIface* iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(WebAuthImplRequest, webauth_impl_request, XDP_IMPL_TYPE_REQUEST_SKELETON,
                              G_IMPLEMENT_INTERFACE(XDP_IMPL_TYPE_REQUEST,
                                                    webauth_impl_request_iface_init))

/* The transaction handler returns FALSE so this default handler can unexport and answer Close. */
static gboolean webauth_impl_request_handle_close(XdpImplRequest* object,
                                                  GDBusMethodInvocation* invocation)
{
	/* Owner resolution can cancel the transaction holding the other Request reference. */
	g_autoptr(WebAuthImplRequest) request = g_object_ref(WEBAUTH_IMPL_REQUEST(object));

	/* Request paths are predictable; Close requires frontend authorization. */
	if (!webauth_backend_sender_is_frontend_default(
	        g_dbus_method_invocation_get_sender(invocation)))
	{
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_START_REFUSED, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "request-close-from-stranger", NULL);
		g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR,
		                                             G_DBUS_ERROR_ACCESS_DENIED,
		                                             "Only xdg-desktop-portal may call this interface");
		return TRUE;
	}

	g_cancellable_cancel(request->cancellable);

	if (request->exported)
		webauth_impl_request_unexport(request);

	xdp_impl_request_complete_close(object, invocation);

	return TRUE;
}

static void webauth_impl_request_iface_init(XdpImplRequestIface* iface)
{
	iface->handle_close = webauth_impl_request_handle_close;
}

static void webauth_impl_request_finalize(GObject* object)
{
	WebAuthImplRequest* request = WEBAUTH_IMPL_REQUEST(object);

	g_clear_object(&request->cancellable);
	g_clear_pointer(&request->sender, g_free);
	g_clear_pointer(&request->app_id, g_free);
	g_clear_pointer(&request->id, g_free);

	G_OBJECT_CLASS(webauth_impl_request_parent_class)->finalize(object);
}

static void webauth_impl_request_class_init(WebAuthImplRequestClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = webauth_impl_request_finalize;
}

static void webauth_impl_request_init(WebAuthImplRequest* request)
{
	request->cancellable = g_cancellable_new();
}

WebAuthImplRequest* webauth_impl_request_new(const char* sender, const char* app_id,
                                             const char* handle)
{
	WebAuthImplRequest* request = g_object_new(WEBAUTH_TYPE_IMPL_REQUEST, NULL);

	request->sender = g_strdup(sender);
	request->app_id = g_strdup(app_id);
	request->id = g_strdup(handle);

	return request;
}

gboolean webauth_impl_request_export(WebAuthImplRequest* request, GDBusConnection* connection,
                                     GError** error)
{
	if (request->exported)
		return TRUE;

	/* A request export failure must abort Start so the frontend can always cancel it. */
	if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(request), connection,
	                                      request->id, error))
		return FALSE;

	g_object_ref(request);
	request->exported = TRUE;

	return TRUE;
}

void webauth_impl_request_unexport(WebAuthImplRequest* request)
{
	if (!request->exported)
		return;

	request->exported = FALSE;
	g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(request));
	g_object_unref(request);
}

gboolean webauth_impl_request_is_exported(WebAuthImplRequest* request)
{
	return request->exported;
}

GCancellable* webauth_impl_request_get_cancellable(WebAuthImplRequest* request)
{
	return request->cancellable;
}

const char* webauth_impl_request_get_app_id(WebAuthImplRequest* request)
{
	return request->app_id;
}
