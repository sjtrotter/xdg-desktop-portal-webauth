/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_REDACT_H
#define WEBAUTH_GTK_REDACT_H

#include <glib.h>

/** @file
 *  Logging that cannot leak, because it never sees the value.
 *
 *  Redaction here is structural rather than textual: the logging interface takes
 *  typed fields, and the field's kind decides what may be printed. There is
 *  deliberately no "log this URL" entry point that a later edit could be pointed at a
 *  redirect, and no format string a caller can slip a token through. A field whose
 *  kind is not loggable renders as its kind and its length, "<url:212>", never its
 *  value. The backend therefore logs what happened and never what it was
 *  carrying.
 *
 *  The frontend is under exactly the same obligation and has no copy of this
 *  file: it must never log a start URI, a completion URI or a query string
 *  either, and upstream's frontend offers no entry point that could. Duplicating
 *  the implementation would be worse than sharing the rule; at upstream
 *  acceptance the redaction helpers belong in xdg-desktop-portal's shared code
 *  next to the completion matcher, for the same reason.
 *
 *  Sketch only; nothing here is implemented. See docs/SECURITY.md for the artifacts
 *  that must never appear at any level.
 */

typedef enum
{
	WEBAUTH_FIELD_OUTCOME,   /**< a stable symbol: MATCHED, UNRELATED, CANCELLED, TIMEOUT */
	WEBAUTH_FIELD_HOST,      /**< a host name, loggable: it is what makes a report useful */
	WEBAUTH_FIELD_SCHEME,    /**< loggable */
	WEBAUTH_FIELD_PORT,      /**< loggable */
	WEBAUTH_FIELD_APP_ID,    /**< loggable */
	WEBAUTH_FIELD_COUNT,     /**< loggable: tokens found, certificates found, chosen index */
	WEBAUTH_FIELD_DURATION,  /**< loggable */
	WEBAUTH_FIELD_URI,       /**< NEVER loggable: a completion URI carries the credential */
	WEBAUTH_FIELD_QUERY,     /**< NEVER loggable */
	WEBAUTH_FIELD_CERT_URI,  /**< NEVER loggable: names the card and its holder */
	WEBAUTH_FIELD_SECRET     /**< NEVER loggable, and never rendered with a length either */
} WebAuthFieldKind;

/** Log one event with typed fields. @... is (WebAuthFieldKind, const char*) pairs
 *  terminated by WEBAUTH_FIELD_OUTCOME with a NULL value. */
void webauth_log_event(GLogLevelFlags level, const char* event, ...) G_GNUC_NULL_TERMINATED;

/** Render one field the way it would be logged, for tests. */
char* webauth_redact_field(WebAuthFieldKind kind, const char* value);

/** Cut @message before any embedded URI, for loader and TLS error text. */
char* webauth_redact_error_text(const char* message);

#endif /* WEBAUTH_GTK_REDACT_H */
