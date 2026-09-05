/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <glib.h>
#include <json-glib/json-glib.h>

#include "../cache/keyring.h"
#include "request.h"

int entra_status_exit_code(EntraStatus status)
{
	switch (status)
	{
		case ENTRA_STATUS_OK:
			return 0;
		case ENTRA_STATUS_INTERACTION_REQUIRED:
			return 10;
		case ENTRA_STATUS_CANCELLED:
			return 20;
		case ENTRA_STATUS_NO_ACCOUNT:
			return 30;
		case ENTRA_STATUS_UNAVAILABLE:
			return 40;
		case ENTRA_STATUS_SERVER_ERROR:
			return 50;
		case ENTRA_STATUS_USAGE:
			return 64;
		case ENTRA_STATUS_INTERNAL:
		default:
			return 70;
	}
}

const char* entra_status_symbol(EntraStatus status)
{
	switch (status)
	{
		case ENTRA_STATUS_OK:
			return "ok";
		case ENTRA_STATUS_INTERACTION_REQUIRED:
			return "interaction_required";
		case ENTRA_STATUS_CANCELLED:
			return "cancelled";
		case ENTRA_STATUS_NO_ACCOUNT:
			return "no_account";
		case ENTRA_STATUS_UNAVAILABLE:
			return "unavailable";
		case ENTRA_STATUS_SERVER_ERROR:
			return "server_error";
		case ENTRA_STATUS_USAGE:
			return "usage";
		case ENTRA_STATUS_INTERNAL:
		default:
			return "internal";
	}
}

EntraStatus entra_status_from_error(const GError* error)
{
	if (error == NULL)
		return ENTRA_STATUS_OK;

	if (error->domain != ENTRA_ERROR)
		return ENTRA_STATUS_INTERNAL;

	switch ((EntraError)error->code)
	{
		case ENTRA_ERROR_INTERACTION_REQUIRED:
			return ENTRA_STATUS_INTERACTION_REQUIRED;
		case ENTRA_ERROR_CANCELLED:
			return ENTRA_STATUS_CANCELLED;
		case ENTRA_ERROR_NO_ACCOUNT:
			return ENTRA_STATUS_NO_ACCOUNT;
		case ENTRA_ERROR_UNAVAILABLE:
			return ENTRA_STATUS_UNAVAILABLE;
		case ENTRA_ERROR_SERVER:
			return ENTRA_STATUS_SERVER_ERROR;
		case ENTRA_ERROR_USAGE:
			return ENTRA_STATUS_USAGE;
		case ENTRA_ERROR_INTERNAL:
		default:
			return ENTRA_STATUS_INTERNAL;
	}
}

static char* entra_generate(JsonBuilder* builder)
{
	g_autoptr(JsonGenerator) generator = json_generator_new();
	g_autoptr(JsonNode) root = json_builder_get_root(builder);

	json_generator_set_root(generator, root);
	json_generator_set_pretty(generator, TRUE);
	return json_generator_to_data(generator, NULL);
}

char* entra_response_token_json(const char* token, const char* token_type, gint64 expires_in,
                                const char* scope, const char* account)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "schema");
	json_builder_add_int_value(builder, ENTRA_SCHEMA_VERSION);
	json_builder_set_member_name(builder, "status");
	json_builder_add_string_value(builder, entra_status_symbol(ENTRA_STATUS_OK));
	/* "token" is the name the contract fixed; "access_token" is the name every
	 * OAuth consumer already reads. Both, with the same value. */
	json_builder_set_member_name(builder, "token");
	json_builder_add_string_value(builder, token);
	json_builder_set_member_name(builder, "access_token");
	json_builder_add_string_value(builder, token);
	json_builder_set_member_name(builder, "token_type");
	json_builder_add_string_value(builder, token_type);
	json_builder_set_member_name(builder, "expires_in");
	json_builder_add_int_value(builder, expires_in);
	json_builder_set_member_name(builder, "scope");
	json_builder_add_string_value(builder, scope);
	json_builder_set_member_name(builder, "account");
	json_builder_add_string_value(builder, account);
	json_builder_end_object(builder);

	return entra_generate(builder);
}

char* entra_response_error_json(EntraStatus status, const char* error_symbol, const char* message)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "schema");
	json_builder_add_int_value(builder, ENTRA_SCHEMA_VERSION);
	json_builder_set_member_name(builder, "status");
	json_builder_add_string_value(builder, entra_status_symbol(status));
	json_builder_set_member_name(builder, "error");
	json_builder_add_string_value(builder, error_symbol);
	json_builder_set_member_name(builder, "message");
	json_builder_add_string_value(builder, message);
	json_builder_end_object(builder);

	return entra_generate(builder);
}

char* entra_response_accounts_json(GPtrArray* records)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();

	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "schema");
	json_builder_add_int_value(builder, ENTRA_SCHEMA_VERSION);
	json_builder_set_member_name(builder, "status");
	json_builder_add_string_value(builder, entra_status_symbol(ENTRA_STATUS_OK));
	json_builder_set_member_name(builder, "accounts");
	json_builder_begin_array(builder);

	for (guint i = 0; records != NULL && i < records->len; i++)
	{
		const EntraAccountRecord* record = g_ptr_array_index(records, i);

		json_builder_begin_object(builder);
		json_builder_set_member_name(builder, "account");
		json_builder_add_string_value(builder, record->account);
		json_builder_set_member_name(builder, "authority");
		json_builder_add_string_value(builder, record->host);
		json_builder_set_member_name(builder, "tenant");
		json_builder_add_string_value(builder, record->tenant);
		json_builder_set_member_name(builder, "client_id");
		json_builder_add_string_value(builder, record->client_id);
		json_builder_end_object(builder);
	}

	json_builder_end_array(builder);
	json_builder_end_object(builder);

	return entra_generate(builder);
}
