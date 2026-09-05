/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_REDACT_H
#define WEBAUTH_REDACT_H

#include <glib.h>

/** @file
 *  Logging that cannot leak, because it never sees the value.
 *
 *  Redaction here is structural rather than textual: the logging interface takes
 *  typed fields, and the field's kind decides what may be printed. There is
 *  deliberately no "log this URL" entry point that a later edit could be pointed
 *  at a redirect, and no format string a caller can slip a token through. A
 *  field whose kind is not loggable renders as its kind and its length,
 *  "<uri:212>", never its value. The backend therefore logs what happened and
 *  never what it was carrying.
 *
 *  A URI may be logged in ONE shape only, WEBAUTH_FIELD_URI_SHAPE: scheme, host,
 *  port and the LENGTH of the path. The query is where an authorization code
 *  lives, so no kind renders it at all.
 *
 *  The frontend -- xdg-desktop-portal -- is under exactly the same obligation
 *  and has no copy of this file: it must never log a start URI, a completion URI
 *  or a query string either.
 */

typedef enum
{
	WEBAUTH_FIELD_OUTCOME,   /**< a stable symbol: matched, unrelated, cancelled, timeout */
	WEBAUTH_FIELD_HOST,      /**< a host name, loggable: it is what makes a report useful */
	WEBAUTH_FIELD_SCHEME,    /**< loggable */
	WEBAUTH_FIELD_PORT,      /**< loggable */
	WEBAUTH_FIELD_APP_ID,    /**< loggable */
	WEBAUTH_FIELD_COUNT,     /**< loggable: certificates found, chosen index, seconds */
	WEBAUTH_FIELD_DURATION,  /**< loggable */
	WEBAUTH_FIELD_URI_SHAPE, /**< scheme, host, port and path LENGTH; never the query */
	WEBAUTH_FIELD_URI,       /**< NEVER loggable: a completion URI carries the credential */
	WEBAUTH_FIELD_QUERY,     /**< NEVER loggable */
	WEBAUTH_FIELD_CERT_URI,  /**< NEVER loggable: names the card and its holder */
	WEBAUTH_FIELD_SECRET     /**< NEVER loggable, and never rendered with a length either */
} WebAuthFieldKind;

/** Stable event names. Machine-greppable, translation-independent. */
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

/** Log one event with typed fields. @... is (const char *name, WebAuthFieldKind
 *  kind, const char *value) triples terminated by a NULL name. */
void webauth_log_event(GLogLevelFlags level, const char* event, ...) G_GNUC_NULL_TERMINATED;

/** Render one field the way it would be logged. */
char* webauth_redact_field(WebAuthFieldKind kind, const char* value);

/** Cut @message before any embedded URI, for loader, TLS and PKCS#11 error text.
 *  Returns a newly allocated string safe to log. */
char* webauth_redact_error_text(const char* message);

void webauth_log_set_verbose(gboolean verbose);
gboolean webauth_log_get_verbose(void);

#endif /* WEBAUTH_REDACT_H */
