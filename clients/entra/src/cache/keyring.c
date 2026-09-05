/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>
#include <json-glib/json-glib.h>
#include <libsecret/secret.h>

#include "../entra-error.h"
#include "../log/redact.h"
#include "keyring.h"

static const SecretSchema* entra_schema(void)
{
	static const SecretSchema schema = {
		ENTRA_KEYRING_SCHEMA_NAME,
		SECRET_SCHEMA_NONE,
		{
		    { "authority", SECRET_SCHEMA_ATTRIBUTE_STRING },
		    { "client_id", SECRET_SCHEMA_ATTRIBUTE_STRING },
		    { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
		    { "NULL", 0 },
		},
		0, 0, 0, 0, 0, 0, 0, 0
	};

	return &schema;
}

GStrv entra_scopes_split(const char* const* scopes)
{
	g_autoptr(GPtrArray) out = g_ptr_array_new();

	if (scopes != NULL)
	{
		for (gsize i = 0; scopes[i] != NULL; i++)
		{
			g_auto(GStrv) parts = g_strsplit_set(scopes[i], " \t\r\n", -1);

			for (gsize j = 0; parts[j] != NULL; j++)
			{
				if (*parts[j] != '\0')
					g_ptr_array_add(out, g_strdup(parts[j]));
			}
		}
	}

	g_ptr_array_add(out, NULL);
	return (GStrv)g_ptr_array_free(g_steal_pointer(&out), FALSE);
}

/* g_ptr_array_sort_values() hands the comparator the ELEMENTS, not pointers to
 * them, unlike the g_ptr_array_sort() it replaces. */
static int entra_strcmp(gconstpointer a, gconstpointer b)
{
	return g_strcmp0(a, b);
}

char* entra_cache_scope_key(const char* const* scopes)
{
	g_auto(GStrv) split = entra_scopes_split(scopes);
	g_autoptr(GPtrArray) unique = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GString) key = g_string_new(NULL);

	for (gsize i = 0; split[i] != NULL; i++)
		g_ptr_array_add(unique, g_strdup(split[i]));

	g_ptr_array_sort_values(unique, entra_strcmp);

	for (guint i = 0; i < unique->len; i++)
	{
		const char* scope = g_ptr_array_index(unique, i);

		if (i > 0 && g_strcmp0(scope, g_ptr_array_index(unique, i - 1)) == 0)
			continue;

		if (key->len > 0)
			g_string_append_c(key, ' ');
		g_string_append(key, scope);
	}

	return g_string_free(g_steal_pointer(&key), FALSE);
}

static void entra_cached_token_free(gpointer data)
{
	EntraCachedToken* token = data;

	if (token == NULL)
		return;

	entra_scrub(token->access_token);
	g_free(token->token_type);
	g_free(token->scope);
	g_free(token);
}

EntraAccountRecord* entra_account_record_new(void)
{
	EntraAccountRecord* record = g_new0(EntraAccountRecord, 1);

	record->tokens = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, entra_cached_token_free);
	return record;
}

void entra_account_record_free(EntraAccountRecord* record)
{
	if (record == NULL)
		return;

	g_free(record->account);
	g_free(record->authority);
	g_free(record->host);
	g_free(record->tenant);
	g_free(record->client_id);
	entra_scrub(record->refresh_token);
	entra_scrub(record->id_token);
	g_clear_pointer(&record->tokens, g_hash_table_unref);
	g_free(record);
}

const EntraCachedToken* entra_account_record_lookup(EntraAccountRecord* record,
                                                    const char* scope_key)
{
	const EntraCachedToken* token = NULL;
	gint64 now = g_get_real_time() / G_USEC_PER_SEC;

	if (record == NULL || scope_key == NULL)
		return NULL;

	token = g_hash_table_lookup(record->tokens, scope_key);
	if (token == NULL || token->access_token == NULL)
		return NULL;

	if (token->expires_at - now <= ENTRA_TOKEN_EXPIRY_MARGIN_SECONDS)
		return NULL;

	return token;
}

void entra_account_record_store_token(EntraAccountRecord* record, const char* scope_key,
                                      const char* access_token, const char* token_type,
                                      const char* scope, gint64 expires_at)
{
	EntraCachedToken* token = g_new0(EntraCachedToken, 1);

	token->access_token = g_strdup(access_token);
	token->token_type = g_strdup(token_type);
	token->scope = g_strdup(scope);
	token->expires_at = expires_at;

	g_hash_table_replace(record->tokens, g_strdup(scope_key), token);
}

char* entra_account_record_to_json(const EntraAccountRecord* record)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonGenerator) generator = json_generator_new();
	g_autoptr(JsonNode) root = NULL;
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "schema");
	json_builder_add_int_value(builder, 1);
	json_builder_set_member_name(builder, "account");
	json_builder_add_string_value(builder, record->account);
	json_builder_set_member_name(builder, "authority");
	json_builder_add_string_value(builder, record->authority);
	json_builder_set_member_name(builder, "host");
	json_builder_add_string_value(builder, record->host);
	json_builder_set_member_name(builder, "tenant");
	json_builder_add_string_value(builder, record->tenant);
	json_builder_set_member_name(builder, "client_id");
	json_builder_add_string_value(builder, record->client_id);
	json_builder_set_member_name(builder, "refresh_token");
	json_builder_add_string_value(builder, record->refresh_token);
	json_builder_set_member_name(builder, "id_token");
	json_builder_add_string_value(builder, record->id_token);

	json_builder_set_member_name(builder, "tokens");
	json_builder_begin_object(builder);
	g_hash_table_iter_init(&iter, record->tokens);
	while (g_hash_table_iter_next(&iter, &key, &value))
	{
		const EntraCachedToken* token = value;

		json_builder_set_member_name(builder, key);
		json_builder_begin_object(builder);
		json_builder_set_member_name(builder, "access_token");
		json_builder_add_string_value(builder, token->access_token);
		json_builder_set_member_name(builder, "token_type");
		json_builder_add_string_value(builder, token->token_type);
		json_builder_set_member_name(builder, "scope");
		json_builder_add_string_value(builder, token->scope);
		json_builder_set_member_name(builder, "expires_at");
		json_builder_add_int_value(builder, token->expires_at);
		json_builder_end_object(builder);
	}
	json_builder_end_object(builder);
	json_builder_end_object(builder);

	root = json_builder_get_root(builder);
	json_generator_set_root(generator, root);
	return json_generator_to_data(generator, NULL);
}

static char* entra_member_string(JsonObject* object, const char* name)
{
	JsonNode* node = NULL;

	if (!json_object_has_member(object, name))
		return NULL;

	node = json_object_get_member(object, name);
	if (JSON_NODE_HOLDS_NULL(node) || json_node_get_value_type(node) != G_TYPE_STRING)
		return NULL;

	return g_strdup(json_node_get_string(node));
}

EntraAccountRecord* entra_account_record_from_json(const char* json, GError** error)
{
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autoptr(EntraAccountRecord) record = NULL;
	JsonNode* root = NULL;
	JsonObject* object = NULL;

	if (!json_parser_load_from_data(parser, json, -1, NULL))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
		                    "the stored account record is not JSON");
		return NULL;
	}

	root = json_parser_get_root(parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
		                    "the stored account record is not an object");
		return NULL;
	}

	object = json_node_get_object(root);
	record = entra_account_record_new();
	record->account = entra_member_string(object, "account");
	record->authority = entra_member_string(object, "authority");
	record->host = entra_member_string(object, "host");
	record->tenant = entra_member_string(object, "tenant");
	record->client_id = entra_member_string(object, "client_id");
	record->refresh_token = entra_member_string(object, "refresh_token");
	record->id_token = entra_member_string(object, "id_token");

	if (json_object_has_member(object, "tokens"))
	{
		JsonObject* tokens = json_object_get_object_member(object, "tokens");
		g_autoptr(GList) members = tokens != NULL ? json_object_get_members(tokens) : NULL;

		for (GList* item = members; item != NULL; item = item->next)
		{
			JsonObject* entry = json_object_get_object_member(tokens, item->data);
			g_autofree char* access = NULL;
			g_autofree char* type = NULL;
			g_autofree char* scope = NULL;

			if (entry == NULL)
				continue;

			access = entra_member_string(entry, "access_token");
			type = entra_member_string(entry, "token_type");
			scope = entra_member_string(entry, "scope");

			if (access == NULL)
				continue;

			entra_account_record_store_token(record, item->data, access, type, scope,
			                                 json_object_get_int_member(entry, "expires_at"));
		}
	}

	return g_steal_pointer(&record);
}

static void entra_keyring_unavailable(GError** error, const GError* cause)
{
	g_autofree char* safe =
	    entra_redact_error_text(cause != NULL ? cause->message : "no Secret Service");

	g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_UNAVAILABLE,
	            "no keyring: %s. A Secret Service must be running, or set SECRET_BACKEND=file "
	            "with a keyring this session can unlock.",
	            safe);
}

gboolean entra_keyring_available(GError** error)
{
	g_autoptr(GError) local = NULL;
	g_autoptr(GHashTable) attributes =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	GList* found = NULL;

	found = secret_password_searchv_sync(entra_schema(), attributes, SECRET_SEARCH_ALL, NULL,
	                                     &local);
	g_list_free_full(found, g_object_unref);

	if (local != NULL)
	{
		entra_keyring_unavailable(error, local);
		return FALSE;
	}

	return TRUE;
}

static GHashTable* entra_attributes(const char* authority, const char* client_id,
                                    const char* account)
{
	GHashTable* attributes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	if (authority != NULL)
		g_hash_table_insert(attributes, g_strdup("authority"), g_strdup(authority));
	if (client_id != NULL)
		g_hash_table_insert(attributes, g_strdup("client_id"), g_strdup(client_id));
	if (account != NULL)
		g_hash_table_insert(attributes, g_strdup("account"), g_strdup(account));

	return attributes;
}

gboolean entra_keyring_store(const EntraAccountRecord* record, GError** error)
{
	g_autoptr(GHashTable) attributes =
	    entra_attributes(record->authority, record->client_id, record->account);
	g_autofree char* json = entra_account_record_to_json(record);
	g_autofree char* label = g_strdup_printf("Entra ID sign-in: %s (%s)", record->account,
	                                         record->host != NULL ? record->host : "");
	g_autoptr(GError) local = NULL;
	gboolean ok;

	ok = secret_password_storev_sync(entra_schema(), attributes, SECRET_COLLECTION_DEFAULT, label,
	                                 json, NULL, &local);
	memset(json, 0, strlen(json));

	if (!ok)
	{
		entra_keyring_unavailable(error, local);
		return FALSE;
	}

	entra_log_event(G_LOG_LEVEL_DEBUG, ENTRA_EVENT_KEYRING, "outcome", ENTRA_FIELD_OUTCOME, "stored",
	                "authority", ENTRA_FIELD_AUTHORITY, record->host, NULL);
	return TRUE;
}

static EntraAccountRecord* entra_record_from_retrievable(SecretRetrievable* item, GError** error)
{
	g_autoptr(SecretValue) value = NULL;
	g_autoptr(GError) local = NULL;
	const char* text = NULL;

	value = secret_retrievable_retrieve_secret_sync(item, NULL, &local);
	if (value == NULL)
	{
		entra_keyring_unavailable(error, local);
		return NULL;
	}

	text = secret_value_get_text(value);
	if (text == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_INTERNAL,
		                    "the stored account record is not text");
		return NULL;
	}

	return entra_account_record_from_json(text, error);
}

EntraAccountRecord* entra_keyring_load(const char* authority, const char* client_id,
                                       const char* account, GError** error)
{
	g_autoptr(GHashTable) attributes = entra_attributes(authority, client_id, account);
	g_autoptr(GError) local = NULL;
	GList* found = NULL;
	EntraAccountRecord* record = NULL;
	guint count = 0;

	found = secret_password_searchv_sync(entra_schema(), attributes,
	                                     SECRET_SEARCH_ALL | SECRET_SEARCH_UNLOCK, NULL, &local);
	if (local != NULL)
	{
		entra_keyring_unavailable(error, local);
		return NULL;
	}

	count = g_list_length(found);

	if (count == 0)
	{
		g_set_error(error, ENTRA_ERROR, ENTRA_ERROR_NO_ACCOUNT,
		            "no stored account for this authority and client id%s",
		            account != NULL ? " and account" : "; run 'entra-token-helper login'");
	}
	else if (count > 1 && account == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_NO_ACCOUNT,
		                    "more than one stored account matches; name one with --account");
	}
	else
	{
		record = entra_record_from_retrievable(found->data, error);
	}

	g_list_free_full(found, g_object_unref);
	return record;
}

static gint entra_record_compare(gconstpointer a, gconstpointer b)
{
	const EntraAccountRecord* left = a;
	const EntraAccountRecord* right = b;
	int by_account = g_strcmp0(left->account, right->account);

	return by_account != 0 ? by_account : g_strcmp0(left->authority, right->authority);
}

GPtrArray* entra_keyring_list(GError** error)
{
	g_autoptr(GHashTable) attributes = entra_attributes(NULL, NULL, NULL);
	g_autoptr(GPtrArray) records =
	    g_ptr_array_new_with_free_func((GDestroyNotify)entra_account_record_free);
	g_autoptr(GError) local = NULL;
	GList* found = NULL;

	found = secret_password_searchv_sync(entra_schema(), attributes, SECRET_SEARCH_ALL, NULL,
	                                     &local);
	if (local != NULL)
	{
		entra_keyring_unavailable(error, local);
		return NULL;
	}

	/* The attributes alone, so that listing accounts never unlocks anything and
	 * never has a token in hand. */
	for (GList* item = found; item != NULL; item = item->next)
	{
		g_autoptr(GHashTable) got = secret_retrievable_get_attributes(item->data);
		EntraAccountRecord* record = entra_account_record_new();
		const char* authority = g_hash_table_lookup(got, "authority");

		record->account = g_strdup(g_hash_table_lookup(got, "account"));
		record->authority = g_strdup(authority);
		record->client_id = g_strdup(g_hash_table_lookup(got, "client_id"));

		if (authority != NULL)
		{
			g_autoptr(GUri) uri = g_uri_parse(authority, G_URI_FLAGS_NONE, NULL);

			if (uri != NULL)
			{
				if (g_uri_get_port(uri) > 0)
					record->host = g_strdup_printf("%s:%d", g_uri_get_host(uri),
					                               g_uri_get_port(uri));
				else
					record->host = g_strdup(g_uri_get_host(uri));

				record->tenant = g_strdup(g_uri_get_path(uri) + 1);
			}
		}

		g_ptr_array_add(records, record);
	}

	g_list_free_full(found, g_object_unref);
	g_ptr_array_sort_values(records, entra_record_compare);
	return g_steal_pointer(&records);
}

gboolean entra_keyring_forget(const char* authority, const char* client_id, const char* account,
                              guint* removed, GError** error)
{
	g_autoptr(GHashTable) attributes = entra_attributes(authority, client_id, account);
	g_autoptr(GError) local = NULL;
	GList* found = NULL;
	guint count = 0;

	found = secret_password_searchv_sync(entra_schema(), attributes, SECRET_SEARCH_ALL, NULL,
	                                     &local);
	if (local != NULL)
	{
		entra_keyring_unavailable(error, local);
		return FALSE;
	}

	for (GList* item = found; item != NULL; item = item->next)
	{
		g_autoptr(GHashTable) got = secret_retrievable_get_attributes(item->data);

		if (secret_password_clearv_sync(entra_schema(), got, NULL, &local))
			count++;

		if (local != NULL)
			break;
	}

	g_list_free_full(found, g_object_unref);

	if (local != NULL)
	{
		entra_keyring_unavailable(error, local);
		return FALSE;
	}

	if (removed != NULL)
		*removed = count;

	return TRUE;
}
