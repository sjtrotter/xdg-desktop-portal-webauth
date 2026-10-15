/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 *
 * The rule is xdg-desktop-portal's, on the branch
 * experimental/integration: desktop-portal/web-authentication.c,
 * parse_uri() and completion_uri_matches(), LGPL-2.1-or-later, Copyright (C) the
 * xdg-desktop-portal contributors. Translated rather than shared, because the
 * frontend does not install it; the two must not drift.
 */

#include "completion.h"

#include <string.h>

#include <gio/gio.h>

/* GLib provides no scheme default-port lookup; custom schemes have no default port. */
static int default_port_for_scheme(const char* scheme)
{
	if (g_ascii_strcasecmp(scheme, "https") == 0)
		return 443;
	if (g_ascii_strcasecmp(scheme, "http") == 0)
		return 80;

	return -1;
}

static int effective_port(GUri* uri)
{
	int port = g_uri_get_port(uri);

	if (port != -1)
		return port;

	return default_port_for_scheme(g_uri_get_scheme(uri));
}

/* RFC 8252 private-use schemes may omit the authority. */
static gboolean scheme_requires_host(const char* scheme)
{
	return g_ascii_strcasecmp(scheme, "https") == 0 || g_ascii_strcasecmp(scheme, "http") == 0;
}

static gboolean uri_has_host(GUri* uri)
{
	const char* host = g_uri_get_host(uri);

	return host != NULL && *host != '\0';
}

static GUri* parse_uri(const char* uri_string, const char* what, GError** error)
{
	g_autoptr(GUri) uri = NULL;

	if (uri_string == NULL || *uri_string == '\0')
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s is required", what);
		return NULL;
	}

	if (strlen(uri_string) > WEBAUTH_MAX_URI_LENGTH)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s is too long", what);
		return NULL;
	}

	for (size_t i = 0; uri_string[i]; i++)
	{
		if ((guchar) uri_string[i] < 0x20 || (guchar) uri_string[i] == 0x7f)
		{
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			            "%s contains control characters", what);
			return NULL;
		}

		if (uri_string[i] == '\\')
		{
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			            "%s contains a backslash", what);
			return NULL;
		}
	}

	/* Encoded parsing must match the frontend's completion comparison. */
	uri = g_uri_parse(uri_string, G_URI_FLAGS_ENCODED, NULL);
	if (!uri)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s is not a valid URI",
		            what);
		return NULL;
	}

	if (g_uri_get_userinfo(uri) != NULL)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s contains userinfo",
		            what);
		return NULL;
	}

	return g_steal_pointer(&uri);
}

gboolean webauth_completion_start_uri_is_valid(const char* uri_string, GError** error)
{
	g_autoptr(GUri) uri = parse_uri(uri_string, "start_uri", error);

	if (uri == NULL)
		return FALSE;

	if (g_ascii_strcasecmp(g_uri_get_scheme(uri), "https") != 0)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "start_uri must use HTTPS");
		return FALSE;
	}

	if (!uri_has_host(uri))
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "start_uri has no host");
		return FALSE;
	}

	return TRUE;
}

gboolean webauth_completion_uri_is_valid(const char* uri_string, GError** error)
{
	g_autoptr(GUri) uri = parse_uri(uri_string, "completion_uri", error);

	if (uri == NULL)
		return FALSE;

	if (scheme_requires_host(g_uri_get_scheme(uri)))
	{
		if (!uri_has_host(uri))
		{
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "completion_uri has no host");
			return FALSE;
		}
	}
	else if (!uri_has_host(uri) && *g_uri_get_path(uri) == '\0')
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "completion_uri must have a host or path");
		return FALSE;
	}

	if (uri_has_host(uri) && strchr(g_uri_get_host(uri), '*') != NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "completion_uri host contains a wildcard");
		return FALSE;
	}

	return TRUE;
}

gboolean webauth_completion_matches(const char* candidate_string, const char* completion_string)
{
	g_autoptr(GUri) requested = NULL;
	g_autoptr(GUri) candidate = NULL;

	if (candidate_string == NULL || completion_string == NULL)
		return FALSE;

	requested = parse_uri(completion_string, "completion_uri", NULL);
	candidate = parse_uri(candidate_string, "navigation", NULL);

	if (requested == NULL || candidate == NULL)
		return FALSE;

	if (g_ascii_strcasecmp(g_uri_get_scheme(requested), g_uri_get_scheme(candidate)) != 0)
		return FALSE;

	if (uri_has_host(requested) != uri_has_host(candidate))
		return FALSE;

	if (uri_has_host(requested) &&
	    g_ascii_strcasecmp(g_uri_get_host(requested), g_uri_get_host(candidate)) != 0)
		return FALSE;

	if (effective_port(requested) != effective_port(candidate))
		return FALSE;

	if (g_uri_get_userinfo(candidate) != NULL)
		return FALSE;

	if (g_strcmp0(g_uri_get_path(requested), g_uri_get_path(candidate)) != 0)
		return FALSE;

	return TRUE;
}
