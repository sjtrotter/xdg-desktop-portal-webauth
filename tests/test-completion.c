/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <glib.h>

#include "completion.h"

/* URI fixtures follow the frontend tests on experimental/integration. */
#define COMPLETION "https://example.com/callback"

static void test_start_uri_valid(void)
{
	static const char* good[] = {
		"https://login.example.com/authorize?client_id=test",
		"https://login.example.com",
		"https://login.example.com:8443/authorize",
		"https://login.example.com/common/oauth2/v2.0/authorize?client_id=a#f",
		NULL,
	};

	for (int i = 0; good[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;

		g_assert_true(webauth_completion_start_uri_is_valid(good[i], &error));
		g_assert_no_error(error);
	}
}

static void test_start_uri_invalid(void)
{
	static const char* bad[] = {
		"http://example.com/start",
		"file:///etc/passwd",
		"not a uri",
		"https:///nohost",
		"",
		"https://user:pw@example.com/start",
		"https://example.com/sta\nrt",
		"https://example.com/sta\\rt",
		"HTTP://example.com/start",
		NULL,
	};

	for (int i = 0; bad[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;

		g_assert_false(webauth_completion_start_uri_is_valid(bad[i], &error));
		g_assert_nonnull(error);
	}
}

static void test_start_uri_length(void)
{
	g_autofree char* padding = g_strnfill(4096, 'a');
	g_autofree char* too_long = g_strconcat("https://example.com/", padding, NULL);
	g_autoptr(GError) error = NULL;

	g_assert_false(webauth_completion_start_uri_is_valid(too_long, &error));
}

static void test_completion_uri_valid(void)
{
	static const char* good[] = {
		COMPLETION,
		"https://login.example.com/common/oauth2/nativeclient",
		"com.example.app://callback.example/done",
		"com.example.app:/oauth2redirect",
		"com.example.app:oauth2redirect",
		"http://127.0.0.1:1234/cb",
		NULL,
	};

	for (int i = 0; good[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;

		g_assert_true(webauth_completion_uri_is_valid(good[i], &error));
	}
}

static void test_completion_uri_invalid(void)
{
	static const char* bad[] = {
		"https://user:pw@example.com/callback",
		"relative/path",
		"https://*.example.com/callback",
		"com.example.app://*/callback",
		"com.example.app:",
		"",
		"https:///cb",
		"https://example.com/c\x7f" "b",
		NULL,
	};

	for (int i = 0; bad[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;

		g_assert_false(webauth_completion_uri_is_valid(bad[i], &error));
	}
}

static void test_matches(void)
{
	static const struct
	{
		const char* candidate;
		gboolean expected;
	} cases[] = {
		{ COMPLETION, TRUE },
		{ "https://example.com/callback?code=secret", TRUE },
		{ "https://example.com/callback?code=secret&state=abc", TRUE },
		{ "https://example.com/callback#fragment", TRUE },
		{ "https://example.com/callback?code=a#b", TRUE },

		{ "https://EXAMPLE.COM:443/callback", TRUE },
		{ "HTTPS://example.com/callback", TRUE },
		{ "https://Example.Com/callback?code=secret", TRUE },

		{ "https://evil.invalid/callback", FALSE },

		{ "https://example.com/callback.evil", FALSE },
		{ "https://example.com/callbacker", FALSE },
		{ "https://example.com/callback/", FALSE },
		{ "https://example.com@evil.invalid/callback", FALSE },
		{ "https://example.com.evil.invalid/callback", FALSE },

		{ "http://example.com/callback", FALSE },
		{ "https://example.com:8443/callback", FALSE },
		{ "https://example.com/CALLBACK", FALSE },
		{ "https://example.com/", FALSE },

		/* GLib decodes unreserved percent escapes even with G_URI_FLAGS_ENCODED. */
		{ "https://example.com/call%62ack", TRUE },

		{ "https://example.com/callback\\", FALSE },
		{ "not a uri", FALSE },
		{ "", FALSE },
		{ NULL, FALSE },
	};

	for (int i = 0; cases[i].candidate != NULL; i++)
	{
		gboolean got = webauth_completion_matches(cases[i].candidate, COMPLETION);

		if (got != cases[i].expected)
			g_error("completion rule disagreed on '%s': got %d", cases[i].candidate, got);
	}

	g_assert_false(webauth_completion_matches(NULL, COMPLETION));
	g_assert_false(webauth_completion_matches(COMPLETION, NULL));
}

static void test_matches_custom_scheme(void)
{
	const char* completion = "com.example.app://done/here";

	g_assert_true(webauth_completion_matches("com.example.app://done/here?code=1", completion));
	g_assert_true(webauth_completion_matches("COM.EXAMPLE.APP://DONE/here", completion));
	g_assert_false(webauth_completion_matches("com.example.app://done/here/more", completion));
	g_assert_false(webauth_completion_matches("com.example.app://done:443/here", completion));
}

static void test_matches_private_use_scheme(void)
{
	const char* completion = "com.example.app:/oauth2redirect";

	g_assert_true(webauth_completion_matches("com.example.app:/oauth2redirect", completion));
	g_assert_true(
	    webauth_completion_matches("com.example.app:/oauth2redirect?code=1", completion));
	g_assert_true(
	    webauth_completion_matches("COM.EXAMPLE.APP:/x/../oauth2%72edirect", completion));

	g_assert_false(webauth_completion_matches("com.example.app:/other", completion));
	g_assert_false(webauth_completion_matches("com.other.app:/oauth2redirect", completion));
	g_assert_false(
	    webauth_completion_matches("com.example.app://host/oauth2redirect", completion));
	g_assert_false(webauth_completion_matches("com.example.app:/oauth2redirect",
	                                          "com.example.app://host/oauth2redirect"));
}

static void test_matches_explicit_port(void)
{
	const char* completion = "https://example.com:8443/cb";

	g_assert_true(webauth_completion_matches("https://example.com:8443/cb?code=1", completion));
	g_assert_false(webauth_completion_matches("https://example.com/cb", completion));
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/completion/start-uri-valid", test_start_uri_valid);
	g_test_add_func("/completion/start-uri-invalid", test_start_uri_invalid);
	g_test_add_func("/completion/start-uri-length", test_start_uri_length);
	g_test_add_func("/completion/completion-uri-valid", test_completion_uri_valid);
	g_test_add_func("/completion/completion-uri-invalid", test_completion_uri_invalid);
	g_test_add_func("/completion/matches", test_matches);
	g_test_add_func("/completion/matches-custom-scheme", test_matches_custom_scheme);
	g_test_add_func("/completion/matches-private-use-scheme", test_matches_private_use_scheme);
	g_test_add_func("/completion/matches-explicit-port", test_matches_explicit_port);

	return g_test_run();
}
