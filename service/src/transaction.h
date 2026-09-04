/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_TRANSACTION_H
#define WEBAUTH_TRANSACTION_H

#include <gio/gio.h>

#include "identity.h"

/** @file
 *  One transaction, and the only object allowed to complete it.
 *
 *  A transaction owns the start URI, the completion URI, the deadline, the storage
 *  partition it was assigned, the resolved caller identity, and exactly one terminal
 *  result. It is reference counted because the browser session, each UI-thread idle,
 *  the bus method invocation and the timeout source all hold references and outlive
 *  one another in unpredictable orders.
 *
 *  The races are specified rather than discovered: a committed completion wins over a
 *  simultaneous Close(); Close() never yields a later success; caller bus
 *  disconnection cancels immediately; a timeout closes the session and answers 2;
 *  browser-session termination answers 2 exactly once; and every late event after the
 *  terminal result is discarded. Certificate and PIN dialogs belong to the transaction
 *  and close with it.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef enum
{
	WEBAUTH_RESPONSE_COMPLETED = 0, /**< @results carries "completion_uri" */
	WEBAUTH_RESPONSE_CANCELLED = 1, /**< the user closed the window, or Close() was called */
	WEBAUTH_RESPONSE_OTHER = 2      /**< timeout, no engine, no display, session died */
} WebAuthResponse;

typedef struct WebAuthTransaction WebAuthTransaction;

WebAuthTransaction* webauth_transaction_new(GDBusConnection* connection, const char* sender,
                                            const WebAuthIdentity* caller, const char* handle_path,
                                            const char* start_uri, const char* completion_uri,
                                            const char* partition_id, guint timeout_seconds);

WebAuthTransaction* webauth_transaction_ref(WebAuthTransaction* self);
void webauth_transaction_unref(WebAuthTransaction* self);

/** Record the terminal result and emit Response, if none was recorded yet.
 *  @return TRUE if this call is the one that completed the transaction. */
gboolean webauth_transaction_finish(WebAuthTransaction* self, WebAuthResponse response,
                                    const char* completion_uri);

gboolean webauth_transaction_is_done(WebAuthTransaction* self);

const char* webauth_transaction_completion_uri(WebAuthTransaction* self);
const char* webauth_transaction_partition_id(WebAuthTransaction* self);
const WebAuthIdentity* webauth_transaction_caller(WebAuthTransaction* self);

#endif /* WEBAUTH_TRANSACTION_H */
