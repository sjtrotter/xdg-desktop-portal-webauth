/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_DISCOVERY_H
#define ENTRA_OAUTH_DISCOVERY_H

#include <gio/gio.h>
#include <glib.h>

#include "clouds.h"
#include "http.h"

/** @file
 *  OpenID configuration for an authority, and the authorization URL built from it.
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
 *  same-UID caller point a credential bearing exchange at a server it controls. A
 *  discovered endpoint is subject to the same rule from the other side: it is used
 *  only if it is https and on the SAME HOST as the authority, so a compromised or
 *  redirected discovery document cannot move the token request elsewhere.
 */

typedef struct
{
	char* authorization_endpoint;
	char* token_endpoint;
} EntraEndpoints;

/** Derive the endpoints for @authority from the cloud table. Never performs I/O
 *  and never fails for a reason the network can cause. */
gboolean entra_endpoints_derive(const EntraAuthority* authority, EntraEndpoints* out,
                                GError** error);

/** The OpenID configuration URL for @authority: <base>/v2.0/.well-known/openid-configuration */
char* entra_discovery_url(const EntraAuthority* authority);

/** Parse a discovery document, keeping only endpoints on @expected_host. Separated
 *  from the fetch so that a test can exhaust it without a server. */
gboolean entra_endpoints_parse_document(const char* document, gsize length,
                                        const char* expected_host, EntraEndpoints* out,
                                        GError** error);

/** Refine @out with the authority's OpenID configuration. A failure here is not
 *  fatal: the derived endpoints stand, and this returns FALSE with @error set for
 *  the caller to log at DEBUG. */
gboolean entra_endpoints_discover(EntraHttp* http, const EntraAuthority* authority,
                                  EntraEndpoints* out, GCancellable* cancellable, GError** error);

/** Build the authorization URL for an authorization code request with PKCE.
 *  @prompt may be NULL, "select_account", "login" or "consent". */
char* entra_build_authorize_url(const EntraEndpoints* endpoints, const char* client_id,
                                const char* scope, const char* state, const char* challenge,
                                const char* redirect_uri, const char* prompt,
                                const char* login_hint);

void entra_endpoints_clear(EntraEndpoints* endpoints);

#endif /* ENTRA_OAUTH_DISCOVERY_H */
