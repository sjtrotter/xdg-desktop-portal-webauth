/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The cache key, and the account record's round trip through JSON. Anything
 * missing from the key is a token returned to the wrong requester.
 */

#include <string.h>

#include <glib.h>

#include <gio/gio.h>

#include "cache/keyring.h"
#include "ipc/request.h"

static char* key_of(const char* const* scopes)
{
	return entra_cache_scope_key(scopes);
}

static void test_the_key_ignores_order(void)
{
	const char* const one[] = { "openid", "profile", "https://www.wvd.azure.us/.default", NULL };
	const char* const two[] = { "https://www.wvd.azure.us/.default", "profile", "openid", NULL };
	g_autofree char* first = key_of(one);
	g_autofree char* second = key_of(two);

	g_assert_cmpstr(first, ==, second);
}

static void test_the_key_de_duplicates(void)
{
	const char* const twice[] = { "openid", "openid", NULL };
	const char* const once[] = { "openid", NULL };
	g_autofree char* first = key_of(twice);
	g_autofree char* second = key_of(once);

	g_assert_cmpstr(first, ==, second);
	g_assert_cmpstr(first, ==, "openid");
}

/* One --scope carrying a space separated list and several --scope options are the
 * same request, so they must be the same key. */
static void test_the_key_splits_on_whitespace(void)
{
	const char* const joined[] = { "openid profile\toffline_access", NULL };
	const char* const split[] = { "openid", "profile", "offline_access", NULL };
	g_autofree char* first = key_of(joined);
	g_autofree char* second = key_of(split);

	g_assert_cmpstr(first, ==, second);
	g_assert_cmpstr(first, ==, "offline_access openid profile");
}

static void test_the_key_distinguishes_different_sets(void)
{
	const char* const arm[] = { "https://www.wvd.azure.us/.default", NULL };
	const char* const commercial[] = { "https://www.wvd.microsoft.com/.default", NULL };
	g_autofree char* first = key_of(arm);
	g_autofree char* second = key_of(commercial);

	g_assert_cmpstr(first, !=, second);
}

static void test_an_empty_set_is_an_empty_key(void)
{
	const char* const none[] = { NULL };
	g_autofree char* key = key_of(none);

	g_assert_cmpstr(key, ==, "");
}

static void test_scopes_split(void)
{
	const char* const given[] = { "  a  b ", "c", "", NULL };
	g_auto(GStrv) split = entra_scopes_split(given);

	g_assert_cmpuint(g_strv_length(split), ==, 3);
	g_assert_cmpstr(split[0], ==, "a");
	g_assert_cmpstr(split[2], ==, "c");
}

static void test_a_record_survives_json(void)
{
	g_autoptr(EntraAccountRecord) record = entra_account_record_new();
	g_autofree char* json = NULL;
	g_autoptr(EntraAccountRecord) back = NULL;
	const EntraCachedToken* token = NULL;
	gint64 later = g_get_real_time() / G_USEC_PER_SEC + 3600;

	record->account = g_strdup("a@b.mil");
	record->authority = g_strdup("https://login.microsoftonline.us/t1");
	record->host = g_strdup("login.microsoftonline.us");
	record->tenant = g_strdup("t1");
	record->client_id = g_strdup("a85cf173-4192-42f8-81fa-777a763e6e2c");
	record->refresh_token = g_strdup("0.AXkA-refresh");
	record->id_token = g_strdup("eyJ.eyJ.sig");
	entra_account_record_store_token(record, "openid profile", "eyJhY2Nlc3M", "Bearer",
	                                 "openid profile", later);

	json = entra_account_record_to_json(record);
	back = entra_account_record_from_json(json, NULL);

	g_assert_nonnull(back);
	g_assert_cmpstr(back->account, ==, "a@b.mil");
	g_assert_cmpstr(back->tenant, ==, "t1");
	g_assert_cmpstr(back->refresh_token, ==, "0.AXkA-refresh");

	token = entra_account_record_lookup(back, "openid profile");
	g_assert_nonnull(token);
	g_assert_cmpstr(token->access_token, ==, "eyJhY2Nlc3M");
	g_assert_cmpstr(token->token_type, ==, "Bearer");
}

/* A token that expires while the connection is being set up is a token that was
 * not there. The margin is five minutes. */
static void test_a_token_inside_the_margin_is_a_miss(void)
{
	g_autoptr(EntraAccountRecord) record = entra_account_record_new();
	gint64 now = g_get_real_time() / G_USEC_PER_SEC;

	entra_account_record_store_token(record, "s", "tok", "Bearer", "s",
	                                 now + ENTRA_TOKEN_EXPIRY_MARGIN_SECONDS - 1);
	g_assert_null(entra_account_record_lookup(record, "s"));

	entra_account_record_store_token(record, "s", "tok", "Bearer", "s",
	                                 now + ENTRA_TOKEN_EXPIRY_MARGIN_SECONDS + 60);
	g_assert_nonnull(entra_account_record_lookup(record, "s"));

	/* A different scope set is a different key, and never a hit. */
	g_assert_null(entra_account_record_lookup(record, "s openid"));
}

static void test_an_expired_token_is_a_miss(void)
{
	g_autoptr(EntraAccountRecord) record = entra_account_record_new();

	entra_account_record_store_token(record, "s", "tok", "Bearer", "s", 0);
	g_assert_null(entra_account_record_lookup(record, "s"));
}

static void test_rubbish_records(void)
{
	g_autoptr(GError) error = NULL;

	g_assert_null(entra_account_record_from_json("not json", &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_null(entra_account_record_from_json("[]", &error));
}

static void test_exit_codes_are_the_documented_ones(void)
{
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_OK), ==, 0);
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_INTERACTION_REQUIRED), ==, 10);
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_CANCELLED), ==, 20);
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_NO_ACCOUNT), ==, 30);
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_UNAVAILABLE), ==, 40);
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_SERVER_ERROR), ==, 50);
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_USAGE), ==, 64);
	g_assert_cmpint(entra_status_exit_code(ENTRA_STATUS_INTERNAL), ==, 70);

	g_assert_cmpstr(entra_status_symbol(ENTRA_STATUS_NO_ACCOUNT), ==, "no_account");
	g_assert_cmpstr(entra_status_symbol(ENTRA_STATUS_UNAVAILABLE), ==, "unavailable");
}

static void test_errors_are_classified(void)
{
	g_autoptr(GError) unavailable =
	    g_error_new_literal(ENTRA_ERROR, ENTRA_ERROR_UNAVAILABLE, "no portal");
	g_autoptr(GError) foreign = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "something else");

	g_assert_cmpint(entra_status_from_error(NULL), ==, ENTRA_STATUS_OK);
	g_assert_cmpint(entra_status_from_error(unavailable), ==, ENTRA_STATUS_UNAVAILABLE);
	g_assert_cmpint(entra_status_from_error(foreign), ==, ENTRA_STATUS_INTERNAL);
}

/* The response object never carries a refresh token, whatever else it carries. */
static void test_the_response_object(void)
{
	g_autofree char* ok =
	    entra_response_token_json("eyJ0b2tlbg", "pop", 3599, "scope", "a@b.mil");
	g_autofree char* bad = entra_response_error_json(ENTRA_STATUS_INTERACTION_REQUIRED,
	                                                 "prompt_never", "a window is required");

	g_assert_nonnull(strstr(ok, "\"access_token\""));
	g_assert_nonnull(strstr(ok, "\"token\""));
	g_assert_nonnull(strstr(ok, "\"pop\""));
	g_assert_nonnull(strstr(ok, "\"expires_in\" : 3599"));
	g_assert_null(strstr(ok, "refresh"));

	g_assert_nonnull(strstr(bad, "interaction_required"));
	g_assert_null(strstr(bad, "\"token\""));
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/cache/key-order", test_the_key_ignores_order);
	g_test_add_func("/cache/key-duplicates", test_the_key_de_duplicates);
	g_test_add_func("/cache/key-whitespace", test_the_key_splits_on_whitespace);
	g_test_add_func("/cache/key-distinct", test_the_key_distinguishes_different_sets);
	g_test_add_func("/cache/key-empty", test_an_empty_set_is_an_empty_key);
	g_test_add_func("/cache/scopes-split", test_scopes_split);
	g_test_add_func("/cache/record-json", test_a_record_survives_json);
	g_test_add_func("/cache/margin", test_a_token_inside_the_margin_is_a_miss);
	g_test_add_func("/cache/expired", test_an_expired_token_is_a_miss);
	g_test_add_func("/cache/rubbish", test_rubbish_records);
	g_test_add_func("/status/exit-codes", test_exit_codes_are_the_documented_ones);
	g_test_add_func("/status/classify", test_errors_are_classified);
	g_test_add_func("/status/response", test_the_response_object);

	return g_test_run();
}
