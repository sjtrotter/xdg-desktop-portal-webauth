/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_HTTP_H
#define ENTRA_OAUTH_HTTP_H

#include <gio/gio.h>
#include <glib.h>

#include "../entra-config.h"

/** @file
 *  The one HTTP client in this program, and the rules it is built with.
 *
 *  TLS errors fail closed: there is no "ignore certificate errors" anywhere, and
 *  the only way to trust anything the system store does not is the [testing]
 *  group of the user's configuration file, which exists for tools/entra-e2e.sh's
 *  fixture identity provider (see entra-config.h).
 *
 *  Nothing here ever logs a URL with a query, a request body or a response body:
 *  the first two carry the authorization code and the PKCE verifier and the third
 *  carries the tokens.
 */

typedef struct _EntraHttp EntraHttp;

EntraHttp* entra_http_new(EntraConfig* config, GError** error);

/** GET @url and return its body. Non-2xx is an error carrying the status code. */
GBytes* entra_http_get(EntraHttp* self, const char* url, GCancellable* cancellable,
                       GError** error);

/** POST @body as application/x-www-form-urlencoded. The body is returned for any
 *  status, because an authorization server's 400 carries the error object this
 *  client has to classify; @status_out receives the code. */
GBytes* entra_http_post_form(EntraHttp* self, const char* url, const char* body, guint* status_out,
                             GCancellable* cancellable, GError** error);

void entra_http_free(EntraHttp* self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EntraHttp, entra_http_free)

#endif /* ENTRA_OAUTH_HTTP_H */
