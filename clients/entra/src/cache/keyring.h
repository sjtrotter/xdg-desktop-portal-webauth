/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_CACHE_KEYRING_H
#define ENTRA_CACHE_KEYRING_H

#include <glib.h>

/** @file
 *  Where the account lives, and the cache key that decides which token a request
 *  gets back.
 *
 *  THE KEYRING IS THE ONLY STORE. Nothing here ever writes a file of its own: when
 *  the Secret Service is unavailable the client reports unavailable (exit 40) and a
 *  dispatcher falls through to another provider. A silent downgrade from "keyring"
 *  to "file in the home directory" is the kind of thing nobody notices until it is
 *  in a backup.
 *
 *  ONE ITEM PER ACCOUNT, identified by three attributes — authority, client_id,
 *  account — where the authority attribute is the full https://<host>/<tenant>
 *  base, so that the same person in two tenants is two items rather than one
 *  overwriting the other. The secret is a JSON record holding the refresh token,
 *  the ID token, and the cached access tokens keyed by the sorted scope set.
 *
 *  WHY THE ACCESS TOKEN IS IN THERE TOO. The CLI is a one-shot process: an
 *  in-memory cache would live for the length of one `token` call and every
 *  connection would spend a refresh round trip. It is stored inside the same
 *  secret, under the same protection as the refresh token, and it is still never
 *  written to a file, a log or the JSON response. See docs/SECURITY.md.
 *
 *  A PROOF-OF-POSSESSION TOKEN IS NEVER STORED. It is bound to one key, and a
 *  cache that ignored the binding would hand back a token the caller cannot use.
 */

#define ENTRA_KEYRING_SCHEMA_NAME "io.github.sjtrotter.entra-token-helper"

/** How close to expiry a cached access token stops being usable. A token that
 *  expires while the connection is being set up is a token that was not there. */
#define ENTRA_TOKEN_EXPIRY_MARGIN_SECONDS 300

typedef struct
{
	char* access_token;
	char* token_type;
	char* scope;       /**< as the authority granted it */
	gint64 expires_at; /**< wall clock seconds */
} EntraCachedToken;

typedef struct
{
	char* account;
	char* authority; /**< https://<host>/<tenant> */
	char* host;
	char* tenant;
	char* client_id;
	char* refresh_token; /**< the only thing here that is months of standing access */
	char* id_token;
	GHashTable* tokens; /**< scope key -> EntraCachedToken */
} EntraAccountRecord;

/** The cache key for a scope set: sorted, de-duplicated, space joined. Order is
 *  not significant to the authority and must not be significant here either, or a
 *  caller that lists the same scopes in a different order misses the cache and
 *  spends a round trip. Exposed for tests. */
char* entra_cache_scope_key(const char* const* scopes);

/** Split a scope string on any whitespace, so that one --scope carrying a space
 *  separated list and several --scope options mean the same thing. */
GStrv entra_scopes_split(const char* const* scopes);

EntraAccountRecord* entra_account_record_new(void);
void entra_account_record_free(EntraAccountRecord* record);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EntraAccountRecord, entra_account_record_free)

/** The cached token for @scope_key, or NULL when there is none or it is inside
 *  ENTRA_TOKEN_EXPIRY_MARGIN_SECONDS of expiry. */
const EntraCachedToken* entra_account_record_lookup(EntraAccountRecord* record,
                                                    const char* scope_key);

void entra_account_record_store_token(EntraAccountRecord* record, const char* scope_key,
                                      const char* access_token, const char* token_type,
                                      const char* scope, gint64 expires_at);

/** The JSON the secret holds. Separated from the keyring so a test can exhaust
 *  the round trip with no Secret Service at all. */
char* entra_account_record_to_json(const EntraAccountRecord* record);
EntraAccountRecord* entra_account_record_from_json(const char* json, GError** error);

/** Whether a Secret Service is usable right now. */
gboolean entra_keyring_available(GError** error);

gboolean entra_keyring_store(const EntraAccountRecord* record, GError** error);

/** Load the record for @authority + @client_id, and @account if it is given. With
 *  no @account: the single match, or ENTRA_ERROR_NO_ACCOUNT when there are none
 *  or more than one — never a guess. */
EntraAccountRecord* entra_keyring_load(const char* authority, const char* client_id,
                                       const char* account, GError** error);

/** Every stored account, for the accounts verb. */
GPtrArray* entra_keyring_list(GError** error);

/** Forget accounts. A NULL field matches every value of it. Returns how many
 *  items were removed. */
gboolean entra_keyring_forget(const char* authority, const char* client_id, const char* account,
                              guint* removed, GError** error);

#endif /* ENTRA_CACHE_KEYRING_H */
