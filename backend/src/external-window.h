/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 * SPDX-FileCopyrightText: 2016 Red Hat, Inc.
 *
 * xdg-desktop-portal-webauth
 *
 * Derived from xdg-desktop-portal-gtk's src/externalwindow*.c and from
 * libgxdp's gxdp-external-window*.c, by Jonas Ådahl.
 */
#ifndef WEBAUTH_EXTERNAL_WINDOW_H
#define WEBAUTH_EXTERNAL_WINDOW_H

#include <gtk/gtk.h>

/** @file
 *  Parenting one of this backend's windows to the application window that
 *  provoked it, from the portal window-identifier string.
 *
 *  AN INVALID OR EXPIRED PARENT MUST NOT FAIL THE REQUEST. Every failure here
 *  degrades to an unparented, service-controlled window, which is what the
 *  sign-in window has to be able to draw anyway: a caller may legitimately pass
 *  "". A backend that refused to open a sign-in window because a window handle
 *  had gone stale would be a backend that fails closed in the one place where
 *  failing closed means the user cannot sign in.
 */

/** Realize @window, parent it to @parent_window, and present it.
 *  @parent_window uses the portal convention: "wayland:<xdg_foreign handle>",
 *  "x11:<hex XID>", or "" for none. @activation_token authorises focus; a
 *  background caller without one does not steal it. */
void webauth_external_window_present(GtkWindow* window, const char* parent_window,
                                         const char* activation_token);

#endif /* WEBAUTH_EXTERNAL_WINDOW_H */
