/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ENTRA_CACHE_KEYRING_H
#define ENTRA_CACHE_KEYRING_H

#include <glib.h>

/** @file
 *  Where refresh tokens live, and the cache key that decides which token a request
 *  gets back.
 *
 *  Refresh tokens go in the Secret Service keyring and nowhere else: not to stdout,
 *  not into the JSON response, not into a log, not into a file. A refresh token is in
 *  practice months of standing access, redeemable without the smart card that
 *  produced it, so handing one to a caller hands over the identity rather than a
 *  token for one connection. When no keyring is available the client runs with no
 *  persistence or reports unavailable; it never quietly writes secrets to a file.
 *
 *  Access tokens are cached in memory only, keyed by account, authority, tenant,
 *  client id, the sorted scope set, the token kind and the proof-of-possession
 *  binding. The binding is part of the key because a PoP token bound to one key is
 *  useless for another, and a cache that ignored it would return a token the caller
 *  cannot use.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef struct
{
	char* account;
	char* authority;
	char* tenant;
	char* client_id;
	char** scopes; /**< sorted, NULL terminated */
	gboolean pop;
	char* cnf_kid; /**< NULL for bearer */
} EntraCacheKey;

/** Whether a Secret Service is usable right now. A locked keyring counts as an
 *  interaction, not as availability. */
gboolean entra_keyring_available(GError** error);

gboolean entra_keyring_store_refresh_token(const EntraCacheKey* key, const char* refresh_token,
                                           GError** error);

/** Look up the refresh token for @key. The returned buffer must be scrubbed. */
char* entra_keyring_load_refresh_token(const EntraCacheKey* key, GError** error);

/** Replace a rotated refresh token atomically, so a lost race cannot leave the
 *  account with the invalidated one. */
gboolean entra_keyring_rotate_refresh_token(const EntraCacheKey* key, const char* old_token,
                                            const char* new_token, GError** error);

/** List stored accounts, for the accounts verb. */
GPtrArray* entra_keyring_list_accounts(GError** error);

/** Forget an account: its refresh token, its record, and any web session
 *  the authentication service still holds for it. */
gboolean entra_keyring_forget(const char* account, GError** error);

#endif /* ENTRA_CACHE_KEYRING_H */
