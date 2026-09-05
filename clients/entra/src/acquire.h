/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_ACQUIRE_H
#define ENTRA_ACQUIRE_H

#include <glib.h>

#include "cache/keyring.h"
#include "entra-config.h"
#include "oauth/clouds.h"
#include "oauth/discovery.h"
#include "oauth/http.h"

/** @file
 *  What `login` and `token` actually do, with the CLI taken away.
 *
 *  Everything interactive here is one call to the web authentication portal. This
 *  file owns the OAuth: the transaction, the callback check, the grants, the cache
 *  and the decision about when a human has to be asked.
 */

typedef enum
{
	ENTRA_PROMPT_AUTO,  /**< silent if possible, a window if not */
	ENTRA_PROMPT_ALWAYS, /**< a window even if a cached token would do */
	ENTRA_PROMPT_NEVER  /**< silent only: exit 10 rather than opening a window */
} EntraPrompt;

typedef struct
{
	EntraAuthority authority;
	const char* client_id;
	GStrv scopes;       /**< split on whitespace, in the order given */
	char* scope_request; /**< what goes on the wire: the scopes, space joined */
	char* scope_key;     /**< the cache key: sorted and de-duplicated */
	const char* account;
	const char* req_cnf;
	const char* parent_window;
	const char* session_mode;
	EntraPrompt prompt;
	guint timeout_seconds;

	EntraConfig* config;
	EntraHttp* http;
	EntraEndpoints endpoints;
	gboolean discovered;
	const char* redirect;
} EntraAcquire;

typedef struct
{
	char* access_token;
	char* token_type;
	gint64 expires_in;
	char* scope;
	char* account;
} EntraTokenResult;

void entra_token_result_clear(EntraTokenResult* result);

/** Resolve the authority, check the allowlists, build the HTTP client and derive
 *  the endpoints from the cloud table. PERFORMS NO I/O: discovery happens on the
 *  first request that actually needs an endpoint, so a request answered from the
 *  cache touches the network not at all. */
gboolean entra_acquire_prepare(EntraAcquire* self, GError** error);

void entra_acquire_clear(EntraAcquire* self);

/** Run an interactive sign-in and store the account. @account_out is the UPN. */
gboolean entra_acquire_login(EntraAcquire* self, char** account_out, GError** error);

/** Acquire an access token for the request's scopes, silently if it can. */
gboolean entra_acquire_token(EntraAcquire* self, EntraTokenResult* result, GError** error);

#endif /* ENTRA_ACQUIRE_H */
