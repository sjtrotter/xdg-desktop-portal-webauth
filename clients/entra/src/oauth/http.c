/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>
#include <libsoup/soup.h>

#include "../entra-error.h"
#include "../log/redact.h"
#include "http.h"

#define ENTRA_HTTP_TIMEOUT_SECONDS 30

struct _EntraHttp
{
	SoupSession* session;
};

EntraHttp* entra_http_new(EntraConfig* config, GError** error)
{
	EntraHttp* self = g_new0(EntraHttp, 1);
	const char* anchor = entra_config_trust_certificate(config);

	self->session = soup_session_new();
	soup_session_set_timeout(self->session, ENTRA_HTTP_TIMEOUT_SECONDS);
	soup_session_set_idle_timeout(self->session, 1);
	soup_session_set_user_agent(self->session, "entra-token-helper/" ENTRA_VERSION " ");

	if (anchor != NULL)
	{
		g_autoptr(GTlsDatabase) database = g_tls_file_database_new(anchor, error);

		if (database == NULL)
		{
			entra_http_free(self);
			return NULL;
		}

		soup_session_set_tls_database(self->session, database);
	}

	return self;
}

static GBytes* entra_http_send(EntraHttp* self, SoupMessage* message, guint* status_out,
                               GCancellable* cancellable, GError** error)
{
	g_autoptr(GBytes) body = NULL;
	g_autoptr(GError) local = NULL;
	guint status;

	body = soup_session_send_and_read(self->session, message, cancellable, &local);
	status = soup_message_get_status(message);

	if (body == NULL)
	{
		g_autofree char* safe = entra_redact_error_text(local != NULL ? local->message : NULL);

		g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_UNAVAILABLE, "the authority is unreachable: %s",
		            safe);
		return NULL;
	}

	if (status_out != NULL)
		*status_out = status;

	return g_steal_pointer(&body);
}

GBytes* entra_http_get(EntraHttp* self, const char* url, GCancellable* cancellable, GError** error)
{
	g_autoptr(SoupMessage) message = soup_message_new(SOUP_METHOD_GET, url);
	g_autoptr(GBytes) body = NULL;
	guint status = 0;

	if (message == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL, "could not build a request");
		return NULL;
	}

	body = entra_http_send(self, message, &status, cancellable, error);
	if (body == NULL)
		return NULL;

	if (status < 200 || status >= 300)
	{
		g_autofree char* code = g_strdup_printf("%u", status);

		g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_UNAVAILABLE, "the authority answered HTTP %s",
		            code);
		return NULL;
	}

	return g_steal_pointer(&body);
}

GBytes* entra_http_post_form(EntraHttp* self, const char* url, const char* body, guint* status_out,
                             GCancellable* cancellable, GError** error)
{
	g_autoptr(SoupMessage) message = soup_message_new(SOUP_METHOD_POST, url);
	g_autoptr(GBytes) payload = NULL;

	if (message == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL, "could not build a request");
		return NULL;
	}

	payload = g_bytes_new(body, strlen(body));
	soup_message_set_request_body_from_bytes(message, "application/x-www-form-urlencoded", payload);

	return entra_http_send(self, message, status_out, cancellable, error);
}

void entra_http_free(EntraHttp* self)
{
	if (self == NULL)
		return;

	g_clear_object(&self->session);
	g_free(self);
}
