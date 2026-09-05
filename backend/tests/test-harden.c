/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * The window in which this process lets xdg-desktop-portal identify it.
 *
 * WHAT THIS CAN AND CANNOT COVER. It runs without webauth_harden() having been
 * called, so PR_SET_DUMPABLE is never touched and the process is left exactly as
 * the test runner started it -- a test that hardened the test binary would make
 * every later test in the same process unreadable to a debugger. What is under
 * test is the COUNTING, which is the part that can be got wrong: an unbalanced
 * pair leaves the window open for the life of the process, which is precisely
 * the exposure the window exists to bound.
 */

#include <glib.h>

#include "harden.h"

static void test_window_is_counted(void)
{
	g_assert_false(webauth_harden_is_identifiable());

	webauth_harden_identifiable_begin();
	g_assert_true(webauth_harden_is_identifiable());

	/* Nested, because a provider may hold it across several operations. The
	 * inner release must not close it. */
	webauth_harden_identifiable_begin();
	g_assert_true(webauth_harden_is_identifiable());
	webauth_harden_identifiable_end();
	g_assert_true(webauth_harden_is_identifiable());

	webauth_harden_identifiable_end();
	g_assert_false(webauth_harden_is_identifiable());
}

/* An end without a begin is a bug somewhere else, and it must not make the
 * counter negative -- which would make the NEXT begin/end pair leave the window
 * open. */
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
