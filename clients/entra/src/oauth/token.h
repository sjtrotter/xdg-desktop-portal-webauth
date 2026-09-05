/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_TOKEN_H
#define ENTRA_OAUTH_TOKEN_H

#include <gio/gio.h>
#include <glib.h>

#include "http.h"

/** @file
 *  The token endpoint: the two grants this client uses, and the proof-of-possession
 *  variant of each.
 *
 *  PROOF OF POSSESSION. FreeRDP generates the RDS key, keeps it, and hands this
 *  client only `req_cnf` — a base64url JSON object naming the key's thumbprint. The
 *  request adds `req_cnf` and `token_type=pop` to an otherwise ordinary grant and
 *  the response comes back with `"token_type":"pop"` and NO refresh token.
 *
 *  Observed on hardware (FreeRDP-plan/test-avd-20260903-080717.log): Entra infers
 *  the PoP request from `req_cnf` alone and FreeRDP does not send `token_type=pop`
 *  at all. This client sends both, which is what MSAL does and what the parameter
 *  is for; the response shape was identical either way.
 *
 *  A PoP token is never cached. It is bound to one key, and a cache that ignored
 *  the binding would hand back a token the caller cannot use.
 */

typedef struct
{
	char* access_token;
	char* refresh_token; /**< absent from a PoP response */
	char* id_token;
	char* token_type; /**< Bearer, or pop */
	char* scope;
	gint64 expires_in;
	gint64 expires_at; /**< monotonic-independent wall clock seconds */
} EntraTokenSet;

/** Parse a token endpoint response. On an OAuth error object it returns FALSE with
 *  @error in the ENTRA_ERROR domain, classified from `error` and `suberror`, and
 *  @oauth_error (optional) set to the server's error code — which is loggable. The
 *  server's error_description never reaches either: it routinely names the account,
 *  the tenant and the policy that failed.
 *
 *  ASKING THE HUMAN IS NOT A FAILURE. invalid_grant, interaction_required,
 *  consent_required and login_required all become ENTRA_ERROR_INTERACTION_REQUIRED,
 *  which the caller turns into a sign-in or into exit 10. Everything else is
 *  ENTRA_ERROR_SERVER, exit 50. */
gboolean entra_token_set_from_json(const char* document, gsize length, EntraTokenSet* out,
                                   char** oauth_error, GError** error);

/** The authorization code grant. @req_cnf may be NULL for an ordinary bearer token. */
gboolean entra_token_by_code(EntraHttp* http, const char* token_endpoint, const char* client_id,
                             const char* code, const char* redirect_uri, const char* verifier,
                             const char* scope, const char* req_cnf, EntraTokenSet* out,
                             char** oauth_error, GCancellable* cancellable, GError** error);

/** The refresh token grant. @req_cnf may be NULL for an ordinary bearer token. */
gboolean entra_token_by_refresh(EntraHttp* http, const char* token_endpoint, const char* client_id,
                                const char* refresh_token, const char* scope, const char* req_cnf,
                                EntraTokenSet* out, char** oauth_error, GCancellable* cancellable,
                                GError** error);

void entra_token_set_clear(EntraTokenSet* set); /**< scrubs every token */

#endif /* ENTRA_OAUTH_TOKEN_H */
