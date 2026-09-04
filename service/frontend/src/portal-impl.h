/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_PORTAL_IMPL_H
#define WEBAUTH_PORTAL_IMPL_H

#include <gio/gio.h>

/** @file
 *  Finding a backend: .portal files and a portals.conf-style preference list.
 *
 *  Upstream this is desktop-portal/xdp-portal-config.[ch] (it was
 *  src/portal-impl.c before the tree was reorganised), and its struct is
 *  exactly the four fields below:
 *
 *      typedef struct { char *source; char *dbus_name;
 *                       char **interfaces; char **use_in; } XdpImplConfig;
 *
 *  with xdp_portal_config_find(config, "org.freedesktop.impl.portal.Account")
 *  returning the backend a portal should proxy to. This header is that, renamed.
 *
 *  A .portal file is a key file with one [portal] group:
 *
 *      [portal]
 *      DBusName=io.github.sjtrotter.impl.portal.WebAuthentication.gtk
 *      Interfaces=io.github.sjtrotter.impl.portal.WebAuthentication1;
 *      UseIn=gnome
 *
 *  DBusName is the backend's D-Bus activation name; Interfaces is a semicolon
 *  separated list of the impl interfaces it implements; UseIn is the deprecated
 *  legacy desktop-environment key, kept because upstream still ships it in
 *  xdg-desktop-portal-gtk's data/gtk.portal.in.
 *  https://flatpak.github.io/xdg-desktop-portal/docs/writing-a-new-backend.html
 *
 *  portals.conf is an ini file whose [preferred] group maps an impl interface
 *  name (or "default") to a semicolon-separated list of backends "to be searched
 *  for an implementation of the requested interface, in the same order as
 *  specified in the configuration file", with the special values "none" (disable
 *  the interface) and "*" (first implementation found, lexicographically):
 *
 *      [preferred]
 *      default=gtk
 *      io.github.sjtrotter.impl.portal.WebAuthentication1=gtk
 *
 *  https://flatpak.github.io/xdg-desktop-portal/docs/portals.conf.html
 *
 *  WHERE THESE FILES LIVE, AND WHY NOT WHERE UPSTREAM PUTS THEM. Upstream reads
 *  $XDG_DATA_DIRS/xdg-desktop-portal/portals/*.portal and
 *  xdg-desktop-portal/portals.conf under the XDG config and data paths. This
 *  incubating frontend deliberately reads its own directories instead:
 *
 *      $XDG_DATA_DIRS/webauth-portal/portals/*.portal
 *      $XDG_CONFIG_HOME/webauth-portal/portals.conf, then $XDG_CONFIG_DIRS,
 *      then sysconfdir, then $XDG_DATA_HOME, then $XDG_DATA_DIRS
 *
 *  in that precedence order, which is upstream's own search order. Reading
 *  upstream's directories would mean an unaccepted prototype parsing - and
 *  potentially being confused by, or confusing - the configuration of the real
 *  xdg-desktop-portal on the same machine. At acceptance this whole file is
 *  deleted: the frontend moves into xdg-desktop-portal, which already has it,
 *  and only the .portal file installed by the backend survives the move (with
 *  its directory and its interface names changed). See docs/UPSTREAMING.md.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef struct
{
	char* source;      /**< the .portal file this came from, for diagnostics */
	char* dbus_name;   /**< the backend's D-Bus activation name */
	char** interfaces; /**< impl interfaces it claims to implement */
	char** use_in;     /**< the deprecated UseIn key, parsed and mostly ignored */
} WebAuthImplConfig;

typedef struct WebAuthPortalConfig WebAuthPortalConfig;

/** Load every readable .portal file and the highest-precedence portals.conf. A
 *  malformed file is skipped with a warning rather than being fatal: one bad
 *  package must not stop every other backend from being found. */
WebAuthPortalConfig* webauth_portal_config_new(GError** error);

/** The preferred backend implementing @impl_interface, honouring the
 *  [preferred] list, "none" and "*". NULL when nothing implements it, in which
 *  case the interface is NOT exported at all - upstream's init_account() simply
 *  returns when no impl is configured, and an interface that cannot be served
 *  should be absent rather than failing every call. */
WebAuthImplConfig* webauth_portal_config_find(WebAuthPortalConfig* self,
                                              const char* impl_interface);

/** Every backend implementing @impl_interface, in preference order. */
GPtrArray* webauth_portal_config_find_all(WebAuthPortalConfig* self, const char* impl_interface);

void webauth_portal_config_free(WebAuthPortalConfig* self);

#endif /* WEBAUTH_PORTAL_IMPL_H */
