/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <gio/gio.h>
#include <glib.h>

#include "entra-error.h"
#include "log/redact.h"
#include "webauth_client.h"

typedef struct
{
	GMainLoop* loop;
	guint32 response;
	char* completion_uri;
	char* reason;
	gboolean answered;
	gboolean timed_out;
} EntraWebAuthRun;

const char* entra_webauth_result_str(EntraWebAuthResult result)
{
	switch (result)
	{
		case ENTRA_WEBAUTH_COMPLETED:
			return "COMPLETED";
		case ENTRA_WEBAUTH_CANCELLED:
			return "CANCELLED";
		case ENTRA_WEBAUTH_OTHER:
			return "OTHER";
		case ENTRA_WEBAUTH_UNAVAILABLE:
		default:
			return "UNAVAILABLE";
	}
}

/* The gate, the missing backend, the missing bus and the missing portal are one
 * outcome by design: all of them mean "there is nothing here to ask". */
static void entra_webauth_set_unavailable(GError** error)
{
	g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_UNAVAILABLE,
	                    "the web authentication portal is not available.\n"
	                    "  " ENTRA_PORTAL_INTERFACE "\n"
	                    "  is experimental and is not exported unless xdg-desktop-portal was "
	                    "started with\n"
	                    "  XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=" ENTRA_PORTAL_EXPERIMENTAL_FLAG
	                    "\n"
	                    "  The same answer covers a portal with the gate on but no backend "
	                    "configured, and no session bus at all.");
}

gboolean entra_webauth_available(GError** error)
{
	g_autoptr(GDBusConnection) bus = NULL;
	g_autoptr(GVariant) reply = NULL;
	g_autoptr(GError) local = NULL;

	bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &local);
	if (bus == NULL)
	{
		entra_webauth_set_unavailable(error);
		return FALSE;
	}

	reply = g_dbus_connection_call_sync(
	    bus, ENTRA_PORTAL_BUS_NAME, ENTRA_PORTAL_OBJECT_PATH, "org.freedesktop.DBus.Properties",
	    "Get", g_variant_new("(ss)", ENTRA_PORTAL_INTERFACE, "version"), G_VARIANT_TYPE("(v)"),
	    G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &local);

	if (reply == NULL)
	{
		entra_webauth_set_unavailable(error);
		return FALSE;
	}

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_PORTAL, "outcome", ENTRA_FIELD_OUTCOME,
	                "available", NULL);
	return TRUE;
}

static void entra_on_response(GDBusConnection* connection, const char* sender, const char* path,
                              const char* interface, const char* signal, GVariant* parameters,
                              gpointer user_data)
{
	EntraWebAuthRun* run = user_data;
	g_autoptr(GVariant) results = NULL;
	guint32 response = 0;

	if (run->answered)
		return;

	g_variant_get(parameters, "(u@a{sv})", &response, &results);

	run->answered = TRUE;
	run->response = response;

	{
		g_autoptr(GVariant) uri = g_variant_lookup_value(results, "completion_uri",
		                                                 G_VARIANT_TYPE_STRING);
		g_autoptr(GVariant) reason = g_variant_lookup_value(results, "reason",
		                                                    G_VARIANT_TYPE_STRING);

		if (uri != NULL)
			run->completion_uri = g_variant_dup_string(uri, NULL);
		if (reason != NULL)
			run->reason = g_variant_dup_string(reason, NULL);
	}

	g_main_loop_quit(run->loop);
}

static gboolean entra_on_deadline(gpointer user_data)
{
	EntraWebAuthRun* run = user_data;

	run->timed_out = TRUE;
	g_main_loop_quit(run->loop);
	return G_SOURCE_REMOVE;
}

/* The request path the frontend will export, derived from this connection's unique
 * name and the handle_token, so that the Response subscription is in place BEFORE
 * Start is called and a fast completion cannot race it. */
static char* entra_request_path(GDBusConnection* bus, const char* token)
{
	g_autofree char* sender = g_strdup(g_dbus_connection_get_unique_name(bus) + 1);

	g_strdelimit(sender, ".", '_');
	return g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
}

EntraWebAuthResult entra_webauth_start(const char* parent_window, const char* start_uri,
                                       const char* completion_uri, const char* session_mode,
                                       const char* title, guint timeout_seconds,
                                       GCancellable* cancellable, char** completion_uri_out,
                                       char** reason_out, GError** error)
{
	g_autoptr(GDBusConnection) bus = NULL;
	g_autoptr(GVariant) reply = NULL;
	g_autoptr(GError) local = NULL;
	g_autofree char* token = NULL;
	g_autofree char* expected = NULL;
	g_autofree char* handle = NULL;
	GVariantBuilder options;
	EntraWebAuthRun run = { 0 };
	guint subscription = 0, extra = 0, deadline = 0;
	EntraWebAuthResult result = ENTRA_WEBAUTH_OTHER;

	if (completion_uri_out != NULL)
		*completion_uri_out = NULL;
	if (reason_out != NULL)
		*reason_out = NULL;

	bus = g_bus_get_sync(G_BUS_TYPE_SESSION, cancellable, &local);
	if (bus == NULL)
	{
		entra_webauth_set_unavailable(error);
		return ENTRA_WEBAUTH_UNAVAILABLE;
	}

	token = g_strdup_printf("entra%u", g_random_int());
	expected = entra_request_path(bus, token);

	run.loop = g_main_loop_new(NULL, FALSE);
	subscription = g_dbus_connection_signal_subscribe(
	    bus, NULL, ENTRA_PORTAL_REQUEST_INTERFACE, "Response", expected, NULL,
	    G_DBUS_SIGNAL_FLAGS_NONE, entra_on_response, &run, NULL);

	g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token));
	if (session_mode != NULL)
		g_variant_builder_add(&options, "{sv}", "session_mode", g_variant_new_string(session_mode));
	if (title != NULL)
		g_variant_builder_add(&options, "{sv}", "title", g_variant_new_string(title));
	if (timeout_seconds > 0)
		g_variant_builder_add(&options, "{sv}", "timeout", g_variant_new_uint32(timeout_seconds));

	reply = g_dbus_connection_call_sync(
	    bus, ENTRA_PORTAL_BUS_NAME, ENTRA_PORTAL_OBJECT_PATH, ENTRA_PORTAL_INTERFACE, "Start",
	    g_variant_new("(sssa{sv})", parent_window != NULL ? parent_window : "", start_uri,
	                  completion_uri, &options),
	    G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 30000, cancellable, &local);

	if (reply == NULL)
	{
		g_dbus_connection_signal_unsubscribe(bus, subscription);
		g_main_loop_unref(run.loop);

		if (g_error_matches(local, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_CANCELLED, "cancelled");
			return ENTRA_WEBAUTH_CANCELLED;
		}

		/* UnknownMethod, UnknownInterface, ServiceUnknown: the gate is off, or
		 * nothing owns the name. Every one of them is "nothing to ask". */
		entra_webauth_set_unavailable(error);
		return ENTRA_WEBAUTH_UNAVAILABLE;
	}

	g_variant_get(reply, "(o)", &handle);

	if (g_strcmp0(handle, expected) != 0)
	{
		extra = g_dbus_connection_signal_subscribe(
		    bus, NULL, ENTRA_PORTAL_REQUEST_INTERFACE, "Response", handle, NULL,
		    G_DBUS_SIGNAL_FLAGS_NONE, entra_on_response, &run, NULL);
	}

	deadline = g_timeout_add_seconds(timeout_seconds > 0 ? timeout_seconds + 15 : 300,
	                                 entra_on_deadline, &run);
	g_main_loop_run(run.loop);

	g_source_remove(deadline);
	g_dbus_connection_signal_unsubscribe(bus, subscription);
	if (extra != 0)
		g_dbus_connection_signal_unsubscribe(bus, extra);
	g_main_loop_unref(run.loop);

	if (run.timed_out)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
		                    "the portal never answered the request");
		result = ENTRA_WEBAUTH_OTHER;
	}
	else if (run.response == 0)
	{
		if (run.completion_uri == NULL)
		{
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
			                    "the portal reported success with no completion URI");
			result = ENTRA_WEBAUTH_OTHER;
		}
		else
		{
			result = ENTRA_WEBAUTH_COMPLETED;
		}
	}
	else if (run.response == 1)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_CANCELLED,
		                    "the sign-in window was closed");
		result = ENTRA_WEBAUTH_CANCELLED;
	}
	else
	{
		/* A 2 that means "no window could be shown at all" is unavailability and
		 * a dispatcher should fall through; any other 2 is this client's problem.
		 * The reason symbol is the only thing that can tell them apart. */
		if (g_strcmp0(run.reason, "no_backend") == 0 ||
		    g_strcmp0(run.reason, "backend_disappeared") == 0 ||
		    g_strcmp0(run.reason, "no_certificate_adapter") == 0 ||
		    g_strcmp0(run.reason, "no_display") == 0)
		{
			entra_webauth_set_unavailable(error);
			result = ENTRA_WEBAUTH_UNAVAILABLE;
		}
		else
		{
			g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
			            "the portal ended the transaction: %s",
			            run.reason != NULL ? run.reason : "no reason given");
			result = ENTRA_WEBAUTH_OTHER;
		}
	}

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_PORTAL, "outcome", ENTRA_FIELD_OUTCOME,
	                entra_webauth_result_str(result), "reason", ENTRA_FIELD_OUTCOME, run.reason,
	                NULL);

	if (result == ENTRA_WEBAUTH_COMPLETED && completion_uri_out != NULL)
		*completion_uri_out = g_steal_pointer(&run.completion_uri);
	if (reason_out != NULL)
		*reason_out = g_steal_pointer(&run.reason);

	g_free(run.completion_uri);
	g_free(run.reason);
	return result;
}
