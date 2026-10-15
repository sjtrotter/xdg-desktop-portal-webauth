/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_REDACT_H
#define WEBAUTH_REDACT_H

#include <glib.h>

/** Field kinds restrict logged values; URI queries and secret values must never reach logs. */

typedef enum
{
	WEBAUTH_FIELD_OUTCOME,
	WEBAUTH_FIELD_HOST,
	WEBAUTH_FIELD_SCHEME,
	WEBAUTH_FIELD_PORT,
	WEBAUTH_FIELD_APP_ID,
	WEBAUTH_FIELD_COUNT,
	WEBAUTH_FIELD_DURATION,
	WEBAUTH_FIELD_URI_SHAPE, /**< Scheme, host, port and path length only. */
	WEBAUTH_FIELD_URI,       /**< Value redacted. */
	WEBAUTH_FIELD_QUERY,     /**< Value redacted. */
	WEBAUTH_FIELD_CERT_URI,  /**< Value redacted; identifies the card holder. */
	WEBAUTH_FIELD_SECRET     /**< Value and length redacted. */
} WebAuthFieldKind;

/** Stable event names for log consumers. */
#define WEBAUTH_EVENT_START_RECEIVED "start-received"
#define WEBAUTH_EVENT_START_REFUSED "start-refused"
#define WEBAUTH_EVENT_WINDOW_OPENED "window-opened"
#define WEBAUTH_EVENT_NAVIGATION "navigation"
#define WEBAUTH_EVENT_COMPLETED "completed"
#define WEBAUTH_EVENT_CANCELLED "cancelled"
#define WEBAUTH_EVENT_TIMEOUT "timeout"
#define WEBAUTH_EVENT_LOAD_FAILED "load-failed"
#define WEBAUTH_EVENT_TLS_ERROR "tls-error"
#define WEBAUTH_EVENT_WEB_PROCESS_GONE "web-process-gone"
#define WEBAUTH_EVENT_CERT_CHALLENGE "certificate-challenge"
#define WEBAUTH_EVENT_CERT_ANSWERED "certificate-answered"
#define WEBAUTH_EVENT_CERT_DECLINED "certificate-declined"
#define WEBAUTH_EVENT_PIN_REQUESTED "certificate-pin-requested"
#define WEBAUTH_EVENT_CERT_RELEASED "certificate-released"
#define WEBAUTH_EVENT_FRONTEND "frontend"
#define WEBAUTH_EVENT_STORAGE "storage"
#define WEBAUTH_EVENT_HARDENING "process-hardening"

/** Arguments are (const char *name, WebAuthFieldKind kind, const char *value) groups terminated by a NULL name. */
void webauth_log_event(GLogLevelFlags level, const char* event, ...) G_GNUC_NULL_TERMINATED;

/** The returned string is newly allocated. */
char* webauth_redact_field(WebAuthFieldKind kind, const char* value);

/** Returns newly allocated text truncated before any embedded URI. */
char* webauth_redact_error_text(const char* message);

void webauth_log_set_verbose(gboolean verbose);
gboolean webauth_log_get_verbose(void);

#endif /* WEBAUTH_REDACT_H */
