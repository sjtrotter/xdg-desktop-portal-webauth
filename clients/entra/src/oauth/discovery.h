/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_DISCOVERY_H
#define ENTRA_OAUTH_DISCOVERY_H

#include <glib.h>

/** @file
 *  OpenID configuration for an authority, and the token requests built from it.
 *
 *  Discovery is optional by design. The endpoints are derivable from the authority
 *  and the tenant, and the client always has both; a discovery document only refines
 *  them. FreeRDP today has the opposite coupling — it fetches the OpenID
 *  configuration before asking a provider for a token, so a provider perfectly
 *  capable of resolving the authority itself is blocked when that fetch fails — and
 *  this client deliberately does not reproduce it.
 *
 *  Endpoints are always derived from the authority the caller named, never taken
 *  from the caller. A request that carried its own token endpoint would let a
 *  same-UID caller point a credential bearing exchange at a server it controls.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef struct
{
	char* authorization_endpoint;
	char* token_endpoint;
} EntraEndpoints;

/** Derive the endpoints for @authority and @tenant from the cloud table. Never
 *  performs I/O and never fails for a reason the network can cause. */
gboolean entra_endpoints_derive(const char* authority, const char* tenant, EntraEndpoints* out,
                                GError** error);

/** Refine @out with the authority's OpenID configuration. A failure here is not
 *  fatal: the derived endpoints stand. */
gboolean entra_endpoints_discover(const char* authority, const char* tenant, EntraEndpoints* out,
                                  GCancellable* cancellable, GError** error);

/** Build the authorization URL for an authorization code request with PKCE. */
char* entra_build_authorize_url(const EntraEndpoints* endpoints, const char* client_id,
                                const char* const* scopes, const char* state,
                                const char* challenge, const char* redirect_uri);

void entra_endpoints_clear(EntraEndpoints* endpoints);

#endif /* ENTRA_OAUTH_DISCOVERY_H */
