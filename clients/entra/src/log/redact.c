/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>

#include "redact.h"

static gboolean entra_verbose = FALSE;

void entra_log_set_verbose(gboolean verbose)
{
	entra_verbose = verbose;
}

gboolean entra_log_get_verbose(void)
{
	return entra_verbose;
}

char* entra_redact_field(EntraFieldKind kind, const char* value)
{
	gsize length = value ? strlen(value) : 0;

	switch (kind)
	{
		case ENTRA_FIELD_OUTCOME:
		case ENTRA_FIELD_AUTHORITY:
		case ENTRA_FIELD_ERROR_CODE:
		case ENTRA_FIELD_COUNT:
			return g_strdup(value ? value : "(none)");

		case ENTRA_FIELD_TENANT:
		{
			g_autofree char* digest = NULL;

			if (value == NULL || *value == '\0')
				return g_strdup("(none)");

			digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, value, -1);
			return g_strdup_printf("<tenant:%.8s>", digest);
		}

		case ENTRA_FIELD_ACCOUNT:
			return g_strdup_printf("<account:%" G_GSIZE_FORMAT ">", length);

		case ENTRA_FIELD_CODE:
			return g_strdup_printf("<code:%" G_GSIZE_FORMAT ">", length);

		case ENTRA_FIELD_TOKEN:
			return g_strdup_printf("<token:%" G_GSIZE_FORMAT ">", length);

		case ENTRA_FIELD_DESCRIPTION:
			return g_strdup("<description>");

		default:
			return g_strdup("<field>");
	}
}

char* entra_redact_error_text(const char* message)
{
	static const char* const markers[] = { "http://", "https://", "?", NULL };
	gsize cut;

	if (message == NULL)
		return g_strdup("(no message)");

	cut = strlen(message);
	for (gsize i = 0; markers[i] != NULL; i++)
	{
		const char* found = strstr(message, markers[i]);

		if (found != NULL && (gsize)(found - message) < cut)
			cut = (gsize)(found - message);
	}

	if (cut == 0)
		return g_strdup("<redacted>");

	return g_strndup(message, cut);
}

void entra_scrub(char* secret)
{
	if (secret == NULL)
		return;

	memset(secret, 0, strlen(secret));
	g_free(secret);
}

void entra_log_event(GLogLevelFlags level, const char* event, ...)
{
	g_autoptr(GString) line = NULL;
	va_list ap;
	const char* name;

	if (level == G_LOG_LEVEL_DEBUG && !entra_verbose)
		return;

	line = g_string_new(event);

	va_start(ap, event);
	while ((name = va_arg(ap, const char*)) != NULL)
	{
		EntraFieldKind kind = (EntraFieldKind)va_arg(ap, int);
		const char* value = va_arg(ap, const char*);
		g_autofree char* rendered = entra_redact_field(kind, value);

		g_string_append_printf(line, " %s=%s", name, rendered);
	}
	va_end(ap);

	g_log(G_LOG_DOMAIN, level, "%s", line->str);
}
