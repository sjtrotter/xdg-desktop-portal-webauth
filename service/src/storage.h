/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_STORAGE_H
#define WEBAUTH_STORAGE_H

#include <glib.h>

#include "identity.h"

/** @file
 *  Which website data store a transaction runs in, and what "a store" covers.
 *
 *  A cookie jar is not the boundary; the boundary is every piece of state the engine
 *  can keep. Cookies, local and session storage, IndexedDB, HTTP and disk caches,
 *  service workers, HSTS and related network state, HTTP authentication credentials,
 *  permission decisions, and client-certificate selection memory all belong to the
 *  partition or the isolation is decorative. Downloads and autofill are disabled
 *  rather than partitioned.
 *
 *  Version 1 has two modes and no third. "shared" is one store used by all of this
 *  service's transactions — and it must be described that way to users, because it is
 *  NOT the user's Firefox or Chrome profile and does not inherit their sessions,
 *  policies, extensions or device registration. "ephemeral" uses an ephemeral
 *  WebsiteDataManager created with the transaction and destroyed with it: using the
 *  persistent store and clearing it afterwards is not equivalent and is race-prone.
 *
 *  There is deliberately no per-application persistent mode until caller identity is
 *  credible enough to key one; a partition named by an unverified caller is worse
 *  than no partition at all. See identity.h and docs/decisions/0005-service-shape.md.
 *
 *  Sketch only; nothing here is implemented.
 */

typedef enum
{
	WEBAUTH_SESSION_SHARED,   /**< the default: shared among this service's transactions */
	WEBAUTH_SESSION_EPHEMERAL /**< created with the transaction, destroyed with it */
} WebAuthSessionMode;

/** Parse the "session_mode" option. An unknown value is an error, never a fallback. */
gboolean webauth_session_mode_parse(const char* value, WebAuthSessionMode* out, GError** error);

/** Decide the mode for this transaction: the caller's request, narrowed by policy and
 *  by what @caller's identity can be trusted with. Policy may force ephemeral; a
 *  caller may never defeat that. */
WebAuthSessionMode webauth_storage_mode_for(const WebAuthIdentity* caller,
                                            WebAuthSessionMode requested);

/** The opaque partition identifier a transaction is given. Never derived by the
 *  browser session itself, and never named by the caller. */
char* webauth_storage_partition_id(const WebAuthIdentity* caller, WebAuthSessionMode mode);

/** Destroy an ephemeral partition and everything in it. */
void webauth_storage_discard(const char* partition_id);

#endif /* WEBAUTH_STORAGE_H */
