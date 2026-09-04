/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_SERVICE_H
#define WEBAUTH_SERVICE_H

#include <gio/gio.h>

/** @file
 *  The D-Bus transaction layer: the object that owns
 *  io.github.sjtrotter.WebAuthentication1 on the session bus.
 *
 *  This layer draws nothing and knows nothing about web engines. It checks that the
 *  peer is the same UID, resolves what can be resolved about the caller's identity
 *  (identity.h), applies policy the caller may not influence — session mode, timeout
 *  ceiling, storage partition — validates @start_uri and @completion_uri before
 *  anything is opened, mints the Request object, and drives one browser session
 *  (browser_session.h) to exactly one terminal response.
 *
 *  There is no org.freedesktop.impl.portal.* backend ABI in version 0. The browser
 *  session is an in-process C vtable, not a second bus interface: publishing a
 *  backend ABI before the front ABI is settled doubles the versioning, activation,
 *  crash-handling and packaging obligations for no gain. See docs/ROADMAP.md.
 *
 *  Sketch only; nothing here is implemented.
 */

#define WEBAUTH_BUS_NAME "io.github.sjtrotter.WebAuthentication1"
#define WEBAUTH_OBJECT_PATH "/io/github/sjtrotter/WebAuthentication1"
#define WEBAUTH_INTERFACE_VERSION 1u

typedef struct WebAuthService WebAuthService;

/** Create the service and export it on @connection. Does not acquire the name. */
WebAuthService* webauth_service_new(GDBusConnection* connection, GError** error);

/** Acquire WEBAUTH_BUS_NAME, refusing to start if no browser session can be
 *  created at all. */
gboolean webauth_service_acquire(WebAuthService* self, GError** error);

/** Whether @sender runs as the same UID as the service. Checked before the request
 *  is parsed, not after. */
gboolean webauth_service_peer_is_same_user(WebAuthService* self, const char* sender);

/** Rate limit @sender. Repeated background requests from one connection are the
 *  cheapest way to turn this service into a phishing launcher. */
gboolean webauth_service_rate_limit_allows(WebAuthService* self, const char* sender);

void webauth_service_free(WebAuthService* self);

#endif /* WEBAUTH_SERVICE_H */
