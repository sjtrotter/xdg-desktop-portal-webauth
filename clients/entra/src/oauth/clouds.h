/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_CLOUDS_H
#define ENTRA_OAUTH_CLOUDS_H

#include <glib.h>

#include "../entra-config.h"

/** @file
 *  The sovereign cloud table: the constants that differ between commercial and US
 *  Government Azure, and the allowlist of what this client will talk to.
 *
 *  The table serves two purposes at once, and it is important that it is one table:
 *  the set of authorities an authorization URL may be built for, and the set of
 *  authorities a token request may be posted to, must be the same set.
 *
 *  Note the asymmetry that the AVD application registration forces, and do not
 *  "fix" it: THE REDIRECT IS THE COMMERCIAL nativeclient URL FOR BOTH CLOUDS.
 *  There is no .us variant; it was tried on hardware against
 *  login.microsoftonline.us and rejected with AADSTS50011.
 *
 *  This table belongs to the client and never to the portal, which knows nothing
 *  about Entra ID.
 */

#define ENTRA_AVD_CLIENT_ID "a85cf173-4192-42f8-81fa-777a763e6e2c"
#define ENTRA_NATIVECLIENT_REDIRECT "https://login.microsoftonline.com/common/oauth2/nativeclient"

typedef struct
{
	const char* name;      /**< "commercial", "usgov" */
	const char* authority; /**< login.microsoftonline.com, login.microsoftonline.us */
	const char* avd_scope; /**< https://www.wvd.microsoft.com/.default, .../wvd.azure.us/... */
	const char* redirect;  /**< always ENTRA_NATIVECLIENT_REDIRECT */
} EntraCloud;

/** Look a cloud up by authority host. NULL if it is not on the allowlist. */
const EntraCloud* entra_cloud_for_authority(const char* authority);

/** Look a cloud up by --cloud shortcut name. NULL if there is no such cloud. */
const EntraCloud* entra_cloud_by_name(const char* name);

/** Iterate the table, for --help output and for tests. */
const EntraCloud* entra_cloud_nth(gsize index);

/** Whether @client_id is allowlisted, or was explicitly permitted by @config. */
gboolean entra_cloud_client_id_allowed(const char* client_id, EntraConfig* config);

/** An authority, however the caller spelled it: a host and a tenant, and never a
 *  URL a caller supplied for anything but this. */
typedef struct
{
	char* host;   /**< login.microsoftonline.us */
	char* tenant; /**< a tenant id, or common */
	char* base;   /**< https://<host>/<tenant>, built here and never taken in */
} EntraAuthority;

/** Parse @authority — either a bare host or an https URL of the form
 *  https://<host>/<tenant> — together with an optional separate @tenant.
 *
 *  A URL is accepted for one reason only: it is what an .rdpw file and every
 *  Microsoft document call "the authority", and refusing it would make the CLI
 *  disagree with its own live-run instructions. Only its host and its first path
 *  segment are ever used; a query, a fragment, userinfo, a non-https scheme or a
 *  deeper path is a usage error, and NO endpoint is ever taken from it. */
gboolean entra_authority_parse(const char* authority, const char* tenant, EntraAuthority* out,
                               GError** error);

/** Whether the parsed authority's host is one this client will talk to. */
gboolean entra_authority_allowed(const EntraAuthority* self, EntraConfig* config);

void entra_authority_clear(EntraAuthority* self);

#endif /* ENTRA_OAUTH_CLOUDS_H */
