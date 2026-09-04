/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_STORAGE_H
#define WEBAUTH_STORAGE_H

#include <glib.h>

/** @file
 *  Which website data store a transaction runs in, and what "a store" covers.
 *
 *  A cookie jar is not the boundary; the boundary is every piece of state the
 *  engine can keep. Cookies, local and session storage, IndexedDB, HTTP and disk
 *  caches, service workers, HSTS and related network state, HTTP authentication
 *  credentials, permission decisions, and client-certificate selection memory
 *  all belong to the partition or the isolation is decorative. Downloads and
 *  autofill are disabled rather than partitioned.
 *
 *  Version 1 has two modes and no third. "shared" is one store used by all of
 *  this backend's transactions - and it must be described that way to users,
 *  because it is NOT the user's Firefox or Chrome profile and does not inherit
 *  their sessions, policies, extensions or device registration. "ephemeral" uses
 *  an ephemeral WebsiteDataManager created with the transaction and destroyed
 *  with it: using the persistent store and clearing it afterwards is not
 *  equivalent and is race-prone.
 *
 *  WHO DECIDES, AND WHO OBEYS. The mode arrives in the impl call's options as a
 *  DECISION the frontend has already made: it parsed the caller's request,
 *  rejected an unknown value rather than falling back, and applied any policy
 *  that forces ephemeral. This backend never re-reads the application's
 *  preference and never derives a partition from an app id, because it has no
 *  way to judge how much that app id can be believed - that judgement lives in
 *  the frontend (xdp_invocation_get_app_info(), upstream) and does not survive a D-Bus hop as
 *  anything but a label. A backend that derived its own partition would be a
 *  second place where the isolation rule lived, and the two would disagree.
 *
 *  The one thing this backend may still refuse: a mode it cannot honour. If an
 *  ephemeral data manager cannot be created, the transaction fails. It does not
 *  quietly use the shared store.
 *
 *  There is deliberately no per-application persistent mode until caller
 *  identity is credible enough to key one. Note that the split makes that
 *  MORE plausible than it was, not less - a sandboxed app id derived by the
 *  frontend is authenticated metadata - but "more plausible" is not "decided",
 *  and it stays out of version 1. See docs/decisions/0005-service-shape.md.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef enum
{
	WEBAUTH_SESSION_SHARED,   /**< the default: shared among this backend's transactions */
	WEBAUTH_SESSION_EPHEMERAL /**< created with the transaction, destroyed with it */
} WebAuthSessionMode;

/** Parse the "session_mode" option as the frontend forwarded it. An unknown
 *  value is an error here too: the frontend should already have rejected it, and
 *  a backend that accepted what its frontend rejected is a hole. */
gboolean webauth_session_mode_parse(const char* value, WebAuthSessionMode* out, GError** error);

/** The opaque partition identifier for a transaction in @mode. Never named by
 *  the application, never derived from an app id. */
char* webauth_storage_partition_id(WebAuthSessionMode mode);

/** Destroy an ephemeral partition and everything in it. */
void webauth_storage_discard(const char* partition_id);

#endif /* WEBAUTH_STORAGE_H */
