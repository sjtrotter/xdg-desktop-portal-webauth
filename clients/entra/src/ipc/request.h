/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_IPC_REQUEST_H
#define ENTRA_IPC_REQUEST_H

#include <glib.h>

#include "../entra-error.h"

/** @file
 *  The versioned request and response objects of docs/ENTRA-CLIENT-CLI.md.
 *
 *  Today these are only ever built from argv and printed to stdout, but they are
 *  shaped as messages from the start: a schema number rather than a positional
 *  convention, named fields rather than varargs, and a structured status rather than
 *  a boolean. Moving the client behind a socket later should be a transport change,
 *  not a redesign — and the same shape is what a typed FreeRDP provider request would
 *  carry, so the two can converge.
 *
 *  Fields a caller may never set, in any transport: a token endpoint, an
 *  authorization endpoint, or a redirect URI. The client derives every URL from the
 *  authority and its own cloud table.
 */

#define ENTRA_SCHEMA_VERSION 1

typedef enum
{
	ENTRA_STATUS_OK,
	ENTRA_STATUS_INTERACTION_REQUIRED,
	ENTRA_STATUS_CANCELLED,
	ENTRA_STATUS_NO_ACCOUNT,
	ENTRA_STATUS_UNAVAILABLE,
	ENTRA_STATUS_SERVER_ERROR,
	ENTRA_STATUS_USAGE,
	ENTRA_STATUS_INTERNAL
} EntraStatus;

/** The documented exit code for @status. */
int entra_status_exit_code(EntraStatus status);

/** The stable symbol for @status: the JSON "status" field. */
const char* entra_status_symbol(EntraStatus status);

/** Classify a GError from this client's own domain. Anything else is internal. */
EntraStatus entra_status_from_error(const GError* error);

/** The success response of docs/ENTRA-CLIENT-CLI.md, as one JSON object.
 *  @token is written; nothing else in this program ever puts one in a string. */
char* entra_response_token_json(const char* token, const char* token_type, gint64 expires_in,
                                const char* scope, const char* account);

/** The failure response. It never carries a token, a code, a state value or an
 *  authorization server error_description. */
char* entra_response_error_json(EntraStatus status, const char* error_symbol, const char* message);

/** The accounts response. @records is a GPtrArray of EntraAccountRecord. */
char* entra_response_accounts_json(GPtrArray* records);

#endif /* ENTRA_IPC_REQUEST_H */
