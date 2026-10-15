/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_WEBKIT_SESSION_H
#define WEBAUTH_WEBKIT_SESSION_H

#include <glib.h>

#include "transaction.h"

/** WebKit sessions intercept completion before load; engine security limits are described in docs/SECURITY.md. */

/** Development trust override: @spec is "host=/path/to/certificate.pem" and applies process-wide; see docs/TESTING.md. */
gboolean webauth_webkit_debug_trust_add(const char* spec, GError** error);

/** A display is required before the backend claims the interface. */
gboolean webauth_webkit_session_available(GError** error);

typedef struct WebAuthWebkitSession WebAuthWebkitSession;

/** Holds a reference to @transaction and registers its teardown hook. */
WebAuthWebkitSession* webauth_webkit_session_new(WebAuthTransaction* transaction,
                                                 const char* parent_window,
                                                 const char* activation_token,
                                                 const char* title_hint, GError** error);

/** All terminal outcomes complete through webauth_transaction_finish(). */
void webauth_webkit_session_present(WebAuthWebkitSession* self);

#endif /* WEBAUTH_WEBKIT_SESSION_H */
