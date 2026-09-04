/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_PORTAL_APP_INFO_H
#define WEBAUTH_PORTAL_APP_INFO_H

#include <gio/gio.h>

/** @file
 *  Who is asking, and how much of that answer can be believed.
 *
 *  This is the frontend's job and ONLY the frontend's job, which is the single
 *  biggest thing the frontend/backend split buys. Upstream the same code is
 *  shared/xdp-app-info.c with one file per containment framework -
 *  xdp-app-info-flatpak.c, xdp-app-info-snap.c, xdp-app-info-host.c - and the
 *  derived app id is what every portal passes to its backend as the @app_id
 *  argument. The backend never asks who the caller is; it is TOLD, by a process
 *  the caller cannot talk to.
 *
 *  An executable path is not an application identity: a same-UID process can
 *  execute another path, manipulate its launch context, or connect straight to
 *  the bus. So this header exists so that the difference between an
 *  authenticated identity and a plausible label is a type, not a comment.
 *
 *  Flatpak and Snap identities count as authenticated only when they come from
 *  the containment framework's mediation. A cgroup-derived desktop identity is
 *  a useful label and not a security principal. A caller-supplied app id is
 *  only a claim and is never accepted at all - note that the public interface
 *  has no app_id argument, precisely so that there is nothing to claim. The
 *  D-Bus unique name and the UID identify the connection and the user reliably,
 *  and identify the application publisher not at all.
 *
 *  Consequences enforced elsewhere: results are bound to the initiating unique
 *  connection (request.h); an unverified label is never the sole key for a
 *  storage partition and never crosses to the backend as one
 *  (backends/gtk/src/storage.h); an unidentified host caller gets shared or
 *  ephemeral mode, never a partition it named; and first use by an unidentified
 *  caller may warrant a confirmation before client-certificate access.
 *
 *  Sketch only; nothing here is implemented. See docs/SECURITY.md.
 */

typedef enum
{
	WEBAUTH_APP_INFO_SANDBOXED,  /**< from Flatpak/Snap mediation: authenticated */
	WEBAUTH_APP_INFO_CGROUP,     /**< derived from a systemd/cgroup unit: a label */
	WEBAUTH_APP_INFO_HOST        /**< an unsandboxed peer we can name but not vouch for */
} WebAuthAppInfoKind;

typedef struct
{
	WebAuthAppInfoKind kind;
	char* app_id;      /**< "" for a host caller, as upstream does; never NULL */
	char* unique_name; /**< the D-Bus unique name; always present; binds the result */
	uid_t uid;
} WebAuthAppInfo;

/** Resolve what can be resolved about @sender. Never fails open onto another
 *  caller's identity: an unresolvable caller is WEBAUTH_APP_INFO_HOST. */
WebAuthAppInfo* webauth_app_info_resolve(GDBusConnection* connection, const char* sender,
                                         GError** error);

/** The app id passed to the backend as @app_id. Empty string for a host caller,
 *  matching upstream's convention, so that a backend can tell "unidentified"
 *  from "identified as something" without a second argument. */
const char* webauth_app_info_get_id(const WebAuthAppInfo* self);

/** Whether @sender runs as the same UID as the frontend. Checked before the
 *  request is parsed, not after. */
gboolean webauth_app_info_is_same_user(const WebAuthAppInfo* self);

/** How honestly the backend may present this caller to the user. Passed to the
 *  backend as the "app_id_kind" option, because the backend renders the chrome
 *  and must be able to say "an unidentified application" rather than showing an
 *  empty string where a name belongs. The backend must never re-derive it. */
const char* webauth_app_info_kind_symbol(const WebAuthAppInfo* self);

void webauth_app_info_free(WebAuthAppInfo* self);

#endif /* WEBAUTH_PORTAL_APP_INFO_H */
