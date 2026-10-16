/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_TRANSACTION_H
#define WEBAUTH_TRANSACTION_H

#include <gio/gio.h>

#include "request-impl.h"
#include "storage.h"

/** The backend owns the deadline; the frontend waits with a G_MAXINT D-Bus timeout. See docs/IMPL-INTERFACE.md. */

typedef enum
{
	WEBAUTH_RESPONSE_COMPLETED = 0, /**< @results carries "completion_uri". */
	WEBAUTH_RESPONSE_CANCELLED = 1,
	WEBAUTH_RESPONSE_OTHER = 2
} WebAuthResponse;

/** Reason symbols and extensions are listed in docs/IMPL-INTERFACE.md. */
#define WEBAUTH_REASON_TIMEOUT "timeout"
#define WEBAUTH_REASON_NO_DISPLAY "no_display"
#define WEBAUTH_REASON_NO_ENGINE "no_engine"
#define WEBAUTH_REASON_SESSION_TERMINATED "session_terminated"
#define WEBAUTH_REASON_CREDENTIAL_UNAVAILABLE "credential_unavailable"
#define WEBAUTH_REASON_UNRELATED_CERTIFICATE_CHALLENGE "unrelated_certificate_challenge"
#define WEBAUTH_REASON_USER_CANCELLED "user_cancelled"
#define WEBAUTH_REASON_REQUEST_CLOSED "request_closed"
#define WEBAUTH_REASON_TLS_ERROR "tls_error"
#define WEBAUTH_REASON_LOAD_FAILED "load_failed"
#define WEBAUTH_REASON_INVALID_REQUEST "invalid_request"
#define WEBAUTH_REASON_NO_STORAGE "no_storage"

typedef struct WebAuthTransaction WebAuthTransaction;

typedef void (*WebAuthTransactionHook)(gpointer user_data);

/** Application identity comes from the frontend; @mode is the effective storage mode. */
WebAuthTransaction* webauth_transaction_new(GDBusMethodInvocation* invocation,
                                            WebAuthImplRequest* request, const char* app_id,
                                            const char* app_identity_level, const char* start_uri,
                                            const char* completion_uri, WebAuthSessionMode mode,
                                            guint timeout_seconds);

WebAuthTransaction* webauth_transaction_ref(WebAuthTransaction* self);
void webauth_transaction_unref(WebAuthTransaction* self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(WebAuthTransaction, webauth_transaction_unref)

/** The deadline starts after the window is presented. */
void webauth_transaction_start_deadline(WebAuthTransaction* self);

/** Runs once on every terminal outcome to destroy the window and release certificate resources. */
void webauth_transaction_set_teardown(WebAuthTransaction* self, WebAuthTransactionHook hook,
                                      gpointer user_data);

/** Runs after teardown and the method reply to remove the transaction from the backend. */
void webauth_transaction_set_finished(WebAuthTransaction* self, WebAuthTransactionHook hook,
                                      gpointer user_data);

/** Returns TRUE only for the call that commits the terminal result. */
gboolean webauth_transaction_finish(WebAuthTransaction* self, WebAuthResponse response,
                                    const char* completion_uri, const char* reason);

gboolean webauth_transaction_is_done(WebAuthTransaction* self);

const char* webauth_transaction_start_uri(WebAuthTransaction* self);
const char* webauth_transaction_completion_uri(WebAuthTransaction* self);
const char* webauth_transaction_app_id(WebAuthTransaction* self);
const char* webauth_transaction_app_identity_level(WebAuthTransaction* self);
WebAuthSessionMode webauth_transaction_session_mode(WebAuthTransaction* self);
GCancellable* webauth_transaction_cancellable(WebAuthTransaction* self);

#endif /* WEBAUTH_TRANSACTION_H */
