/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The storage mode a transaction runs in, and the one path component an app id
 * is allowed to become.
 */

#include <string.h>

#include <glib.h>

#include "storage.h"

static void test_mode_parse(void)
{
	WebAuthSessionMode mode = WEBAUTH_SESSION_EPHEMERAL;
	g_autoptr(GError) error = NULL;

	g_assert_true(webauth_session_mode_parse("shared", &mode, &error));
	g_assert_cmpint(mode, ==, WEBAUTH_SESSION_SHARED);

	g_assert_true(webauth_session_mode_parse("ephemeral", &mode, &error));
	g_assert_cmpint(mode, ==, WEBAUTH_SESSION_EPHEMERAL);

	/* Absent is the public interface's documented default. */
	g_assert_true(webauth_session_mode_parse(NULL, &mode, &error));
	g_assert_cmpint(mode, ==, WEBAUTH_SESSION_SHARED);

	/* An unknown value is an error here too: a backend that accepted what its
	 * frontend rejected is a hole. */
	g_assert_false(webauth_session_mode_parse("shred", &mode, &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_false(webauth_session_mode_parse("", &mode, &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_false(webauth_session_mode_parse("SHARED", &mode, &error));
}

static void test_effective_mode(void)
{
	/* An identified caller gets what the frontend decided. */
	g_assert_cmpint(webauth_storage_effective_mode(WEBAUTH_SESSION_SHARED, "org.example.App"), ==,
	                WEBAUTH_SESSION_SHARED);
	g_assert_cmpint(webauth_storage_effective_mode(WEBAUTH_SESSION_EPHEMERAL, "org.example.App"),
	                ==, WEBAUTH_SESSION_EPHEMERAL);

	/* An unidentified one gets ephemeral whatever it asked for: this backend
	 * only ever narrows. */
	g_assert_cmpint(webauth_storage_effective_mode(WEBAUTH_SESSION_SHARED, ""), ==,
	                WEBAUTH_SESSION_EPHEMERAL);
	g_assert_cmpint(webauth_storage_effective_mode(WEBAUTH_SESSION_SHARED, NULL), ==,
	                WEBAUTH_SESSION_EPHEMERAL);
}

static void test_directory_name(void)
{
	static const struct
	{
		const char* app_id;
		const char* expected;
	} cases[] = {
		{ "org.example.App", "org.example.App" },
		{ "com.example.App-2_x", "com.example.App-2_x" },
		{ "org/example/App", "org_example_App" },
		/* No separator survives, and the leading dots go with them: a name
		 * beginning with a dot is a hidden directory, and ".." is not a name. */
		{ "../../etc/passwd", "_.._etc_passwd" },
		{ "..", NULL },
		{ ".", NULL },
		{ ".hidden", "hidden" },
		{ "a b", "a_b" },
		{ "a\nb", "a_b" },
		{ "", NULL },
		{ "///", "___" },
		{ NULL, NULL },
	};

	for (int i = 0; i < (int) G_N_ELEMENTS(cases); i++)
	{
		g_autofree char* got = webauth_storage_directory_name(cases[i].app_id);

		g_assert_cmpstr(got, ==, cases[i].expected);
	}
}

static void test_directory_name_is_bounded(void)
{
	g_autofree char* long_id = g_strnfill(4096, 'a');
	g_autofree char* got = webauth_storage_directory_name(long_id);

	g_assert_nonnull(got);
	g_assert_cmpuint(strlen(got), <=, 128);
}

static void test_paths(void)
{
	g_autofree char* home = g_dir_make_tmp("webauth-storage-XXXXXX", NULL);
	g_autofree char* data = NULL;
	g_autofree char* cache = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_nonnull(home);
	g_setenv("XDG_DATA_HOME", home, TRUE);

	/* g_get_user_data_dir() caches on first use, so this only proves the shape
	 * when the test process has not asked for it yet -- which is why the
	 * assertion is on the tail of the path rather than the whole of it. */
	g_assert_true(webauth_storage_paths("org.example.App", &data, &cache, &error));
	g_assert_no_error(error);
	g_assert_true(g_str_has_suffix(data, "xdg-desktop-portal-webauth/org.example.App/data"));
	g_assert_true(g_str_has_suffix(cache, "xdg-desktop-portal-webauth/org.example.App/cache"));
	g_assert_true(g_file_test(data, G_FILE_TEST_IS_DIR));

	/* An unidentified caller never reaches a persistent path. */
	g_clear_pointer(&data, g_free);
	g_clear_pointer(&cache, g_free);
	g_assert_false(webauth_storage_paths("", &data, &cache, &error));
	g_assert_nonnull(error);
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/storage/mode-parse", test_mode_parse);
	g_test_add_func("/storage/effective-mode", test_effective_mode);
	g_test_add_func("/storage/directory-name", test_directory_name);
	g_test_add_func("/storage/directory-name-bounded", test_directory_name_is_bounded);
	g_test_add_func("/storage/paths", test_paths);

	return g_test_run();
}
