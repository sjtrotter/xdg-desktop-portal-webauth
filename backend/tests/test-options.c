/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The options vardict, read the way the impl handler reads it.
 */

#include <glib.h>

#include "options.h"

static GVariant* options_of(const char* text)
{
	/* g_variant_parse() returns a full reference, not a floating one, so the
	 * caller's g_autoptr is the only unref this needs. */
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
		/* The frontend clamps too, and a backend that trusted it would be a
		 * backend whose deadline depended on a process it does not ship with. */
		{ "{'timeout': <uint32 100000>}", 900 },
		/* Zero is not a timeout. */
		{ "{'timeout': <uint32 0>}", 300 },
		/* The wrong type is the same as absent: the frontend rejects it before
		 * this backend ever sees it. */
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
	/* A key of the wrong type reads as absent rather than as a cast. */
	g_assert_null(webauth_options_string(options, "timeout"));
	g_assert_null(webauth_options_string(NULL, "title"));
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/options/timeout", test_timeout);
	g_test_add_func("/options/strings", test_strings);

	return g_test_run();
}
