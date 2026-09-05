/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * Reading the account name out of an ID token, and refusing to display rubbish.
 */

#include <string.h>

#include <glib.h>

#include "oauth/jwt.h"

/* A JWT is three base64url segments. Only the middle one is read here, and the
 * signature is deliberately not checked: see jwt.h. */
static char* jwt_with(const char* payload)
{
	g_autofree char* encoded = g_base64_encode((const guchar*)payload, strlen(payload));
	g_autoptr(GString) url = g_string_new(encoded);

	for (gsize i = 0; i < url->len; i++)
	{
		if (url->str[i] == '+')
			url->str[i] = '-';
		else if (url->str[i] == '/')
			url->str[i] = '_';
	}

	while (url->len > 0 && url->str[url->len - 1] == '=')
		g_string_truncate(url, url->len - 1);

	return g_strdup_printf("eyJhbGciOiJSUzI1NiJ9.%s.c2lnbmF0dXJl", url->str);
}

static void test_preferred_username_wins(void)
{
	g_autofree char* token = jwt_with("{\"preferred_username\":\"a@b.mil\",\"upn\":\"c@d.mil\"}");
	g_autofree char* account = entra_jwt_account_name(token);

	g_assert_cmpstr(account, ==, "a@b.mil");
}

static void test_upn_is_the_fallback(void)
{
	g_autofree char* token = jwt_with("{\"upn\":\"c@d.mil\",\"sub\":\"xyz\"}");
	g_autofree char* account = entra_jwt_account_name(token);

	g_assert_cmpstr(account, ==, "c@d.mil");
}

static void test_sub_is_the_last_resort(void)
{
	g_autofree char* token = jwt_with("{\"sub\":\"2yobDmDa71zDizMehvYTNh1c\"}");
	g_autofree char* account = entra_jwt_account_name(token);

	g_assert_cmpstr(account, ==, "2yobDmDa71zDizMehvYTNh1c");
}

static void test_a_claim_is_read_by_name(void)
{
	g_autofree char* token = jwt_with("{\"tid\":\"8331b18d-2d87-48ef-a35f-ac8818ebf9b4\"}");
	g_autofree char* tid = entra_jwt_claim(token, "tid");

	g_assert_cmpstr(tid, ==, "8331b18d-2d87-48ef-a35f-ac8818ebf9b4");
}

static void test_padding_is_restored(void)
{
	/* Payload lengths that need one and two '=' back. */
	g_autofree char* one = jwt_with("{\"sub\":\"aaaa\"}");
	g_autofree char* two = jwt_with("{\"sub\":\"aaaaa\"}");
	g_autofree char* first = entra_jwt_account_name(one);
	g_autofree char* second = entra_jwt_account_name(two);

	g_assert_cmpstr(first, ==, "aaaa");
	g_assert_cmpstr(second, ==, "aaaaa");
}

/* A name that reaches a terminal is a place to hide an escape sequence. */
static void test_a_control_character_is_refused(void)
{
	g_autofree char* token = jwt_with("{\"upn\":\"a\\u001b[2Jb@c.mil\"}");
	g_autofree char* account = entra_jwt_account_name(token);

	g_assert_null(account);
}

static void test_rubbish_is_refused(void)
{
	g_assert_null(entra_jwt_account_name(NULL));
	g_assert_null(entra_jwt_account_name(""));
	g_assert_null(entra_jwt_account_name("not.a.jwt"));
	g_assert_null(entra_jwt_account_name("only.two"));
	g_assert_null(entra_jwt_payload("a.b.c.d"));
}

static void test_a_non_string_claim_is_refused(void)
{
	g_autofree char* token = jwt_with("{\"upn\":42}");

	g_assert_null(entra_jwt_claim(token, "upn"));
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/jwt/preferred-username", test_preferred_username_wins);
	g_test_add_func("/jwt/upn", test_upn_is_the_fallback);
	g_test_add_func("/jwt/sub", test_sub_is_the_last_resort);
	g_test_add_func("/jwt/claim", test_a_claim_is_read_by_name);
	g_test_add_func("/jwt/padding", test_padding_is_restored);
	g_test_add_func("/jwt/control-character", test_a_control_character_is_refused);
	g_test_add_func("/jwt/rubbish", test_rubbish_is_refused);
	g_test_add_func("/jwt/non-string", test_a_non_string_claim_is_refused);

	return g_test_run();
}
