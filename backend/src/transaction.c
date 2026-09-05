/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "transaction.h"

#include "redact.h"
#include "webauthentication-impl.h"

struct WebAuthTransaction
{
	int ref_count;

	GDBusMethodInvocation* invocation; /* completed exactly once, in finish() */
	WebAuthImplRequest* request;

	char* app_id;
	char* app_id_kind;
	char* start_uri;
	char* completion_uri;
	WebAuthSessionMode mode;

	guint timeout_seconds;
	guint deadline_id;
	gint64 started;

	WebAuthTransactionHook teardown;
	gpointer teardown_data;
	WebAuthTransactionHook finished;
	gpointer finished_data;

	gulong close_id;

	gboolean done;
};

/* THE CLOSE PATH. The handler is connected here rather than by the backend so
 * that the transaction, which already owns the Request, is the only thing
 * holding it -- a Request holding a reference back to its transaction would be a
 * cycle neither could leave. It is disconnected in finish() for the same
 * reason. Returning FALSE continues emission into the Request's own default
 * handler, which unexports and answers Close(). */
static gboolean on_request_close(XdpImplRequest* object, GDBusMethodInvocation* invocation,
                                 gpointer user_data)
{
	WebAuthTransaction* self = user_data;

	if (!webauth_backend_sender_is_frontend_default(
	        g_dbus_method_invocation_get_sender(invocation)))
		return FALSE;

	webauth_transaction_finish(self, WEBAUTH_RESPONSE_CANCELLED, NULL,
	                           WEBAUTH_REASON_REQUEST_CLOSED);

	return FALSE;
}

WebAuthTransaction* webauth_transaction_new(GDBusMethodInvocation* invocation,
                                            WebAuthImplRequest* request, const char* app_id,
                                            const char* app_id_kind, const char* start_uri,
                                            const char* completion_uri, WebAuthSessionMode mode,
                                            guint timeout_seconds)
{
	WebAuthTransaction* self = g_new0(WebAuthTransaction, 1);

	self->ref_count = 1;
	self->invocation = invocation;
	self->request = g_object_ref(request);
	self->app_id = g_strdup(app_id);
	self->app_id_kind = g_strdup(app_id_kind);
	self->start_uri = g_strdup(start_uri);
	self->completion_uri = g_strdup(completion_uri);
	self->mode = mode;
	self->timeout_seconds = timeout_seconds;
	self->started = g_get_monotonic_time();

	self->close_id =
	    g_signal_connect(request, "handle-close", G_CALLBACK(on_request_close), self);

	return self;
}

WebAuthTransaction* webauth_transaction_ref(WebAuthTransaction* self)
{
	g_atomic_int_inc(&self->ref_count);
	return self;
}

void webauth_transaction_unref(WebAuthTransaction* self)
{
	if (self == NULL || !g_atomic_int_dec_and_test(&self->ref_count))
		return;

	g_clear_handle_id(&self->deadline_id, g_source_remove);
	g_clear_signal_handler(&self->close_id, self->request);
	g_clear_object(&self->request);
	g_clear_pointer(&self->app_id, g_free);
	g_clear_pointer(&self->app_id_kind, g_free);
	g_clear_pointer(&self->start_uri, g_free);
	g_clear_pointer(&self->completion_uri, g_free);
	g_free(self);
}

static gboolean on_deadline(gpointer user_data)
{
	WebAuthTransaction* self = user_data;

	self->deadline_id = 0;
	webauth_transaction_finish(self, WEBAUTH_RESPONSE_OTHER, NULL, WEBAUTH_REASON_TIMEOUT);

	return G_SOURCE_REMOVE;
}

void webauth_transaction_start_deadline(WebAuthTransaction* self)
{
	if (self->done || self->deadline_id != 0 || self->timeout_seconds == 0)
		return;

	self->deadline_id = g_timeout_add_seconds(self->timeout_seconds, on_deadline, self);
}

void webauth_transaction_set_teardown(WebAuthTransaction* self, WebAuthTransactionHook hook,
                                      gpointer user_data)
{
	self->teardown = hook;
	self->teardown_data = user_data;
}

void webauth_transaction_set_finished(WebAuthTransaction* self, WebAuthTransactionHook hook,
                                      gpointer user_data)
{
	self->finished = hook;
	self->finished_data = user_data;
}

gboolean webauth_transaction_finish(WebAuthTransaction* self, WebAuthResponse response,
                                    const char* completion_uri, const char* reason)
{
	g_auto(GVariantBuilder) results = G_VARIANT_BUILDER_INIT(G_VARIANT_TYPE_VARDICT);
	g_autofree char* duration = NULL;
	GDBusMethodInvocation* invocation = NULL;

	/* Every late event after the terminal result is discarded, and that is the
	 * only thing keeping "exactly one answer" true when a completion and a
	 * Close() arrive in the same main loop iteration. */
	if (self->done)
		return FALSE;

	self->done = TRUE;

	/* The teardown and the finished hook between them drop the last two
	 * references, so this function holds one of its own until it has answered. */
	webauth_transaction_ref(self);

	g_clear_handle_id(&self->deadline_id, g_source_remove);
	g_clear_signal_handler(&self->close_id, self->request);

	if (self->teardown != NULL)
		self->teardown(self->teardown_data);

	/* Unexported BEFORE the method call is completed, so that a Close() arriving
	 * in the same instant finds nothing to close. */
	webauth_impl_request_unexport(self->request);

	if (response == WEBAUTH_RESPONSE_COMPLETED && completion_uri != NULL)
		g_variant_builder_add(&results, "{sv}", "completion_uri",
		                      g_variant_new_string(completion_uri));

	if (reason != NULL)
		g_variant_builder_add(&results, "{sv}", "reason", g_variant_new_string(reason));

	duration = g_strdup_printf("%" G_GINT64_FORMAT,
	                           (g_get_monotonic_time() - self->started) / G_USEC_PER_SEC);

	webauth_log_event(G_LOG_LEVEL_MESSAGE,
	                  response == WEBAUTH_RESPONSE_COMPLETED  ? WEBAUTH_EVENT_COMPLETED
	                  : response == WEBAUTH_RESPONSE_CANCELLED ? WEBAUTH_EVENT_CANCELLED
	                                                           : WEBAUTH_EVENT_TIMEOUT,
	                  "app_id", WEBAUTH_FIELD_APP_ID, self->app_id, "reason",
	                  WEBAUTH_FIELD_OUTCOME, reason != NULL ? reason : "-", "seconds",
	                  WEBAUTH_FIELD_DURATION, duration, NULL);

	invocation = g_steal_pointer(&self->invocation);
	g_dbus_method_invocation_return_value(
	    invocation, g_variant_new("(u@a{sv})", (guint32) response, g_variant_builder_end(&results)));

	if (self->finished != NULL)
		self->finished(self->finished_data);

	webauth_transaction_unref(self);

	return TRUE;
}

gboolean webauth_transaction_is_done(WebAuthTransaction* self)
{
	return self->done;
}

const char* webauth_transaction_start_uri(WebAuthTransaction* self)
{
	return self->start_uri;
}

const char* webauth_transaction_completion_uri(WebAuthTransaction* self)
{
	return self->completion_uri;
}

const char* webauth_transaction_app_id(WebAuthTransaction* self)
{
	return self->app_id;
}

const char* webauth_transaction_app_id_kind(WebAuthTransaction* self)
{
	return self->app_id_kind;
}

WebAuthSessionMode webauth_transaction_session_mode(WebAuthTransaction* self)
{
	return self->mode;
}

GCancellable* webauth_transaction_cancellable(WebAuthTransaction* self)
{
	return webauth_impl_request_get_cancellable(self->request);
}
