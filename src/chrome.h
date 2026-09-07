/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_CHROME_H
#define WEBAUTH_CHROME_H

#include <gtk/gtk.h>

/** @file
 *  The security chrome: the part of the window nobody outside this process can
 *  influence.
 *
 *  Any application can ask the portal to show a convincing Microsoft, Google or
 *  corporate sign-in page. The only thing standing between that and a phishing
 *  launcher is chrome that states, independently of anything the application
 *  supplied, which application asked and which origin is being shown. So the
 *  chrome renders the app id THE FRONTEND ESTABLISHED - and says plainly when
 *  the frontend could not verify it - alongside the engine's own idea of the
 *  current origin, and it renders the application's "title" hint, if any,
 *  visibly beneath and marked as coming from the application.
 *
 *  An empty @app_id means unidentified, and unidentified is displayed as "an
 *  unidentified application" with the strongest warning this design has.
 *
 *  The origin comes from the engine, never from the start URI: a redirect that
 *  moved the flow to another host must move the label with it, and a label
 *  showing where the flow was ASKED to start is a label that can be made to lie.
 *
 *  Accessibility is an acceptance criterion here, not a refinement: every
 *  control is exposed over AT-SPI, the caller and origin are in the accessible
 *  description, nothing is conveyed by colour alone, and focus returns to the
 *  calling application when the window closes (external-window.h).
 */

#define WEBAUTH_CHROME_WINDOW_TITLE "Web sign-in"

typedef struct WebAuthChrome WebAuthChrome;

/** Called when the user asks to stop: the Cancel button, Escape, or the window
 *  manager's close. */
typedef void (*WebAuthChromeCancel)(gpointer user_data);

/** Build the trusted chrome. @app_id and @app_identity_level come from the frontend;
 *  @title_hint is application text and is rendered as such, never as the
 *  window's identity. */
WebAuthChrome* webauth_chrome_new(const char* app_id, const char* app_identity_level,
                                  const char* title_hint, WebAuthChromeCancel on_cancel,
                                  gpointer user_data);

GtkWindow* webauth_chrome_window(WebAuthChrome* self);

/** Put the web view in the window. */
void webauth_chrome_set_content(WebAuthChrome* self, GtkWidget* content);

/** Update the displayed origin as the engine navigates. Takes the engine's
 *  verified origin, never a URI from anywhere else. @secure is the engine's own
 *  verdict on the connection. */
void webauth_chrome_set_origin(WebAuthChrome* self, const char* origin, gboolean secure);

/** Say that a page is loading, so that a blank view is not mistaken for a page. */
void webauth_chrome_set_busy(WebAuthChrome* self, gboolean busy);

void webauth_chrome_free(WebAuthChrome* self);

#endif /* WEBAUTH_CHROME_H */
