/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "../entra-error.h"
#include "../log/redact.h"
#include "token.h"

/* The authorization server's ways of saying "ask the human". A refresh token that
 * has expired, been revoked, or lost its Conditional Access claim arrives as
 * invalid_grant, and treating that as a hard failure is what makes a client feel
 * broken a fortnight after it was set up. */
static gboolean entra_error_is_interactive(const char* code)
{
	static const char* const interactive[] = { "invalid_grant",  "interaction_required",
		                                       "consent_required", "login_required",
		                                       "account_selection_required", NULL };

	if (code == NULL)
		return FALSE;

	for (gsize i = 0; interactive[i] != NULL; i++)
	{
		if (g_ascii_strcasecmp(code, interactive[i]) == 0)
			return TRUE;
	}

	return FALSE;
}

static const char* entra_json_string(JsonObject* object, const char* member)
{
	if (object == NULL || !json_object_has_member(object, member))
		return NULL;

	if (json_node_get_value_type(json_object_get_member(object, member)) != G_TYPE_STRING)
		return NULL;

	return json_object_get_string_member(object, member);
}

static gint64 entra_json_int(JsonObject* object, const char* member)
{
	JsonNode* node = NULL;

	if (object == NULL || !json_object_has_member(object, member))
		return 0;

	node = json_object_get_member(object, member);
	if (json_node_get_value_type(node) == G_TYPE_INT64)
		return json_node_get_int(node);

	if (json_node_get_value_type(node) == G_TYPE_STRING)
		return g_ascii_strtoll(json_node_get_string(node), NULL, 10);

	return 0;
}

gboolean entra_token_set_from_json(const char* document, gsize length, EntraTokenSet* out,
                                   char** oauth_error, GError** error)
{
	g_autoptr(JsonParser) parser = json_parser_new();
	JsonNode* root = NULL;
	JsonObject* object = NULL;
	const char* code = NULL;
	const char* suberror = NULL;

	g_return_val_if_fail(out != NULL, FALSE);

	if (oauth_error != NULL)
		*oauth_error = NULL;

	if (!json_parser_load_from_data(parser, document, (gssize)length, NULL))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_SERVER,
		                    "the token endpoint did not answer with JSON");
		return FALSE;
	}

	root = json_parser_get_root(parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_SERVER,
		                    "the token endpoint did not answer with an object");
		return FALSE;
	}

	object = json_node_get_object(root);
	code = entra_json_string(object, "error");
	suberror = entra_json_string(object, "suberror");

	if (code != NULL)
	{
		gboolean interactive = entra_error_is_interactive(code) ||
		                       entra_error_is_interactive(suberror);

		if (oauth_error != NULL)
			*oauth_error = g_strdup(code);

		/* The message carries the CODE and never the description. */
		g_set_error(error, ENTRA_ERROR,
		            interactive ? ENTRA_ERROR_INTERACTION_REQUIRED : ENTRA_ERROR_SERVER,
		            "the authority refused: %s%s%s", code, suberror != NULL ? "/" : "",
		            suberror != NULL ? suberror : "");
		return FALSE;
	}

	if (entra_json_string(object, "access_token") == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_SERVER,
		                    "the token endpoint returned no access token");
		return FALSE;
	}

	entra_token_set_clear(out);
	out->access_token = g_strdup(entra_json_string(object, "access_token"));
	out->refresh_token = g_strdup(entra_json_string(object, "refresh_token"));
	out->id_token = g_strdup(entra_json_string(object, "id_token"));
	out->token_type = g_strdup(entra_json_string(object, "token_type"));
	out->scope = g_strdup(entra_json_string(object, "scope"));
	out->expires_in = entra_json_int(object, "expires_in");
	out->expires_at = g_get_real_time() / G_USEC_PER_SEC + out->expires_in;

	if (out->token_type == NULL)
		out->token_type = g_strdup("Bearer");

	return TRUE;
}

static gboolean entra_token_post(EntraHttp* http, const char* token_endpoint, GHashTable* form,
                                 EntraTokenSet* out, char** oauth_error, GCancellable* cancellable,
                                 GError** error)
{
	g_autofree char* body = soup_form_encode_hash(form);
	g_autoptr(GBytes) response = NULL;
	gconstpointer data = NULL;
	gsize length = 0;
	guint status = 0;

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_TOKEN_REQUEST, "grant", ENTRA_FIELD_OUTCOME,
	                g_hash_table_lookup(form, "grant_type"), "pop", ENTRA_FIELD_OUTCOME,
	                g_hash_table_contains(form, "req_cnf") ? "yes" : "no", NULL);

	response = entra_http_post_form(http, token_endpoint, body, &status, cancellable, error);
	memset(body, 0, strlen(body));

	if (response == NULL)
		return FALSE;

	data = g_bytes_get_data(response, &length);
	if (!entra_token_set_from_json(data, length, out, oauth_error, error))
		return FALSE;

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_TOKEN_RESPONSE, "outcome", ENTRA_FIELD_OUTCOME,
	                "ok", "token_type", ENTRA_FIELD_OUTCOME, out->token_type, NULL);
	return TRUE;
}

static void entra_form_add_pop(GHashTable* form, const char* req_cnf)
{
	if (req_cnf == NULL)
		return;

	/* See token.h: both, because MSAL sends both and the hardware run proved
	 * req_cnf alone is enough. */
	g_hash_table_insert(form, (gpointer) "token_type", (gpointer) "pop");
	g_hash_table_insert(form, (gpointer) "req_cnf", (gpointer)req_cnf);
}

gboolean entra_token_by_code(EntraHttp* http, const char* token_endpoint, const char* client_id,
                             const char* code, const char* redirect_uri, const char* verifier,
                             const char* scope, const char* req_cnf, EntraTokenSet* out,
                             char** oauth_error, GCancellable* cancellable, GError** error)
{
	g_autoptr(GHashTable) form = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_insert(form, (gpointer) "grant_type", (gpointer) "authorization_code");
	g_hash_table_insert(form, (gpointer) "client_id", (gpointer)client_id);
	g_hash_table_insert(form, (gpointer) "code", (gpointer)code);
	g_hash_table_insert(form, (gpointer) "redirect_uri", (gpointer)redirect_uri);
	g_hash_table_insert(form, (gpointer) "code_verifier", (gpointer)verifier);
	g_hash_table_insert(form, (gpointer) "scope", (gpointer)scope);
	entra_form_add_pop(form, req_cnf);

	return entra_token_post(http, token_endpoint, form, out, oauth_error, cancellable, error);
}

gboolean entra_token_by_refresh(EntraHttp* http, const char* token_endpoint, const char* client_id,
                                const char* refresh_token, const char* scope, const char* req_cnf,
                                EntraTokenSet* out, char** oauth_error, GCancellable* cancellable,
                                GError** error)
{
	g_autoptr(GHashTable) form = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_insert(form, (gpointer) "grant_type", (gpointer) "refresh_token");
	g_hash_table_insert(form, (gpointer) "client_id", (gpointer)client_id);
	g_hash_table_insert(form, (gpointer) "refresh_token", (gpointer)refresh_token);
	g_hash_table_insert(form, (gpointer) "scope", (gpointer)scope);
	entra_form_add_pop(form, req_cnf);

	return entra_token_post(http, token_endpoint, form, out, oauth_error, cancellable, error);
}

void entra_token_set_clear(EntraTokenSet* set)
{
	if (set == NULL)
		return;

	entra_scrub(set->access_token);
	entra_scrub(set->refresh_token);
	entra_scrub(set->id_token);
	g_clear_pointer(&set->token_type, g_free);
	g_clear_pointer(&set->scope, g_free);
	set->access_token = NULL;
	set->refresh_token = NULL;
	set->id_token = NULL;
	set->expires_in = 0;
	set->expires_at = 0;
}
