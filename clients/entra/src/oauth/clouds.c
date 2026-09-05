/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>

#include "../entra-error.h"
#include "clouds.h"

/* THE REDIRECT IS THE SAME STRING IN BOTH ROWS. See clouds.h. */
static const EntraCloud entra_clouds[] = {
	{ "commercial", "login.microsoftonline.com", "https://www.wvd.microsoft.com/.default",
	  ENTRA_NATIVECLIENT_REDIRECT },
	{ "usgov", "login.microsoftonline.us", "https://www.wvd.azure.us/.default",
	  ENTRA_NATIVECLIENT_REDIRECT },
};

const EntraCloud* entra_cloud_for_authority(const char* authority)
{
	if (authority == NULL)
		return NULL;

	for (gsize i = 0; i < G_N_ELEMENTS(entra_clouds); i++)
	{
		if (g_ascii_strcasecmp(entra_clouds[i].authority, authority) == 0)
			return &entra_clouds[i];
	}

	return NULL;
}

const EntraCloud* entra_cloud_by_name(const char* name)
{
	if (name == NULL)
		return NULL;

	for (gsize i = 0; i < G_N_ELEMENTS(entra_clouds); i++)
	{
		if (g_strcmp0(entra_clouds[i].name, name) == 0)
			return &entra_clouds[i];
	}

	return NULL;
}

const EntraCloud* entra_cloud_nth(gsize index)
{
	if (index >= G_N_ELEMENTS(entra_clouds))
		return NULL;

	return &entra_clouds[index];
}

gboolean entra_cloud_client_id_allowed(const char* client_id, EntraConfig* config)
{
	if (client_id == NULL)
		return FALSE;

	if (g_ascii_strcasecmp(client_id, ENTRA_AVD_CLIENT_ID) == 0)
		return TRUE;

	return entra_config_allows_client_id(config, client_id);
}

/* A tenant identifier, or a host: ASCII alphanumerics, '-', '.', and for a host
 * a ':' before a port. Not all dots, bounded length. */
static gboolean entra_label_valid(const char* value, gboolean allow_port)
{
	gboolean any = FALSE;

	if (value == NULL || *value == '\0' || strlen(value) > 253)
		return FALSE;

	for (const char* p = value; *p != '\0'; p++)
	{
		if (g_ascii_isalnum(*p))
		{
			any = TRUE;
			continue;
		}

		if (*p == '-' || *p == '.')
			continue;

		if (*p == ':' && allow_port)
			continue;

		return FALSE;
	}

	return any;
}

gboolean entra_authority_parse(const char* authority, const char* tenant, EntraAuthority* out,
                               GError** error)
{
	g_autofree char* host = NULL;
	g_autofree char* from_url = NULL;

	g_return_val_if_fail(out != NULL, FALSE);

	memset(out, 0, sizeof(*out));

	if (authority == NULL || *authority == '\0')
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE, "--authority is required");
		return FALSE;
	}

	if (strstr(authority, "://") != NULL)
	{
		g_autoptr(GUri) uri = g_uri_parse(authority, G_URI_FLAGS_NONE, error);
		g_auto(GStrv) segments = NULL;
		gsize count = 0;

		if (uri == NULL)
			return FALSE;

		if (g_strcmp0(g_uri_get_scheme(uri), "https") != 0)
		{
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
			                    "--authority must be https");
			return FALSE;
		}

		if (g_uri_get_userinfo(uri) != NULL || g_uri_get_query(uri) != NULL ||
		    g_uri_get_fragment(uri) != NULL)
		{
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
			                    "--authority takes no userinfo, query or fragment");
			return FALSE;
		}

		if (g_uri_get_host(uri) == NULL || *g_uri_get_host(uri) == '\0')
		{
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
			                    "--authority has no host");
			return FALSE;
		}

		if (g_uri_get_port(uri) > 0)
			host = g_strdup_printf("%s:%d", g_uri_get_host(uri), g_uri_get_port(uri));
		else
			host = g_strdup(g_uri_get_host(uri));

		segments = g_strsplit(g_uri_get_path(uri), "/", -1);
		for (gsize i = 0; segments[i] != NULL; i++)
		{
			if (*segments[i] == '\0')
				continue;

			count++;
			if (count == 1)
				from_url = g_strdup(segments[i]);
		}

		if (count > 1)
		{
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
			                    "--authority may name a tenant and nothing deeper");
			return FALSE;
		}
	}
	else
	{
		if (strchr(authority, '/') != NULL)
		{
			g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
			                    "--authority is a host or an https URL, not a path");
			return FALSE;
		}

		host = g_ascii_strdown(authority, -1);
	}

	if (from_url != NULL && tenant != NULL && g_ascii_strcasecmp(from_url, tenant) != 0)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
		                    "--authority and --tenant name different tenants");
		return FALSE;
	}

	if (!entra_label_valid(host, TRUE))
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE, "--authority is not a host");
		return FALSE;
	}

	out->host = g_ascii_strdown(host, -1);
	out->tenant = g_strdup(tenant != NULL ? tenant : (from_url != NULL ? from_url : "common"));

	if (!entra_label_valid(out->tenant, FALSE) || g_strcmp0(out->tenant, ".") == 0 ||
	    g_strcmp0(out->tenant, "..") == 0)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE, "--tenant is not a tenant");
		entra_authority_clear(out);
		return FALSE;
	}

	out->base = g_strdup_printf("https://%s/%s", out->host, out->tenant);
	return TRUE;
}

gboolean entra_authority_allowed(const EntraAuthority* self, EntraConfig* config)
{
	if (self == NULL || self->host == NULL)
		return FALSE;

	if (entra_cloud_for_authority(self->host) != NULL)
		return TRUE;

	return entra_config_allows_authority(config, self->host);
}

void entra_authority_clear(EntraAuthority* self)
{
	if (self == NULL)
		return;

	g_clear_pointer(&self->host, g_free);
	g_clear_pointer(&self->tenant, g_free);
	g_clear_pointer(&self->base, g_free);
}
