/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ENTRA_OAUTH_CLOUDS_H
#define ENTRA_OAUTH_CLOUDS_H

#include <glib.h>

/** @file
 *  The sovereign cloud table: the constants that differ between commercial and US
 *  Government Azure, and the allowlist of what this client will talk to.
 *
 *  The table serves two purposes at once, and it is important that it is one table:
 *  the set of authorities an authorization URL may be built for, and the set of
 *  authorities a token request may be posted to, must be the same set.
 *
 *  Note the asymmetry that the AVD application registration forces, and do not
 *  "fix" it: the redirect is the commercial nativeclient URL for BOTH clouds. There
 *  is no .us variant, and asking for one is rejected with AADSTS50011.
 *
 *  This table belongs to the client and never to the service, which knows nothing
 *  about Entra ID. Sketch only; nothing here is implemented.
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

/** Iterate the table, for --help output and for tests. */
const EntraCloud* entra_cloud_nth(gsize index);

/** Whether @client_id is allowlisted, or was explicitly permitted by configuration. */
gboolean entra_cloud_client_id_allowed(const char* client_id);

#endif /* ENTRA_OAUTH_CLOUDS_H */
