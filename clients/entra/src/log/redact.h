/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_LOG_REDACT_H
#define ENTRA_LOG_REDACT_H

#include <glib.h>

/** @file
 *  Client side redaction, on the same structural principle as the portal backend's.
 *
 *  The client handles the artifacts the portal never sees — authorization codes,
 *  access tokens, refresh tokens, PKCE verifiers, state values, and the
 *  authorization server's error_description, which routinely names the account, the
 *  tenant and the policy that failed. None of them may reach a log at any level, and
 *  DEBUG is a licence to log more often, not to log more.
 *
 *  Redaction is structural: the logging interface takes typed fields and the kind
 *  decides what may be printed. There is no format string a caller can slip a token
 *  through and no "log this URL" entry point.
 *
 *  What a DEBUG log may contain: outcome symbols from the callback classifier, the
 *  authority host (a public constant, and the single most useful field when
 *  diagnosing a wrong-cloud failure), OAuth error codes without their descriptions,
 *  cache hit and miss counts, and phase timings. The test for whether the redaction
 *  is right: a support bundle containing a DEBUG log must not let its reader connect
 *  as the user.
 *
 *  See docs/SECURITY.md.
 */

typedef enum
{
	ENTRA_FIELD_OUTCOME,    /**< loggable: a stable symbol */
	ENTRA_FIELD_AUTHORITY,  /**< loggable */
	ENTRA_FIELD_TENANT,     /**< loggable only as a hash; a tenant id names an organisation */
	ENTRA_FIELD_ERROR_CODE, /**< loggable: invalid_grant, interaction_required, AADSTS50011 */
	ENTRA_FIELD_COUNT,      /**< loggable */
	ENTRA_FIELD_ACCOUNT,    /**< NEVER loggable */
	ENTRA_FIELD_CODE,       /**< NEVER loggable */
	ENTRA_FIELD_TOKEN,      /**< NEVER loggable */
	ENTRA_FIELD_DESCRIPTION /**< NEVER loggable: the server's error_description */
} EntraFieldKind;

/** Stable event names. Machine-greppable, translation-independent. */
#define ENTRA_EVENT_DISCOVERY "discovery"
#define ENTRA_EVENT_AUTHORIZE "authorize"
#define ENTRA_EVENT_PORTAL "portal"
#define ENTRA_EVENT_CALLBACK "callback"
#define ENTRA_EVENT_TOKEN_REQUEST "token-request"
#define ENTRA_EVENT_TOKEN_RESPONSE "token-response"
#define ENTRA_EVENT_CACHE "cache"
#define ENTRA_EVENT_KEYRING "keyring"
#define ENTRA_EVENT_RESULT "result"

/** Log one event with typed fields. @... is (const char *name, EntraFieldKind kind,
 *  const char *value) triples terminated by a NULL name. */
void entra_log_event(GLogLevelFlags level, const char* event, ...) G_GNUC_NULL_TERMINATED;

/** Render one field the way it would be logged, for tests. */
char* entra_redact_field(EntraFieldKind kind, const char* value);

/** Cut @message before any embedded URI, for transport and TLS error text. */
char* entra_redact_error_text(const char* message);

/** Overwrite and free a buffer that held credential material. */
void entra_scrub(char* secret);

void entra_log_set_verbose(gboolean verbose);
gboolean entra_log_get_verbose(void);

#endif /* ENTRA_LOG_REDACT_H */
