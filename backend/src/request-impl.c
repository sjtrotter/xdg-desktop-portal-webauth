/* SPDX-License-Identifier: GPL-2.0-or-later
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

/* THE DEFAULT Close() HANDLER. The transaction connects its own handler to
 * "handle-close" to answer its pending Start with response 1; that handler
 * returns FALSE so emission continues into this class closure, which is the only
 * place that unexports and answers Close() itself. Splitting it that way is
 * upstream's idiom and it is what keeps "answered exactly once" true on both the
 * response path and the close path. */
static gboolean webauth_impl_request_handle_close(XdpImplRequest* object,
                                                  GDBusMethodInvocation* invocation)
{
	/* The reference comes first: resolving who owns the frontend name can
	 * discover that it has changed hands, and that discovery cancels every
	 * transaction the previous owner created -- which is the one holding the
	 * other reference to this Request. */
	g_autoptr(WebAuthImplRequest) request = g_object_ref(WEBAUTH_IMPL_REQUEST(object));

	/* CLOSE IS AUTHORISED LIKE EVERY OTHER METHOD. A request path is
	 * /org/freedesktop/portal/desktop/request/<sender>/<handle_token> and the
	 * token is very often a fixed string, so anything on the bus could
	 * otherwise cancel sign-in windows as they appeared. */
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

	/* A failure here aborts the call: a transaction with no Request on the bus
	 * is a window the frontend cannot cancel. */
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
