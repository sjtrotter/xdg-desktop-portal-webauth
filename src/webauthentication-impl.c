/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 *
 * The peer-identity rule and the transaction bookkeeping follow the sibling
 * backend xdg-desktop-portal-certificate's src/certificate-impl.c.
 */

#include "webauthentication-impl.h"

#include "completion.h"
#include "options.h"
#include "redact.h"
#include "request-impl.h"
#include "storage.h"
#include "transaction.h"
#include "webkit-session.h"
#include "xdp-impl-dbus.h"

struct WebAuthBackend
{
	GDBusConnection* connection;
	XdpImplWebAuthenticationX1* skeleton;

	/* The frontend's unique name, cached for refusals only; see
	 * webauth_backend_sender_is_frontend(). */
	char* frontend_owner;
	gboolean owner_resolved;
	guint frontend_watch;

	/* Every transaction with a window still open. Borrowed pointers: a
	 * transaction removes itself when it answers. The list exists so that the
	 * frontend going away can take the windows down with it. */
	GPtrArray* transactions;
};

/* ONE BACKEND PER PROCESS. The Request skeleton is exported by this object but
 * is a separate GObject with its own default handler, and that handler has to
 * ask the same question every method asks. */
static WebAuthBackend* singleton = NULL;

/* ------------------------------------------------------------ peer identity */

static void set_frontend_owner(WebAuthBackend* self, const char* owner)
{
	if (g_strcmp0(self->frontend_owner, owner) == 0)
		return;

	g_free(self->frontend_owner);
	self->frontend_owner = g_strdup(owner);

	webauth_log_event(G_LOG_LEVEL_DEBUG, WEBAUTH_EVENT_FRONTEND, "outcome", WEBAUTH_FIELD_OUTCOME,
	                  owner != NULL ? "present" : "gone", NULL);
}

/* THE WATCHER IS FOR CLEANUP; THE BUS IS THE AUTHORITY. D-Bus does not order
 * NameOwnerChanged against the messages of the process that lost the name, so a
 * cached owner alone would accept calls from a former frontend that is still
 * connected. */
static const char* resolve_frontend_owner(WebAuthBackend* self)
{
	g_autoptr(GVariant) reply = NULL;
	g_autoptr(GError) error = NULL;
	const char* owner = NULL;

	self->owner_resolved = TRUE;

	reply = g_dbus_connection_call_sync(
	    self->connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
	    "GetNameOwner", g_variant_new("(s)", WEBAUTH_FRONTEND_BUS_NAME), G_VARIANT_TYPE("(s)"),
	    G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, &error);

	/* NameHasNoOwner, or a bus that did not answer: nobody may call. Failing
	 * closed is the only safe direction here. */
	if (reply != NULL)
		g_variant_get(reply, "(&s)", &owner);

	set_frontend_owner(self, owner);

	return self->frontend_owner;
}

/* A NEGATIVE MAY COME OUT OF THE CACHE. A POSITIVE MAY NOT.
 *
 *   ACCEPTING a sender hands it Start with an app id of its choosing and a
 *   window on the user's screen, so it is never decided from a remembered
 *   answer: every accept asks the bus who owns the name now.
 *
 *   REFUSING one is free and cannot be wrong in a dangerous direction, so it is
 *   decided from the cached owner with no bus call at all -- and that matters,
 *   because anything on the session bus can send this process a message and a
 *   GetNameOwner round trip per stranger's message IS the denial of service. */
gboolean webauth_backend_sender_is_frontend(WebAuthBackend* self, const char* sender)
{
	if (self == NULL || sender == NULL)
		return FALSE;

	if (!self->owner_resolved)
		resolve_frontend_owner(self);

	if (self->frontend_owner == NULL || g_strcmp0(sender, self->frontend_owner) != 0)
		return FALSE;

	/* The cache says yes, which is exactly when it may not be believed. */
	resolve_frontend_owner(self);

	return self->frontend_owner != NULL && g_strcmp0(sender, self->frontend_owner) == 0;
}

gboolean webauth_backend_sender_is_frontend_default(const char* sender)
{
	return webauth_backend_sender_is_frontend(singleton, sender);
}

static gboolean reject_stranger(WebAuthBackend* self, GDBusMethodInvocation* invocation)
{
	if (webauth_backend_sender_is_frontend(self, g_dbus_method_invocation_get_sender(invocation)))
		return FALSE;

	/* Logged by outcome and never explained to the caller: a caller that is not
	 * the portal has no business learning why. */
	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_START_REFUSED, "outcome",
	                  WEBAUTH_FIELD_OUTCOME, "sender-not-frontend", NULL);

	g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR,
	                                              G_DBUS_ERROR_ACCESS_DENIED,
	                                              "Only xdg-desktop-portal may call this interface");
	return TRUE;
}

static void cancel_all(WebAuthBackend* self, const char* reason)
{
	g_autoptr(GPtrArray) doomed = g_ptr_array_new_with_free_func(
	    (GDestroyNotify) webauth_transaction_unref);

	for (guint i = 0; i < self->transactions->len; i++)
		g_ptr_array_add(doomed,
		                webauth_transaction_ref(g_ptr_array_index(self->transactions, i)));

	for (guint i = 0; i < doomed->len; i++)
		webauth_transaction_finish(g_ptr_array_index(doomed, i), WEBAUTH_RESPONSE_OTHER, NULL,
		                           reason);
}

static void on_frontend_appeared(GDBusConnection* connection, const char* name, const char* owner,
                                 gpointer user_data)
{
	set_frontend_owner(user_data, owner);
}

/* THE FRONTEND CAN VANISH. There is nobody left to answer, and a window
 * belonging to no request is the leaked window the interface promises not to
 * have. */
static void on_frontend_vanished(GDBusConnection* connection, const char* name, gpointer user_data)
{
	WebAuthBackend* self = user_data;

	set_frontend_owner(self, NULL);
	cancel_all(self, WEBAUTH_REASON_SESSION_TERMINATED);
}

/* ------------------------------------------------------------------- Start */

/* The transaction is held by the list until it answers; the slot is what the
 * finished hook needs in order to take it off again. */
typedef struct
{
	WebAuthBackend* backend;
	WebAuthTransaction* transaction;
} TransactionSlot;

static void forget_transaction(gpointer user_data)
{
	TransactionSlot* slot = user_data;

	g_ptr_array_remove_fast(slot->backend->transactions, slot->transaction);
	g_free(slot);
}

static void fail(GDBusMethodInvocation* invocation, WebAuthResponse response, const char* reason)
{
	g_auto(GVariantBuilder) results = G_VARIANT_BUILDER_INIT(G_VARIANT_TYPE_VARDICT);

	g_variant_builder_add(&results, "{sv}", "reason", g_variant_new_string(reason));

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_START_REFUSED, "reason",
	                  WEBAUTH_FIELD_OUTCOME, reason, NULL);

	g_dbus_method_invocation_return_value(
	    invocation,
	    g_variant_new("(u@a{sv})", (guint32) response, g_variant_builder_end(&results)));
}

static gboolean handle_start(XdpImplWebAuthenticationX1* object,
                             GDBusMethodInvocation* invocation, const char* arg_handle,
                             const char* arg_app_id, const char* arg_parent_window,
                             const char* arg_start_uri, const char* arg_completion_uri,
                             GVariant* arg_options, gpointer user_data)
{
	WebAuthBackend* self = user_data;
	g_autoptr(WebAuthImplRequest) request = NULL;
	g_autoptr(WebAuthTransaction) transaction = NULL;
	g_autoptr(GError) error = NULL;
	WebAuthWebkitSession* session = NULL;
	TransactionSlot* slot = NULL;
	WebAuthSessionMode mode = WEBAUTH_SESSION_SHARED;
	const char* session_mode = NULL;
	const char* app_identity_level = NULL;
	const char* title = NULL;
	const char* activation_token = NULL;
	guint timeout = WEBAUTH_DEFAULT_TIMEOUT;

	if (reject_stranger(self, invocation))
		return G_DBUS_METHOD_INVOCATION_HANDLED;

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_START_RECEIVED, "app_id",
	                  WEBAUTH_FIELD_APP_ID, arg_app_id, "uri", WEBAUTH_FIELD_URI_SHAPE,
	                  arg_start_uri, NULL);

	/* Validated again, because this backend's safety must not depend on a
	 * frontend having been correct. */
	if (!webauth_completion_start_uri_is_valid(arg_start_uri, &error) ||
	    !webauth_completion_uri_is_valid(arg_completion_uri, &error))
	{
		fail(invocation, WEBAUTH_RESPONSE_OTHER, WEBAUTH_REASON_INVALID_REQUEST);
		return G_DBUS_METHOD_INVOCATION_HANDLED;
	}

	session_mode = webauth_options_string(arg_options, "session_mode");
	if (!webauth_session_mode_parse(session_mode, &mode, &error))
	{
		fail(invocation, WEBAUTH_RESPONSE_OTHER, WEBAUTH_REASON_INVALID_REQUEST);
		return G_DBUS_METHOD_INVOCATION_HANDLED;
	}

	timeout = webauth_options_timeout(arg_options, WEBAUTH_DEFAULT_TIMEOUT, WEBAUTH_MAX_TIMEOUT);

	app_identity_level = webauth_options_string(arg_options, "app_identity_level");
	title = webauth_options_string(arg_options, "title");
	activation_token = webauth_options_string(arg_options, "activation_token");

	mode = webauth_storage_effective_mode(mode, arg_app_id);

	request = webauth_impl_request_new(g_dbus_method_invocation_get_sender(invocation), arg_app_id,
	                                   arg_handle);
	if (!webauth_impl_request_export(request, self->connection, &error))
	{
		fail(invocation, WEBAUTH_RESPONSE_OTHER, WEBAUTH_REASON_INVALID_REQUEST);
		return G_DBUS_METHOD_INVOCATION_HANDLED;
	}

	transaction = webauth_transaction_new(invocation, request, arg_app_id, app_identity_level,
	                                      arg_start_uri, arg_completion_uri, mode, timeout);

	slot = g_new0(TransactionSlot, 1);
	slot->backend = self;
	slot->transaction = transaction;
	webauth_transaction_set_finished(transaction, forget_transaction, slot);

	g_ptr_array_add(self->transactions, webauth_transaction_ref(transaction));

	session = webauth_webkit_session_new(transaction, arg_parent_window, activation_token, title,
	                                     &error);
	if (session == NULL)
	{
		webauth_transaction_finish(transaction, WEBAUTH_RESPONSE_OTHER, NULL,
		                           WEBAUTH_REASON_NO_STORAGE);
		return G_DBUS_METHOD_INVOCATION_HANDLED;
	}

	webauth_webkit_session_present(session);

	return G_DBUS_METHOD_INVOCATION_HANDLED;
}

/* ------------------------------------------------------------------ export */

WebAuthBackend* webauth_backend_new(GDBusConnection* bus, GError** error)
{
	WebAuthBackend* self = g_new0(WebAuthBackend, 1);

	self->connection = g_object_ref(bus);
	self->transactions =
	    g_ptr_array_new_with_free_func((GDestroyNotify) webauth_transaction_unref);
	self->skeleton = xdp_impl_web_authentication_x1_skeleton_new();

	xdp_impl_web_authentication_x1_set_version(self->skeleton, WEBAUTH_IMPL_INTERFACE_VERSION);

	g_signal_connect(self->skeleton, "handle-start", G_CALLBACK(handle_start), self);

	if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->skeleton), bus,
	                                      WEBAUTH_BACKEND_OBJECT_PATH, error))
	{
		webauth_backend_free(self);
		return NULL;
	}

	singleton = self;

	self->frontend_watch = g_bus_watch_name_on_connection(
	    bus, WEBAUTH_FRONTEND_BUS_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE, on_frontend_appeared,
	    on_frontend_vanished, self, NULL);

	return self;
}

void webauth_backend_shutdown(WebAuthBackend* self)
{
	if (self == NULL)
		return;

	cancel_all(self, WEBAUTH_REASON_SESSION_TERMINATED);
}

void webauth_backend_free(WebAuthBackend* self)
{
	if (self == NULL)
		return;

	if (self->frontend_watch != 0)
		g_bus_unwatch_name(self->frontend_watch);

	if (self->skeleton != NULL)
	{
		if (g_dbus_interface_skeleton_get_object_path(G_DBUS_INTERFACE_SKELETON(self->skeleton)) !=
		    NULL)
			g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(self->skeleton));
		g_clear_object(&self->skeleton);
	}

	g_clear_pointer(&self->transactions, g_ptr_array_unref);
	g_clear_pointer(&self->frontend_owner, g_free);
	g_clear_object(&self->connection);

	if (singleton == self)
		singleton = NULL;

	g_free(self);
}
