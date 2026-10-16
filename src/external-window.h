/* SPDX-License-Identifier: LGPL-2.1-or-later
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

/** Invalid or expired parent handles fall back to an unparented window. */

/** @parent_window is "wayland:<xdg_foreign handle>", "x11:<hex XID>", or ""; @activation_token authorises focus. */
void webauth_external_window_present(GtkWindow* window, const char* parent_window,
                                         const char* activation_token);

#endif /* WEBAUTH_EXTERNAL_WINDOW_H */
