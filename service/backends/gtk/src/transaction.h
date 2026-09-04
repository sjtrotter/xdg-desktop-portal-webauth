/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_TRANSACTION_H
#define WEBAUTH_GTK_TRANSACTION_H

#include <gio/gio.h>

/** @file
 *  One transaction in the backend, and the only object allowed to complete it.
 *
 *  A transaction owns the start URI, the completion URI, the deadline, the
 *  storage partition it was given, the app id the FRONTEND established, the
 *  window, whatever the certificate adapter is holding, and exactly one
 *  terminal result. It is reference counted because the web view, each UI-thread
 *  idle, the pending impl method invocation and the timeout source all hold
 *  references and outlive one another in unpredictable orders.
 *
 *  The races are specified rather than discovered, and the specification is
 *  unchanged by the frontend/backend split - only the place each half is
 *  enforced has moved:
 *
 *    - a committed completion wins over a simultaneous Close();
 *    - a Close() forwarded from the frontend never yields a later success;
 *    - a timeout closes the window and answers 2;
 *    - every late event after the terminal result is discarded;
 *    - certificate and PIN dialogs belong to the transaction and close with it;
 *    - whatever the certificate adapter holds - a grant, an endpoint, a PKCS#11
 *      session - is released on EVERY exit path.
 *
 *  Two races are NEW, and they are the price decisions/0008 accepts:
 *
 *    - THE FRONTEND CAN VANISH. If the connection to the frontend drops, the
 *      transaction is cancelled and the window destroyed at once: there is
 *      nobody left to answer, and a window belonging to no request is exactly
 *      the "leaked window" failure the interface promises not to have.
 *    - THIS PROCESS CAN VANISH. The frontend then owes the application a
 *      response with reason "backend_disappeared". Nothing in this process can
 *      help with that, which is the point of putting the guarantee in the
 *      frontend - but it does mean this process must not hold anything the
 *      frontend needs in order to answer.
 *
 *  Also new, and less obvious: the deadline exists in BOTH processes. This one
 *  is authoritative and slightly shorter, so that a live backend answers first
 *  and produces a clean "timeout" reason; the frontend's is the backstop for a
 *  backend that has stopped answering at all.
 *
 *  The model comes from the working Remmina implementation, which replaced a
 *  design that accepted any redirect the web view happened to see and polled a
 *  borrowed pointer every 500 ms without a bound.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef enum
{
	WEBAUTH_RESPONSE_COMPLETED = 0, /**< @results carries "completion_uri" */
	WEBAUTH_RESPONSE_CANCELLED = 1, /**< the user closed the window, or Close() arrived */
	WEBAUTH_RESPONSE_OTHER = 2      /**< timeout, no engine, no display, no cert adapter */
} WebAuthResponse;

typedef struct WebAuthTransaction WebAuthTransaction;

/** @app_id and @app_id_kind are what the frontend established and are used for
 *  display only; this process never re-derives them. @partition_id is the store
 *  chosen from the frontend's session_mode decision (storage.h). */
WebAuthTransaction* webauth_transaction_new(GDBusMethodInvocation* invocation,
                                            const char* handle_path, const char* app_id,
                                            const char* app_id_kind, const char* start_uri,
                                            const char* completion_uri, const char* partition_id,
                                            guint timeout_seconds);

WebAuthTransaction* webauth_transaction_ref(WebAuthTransaction* self);
void webauth_transaction_unref(WebAuthTransaction* self);

/** Record the terminal result, release everything the adapter held, unexport the
 *  impl request, and return from the Start method call.
 *  @return TRUE if this call is the one that completed the transaction. */
gboolean webauth_transaction_finish(WebAuthTransaction* self, WebAuthResponse response,
                                    const char* completion_uri, const char* reason);

gboolean webauth_transaction_is_done(WebAuthTransaction* self);

const char* webauth_transaction_completion_uri(WebAuthTransaction* self);
const char* webauth_transaction_partition_id(WebAuthTransaction* self);
const char* webauth_transaction_app_id(WebAuthTransaction* self);
const char* webauth_transaction_app_id_kind(WebAuthTransaction* self);

#endif /* WEBAUTH_GTK_TRANSACTION_H */
