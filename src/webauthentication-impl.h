/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_WEBAUTHENTICATION_IMPL_H
#define WEBAUTH_WEBAUTHENTICATION_IMPL_H

#include <gio/gio.h>

/** The backend returns the Start result by method reply; the frontend emits Response. See docs/IMPL-INTERFACE.md. */

#define WEBAUTH_BACKEND_BUS_NAME "org.freedesktop.impl.portal.desktop.webauth"
#define WEBAUTH_BACKEND_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define WEBAUTH_IMPL_INTERFACE "org.freedesktop.impl.portal.experimental.WebAuthentication1"
#define WEBAUTH_IMPL_INTERFACE_VERSION 1u

/** Only the current owner of this bus name may call the backend. */
#define WEBAUTH_FRONTEND_BUS_NAME "org.freedesktop.portal.Desktop"

/** Timeout limits are enforced independently of the frontend. */
#define WEBAUTH_DEFAULT_TIMEOUT 300u
#define WEBAUTH_MAX_TIMEOUT 900u

typedef struct WebAuthBackend WebAuthBackend;

WebAuthBackend* webauth_backend_new(GDBusConnection* bus, GError** error);

void webauth_backend_shutdown(WebAuthBackend* backend);

void webauth_backend_free(WebAuthBackend* backend);

gboolean webauth_backend_sender_is_frontend(WebAuthBackend* backend, const char* sender);

/** The Request skeleton uses the process's single backend for sender checks. */
gboolean webauth_backend_sender_is_frontend_default(const char* sender);

#endif /* WEBAUTH_WEBAUTHENTICATION_IMPL_H */
