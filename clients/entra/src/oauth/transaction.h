/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_TRANSACTION_H
#define ENTRA_OAUTH_TRANSACTION_H

#include <glib.h>

/** @file
 *  One authorization attempt, and the secrets that bind its response to it.
 *
 *  A transaction holds the state value, the PKCE verifier and its S256 challenge, the
 *  redirect URI the response must arrive at, and a deadline. It is single use: a
 *  second response, whatever it carries, is either a replay of the first or an answer
 *  to a request this process did not make, and is refused. The state and the verifier
 *  are scrubbed when the transaction ends, not merely freed, because a freed buffer
 *  is still readable and these two are what a stolen authorization code would need.
 *
 *  Since the web view now lives in the portal's backend, the transaction no longer owns a
 *  window or a reference count shared with UI callbacks. It is a plain object owned
 *  by the acquisition that created it.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef struct EntraTransaction EntraTransaction;

/** Create a transaction with a fresh random state and a fresh PKCE verifier. */
EntraTransaction* entra_transaction_new(const char* redirect_uri, guint timeout_seconds,
                                        GError** error);

const char* entra_transaction_state(EntraTransaction* self);
const char* entra_transaction_challenge(EntraTransaction* self); /**< S256, for the auth request */
const char* entra_transaction_verifier(EntraTransaction* self);  /**< for the token request only */
const char* entra_transaction_redirect_uri(EntraTransaction* self);

/** Mark the transaction answered. Returns FALSE if it already was. */
gboolean entra_transaction_consume(EntraTransaction* self);

/** Scrub and release. */
void entra_transaction_free(EntraTransaction* self);

#endif /* ENTRA_OAUTH_TRANSACTION_H */
