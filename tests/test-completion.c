/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The completion rule, against the frontend's own fixture table.
 *
 * The cases marked FRONTEND are ported one for one from xdg-desktop-portal's
 * tests/test_webauthentication.py on the branch
 * experimental/integration: test_invalid_uri_rejected,
 * test_completion_mismatch_rejected and test_completion_normalisation_accepted.
 * The two implementations of one rule are allowed to drift only if nobody is
 * looking, so this table is what looks.
 */

#include <glib.h>

#include "completion.h"

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
		"http://example.com/start",  /* FRONTEND */
		"file:///etc/passwd",        /* FRONTEND */
		"not a uri",                 /* FRONTEND */
		"https:///nohost",           /* FRONTEND */
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
		/* A private-use scheme is matched structurally and never dispatched. */
		"com.example.app://callback.example/done",
		/* RFC 8252 section 7.1: no authority at all. FRONTEND */
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
		"https://user:pw@example.com/callback", /* FRONTEND */
		"relative/path",                        /* FRONTEND */
		"https://*.example.com/callback",       /* FRONTEND */
		"com.example.app://*/callback",         /* FRONTEND */
		/* A private-use scheme with neither a host nor a path has nothing to
		 * match on. FRONTEND */
		"com.example.app:", /* FRONTEND */
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
		/* The same URI, and the same URI carrying the result. */
		{ COMPLETION, TRUE },
		{ "https://example.com/callback?code=secret", TRUE },
		{ "https://example.com/callback?code=secret&state=abc", TRUE },
		{ "https://example.com/callback#fragment", TRUE },
		{ "https://example.com/callback?code=a#b", TRUE },

		/* FRONTEND test_completion_normalisation_accepted: case and default
		 * port normalise. */
		{ "https://EXAMPLE.COM:443/callback", TRUE },
		{ "HTTPS://example.com/callback", TRUE },
		{ "https://Example.Com/callback?code=secret", TRUE },

		/* FRONTEND test_completion_mismatch_rejected. */
		{ "https://evil.invalid/callback", FALSE },

		/* Every one of these a string prefix would accept. */
		{ "https://example.com/callback.evil", FALSE },
		{ "https://example.com/callbacker", FALSE },
		{ "https://example.com/callback/", FALSE },
		{ "https://example.com@evil.invalid/callback", FALSE },
		{ "https://example.com.evil.invalid/callback", FALSE },

		/* Scheme, port and path are all part of the rule. */
		{ "http://example.com/callback", FALSE },
		{ "https://example.com:8443/callback", FALSE },
		{ "https://example.com/CALLBACK", FALSE },
		{ "https://example.com/", FALSE },

		/* GLib normalises an unreserved percent escape even under
		 * G_URI_FLAGS_ENCODED, so %62 and 'b' are the same path. Both
		 * implementations of the rule are GLib's, so both agree; recorded here
		 * because it is a normalisation neither of them performs on purpose. */
		{ "https://example.com/call%62ack", TRUE },

		/* Malformed candidates are refused rather than normalised. */
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
	/* A custom scheme has no default port, so a port makes it a different one. */
	g_assert_false(webauth_completion_matches("com.example.app://done:443/here", completion));
}

/* RFC 8252 section 7.1, the shape a native OAuth client actually registers.
 * FRONTEND test_private_use_scheme_accepted and
 * test_private_use_scheme_mismatch_refused. */
static void test_matches_private_use_scheme(void)
{
	const char* completion = "com.example.app:/oauth2redirect";

	g_assert_true(webauth_completion_matches("com.example.app:/oauth2redirect", completion));
	g_assert_true(
	    webauth_completion_matches("com.example.app:/oauth2redirect?code=1", completion));
	/* The scheme is lowercased and the path normalised, as for https. */
	g_assert_true(
	    webauth_completion_matches("COM.EXAMPLE.APP:/x/../oauth2%72edirect", completion));

	g_assert_false(webauth_completion_matches("com.example.app:/other", completion));
	g_assert_false(webauth_completion_matches("com.other.app:/oauth2redirect", completion));
	/* A URI with a host never matches one without. */
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
