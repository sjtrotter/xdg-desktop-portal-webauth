/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_GTK_EXTERNALWINDOW_H
#define WEBAUTH_GTK_EXTERNALWINDOW_H

#include <glib.h>

/** @file
 *  Parenting the backend's window to the application's, from a window identifier.
 *
 *  Parsing "x11:<xid in hex>" and "wayland:<xdg_foreign handle>" is BACKEND
 *  work, and it is here rather than in the frontend for the reason upstream
 *  puts it in xdg-desktop-portal-gtk's src/externalwindow.c and not in
 *  xdg-desktop-portal: the frontend has no display connection, no toolkit and
 *  no window, so it has nothing to parent and no way to check a handle. It
 *  passes @parent_window through as an opaque string.
 *  https://flatpak.github.io/xdg-desktop-portal/docs/window-identifiers.html
 *
 *  Rules that must survive any implementation:
 *
 *  - An empty identifier is legal and normal, and means an unparented window.
 *  - An INVALID or expired identifier degrades to an unparented window. It never
 *    aborts the transaction. A user whose compositor recycled a handle must
 *    still be able to sign in.
 *  - Parenting is not activation. The parent makes the window transient and
 *    modal to the application; the "activation_token" option is what authorises
 *    taking focus, and a background caller without one does not silently steal
 *    it.
 *  - Focus returns to the application when the window closes. That is an
 *    accessibility acceptance criterion, not a nicety - see chrome.h.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef struct WebAuthExternalWindow WebAuthExternalWindow;

/** Parse @handle_str. Returns NULL for an empty string and for anything it
 *  cannot use; both mean "unparented", and neither is an error. */
WebAuthExternalWindow* webauth_external_window_new(const char* handle_str);

/** Make @window transient for the parsed parent. */
void webauth_external_window_set_parent_of(WebAuthExternalWindow* self, gpointer window);

/** Present @window, using @activation_token if one was supplied and is valid. */
void webauth_external_window_activate(WebAuthExternalWindow* self, gpointer window,
                                      const char* activation_token);

void webauth_external_window_free(WebAuthExternalWindow* self);

#endif /* WEBAUTH_GTK_EXTERNALWINDOW_H */
