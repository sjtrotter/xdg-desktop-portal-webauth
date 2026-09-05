/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_STORAGE_H
#define WEBAUTH_STORAGE_H

#include <glib.h>

/** @file
 *  Which website data store a transaction runs in, and what "a store" covers.
 *
 *  A cookie jar is not the boundary; the boundary is every piece of state the
 *  engine can keep. Cookies, local and session storage, IndexedDB, HTTP and disk
 *  caches, service workers, HSTS and related network state, HTTP authentication
 *  credentials, permission decisions and client-certificate selection memory all
 *  belong to the partition or the isolation is decorative. Downloads are
 *  disabled rather than partitioned.
 *
 *  Version 1 has two modes and no third. "shared" is one store per requesting
 *  application - and it must be described that way to users, because it is NOT
 *  the user's Firefox or Chrome profile and does not inherit their sessions,
 *  policies, extensions or device registration. "ephemeral" uses a
 *  WebKitNetworkSession created with the transaction and destroyed with it:
 *  using the persistent store and clearing it afterwards is not equivalent and
 *  is race-prone.
 *
 *  WHO DECIDES, AND WHO OBEYS. The mode arrives in the impl call's options as a
 *  DECISION the frontend has already made: it parsed the caller's request,
 *  rejected an unknown value rather than falling back, and applied any policy
 *  that forces ephemeral. This backend never re-reads the application's
 *  preference.
 *
 *  THE ONE NARROWING THIS BACKEND MAKES, and it only ever narrows: an
 *  UNIDENTIFIED caller - an empty app_id, which the frontend uses to say it
 *  could not establish one - gets an ephemeral store whatever the mode says.
 *  A shared store keyed on nothing would be a store shared between every
 *  unidentified caller on the machine, which is the amplification
 *  docs/decisions/0005-service-shape.md lists as a reason this portal might be a
 *  bad idea. Refusing to persist is the safe direction and it is the direction
 *  the impl XML permits: a backend must fail rather than quietly widen a store,
 *  and this narrows one.
 *
 *  The other thing this backend may still refuse: a mode it cannot honour. If an
 *  ephemeral session cannot be created, the transaction fails. It does not
 *  quietly use the shared store.
 */

typedef enum
{
	WEBAUTH_SESSION_SHARED,   /**< a persistent store, one per requesting application */
	WEBAUTH_SESSION_EPHEMERAL /**< created with the transaction, destroyed with it */
} WebAuthSessionMode;

/** Parse the "session_mode" option as the frontend forwarded it. An unknown
 *  value is an error here too: the frontend should already have rejected it, and
 *  a backend that accepted what its frontend rejected is a hole. An absent
 *  option is "shared", which is the public interface's documented default. */
gboolean webauth_session_mode_parse(const char* value, WebAuthSessionMode* out, GError** error);

const char* webauth_session_mode_to_string(WebAuthSessionMode mode);

/** The mode that will actually be used: @requested, narrowed to ephemeral when
 *  @app_id is empty. */
WebAuthSessionMode webauth_storage_effective_mode(WebAuthSessionMode requested,
                                                  const char* app_id);

/** One path component for @app_id: the characters a reverse-DNS application id
 *  is allowed to contain, and nothing else. Returns NULL when @app_id is empty
 *  or sanitises to nothing, which is the unidentified case and never reaches a
 *  persistent path. */
char* webauth_storage_directory_name(const char* app_id);

/** $XDG_DATA_HOME/xdg-desktop-portal-webauth/<name>/data and .../cache, created
 *  0700. Returns FALSE and sets @error when they cannot be created, which fails
 *  the transaction rather than falling back to a shared or ephemeral store. */
gboolean webauth_storage_paths(const char* app_id, char** data_directory,
                               char** cache_directory, GError** error);

#endif /* WEBAUTH_STORAGE_H */
