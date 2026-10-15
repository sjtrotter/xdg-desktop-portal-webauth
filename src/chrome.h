/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_CHROME_H
#define WEBAUTH_CHROME_H

#include <gtk/gtk.h>

/** Trusted chrome uses frontend application identity and engine origin; see docs/SECURITY.md. */

#define WEBAUTH_CHROME_WINDOW_TITLE "Web sign-in"

typedef struct WebAuthChrome WebAuthChrome;

typedef void (*WebAuthChromeCancel)(gpointer user_data);

/** @app_id and @app_identity_level come from the frontend; @title_hint is labelled as application text. */
WebAuthChrome* webauth_chrome_new(const char* app_id, const char* app_identity_level,
                                  const char* title_hint, WebAuthChromeCancel on_cancel,
                                  gpointer user_data);

GtkWindow* webauth_chrome_window(WebAuthChrome* self);

void webauth_chrome_set_content(WebAuthChrome* self, GtkWidget* content);

/** @origin and @secure must come from the engine and follow redirects. */
void webauth_chrome_set_origin(WebAuthChrome* self, const char* origin, gboolean secure);

void webauth_chrome_set_busy(WebAuthChrome* self, gboolean busy);

void webauth_chrome_free(WebAuthChrome* self);

#endif /* WEBAUTH_CHROME_H */
