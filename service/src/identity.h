/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_IDENTITY_H
#define WEBAUTH_IDENTITY_H

#include <gio/gio.h>

/** @file
 *  Who is asking, and how much of that answer can be believed.
 *
 *  An executable path is not an application identity: a same-UID process can execute
 *  another path, manipulate its launch context, or connect straight to the bus. This
 *  header exists so that the difference between an authenticated identity and a
 *  plausible label is a type, not a comment.
 *
 *  Flatpak and Snap identities count as authenticated only when they come from the
 *  containment framework's mediation. A cgroup-derived desktop identity is a useful
 *  label and not a security principal. A caller-supplied app id is only a claim. The
 *  D-Bus unique name and the UID identify the connection and the user reliably, and
 *  identify the application publisher not at all.
 *
 *  Consequences enforced elsewhere: results are bound to the initiating unique
 *  connection; an unverified label is never the sole key for a storage partition; an
 *  unidentified host caller gets shared or ephemeral mode, never a partition it
 *  named; and first use by an unidentified caller may warrant a confirmation before
 *  client-certificate access.
 *
 *  Sketch only; nothing here is implemented. See docs/SECURITY.md.
 */

typedef enum
{
	WEBAUTH_IDENTITY_SANDBOXED,  /**< from Flatpak/Snap mediation: authenticated */
	WEBAUTH_IDENTITY_CGROUP,     /**< derived from a systemd/cgroup unit: a label */
	WEBAUTH_IDENTITY_UNVERIFIED  /**< an unsandboxed peer we can name but not vouch for */
} WebAuthIdentityKind;

typedef struct
{
	WebAuthIdentityKind kind;
	char* app_id;      /**< may be NULL; never used alone as a partition key */
	char* unique_name; /**< the D-Bus unique name; always present; binds the result */
	uid_t uid;
} WebAuthIdentity;

/** Resolve what can be resolved about @sender. Never fails open onto another
 *  caller's identity: an unresolvable caller is WEBAUTH_IDENTITY_UNVERIFIED. */
WebAuthIdentity* webauth_identity_resolve(GDBusConnection* connection, const char* sender,
                                          GError** error);

/** The text shown in the security chrome, e.g. "Remmina" or "an unidentified
 *  application". Derived from the resolution, never from anything the caller sent. */
const char* webauth_identity_display_name(const WebAuthIdentity* self);

/** Whether this identity may be used as a storage partition key at all. */
gboolean webauth_identity_is_partitionable(const WebAuthIdentity* self);

void webauth_identity_free(WebAuthIdentity* self);

#endif /* WEBAUTH_IDENTITY_H */
