/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>
#include <json-glib/json-glib.h>

#include "jwt.h"

static guchar* entra_base64url_decode(const char* segment, gsize* length_out)
{
	g_autofree char* padded = NULL;
	gsize length = strlen(segment);
	gsize remainder = length % 4;
	guchar* decoded = NULL;

	padded = g_strconcat(segment, remainder == 2 ? "==" : remainder == 3 ? "=" : "", NULL);
	if (remainder == 1)
		return NULL; /* not a base64 length */

	for (char* p = padded; *p != '\0'; p++)
	{
		if (*p == '-')
			*p = '+';
		else if (*p == '_')
			*p = '/';
	}

	decoded = g_base64_decode(padded, length_out);
	if (decoded == NULL || *length_out == 0)
	{
		g_free(decoded);
		return NULL;
	}

	return decoded;
}

char* entra_jwt_payload(const char* jwt)
{
	g_auto(GStrv) parts = NULL;
	g_autofree guchar* decoded = NULL;
	gsize length = 0;

	if (jwt == NULL)
		return NULL;

	parts = g_strsplit(jwt, ".", -1);
	if (g_strv_length(parts) != 3 || *parts[1] == '\0')
		return NULL;

	decoded = entra_base64url_decode(parts[1], &length);
	if (decoded == NULL)
		return NULL;

	if (memchr(decoded, '\0', length) != NULL)
		return NULL;

	return g_strndup((const char*)decoded, length);
}

/* Displayed, therefore checked: valid UTF-8, no control characters, bounded. */
static gboolean entra_claim_displayable(const char* value)
{
	if (value == NULL || *value == '\0' || strlen(value) > 320)
		return FALSE;

	if (!g_utf8_validate(value, -1, NULL))
		return FALSE;

	for (const char* p = value; *p != '\0'; p = g_utf8_next_char(p))
	{
		gunichar c = g_utf8_get_char(p);

		if (g_unichar_iscntrl(c) || c == 0x7f)
			return FALSE;
	}

	return TRUE;
}

char* entra_jwt_claim(const char* jwt, const char* claim)
{
	g_autofree char* payload = entra_jwt_payload(jwt);
	g_autoptr(JsonParser) parser = NULL;
	JsonNode* root = NULL;
	JsonObject* object = NULL;
	const char* value = NULL;

	if (payload == NULL)
		return NULL;

	parser = json_parser_new();
	if (!json_parser_load_from_data(parser, payload, -1, NULL))
		return NULL;

	root = json_parser_get_root(parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root))
		return NULL;

	object = json_node_get_object(root);
	if (!json_object_has_member(object, claim))
		return NULL;

	if (json_node_get_value_type(json_object_get_member(object, claim)) != G_TYPE_STRING)
		return NULL;

	value = json_object_get_string_member(object, claim);
	if (!entra_claim_displayable(value))
		return NULL;

	return g_strdup(value);
}

char* entra_jwt_account_name(const char* id_token)
{
	static const char* const claims[] = { "preferred_username", "upn", "unique_name", "email",
		                                  "sub", NULL };

	for (gsize i = 0; claims[i] != NULL; i++)
	{
		char* value = entra_jwt_claim(id_token, claims[i]);

		if (value != NULL)
			return value;
	}

	return NULL;
}
