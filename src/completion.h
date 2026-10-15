/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_COMPLETION_H
#define WEBAUTH_COMPLETION_H

#include <glib.h>

/** Completion validation also runs in the frontend; applications must validate the protocol response. See docs/IMPL-INTERFACE.md. */

/** Matches the frontend's WEB_AUTHENTICATION_MAX_URI_LENGTH. */
#define WEBAUTH_MAX_URI_LENGTH 4096

/** Validate an absolute HTTPS URI with a host and no userinfo independently of the frontend. */
gboolean webauth_completion_start_uri_is_valid(const char* uri, GError** error);

/** Validate an absolute completion URI with no userinfo and a host or path. */
gboolean webauth_completion_uri_is_valid(const char* uri, GError** error);

/** WebKitGTK 6 exposes no frame identity on policy decisions; a match in any frame completes the flow. */
gboolean webauth_completion_matches(const char* candidate, const char* completion_uri);

#endif /* WEBAUTH_COMPLETION_H */
