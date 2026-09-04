/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_BROWSER_SESSION_H
#define WEBAUTH_BROWSER_SESSION_H

#include <glib.h>

#include "transaction.h"

/** @file
 *  The in-process seam between the transaction layer and whatever shows the page.
 *
 *  A C vtable, deliberately not a D-Bus interface. Version 0 is one service with one
 *  implementation (webkit_session.h); the abstraction exists so a second one can be
 *  added without touching the transaction layer, and so the eventual
 *  org.freedesktop.impl.portal.* split has an obvious place to happen — but
 *  publishing a backend ABI now would double the versioning, activation, crash
 *  handling and packaging obligations before the front ABI is settled.
 *
 *  Intended implementations, in preference order: the system browser where the
 *  completion mechanism lets it securely return the result (loopback HTTP, claimed
 *  https app links, registered custom schemes); a service-owned WebKitGTK session
 *  where interception or a client certificate from a hardware token requires it;
 *  manual paste as a headless fallback; a browser extension only as an experimental,
 *  explicitly installed integration. A session advertises what it can do so the
 *  transaction layer can choose.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef enum
{
	WEBAUTH_CAP_INTERCEPT_HTTPS = 1 << 0, /**< can complete on a remote https navigation */
	WEBAUTH_CAP_CLIENT_CERTS = 1 << 1,    /**< can answer a TLS client certificate challenge */
	WEBAUTH_CAP_SMARTCARD = 1 << 2,       /**< can use a certificate from the smart card service */
	WEBAUTH_CAP_EPHEMERAL = 1 << 3,       /**< can run in a genuinely ephemeral data store */
	WEBAUTH_CAP_LOOPBACK = 1 << 4         /**< can complete on a loopback redirect */
} WebAuthCapability;

typedef struct WebAuthBrowserSession WebAuthBrowserSession;

typedef void (*WebAuthSessionDone)(WebAuthTransaction* transaction, WebAuthResponse response,
                                   const char* completion_uri, gpointer user_data);

typedef struct
{
	const char* name;
	guint capabilities; /**< a mask of WebAuthCapability */

	WebAuthBrowserSession* (*create)(WebAuthTransaction* transaction, const char* parent_window,
	                                 const char* activation_token, const char* title,
	                                 GError** error);
	gboolean (*present)(WebAuthBrowserSession* session, WebAuthSessionDone done,
	                    gpointer user_data, GError** error);
	void (*cancel)(WebAuthBrowserSession* session);
	void (*destroy)(WebAuthBrowserSession* session);
} WebAuthBrowserSessionVtable;

/** Pick an implementation able to satisfy @required for this transaction. */
const WebAuthBrowserSessionVtable* webauth_browser_session_select(guint required, GError** error);

#endif /* WEBAUTH_BROWSER_SESSION_H */
