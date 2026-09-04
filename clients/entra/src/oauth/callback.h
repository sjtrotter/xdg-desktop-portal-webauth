/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ENTRA_OAUTH_CALLBACK_H
#define ENTRA_OAUTH_CALLBACK_H

#include <glib.h>

#include "transaction.h"

/** @file
 *  Classifying the URI the web authentication service returned.
 *
 *  The service guarantees only that the URI matched the completion URI it was given.
 *  Deciding whether it is a valid authorization response is OAuth knowledge and
 *  therefore lives here. A URI is accepted only when its scheme, host, port and path
 *  equal the
 *  transaction's redirect URI (empty path and "/" are the same resource), it carries
 *  no userinfo and no fragment, "state" occurs exactly once and matches in constant
 *  time, and it has exactly one of "code" or "error" — where a parameter present
 *  without a value still counts as an occurrence, so a second code cannot be smuggled
 *  in as a bare "code". Every percent escape must be well formed and %00 is rejected.
 *
 *  This is the check FreeRDP's aad/oauth-hardening branch performs; keeping it in the
 *  client rather than the service is what lets the service stay protocol agnostic.
 *
 *  Sketch only; nothing here is implemented. See docs/SECURITY.md.
 */

typedef enum
{
	ENTRA_CALLBACK_CODE,      /**< a usable authorization code */
	ENTRA_CALLBACK_ERROR,     /**< the authorization server declined */
	ENTRA_CALLBACK_UNRELATED, /**< not the redirect this transaction expects */
	ENTRA_CALLBACK_INVALID    /**< it looked like the redirect but failed a check */
} EntraCallbackResult;

/** Classify @url against @transaction. On ENTRA_CALLBACK_CODE, @code receives the
 *  decoded authorization code; the caller must scrub it. Only the outcome symbol is
 *  ever logged, never @url and nothing parsed out of it. */
EntraCallbackResult entra_callback_classify(EntraTransaction* transaction, const char* url,
                                            char** code);

/** The stable symbol for @result, for logs and for the JSON "error" field. */
const char* entra_callback_result_str(EntraCallbackResult result);

#endif /* ENTRA_OAUTH_CALLBACK_H */
