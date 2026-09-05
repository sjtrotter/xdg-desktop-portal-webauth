/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "storage.h"

#include <string.h>

#include <gio/gio.h>

#define WEBAUTH_STORAGE_SUBDIR "xdg-desktop-portal-webauth"
#define WEBAUTH_MAX_DIRECTORY_NAME 128

gboolean webauth_session_mode_parse(const char* value, WebAuthSessionMode* out, GError** error)
{
	if (value == NULL || g_strcmp0(value, "shared") == 0)
	{
		*out = WEBAUTH_SESSION_SHARED;
		return TRUE;
	}

	if (g_strcmp0(value, "ephemeral") == 0)
	{
		*out = WEBAUTH_SESSION_EPHEMERAL;
		return TRUE;
	}

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown session_mode");
	return FALSE;
}

const char* webauth_session_mode_to_string(WebAuthSessionMode mode)
{
	return mode == WEBAUTH_SESSION_EPHEMERAL ? "ephemeral" : "shared";
}

WebAuthSessionMode webauth_storage_effective_mode(WebAuthSessionMode requested, const char* app_id)
{
	if (app_id == NULL || *app_id == '\0')
		return WEBAUTH_SESSION_EPHEMERAL;

	return requested;
}

char* webauth_storage_directory_name(const char* app_id)
{
	GString* out = NULL;

	if (app_id == NULL || *app_id == '\0')
		return NULL;

	out = g_string_new(NULL);

	for (const char* p = app_id; *p != '\0' && out->len < WEBAUTH_MAX_DIRECTORY_NAME; p++)
	{
		if (g_ascii_isalnum(*p) || *p == '.' || *p == '-' || *p == '_')
			g_string_append_c(out, *p);
		else
			g_string_append_c(out, '_');
	}

	/* A leading dot hides the directory and "." and ".." are not names at all;
	 * an app id that reduces to one of them is treated as no name. */
	while (out->len > 0 && out->str[0] == '.')
		g_string_erase(out, 0, 1);

	if (out->len == 0)
		return g_string_free(out, TRUE);

	return g_string_free(out, FALSE);
}

gboolean webauth_storage_paths(const char* app_id, char** data_directory, char** cache_directory,
                               GError** error)
{
	g_autofree char* name = webauth_storage_directory_name(app_id);
	g_autofree char* base = NULL;
	g_autofree char* data = NULL;
	g_autofree char* cache = NULL;

	if (name == NULL)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "No application id to key a persistent store on");
		return FALSE;
	}

	base = g_build_filename(g_get_user_data_dir(), WEBAUTH_STORAGE_SUBDIR, name, NULL);
	data = g_build_filename(base, "data", NULL);
	cache = g_build_filename(base, "cache", NULL);

	/* 0700 from the first instant: a store holds session cookies for the
	 * application's identity provider. */
	if (g_mkdir_with_parents(data, 0700) != 0 || g_mkdir_with_parents(cache, 0700) != 0)
	{
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                    "Could not create the persistent website data store");
		return FALSE;
	}

	*data_directory = g_steal_pointer(&data);
	*cache_directory = g_steal_pointer(&cache);

	return TRUE;
}
