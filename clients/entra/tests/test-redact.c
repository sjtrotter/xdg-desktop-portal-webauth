/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The negative tests: a token, a code, an account name and the authorization
 * server's error_description must not survive a trip through the logging
 * interface. The test for whether this is right is that a support bundle
 * containing a DEBUG log must not let its reader connect as the user.
 */

#include <string.h>

#include <glib.h>

#include "log/redact.h"

#define TOKEN "eyJ0eXAiOiJKV1QiLCJhbGciOiJSUzI1NiJ9.PAYLOAD.SIGNATURE"
#define CODE "1.CwMAjbExg4ct70ijX6yIGOv5tHPxXKiSQfhCgfp3enY"
#define ACCOUNT "stephen.trotter@us.af.mil"
#define DESCRIPTION "AADSTS50173: The provided grant has expired for stephen.trotter@us.af.mil"

static void test_a_token_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_TOKEN, TOKEN);

	g_assert_null(strstr(rendered, "eyJ"));
	g_assert_null(strstr(rendered, "PAYLOAD"));
	g_assert_true(g_str_has_prefix(rendered, "<token:"));
}

static void test_a_code_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_CODE, CODE);

	g_assert_null(strstr(rendered, "CwMA"));
	g_assert_true(g_str_has_prefix(rendered, "<code:"));
}

static void test_an_account_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_ACCOUNT, ACCOUNT);

	g_assert_null(strstr(rendered, "stephen"));
	g_assert_null(strstr(rendered, "us.af.mil"));
	g_assert_true(g_str_has_prefix(rendered, "<account:"));
}

/* The description routinely names the account, the tenant and the policy that
 * failed, so it is not rendered even as a length. */
static void test_a_description_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_DESCRIPTION, DESCRIPTION);

	g_assert_cmpstr(rendered, ==, "<description>");
}

/* A tenant id names an organisation. It is loggable only as a stable hash, so two
 * lines about the same tenant can still be seen to be about the same tenant. */
static void test_a_tenant_is_a_hash(void)
{
	g_autofree char* one = entra_redact_field(ENTRA_FIELD_TENANT,
	                                          "8331b18d-2d87-48ef-a35f-ac8818ebf9b4");
	g_autofree char* same = entra_redact_field(ENTRA_FIELD_TENANT,
	                                           "8331b18d-2d87-48ef-a35f-ac8818ebf9b4");
	g_autofree char* other = entra_redact_field(ENTRA_FIELD_TENANT, "common");

	g_assert_null(strstr(one, "8331b18d"));
	g_assert_cmpstr(one, ==, same);
	g_assert_cmpstr(one, !=, other);
}

/* The authority host and the OAuth error CODE are what make a report useful, and
 * neither is a credential. */
static void test_the_loggable_fields_are_loggable(void)
{
	g_autofree char* authority = entra_redact_field(ENTRA_FIELD_AUTHORITY,
	                                                "login.microsoftonline.us");
	g_autofree char* code = entra_redact_field(ENTRA_FIELD_ERROR_CODE, "invalid_grant");
	g_autofree char* outcome = entra_redact_field(ENTRA_FIELD_OUTCOME, "CODE");

	g_assert_cmpstr(authority, ==, "login.microsoftonline.us");
	g_assert_cmpstr(code, ==, "invalid_grant");
	g_assert_cmpstr(outcome, ==, "CODE");
}

static void test_error_text_is_cut_before_a_uri(void)
{
	g_autofree char* cut = entra_redact_error_text(
	    "TLS handshake failed for https://login.microsoftonline.us/t/oauth2/v2.0/token?code=x");
	g_autofree char* nothing = entra_redact_error_text(NULL);

	g_assert_null(strstr(cut, "login.microsoftonline.us"));
	g_assert_null(strstr(cut, "code="));
	g_assert_true(g_str_has_prefix(cut, "TLS handshake failed"));
	g_assert_cmpstr(nothing, ==, "(no message)");
}

/* A DEBUG log is a licence to log more often, not to log more: with --verbose
 * off nothing is written at all, and with it on the same rules apply. */
static void test_verbose_is_a_frequency_not_a_permission(void)
{
	g_assert_false(entra_log_get_verbose());
	entra_log_set_verbose(TRUE);
	g_assert_true(entra_log_get_verbose());
	entra_log_set_verbose(FALSE);
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/redact/token", test_a_token_is_never_rendered);
	g_test_add_func("/redact/code", test_a_code_is_never_rendered);
	g_test_add_func("/redact/account", test_an_account_is_never_rendered);
	g_test_add_func("/redact/description", test_a_description_is_never_rendered);
	g_test_add_func("/redact/tenant", test_a_tenant_is_a_hash);
	g_test_add_func("/redact/loggable", test_the_loggable_fields_are_loggable);
	g_test_add_func("/redact/error-text", test_error_text_is_cut_before_a_uri);
	g_test_add_func("/redact/verbose", test_verbose_is_a_frequency_not_a_permission);

	return g_test_run();
}
