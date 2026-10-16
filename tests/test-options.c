/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <glib.h>

#include "options.h"
#include "tls/client_cert.h"

static GVariant* options_of(const char* text)
{
	GVariant* value = g_variant_parse(G_VARIANT_TYPE_VARDICT, text, NULL, NULL, NULL);

	g_assert_nonnull(value);

	return value;
}

static void test_timeout(void)
{
	static const struct
	{
		const char* options;
		guint expected;
	} cases[] = {
		{ "{}", 300 },
		{ "{'timeout': <uint32 120>}", 120 },
		{ "{'timeout': <uint32 900>}", 900 },
		{ "{'timeout': <uint32 100000>}", 900 },
		{ "{'timeout': <uint32 0>}", 300 },
		{ "{'timeout': <'soon'>}", 300 },
		{ "{'timeout': <120>}", 300 },
		{ NULL, 0 },
	};

	for (int i = 0; cases[i].options != NULL; i++)
	{
		g_autoptr(GVariant) options = options_of(cases[i].options);

		g_assert_cmpuint(webauth_options_timeout(options, 300, 900), ==, cases[i].expected);
	}

	g_assert_cmpuint(webauth_options_timeout(NULL, 300, 900), ==, 300);
}

static void test_strings(void)
{
	g_autoptr(GVariant) options =
	    options_of("{'session_mode': <'ephemeral'>, 'title': <'Sign in'>, 'timeout': <uint32 5>}");

	g_assert_cmpstr(webauth_options_string(options, "session_mode"), ==, "ephemeral");
	g_assert_cmpstr(webauth_options_string(options, "title"), ==, "Sign in");
	g_assert_null(webauth_options_string(options, "activation_token"));
	g_assert_null(webauth_options_string(options, "timeout"));
	g_assert_null(webauth_options_string(NULL, "title"));
}

static void test_client_key_uri_pin_value(void)
{
	const char* key_uris[] = {
		"pkcs11:token=Test;type=private;pin-value=123456",
		"pkcs11:token=Test;type=private?pin-value=123456",
		NULL,
	};

	for (int i = 0; key_uris[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;

		g_assert_false(webauth_cert_adapter_configure(
		    "pkcs11", "pkcs11:token=Test;type=cert", key_uris[i], NULL, &error));
		g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
		g_assert_cmpstr(error->message, ==,
		                "--client-key-uri must not carry pin-value; use --client-cert-pin-file");
	}
}

static void test_client_key_uri(void)
{
	g_autoptr(GError) error = NULL;
	const char* certificate_uri = "pkcs11:token=Test;type=cert";
	const char* private_key_uri = "pkcs11:token=Test;type=private";

	g_assert_true(webauth_cert_adapter_configure("pkcs11", certificate_uri, private_key_uri,
	                                          "/tmp/test-pin", &error));
	g_assert_no_error(error);
	g_assert_cmpstr(webauth_cert_adapter_cert_uri(), ==, certificate_uri);
	g_assert_cmpstr(webauth_cert_adapter_key_uri(), ==, private_key_uri);
	g_assert_cmpstr(webauth_cert_adapter_pin_file(), ==, "/tmp/test-pin");

	g_assert_true(webauth_cert_adapter_configure("pkcs11", certificate_uri, NULL, NULL, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(webauth_cert_adapter_key_uri(), ==, private_key_uri);

	g_assert_true(webauth_cert_adapter_configure(NULL, NULL, NULL, NULL, &error));
	g_assert_no_error(error);
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/options/timeout", test_timeout);
	g_test_add_func("/options/strings", test_strings);
	g_test_add_func("/options/client-key-uri-pin-value", test_client_key_uri_pin_value);
	g_test_add_func("/options/client-key-uri", test_client_key_uri);

	return g_test_run();
}
