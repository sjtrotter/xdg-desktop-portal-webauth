/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <glib.h>

#include "entra-config.h"

struct _EntraConfig
{
	GStrv authorities;
	GStrv client_ids;
	char* trust_certificate;
};

static char* entra_config_default_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "entra-token-helper", "config", NULL);
}

EntraConfig* entra_config_load(const char* path, GError** error)
{
	g_autoptr(GKeyFile) file = g_key_file_new();
	g_autofree char* chosen = NULL;
	EntraConfig* self = NULL;
	GError* local = NULL;

	if (path == NULL)
		path = g_getenv("ENTRA_TOKEN_HELPER_CONFIG");
	if (path == NULL)
		chosen = entra_config_default_path();
	else
		chosen = g_strdup(path);

	self = g_new0(EntraConfig, 1);

	if (!g_key_file_load_from_file(file, chosen, G_KEY_FILE_NONE, &local))
	{
		if (g_error_matches(local, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
		    g_error_matches(local, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_NOT_FOUND))
		{
			g_clear_error(&local);
			return self;
		}

		g_propagate_error(error, local);
		entra_config_free(self);
		return NULL;
	}

	self->authorities = g_key_file_get_string_list(file, "allow", "authorities", NULL, NULL);
	self->client_ids = g_key_file_get_string_list(file, "allow", "client_ids", NULL, NULL);
	self->trust_certificate = g_key_file_get_string(file, "testing", "trust_certificate", NULL);

	return self;
}

static gboolean entra_config_lists(GStrv list, const char* value)
{
	if (list == NULL || value == NULL)
		return FALSE;

	for (gsize i = 0; list[i] != NULL; i++)
	{
		g_autofree char* entry = g_strstrip(g_strdup(list[i]));

		if (*entry != '\0' && g_ascii_strcasecmp(entry, value) == 0)
			return TRUE;
	}

	return FALSE;
}

gboolean entra_config_allows_authority(EntraConfig* self, const char* host)
{
	return self != NULL && entra_config_lists(self->authorities, host);
}

gboolean entra_config_allows_client_id(EntraConfig* self, const char* client_id)
{
	return self != NULL && entra_config_lists(self->client_ids, client_id);
}

const char* entra_config_trust_certificate(EntraConfig* self)
{
	return self != NULL ? self->trust_certificate : NULL;
}

void entra_config_free(EntraConfig* self)
{
	if (self == NULL)
		return;

	g_strfreev(self->authorities);
	g_strfreev(self->client_ids);
	g_free(self->trust_certificate);
	g_free(self);
}
