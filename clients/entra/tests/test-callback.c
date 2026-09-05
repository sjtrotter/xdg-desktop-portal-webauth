/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The callback classifier: every way a URI that reached the completion can still
 * fail to be this transaction's authorization response.
 */

#include <string.h>

#include <glib.h>

#include "oauth/callback.h"

#define REDIRECT "https://login.microsoftonline.com/common/oauth2/nativeclient"

typedef struct
{
	EntraTransaction* transaction;
} Fixture;

static void setup(Fixture* fixture, gconstpointer data)
{
	fixture->transaction = entra_transaction_new(REDIRECT, 60, NULL);
	g_assert_nonnull(fixture->transaction);
}

static void teardown(Fixture* fixture, gconstpointer data)
{
	entra_transaction_free(fixture->transaction);
}

static char* url_with(Fixture* fixture, const char* query)
{
	return g_strdup_printf("%s?%s&state=%s", REDIRECT, query,
	                       entra_transaction_state(fixture->transaction));
}

static void test_a_code_is_accepted(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=abc123");
	g_autofree char* code = NULL;

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, &code, NULL), ==,
	                ENTRA_CALLBACK_CODE);
	g_assert_cmpstr(code, ==, "abc123");
}

static void test_a_percent_escape_is_decoded(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=a%2Fb%20c");
	g_autofree char* code = NULL;

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, &code, NULL), ==,
	                ENTRA_CALLBACK_CODE);
	g_assert_cmpstr(code, ==, "a/b c");
}

static void test_an_error_is_reported_without_its_description(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url =
	    url_with(fixture, "error=access_denied&error_description=AADSTS65004%3A+the+user+declined");
	g_autofree char* oauth_error = NULL;
	g_autofree char* code = NULL;

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, &code, &oauth_error), ==,
	                ENTRA_CALLBACK_ERROR);
	g_assert_cmpstr(oauth_error, ==, "access_denied");
	g_assert_null(code);
}

static void test_another_host_is_unrelated(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = g_strdup_printf("https://evil.invalid/common/oauth2/nativeclient?code=x&state=%s",
	                                       entra_transaction_state(fixture->transaction));

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_UNRELATED);
}

static void test_a_path_prefix_is_unrelated(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = g_strdup_printf("%s.evil?code=x&state=%s", REDIRECT,
	                                       entra_transaction_state(fixture->transaction));

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_UNRELATED);
}

static void test_userinfo_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url =
	    g_strdup_printf("https://login.microsoftonline.com@evil.invalid/common/oauth2/"
	                    "nativeclient?code=x&state=%s",
	                    entra_transaction_state(fixture->transaction));

	/* It is a different host, so it never gets as far as the userinfo rule. */
	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_UNRELATED);
}

static void test_a_fragment_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=x");
	g_autofree char* fragmented = g_strconcat(url, "#anything", NULL);

	g_assert_cmpint(entra_callback_classify(fixture->transaction, fragmented, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_the_wrong_state_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = g_strdup_printf("%s?code=x&state=somethingelse", REDIRECT);

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_a_missing_state_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = g_strdup_printf("%s?code=x", REDIRECT);

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_two_states_are_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=x");
	g_autofree char* twice = g_strdup_printf("%s&state=%s", url,
	                                         entra_transaction_state(fixture->transaction));

	g_assert_cmpint(entra_callback_classify(fixture->transaction, twice, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

/* A bare "code" is an occurrence. Without this rule a second code can be
 * smuggled past a classifier that only looks at the first one it finds. */
static void test_a_bare_second_code_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=good&code");

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_both_code_and_error_are_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=x&error=access_denied");

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_neither_code_nor_error_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "session_state=abc");

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_a_nul_escape_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=a%00b");

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_a_malformed_escape_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=a%zzb");

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_a_second_answer_is_refused(Fixture* fixture, gconstpointer data)
{
	g_autofree char* url = url_with(fixture, "code=abc123");
	g_autofree char* first = NULL;

	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, &first, NULL), ==,
	                ENTRA_CALLBACK_CODE);
	g_assert_cmpint(entra_callback_classify(fixture->transaction, url, NULL, NULL), ==,
	                ENTRA_CALLBACK_INVALID);
}

static void test_result_symbols_are_stable(void)
{
	g_assert_cmpstr(entra_callback_result_str(ENTRA_CALLBACK_CODE), ==, "CODE");
	g_assert_cmpstr(entra_callback_result_str(ENTRA_CALLBACK_ERROR), ==, "ERROR");
	g_assert_cmpstr(entra_callback_result_str(ENTRA_CALLBACK_UNRELATED), ==, "UNRELATED");
	g_assert_cmpstr(entra_callback_result_str(ENTRA_CALLBACK_INVALID), ==, "INVALID");
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

#define ADD(path, function) g_test_add(path, Fixture, NULL, setup, function, teardown)

	ADD("/callback/code", test_a_code_is_accepted);
	ADD("/callback/percent-decoded", test_a_percent_escape_is_decoded);
	ADD("/callback/error", test_an_error_is_reported_without_its_description);
	ADD("/callback/other-host", test_another_host_is_unrelated);
	ADD("/callback/path-prefix", test_a_path_prefix_is_unrelated);
	ADD("/callback/userinfo", test_userinfo_is_refused);
	ADD("/callback/fragment", test_a_fragment_is_refused);
	ADD("/callback/wrong-state", test_the_wrong_state_is_refused);
	ADD("/callback/no-state", test_a_missing_state_is_refused);
	ADD("/callback/two-states", test_two_states_are_refused);
	ADD("/callback/bare-second-code", test_a_bare_second_code_is_refused);
	ADD("/callback/code-and-error", test_both_code_and_error_are_refused);
	ADD("/callback/neither", test_neither_code_nor_error_is_refused);
	ADD("/callback/nul", test_a_nul_escape_is_refused);
	ADD("/callback/malformed-escape", test_a_malformed_escape_is_refused);
	ADD("/callback/single-use", test_a_second_answer_is_refused);

#undef ADD

	g_test_add_func("/callback/symbols", test_result_symbols_are_stable);

	return g_test_run();
}
