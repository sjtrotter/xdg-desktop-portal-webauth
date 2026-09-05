/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * PKCE, the state value, and the promise that a transaction answers once.
 */

#include <string.h>

#include <glib.h>

#include "oauth/transaction.h"

#define REDIRECT "https://login.microsoftonline.com/common/oauth2/nativeclient"

/* RFC 7636 appendix B: the verifier and the challenge it must produce. */
static void test_s256_matches_the_rfc_vector(void)
{
	g_autofree char* challenge =
	    entra_pkce_challenge_for_verifier("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");

	g_assert_cmpstr(challenge, ==, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

static void test_the_challenge_is_base64url_without_padding(void)
{
	g_autofree char* challenge = entra_pkce_challenge_for_verifier("x");

	g_assert_cmpuint(strlen(challenge), ==, 43);
	g_assert_null(strchr(challenge, '='));
	g_assert_null(strchr(challenge, '+'));
	g_assert_null(strchr(challenge, '/'));
}

static void test_a_transaction_is_fresh_every_time(void)
{
	g_autoptr(EntraTransaction) one = entra_transaction_new(REDIRECT, 60, NULL);
	g_autoptr(EntraTransaction) two = entra_transaction_new(REDIRECT, 60, NULL);
	g_autofree char* expected = NULL;

	g_assert_nonnull(one);
	g_assert_nonnull(two);

	g_assert_cmpstr(entra_transaction_state(one), !=, entra_transaction_state(two));
	g_assert_cmpstr(entra_transaction_verifier(one), !=, entra_transaction_verifier(two));

	/* 32 random bytes and 64 random bytes, base64url, unpadded. */
	g_assert_cmpuint(strlen(entra_transaction_state(one)), ==, 43);
	g_assert_cmpuint(strlen(entra_transaction_verifier(one)), ==, 86);

	expected = entra_pkce_challenge_for_verifier(entra_transaction_verifier(one));
	g_assert_cmpstr(expected, ==, entra_transaction_challenge(one));
}

static void test_the_state_comparison_is_exact(void)
{
	g_autoptr(EntraTransaction) transaction = entra_transaction_new(REDIRECT, 60, NULL);
	const char* state = entra_transaction_state(transaction);
	g_autofree char* prefix = g_strndup(state, strlen(state) - 1);
	g_autofree char* longer = g_strconcat(state, "x", NULL);
	g_autofree char* bumped = g_strdup(state);

	bumped[0] = bumped[0] == 'A' ? 'B' : 'A';

	g_assert_true(entra_transaction_state_equal(transaction, state));
	g_assert_false(entra_transaction_state_equal(transaction, prefix));
	g_assert_false(entra_transaction_state_equal(transaction, longer));
	g_assert_false(entra_transaction_state_equal(transaction, bumped));
	g_assert_false(entra_transaction_state_equal(transaction, ""));
	g_assert_false(entra_transaction_state_equal(transaction, NULL));
}

static void test_a_transaction_answers_once(void)
{
	g_autoptr(EntraTransaction) transaction = entra_transaction_new(REDIRECT, 60, NULL);

	g_assert_true(entra_transaction_consume(transaction));
	g_assert_false(entra_transaction_consume(transaction));
	g_assert_false(entra_transaction_consume(transaction));
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/pkce/rfc-vector", test_s256_matches_the_rfc_vector);
	g_test_add_func("/pkce/base64url", test_the_challenge_is_base64url_without_padding);
	g_test_add_func("/pkce/fresh", test_a_transaction_is_fresh_every_time);
	g_test_add_func("/state/exact", test_the_state_comparison_is_exact);
	g_test_add_func("/transaction/single-use", test_a_transaction_answers_once);

	return g_test_run();
}
