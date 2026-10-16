/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <glib.h>

#include "harden.h"

/* Hardening is not applied, so this test covers nesting without changing process dumpability. */
static void test_window_is_counted(void)
{
	g_assert_false(webauth_harden_is_identifiable());

	webauth_harden_identifiable_begin();
	g_assert_true(webauth_harden_is_identifiable());

	webauth_harden_identifiable_begin();
	g_assert_true(webauth_harden_is_identifiable());
	webauth_harden_identifiable_end();
	g_assert_true(webauth_harden_is_identifiable());

	webauth_harden_identifiable_end();
	g_assert_false(webauth_harden_is_identifiable());
}

static void test_unmatched_end_is_ignored(void)
{
	g_assert_false(webauth_harden_is_identifiable());

	webauth_harden_identifiable_end();
	g_assert_false(webauth_harden_is_identifiable());

	webauth_harden_identifiable_begin();
	g_assert_true(webauth_harden_is_identifiable());
	webauth_harden_identifiable_end();
	g_assert_false(webauth_harden_is_identifiable());
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/harden/window-is-counted", test_window_is_counted);
	g_test_add_func("/harden/unmatched-end-is-ignored", test_unmatched_end_is_ignored);

	return g_test_run();
}
