/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_STORAGE_H
#define WEBAUTH_STORAGE_H

#include <glib.h>

/** Storage isolation and engine limits are described in docs/SECURITY.md. */

typedef enum
{
	WEBAUTH_SESSION_SHARED,   /**< Persistent store per requesting application. */
	WEBAUTH_SESSION_EPHEMERAL /**< Session created and destroyed with the transaction. */
} WebAuthSessionMode;

/** An absent mode defaults to shared; unknown values are rejected. */
gboolean webauth_session_mode_parse(const char* value, WebAuthSessionMode* out, GError** error);

const char* webauth_session_mode_to_string(WebAuthSessionMode mode);

/** Unidentified callers always use ephemeral storage. */
WebAuthSessionMode webauth_storage_effective_mode(WebAuthSessionMode requested,
                                                  const char* app_id);

/** Returns a newly allocated path component, or NULL for an empty or unusable application id. */
char* webauth_storage_directory_name(const char* app_id);

/** Creates per-application data and cache directories with mode 0700; failure aborts the transaction. */
gboolean webauth_storage_paths(const char* app_id, char** data_directory,
                               char** cache_directory, GError** error);

#endif /* WEBAUTH_STORAGE_H */
