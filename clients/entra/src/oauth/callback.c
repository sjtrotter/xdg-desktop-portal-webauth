/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>

#include "callback.h"

const char* entra_callback_result_str(EntraCallbackResult result)
{
	switch (result)
	{
		case ENTRA_CALLBACK_CODE:
			return "CODE";
		case ENTRA_CALLBACK_ERROR:
			return "ERROR";
		case ENTRA_CALLBACK_UNRELATED:
			return "UNRELATED";
		case ENTRA_CALLBACK_INVALID:
		default:
			return "INVALID";
	}
}

/* "" and "/" are the same resource; anything else is compared byte for byte. */
static gboolean entra_path_equal(const char* a, const char* b)
{
	const char* left = (a == NULL || *a == '\0') ? "/" : a;
	const char* right = (b == NULL || *b == '\0') ? "/" : b;

	return g_strcmp0(left, right) == 0;
}

static int entra_effective_port(GUri* uri)
{
	int port = g_uri_get_port(uri);

	if (port > 0)
		return port;

	if (g_ascii_strcasecmp(g_uri_get_scheme(uri), "https") == 0)
		return 443;
	if (g_ascii_strcasecmp(g_uri_get_scheme(uri), "http") == 0)
		return 80;

	return -1;
}

/* g_uri_unescape_segment() returns NULL for a malformed escape and for %00, which
 * is the check this needs: a decoded NUL would end every later comparison early. */
static char* entra_decode(const char* value)
{
	if (value == NULL)
		return NULL;

	return g_uri_unescape_segment(value, NULL, NULL);
}

/* One pass over the raw query, counting occurrences. A parameter present without
 * a value still counts, so "?code=good&code" is two occurrences and is refused. */
static gboolean entra_query_scan(const char* query, const char* name, char** value_out,
                                 guint* count_out)
{
	g_auto(GStrv) pairs = NULL;
	gsize name_length = strlen(name);

	*count_out = 0;
	if (value_out != NULL)
		*value_out = NULL;

	if (query == NULL)
		return TRUE;

	pairs = g_strsplit(query, "&", -1);
	for (gsize i = 0; pairs[i] != NULL; i++)
	{
		const char* pair = pairs[i];
		const char* equals = NULL;

		if (*pair == '\0')
			continue;

		equals = strchr(pair, '=');

		if (equals == NULL)
		{
			if (strcmp(pair, name) == 0)
				*count_out += 1;
			continue;
		}

		if ((gsize)(equals - pair) != name_length || strncmp(pair, name, name_length) != 0)
			continue;

		*count_out += 1;

		if (value_out != NULL && *value_out == NULL)
		{
			g_autofree char* raw = g_strdup(equals + 1);

			for (char* p = raw; *p != '\0'; p++)
			{
				if (*p == '+')
					*p = ' ';
			}

			*value_out = entra_decode(raw);
			if (*value_out == NULL)
				return FALSE; /* a malformed escape, or %00 */
		}
	}

	return TRUE;
}

EntraCallbackResult entra_callback_classify(EntraTransaction* transaction, const char* url,
                                            char** code, char** oauth_error)
{
	g_autoptr(GUri) got = NULL;
	g_autoptr(GUri) want = NULL;
	g_autofree char* state = NULL;
	g_autofree char* got_code = NULL;
	g_autofree char* got_error = NULL;
	guint states = 0, codes = 0, errors = 0;

	if (code != NULL)
		*code = NULL;
	if (oauth_error != NULL)
		*oauth_error = NULL;

	if (transaction == NULL || url == NULL)
		return ENTRA_CALLBACK_INVALID;

	got = g_uri_parse(url, G_URI_FLAGS_ENCODED_QUERY | G_URI_FLAGS_ENCODED_FRAGMENT, NULL);
	want = g_uri_parse(entra_transaction_redirect_uri(transaction), G_URI_FLAGS_NONE, NULL);

	if (got == NULL || want == NULL)
		return ENTRA_CALLBACK_INVALID;

	if (g_ascii_strcasecmp(g_uri_get_scheme(got), g_uri_get_scheme(want)) != 0 ||
	    g_uri_get_host(got) == NULL ||
	    g_ascii_strcasecmp(g_uri_get_host(got), g_uri_get_host(want)) != 0 ||
	    entra_effective_port(got) != entra_effective_port(want) ||
	    !entra_path_equal(g_uri_get_path(got), g_uri_get_path(want)))
		return ENTRA_CALLBACK_UNRELATED;

	/* Past here it IS the redirect, so a failure is INVALID rather than
	 * UNRELATED: something answered this transaction and got it wrong. */
	if (g_uri_get_userinfo(got) != NULL || g_uri_get_fragment(got) != NULL)
		return ENTRA_CALLBACK_INVALID;

	if (!entra_query_scan(g_uri_get_query(got), "state", &state, &states) ||
	    !entra_query_scan(g_uri_get_query(got), "code", &got_code, &codes) ||
	    !entra_query_scan(g_uri_get_query(got), "error", &got_error, &errors))
		return ENTRA_CALLBACK_INVALID;

	if (states != 1 || !entra_transaction_state_equal(transaction, state))
		return ENTRA_CALLBACK_INVALID;

	if (codes + errors != 1)
		return ENTRA_CALLBACK_INVALID;

	/* Single use. A second answer is a replay or an answer to a request this
	 * process did not make, and the state matching does not change that. */
	if (!entra_transaction_consume(transaction))
		return ENTRA_CALLBACK_INVALID;

	if (codes == 1)
	{
		if (got_code == NULL || *got_code == '\0')
			return ENTRA_CALLBACK_INVALID;

		if (code != NULL)
			*code = g_steal_pointer(&got_code);

		return ENTRA_CALLBACK_CODE;
	}

	if (got_error == NULL || *got_error == '\0')
		return ENTRA_CALLBACK_INVALID;

	if (oauth_error != NULL)
		*oauth_error = g_steal_pointer(&got_error);

	return ENTRA_CALLBACK_ERROR;
}
