/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "redact.h"

#include <stdarg.h>
#include <string.h>

#include <gio/gio.h>

#define WEBAUTH_MAX_LOGGED_VALUE 128

static gboolean verbose = FALSE;

void webauth_log_set_verbose(gboolean value)
{
	verbose = value;
}

gboolean webauth_log_get_verbose(void)
{
	return verbose;
}

/* A host or an app id is a string from somewhere else, and a log line is read in
 * a terminal: control characters, escape sequences and bidi overrides are
 * dropped rather than passed through. */
static char* sanitise(const char* value)
{
	GString* out = g_string_new(NULL);
	const char* p = value;

	while (p != NULL && *p != '\0' && out->len < WEBAUTH_MAX_LOGGED_VALUE)
	{
		gunichar c = g_utf8_get_char_validated(p, -1);

		if (c == (gunichar) -1 || c == (gunichar) -2)
		{
			g_string_append_c(out, '?');
			p++;
			continue;
		}

		if (g_unichar_iscntrl(c) || g_unichar_type(c) == G_UNICODE_FORMAT || c == ' ')
			g_string_append_c(out, '_');
		else
			g_string_append_unichar(out, c);

		p = g_utf8_next_char(p);
	}

	if (out->len == 0)
		g_string_append_c(out, '-');

	return g_string_free(out, FALSE);
}

/* The one shape a URI may be logged in: enough to say where the flow was, never
 * enough to replay it. */
static char* uri_shape(const char* value)
{
	g_autoptr(GUri) uri = NULL;
	g_autofree char* host = NULL;
	const char* path = NULL;

	if (value == NULL)
		return g_strdup("-");

	uri = g_uri_parse(value, G_URI_FLAGS_ENCODED, NULL);
	if (uri == NULL || g_uri_get_host(uri) == NULL)
		return g_strdup_printf("<uri:%zu>", strlen(value));

	host = sanitise(g_uri_get_host(uri));
	path = g_uri_get_path(uri);

	return g_strdup_printf("%s://%s:%d/[%zu]", g_uri_get_scheme(uri), host,
	                       g_uri_get_port(uri), path != NULL ? strlen(path) : 0);
}

char* webauth_redact_field(WebAuthFieldKind kind, const char* value)
{
	switch (kind)
	{
		case WEBAUTH_FIELD_URI_SHAPE:
			return uri_shape(value);

		case WEBAUTH_FIELD_URI:
			return g_strdup_printf("<uri:%zu>", value != NULL ? strlen(value) : 0);

		case WEBAUTH_FIELD_QUERY:
			return g_strdup_printf("<query:%zu>", value != NULL ? strlen(value) : 0);

		case WEBAUTH_FIELD_CERT_URI:
			return g_strdup_printf("<cert-uri:%zu>", value != NULL ? strlen(value) : 0);

		case WEBAUTH_FIELD_SECRET:
			/* Not even a length: a redacted secret in a log still says one was
			 * entered and how long it was. */
			return g_strdup("<secret>");

		case WEBAUTH_FIELD_OUTCOME:
		case WEBAUTH_FIELD_HOST:
		case WEBAUTH_FIELD_SCHEME:
		case WEBAUTH_FIELD_PORT:
		case WEBAUTH_FIELD_APP_ID:
		case WEBAUTH_FIELD_COUNT:
		case WEBAUTH_FIELD_DURATION:
		default:
			return sanitise(value);
	}
}

void webauth_log_event(GLogLevelFlags level, const char* event, ...)
{
	g_autoptr(GString) line = g_string_new(event);
	va_list args;
	const char* name = NULL;

	va_start(args, event);
	while ((name = va_arg(args, const char*)) != NULL)
	{
		WebAuthFieldKind kind = (WebAuthFieldKind) va_arg(args, int);
		const char* value = va_arg(args, const char*);
		g_autofree char* rendered = webauth_redact_field(kind, value);

		g_string_append_printf(line, " %s=%s", name, rendered);
	}
	va_end(args);

	if (level == G_LOG_LEVEL_DEBUG && !verbose)
		return;

	g_log(G_LOG_DOMAIN, level, "%s", line->str);
}

/* p11-kit, OpenSC, GnuTLS and glib-networking all put URIs in error strings, and
 * a PKCS#11 URI may carry a pin-value attribute. Truncating at the first URI is
 * cheap and correct; passing library error text through unmodified is how a PIN
 * reaches a journal. */
char* webauth_redact_error_text(const char* message)
{
	const char* cut = NULL;
	const char* candidates[] = { "://", "pkcs11:", NULL };
	size_t length;

	if (message == NULL)
		return g_strdup("-");

	for (int i = 0; candidates[i] != NULL; i++)
	{
		const char* found = strstr(message, candidates[i]);

		if (found != NULL && (cut == NULL || found < cut))
			cut = found;
	}

	if (cut == NULL)
		return sanitise(message);

	/* Back up to the start of the token the URI begins in, so that a scheme
	 * left dangling before the cut does not read as part of the message. */
	while (cut > message && !g_ascii_isspace(cut[-1]))
		cut--;

	length = (size_t) (cut - message);

	{
		g_autofree char* head = g_strndup(message, length);
		g_autofree char* clean = sanitise(head);

		return g_strconcat(clean, "<uri-elided>", NULL);
	}
}
