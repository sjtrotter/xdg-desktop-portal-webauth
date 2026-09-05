/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The negative tests: a completion URI's query, a PKCS#11 URI with a pin-value
 * in it, and a PIN must not survive a trip through the logging interface.
 */

#include <string.h>

#include <glib.h>

#include "redact.h"

#define CODE "4/0AY0e-g7SECRETCODEVALUE"
#define COMPLETION "https://login.example.com/common/oauth2/nativeclient?code=" CODE

static void test_uri_is_never_rendered(void)
{
	g_autofree char* rendered = webauth_redact_field(WEBAUTH_FIELD_URI, COMPLETION);

	g_assert_null(strstr(rendered, CODE));
	g_assert_null(strstr(rendered, "login.example.com"));
	g_assert_true(g_str_has_prefix(rendered, "<uri:"));
}

static void test_uri_shape_keeps_the_host_and_drops_the_query(void)
{
	g_autofree char* rendered = webauth_redact_field(WEBAUTH_FIELD_URI_SHAPE, COMPLETION);

	g_assert_nonnull(strstr(rendered, "login.example.com"));
	g_assert_nonnull(strstr(rendered, "https"));
	/* The path is a length, not a path: a path can carry an identifier. */
	g_assert_null(strstr(rendered, "nativeclient"));
	g_assert_null(strstr(rendered, CODE));
	g_assert_null(strstr(rendered, "code="));
}

static void test_uri_shape_of_rubbish(void)
{
	g_autofree char* rendered = webauth_redact_field(WEBAUTH_FIELD_URI_SHAPE, "not a uri at all");

	g_assert_true(g_str_has_prefix(rendered, "<uri:"));
	g_assert_null(strstr(rendered, "rubbish"));
}

static void test_query_and_cert_uri(void)
{
	g_autofree char* query = webauth_redact_field(WEBAUTH_FIELD_QUERY, "code=" CODE);
	g_autofree char* cert = webauth_redact_field(
	    WEBAUTH_FIELD_CERT_URI, "pkcs11:token=Alice%20Smith;object=PIV%20Authentication");

	g_assert_null(strstr(query, CODE));
	g_assert_null(strstr(cert, "Alice"));
	g_assert_true(g_str_has_prefix(cert, "<cert-uri:"));
}

static void test_secret_has_no_length(void)
{
	g_autofree char* rendered = webauth_redact_field(WEBAUTH_FIELD_SECRET, "123456");

	/* Not even a length: a redacted PIN in a log still says one was entered and
	 * how long it was. */
	g_assert_cmpstr(rendered, ==, "<secret>");
}

static void test_loggable_fields_are_sanitised(void)
{
	g_autofree char* host = webauth_redact_field(WEBAUTH_FIELD_HOST, "login.example.com");
	g_autofree char* nasty =
	    webauth_redact_field(WEBAUTH_FIELD_APP_ID, "org.example\x1b[2KApp\nnext");
	g_autofree char* empty = webauth_redact_field(WEBAUTH_FIELD_HOST, NULL);

	g_assert_cmpstr(host, ==, "login.example.com");
	g_assert_null(strchr(nasty, '\x1b'));
	g_assert_null(strchr(nasty, '\n'));
	g_assert_cmpstr(empty, ==, "-");
}

static void test_error_text_is_cut_before_a_uri(void)
{
	g_autofree char* pin = webauth_redact_error_text(
	    "Failed to load key pkcs11:token=Card;object=key;pin-value=123456: bad PIN");
	g_autofree char* url =
	    webauth_redact_error_text("Cannot resolve https://login.example.com/x?code=" CODE);
	g_autofree char* plain = webauth_redact_error_text("Unacceptable TLS certificate");

	g_assert_null(strstr(pin, "123456"));
	g_assert_null(strstr(pin, "pkcs11:"));
	g_assert_nonnull(strstr(pin, "Failed_to_load_key"));

	g_assert_null(strstr(url, CODE));
	g_assert_null(strstr(url, "login.example.com"));

	g_assert_nonnull(strstr(plain, "Unacceptable"));

	{
		g_autofree char* nothing = webauth_redact_error_text(NULL);

		g_assert_cmpstr(nothing, ==, "-");
	}
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/redact/uri-never-rendered", test_uri_is_never_rendered);
	g_test_add_func("/redact/uri-shape", test_uri_shape_keeps_the_host_and_drops_the_query);
	g_test_add_func("/redact/uri-shape-of-rubbish", test_uri_shape_of_rubbish);
	g_test_add_func("/redact/query-and-cert-uri", test_query_and_cert_uri);
	g_test_add_func("/redact/secret-has-no-length", test_secret_has_no_length);
	g_test_add_func("/redact/loggable-sanitised", test_loggable_fields_are_sanitised);
	g_test_add_func("/redact/error-text", test_error_text_is_cut_before_a_uri);

	return g_test_run();
}
