/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ENTRA_IPC_REQUEST_H
#define ENTRA_IPC_REQUEST_H

#include <glib.h>

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
 *
 *  Sketch only; nothing here is implemented.
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

typedef struct
{
	guint schema; /**< ENTRA_SCHEMA_VERSION */
	char* verb;
	char* authority;
	char* tenant;
	char* client_id;
	char** scopes;  /**< decoded, NULL terminated */
	char* req_cnf;  /**< base64url confirmation object; its presence means PoP */
	char* account;
	char* prompt;        /**< auto, always, never */
	char* parent_window; /**< advisory, passed to the web auth service, never trusted */
} EntraRequest;

typedef struct
{
	guint schema;
	EntraStatus status;
	char* token; /**< scrubbed on free; only ever written to stdout */
	char* token_type;
	gint64 expires_in;
	char* account;
	char* error;   /**< stable symbol */
	char* message; /**< already redacted; never parsed by consumers */
} EntraResponse;

/** Parse a request from JSON, rejecting unknown schema versions and forbidden fields. */
EntraRequest* entra_request_from_json(const char* json, GError** error);

/** Serialize a response as the JSON object of docs/ENTRA-CLIENT-CLI.md. */
char* entra_response_to_json(const EntraResponse* response);

/** The documented exit code for @status. */
int entra_status_exit_code(EntraStatus status);

void entra_request_free(EntraRequest* request);
void entra_response_free(EntraResponse* response); /**< scrubs the token */

#endif /* ENTRA_IPC_REQUEST_H */
