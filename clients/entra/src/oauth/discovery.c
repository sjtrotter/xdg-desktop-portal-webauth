/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "../entra-error.h"
#include "../log/redact.h"
#include "discovery.h"

gboolean entra_endpoints_derive(const EntraAuthority* authority, EntraEndpoints* out, GError** error)
{
	g_return_val_if_fail(out != NULL, FALSE);

	if (authority == NULL || authority->base == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE, "no authority");
		return FALSE;
	}

	g_clear_pointer(&out->authorization_endpoint, g_free);
	g_clear_pointer(&out->token_endpoint, g_free);

	out->authorization_endpoint = g_strconcat(authority->base, "/oauth2/v2.0/authorize", NULL);
	out->token_endpoint = g_strconcat(authority->base, "/oauth2/v2.0/token", NULL);
	return TRUE;
}

char* entra_discovery_url(const EntraAuthority* authority)
{
	if (authority == NULL || authority->base == NULL)
		return NULL;

	return g_strconcat(authority->base, "/v2.0/.well-known/openid-configuration", NULL);
}

/* https, and on the authority's own host. Anything else is discarded rather than
 * used: a discovery document is not a licence to move the exchange. */
static char* entra_endpoint_accept(JsonObject* object, const char* member,
                                   const char* expected_host)
{
	const char* value = NULL;
	g_autoptr(GUri) uri = NULL;
	g_autofree char* host = NULL;

	if (!json_object_has_member(object, member))
		return NULL;

	value = json_object_get_string_member(object, member);
	if (value == NULL)
		return NULL;

	uri = g_uri_parse(value, G_URI_FLAGS_NONE, NULL);
	if (uri == NULL || g_uri_get_host(uri) == NULL)
		return NULL;

	if (g_ascii_strcasecmp(g_uri_get_scheme(uri), "https") != 0)
		return NULL;

	if (g_uri_get_port(uri) > 0)
		host = g_strdup_printf("%s:%d", g_uri_get_host(uri), g_uri_get_port(uri));
	else
		host = g_strdup(g_uri_get_host(uri));

	if (g_ascii_strcasecmp(host, expected_host) != 0)
		return NULL;

	return g_strdup(value);
}

gboolean entra_endpoints_parse_document(const char* document, gsize length,
                                        const char* expected_host, EntraEndpoints* out,
                                        GError** error)
{
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autofree char* authorize = NULL;
	g_autofree char* token = NULL;
	JsonNode* root = NULL;
	JsonObject* object = NULL;

	g_return_val_if_fail(out != NULL, FALSE);
	g_return_val_if_fail(expected_host != NULL, FALSE);

	if (!json_parser_load_from_data(parser, document, (gssize)length, NULL))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_SERVER,
		                    "the OpenID configuration is not JSON");
		return FALSE;
	}

	root = json_parser_get_root(parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_SERVER,
		                    "the OpenID configuration is not an object");
		return FALSE;
	}

	object = json_node_get_object(root);
	authorize = entra_endpoint_accept(object, "authorization_endpoint", expected_host);
	token = entra_endpoint_accept(object, "token_endpoint", expected_host);

	if (authorize == NULL || token == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_SERVER,
		                    "the OpenID configuration names no usable endpoint on this authority");
		return FALSE;
	}

	g_free(out->authorization_endpoint);
	g_free(out->token_endpoint);
	out->authorization_endpoint = g_steal_pointer(&authorize);
	out->token_endpoint = g_steal_pointer(&token);
	return TRUE;
}

gboolean entra_endpoints_discover(EntraHttp* http, const EntraAuthority* authority,
                                  EntraEndpoints* out, GCancellable* cancellable, GError** error)
{
	g_autofree char* url = entra_discovery_url(authority);
	g_autoptr(GBytes) body = NULL;
	gconstpointer data = NULL;
	gsize length = 0;

	if (url == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE, "no authority");
		return FALSE;
	}

	body = entra_http_get(http, url, cancellable, error);
	if (body == NULL)
		return FALSE;

	data = g_bytes_get_data(body, &length);
	if (!entra_endpoints_parse_document(data, length, authority->host, out, error))
		return FALSE;

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_DISCOVERY, "outcome", ENTRA_FIELD_OUTCOME,
	                "refined", "authority", ENTRA_FIELD_AUTHORITY, authority->host, NULL);
	return TRUE;
}

char* entra_build_authorize_url(const EntraEndpoints* endpoints, const char* client_id,
                                const char* scope, const char* state, const char* challenge,
                                const char* redirect_uri, const char* prompt,
                                const char* login_hint)
{
	g_autoptr(GString) url = NULL;

	g_return_val_if_fail(endpoints != NULL && endpoints->authorization_endpoint != NULL, NULL);

	url = g_string_new(endpoints->authorization_endpoint);
	g_string_append_c(url, strchr(endpoints->authorization_endpoint, '?') != NULL ? '&' : '?');

	{
		g_autofree char* query = soup_form_encode(
		    "client_id", client_id, "response_type", "code", "redirect_uri", redirect_uri, "scope",
		    scope, "state", state, "code_challenge", challenge, "code_challenge_method", "S256",
		    NULL);

		g_string_append(url, query);
	}

	if (prompt != NULL)
	{
		g_autofree char* escaped = g_uri_escape_string(prompt, NULL, FALSE);

		g_string_append_printf(url, "&prompt=%s", escaped);
	}

	if (login_hint != NULL)
	{
		g_autofree char* escaped = g_uri_escape_string(login_hint, NULL, FALSE);

		g_string_append_printf(url, "&login_hint=%s", escaped);
	}

	return g_string_free(g_steal_pointer(&url), FALSE);
}

void entra_endpoints_clear(EntraEndpoints* endpoints)
{
	if (endpoints == NULL)
		return;

	g_clear_pointer(&endpoints->authorization_endpoint, g_free);
	g_clear_pointer(&endpoints->token_endpoint, g_free);
}
