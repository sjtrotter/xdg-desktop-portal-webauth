/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_TRANSACTION_H
#define WEBAUTH_TRANSACTION_H

#include <gio/gio.h>

#include "request-impl.h"
#include "storage.h"

/** @file
 *  One transaction in the backend, and the only object allowed to complete it.
 *
 *  A transaction owns the start URI, the completion URI, the deadline, the
 *  storage mode it was given, the app id the FRONTEND established, the window,
 *  whatever the certificate adapter is holding, and exactly one terminal result.
 *  It is reference counted because the web view, each idle, the pending impl
 *  method invocation and the timeout source all hold references and outlive one
 *  another in unpredictable orders.
 *
 *  The races are specified rather than discovered:
 *
 *    - a committed completion wins over a simultaneous Close();
 *    - a Close() forwarded from the frontend never yields a later success;
 *    - a timeout closes the window and answers 2;
 *    - every late event after the terminal result is discarded;
 *    - whatever the certificate adapter holds is released on EVERY exit path;
 *    - THE FRONTEND CAN VANISH: the transaction is cancelled and the window
 *      destroyed at once, because a window belonging to no request is exactly
 *      the "leaked window" failure the interface promises not to have.
 *
 *  The deadline lives here and only here. The frontend forwards a clamped
 *  "timeout" and then waits on the impl call with a D-Bus timeout of G_MAXINT:
 *  it has no timer of its own, so a backend that never answers is a request that
 *  never ends. See docs/IMPL-INTERFACE.md.
 */

typedef enum
{
	WEBAUTH_RESPONSE_COMPLETED = 0, /**< @results carries "completion_uri" */
	WEBAUTH_RESPONSE_CANCELLED = 1, /**< the user closed the window, or Close() arrived */
	WEBAUTH_RESPONSE_OTHER = 2      /**< timeout, no engine, no display, no cert adapter */
} WebAuthResponse;

/** The reason symbols this backend emits. The first six are the impl XML's own
 *  vocabulary; the rest are additions, listed in docs/IMPL-INTERFACE.md, which
 *  the XML permits by saying "for instance". */
#define WEBAUTH_REASON_TIMEOUT "timeout"
#define WEBAUTH_REASON_NO_DISPLAY "no_display"
#define WEBAUTH_REASON_NO_ENGINE "no_engine"
#define WEBAUTH_REASON_SESSION_TERMINATED "session_terminated"
#define WEBAUTH_REASON_NO_CERTIFICATE_ADAPTER "no_certificate_adapter"
#define WEBAUTH_REASON_UNRELATED_CERTIFICATE_CHALLENGE "unrelated_certificate_challenge"
#define WEBAUTH_REASON_USER_CANCELLED "user_cancelled"
#define WEBAUTH_REASON_REQUEST_CLOSED "request_closed"
#define WEBAUTH_REASON_TLS_ERROR "tls_error"
#define WEBAUTH_REASON_LOAD_FAILED "load_failed"
#define WEBAUTH_REASON_INVALID_REQUEST "invalid_request"
#define WEBAUTH_REASON_NO_STORAGE "no_storage"

typedef struct WebAuthTransaction WebAuthTransaction;

/** Called when the transaction reaches its terminal result: the window and every
 *  dialog belonging to it are destroyed here, and anything the certificate
 *  adapter holds is released. */
typedef void (*WebAuthTransactionHook)(gpointer user_data);

/** @app_id and @app_id_kind are what the frontend established and are used for
 *  display only; this process never re-derives them. @mode is the EFFECTIVE
 *  storage mode (storage.h). */
WebAuthTransaction* webauth_transaction_new(GDBusMethodInvocation* invocation,
                                            WebAuthImplRequest* request, const char* app_id,
                                            const char* app_id_kind, const char* start_uri,
                                            const char* completion_uri, WebAuthSessionMode mode,
                                            guint timeout_seconds);

WebAuthTransaction* webauth_transaction_ref(WebAuthTransaction* self);
void webauth_transaction_unref(WebAuthTransaction* self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(WebAuthTransaction, webauth_transaction_unref)

/** Start the deadline. Called once the window is up, so that a slow start-up
 *  does not eat the application's timeout. */
void webauth_transaction_start_deadline(WebAuthTransaction* self);

/** Run @hook when the transaction finishes, and again never: the web view's
 *  window and everything it owns go away here. */
void webauth_transaction_set_teardown(WebAuthTransaction* self, WebAuthTransactionHook hook,
                                      gpointer user_data);

/** Run @hook after the teardown, so that the backend can forget a transaction
 *  that has answered. It is the last thing the transaction does. */
void webauth_transaction_set_finished(WebAuthTransaction* self, WebAuthTransactionHook hook,
                                      gpointer user_data);

/** Record the terminal result, release everything, unexport the impl request,
 *  and return from the Start method call.
 *  @return TRUE if this call is the one that completed the transaction. */
gboolean webauth_transaction_finish(WebAuthTransaction* self, WebAuthResponse response,
                                    const char* completion_uri, const char* reason);

gboolean webauth_transaction_is_done(WebAuthTransaction* self);

const char* webauth_transaction_start_uri(WebAuthTransaction* self);
const char* webauth_transaction_completion_uri(WebAuthTransaction* self);
const char* webauth_transaction_app_id(WebAuthTransaction* self);
const char* webauth_transaction_app_id_kind(WebAuthTransaction* self);
WebAuthSessionMode webauth_transaction_session_mode(WebAuthTransaction* self);
GCancellable* webauth_transaction_cancellable(WebAuthTransaction* self);

#endif /* WEBAUTH_TRANSACTION_H */
