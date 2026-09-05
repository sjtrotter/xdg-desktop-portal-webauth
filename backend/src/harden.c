/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "harden.h"

#include <sys/prctl.h>
#include <sys/resource.h>

#include "redact.h"

static gboolean hardened = FALSE;
static guint identifiable_depth = 0;

void webauth_harden(gboolean allow_core)
{
	struct rlimit no_core = { 0, 0 };

	if (allow_core)
	{
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "disabled-by-flag", NULL);
		return;
	}

	if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0)
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "prctl-dumpable-failed", NULL);
	else
		hardened = TRUE;

	if (setrlimit(RLIMIT_CORE, &no_core) != 0)
		webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "rlimit-core-failed", NULL);
}

void webauth_harden_identifiable_begin(void)
{
	if (identifiable_depth++ > 0)
		return;

	if (!hardened)
		return;

	if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0)
	{
		webauth_log_event(G_LOG_LEVEL_WARNING, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "identifiable-begin-failed", NULL);
		return;
	}

	/* Logged at MESSAGE, not DEBUG: an operator reading a journal is entitled to
	 * see every interval in which this process was readable, without having to
	 * have asked for breadcrumbs first. */
	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
	                  WEBAUTH_FIELD_OUTCOME, "identifiable-begin", NULL);
}

void webauth_harden_identifiable_end(void)
{
	if (identifiable_depth == 0)
		return;

	if (--identifiable_depth > 0)
		return;

	if (!hardened)
		return;

	if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0)
	{
		/* THE WINDOW IS STUCK OPEN AND THAT IS A WARNING, not a debug line. */
		webauth_log_event(G_LOG_LEVEL_WARNING, WEBAUTH_EVENT_HARDENING, "outcome",
		                  WEBAUTH_FIELD_OUTCOME, "identifiable-end-failed", NULL);
		return;
	}

	webauth_log_event(G_LOG_LEVEL_MESSAGE, WEBAUTH_EVENT_HARDENING, "outcome",
	                  WEBAUTH_FIELD_OUTCOME, "identifiable-end", NULL);
}

gboolean webauth_harden_is_identifiable(void)
{
	return identifiable_depth > 0;
}
