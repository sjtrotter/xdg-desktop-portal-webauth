/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_COMPLETION_H
#define WEBAUTH_COMPLETION_H

#include <glib.h>

/** @file
 *  Deciding whether a navigation is the completion the application is waiting for.
 *
 *  This is the security critical routine of the whole design: it decides which
 *  URI, out of everything a sign-in flow navigates to, is handed back. Matching
 *  is EXACT and is performed on parsed URIs, never on strings, and there is
 *  deliberately no prefix mode in version 1. Scheme compared case-insensitively;
 *  host compared case-insensitively, and a URI with a host never matches one
 *  without; port normalised so that an explicit 443 and a default 443 are the
 *  same port; path compared exactly; userinfo forbidden; query and fragment
 *  carry the result and take no part in matching.
 *
 *  So https://example.com/callback matches neither /callback.evil nor
 *  /callbacker nor https://example.com@evil.invalid/ - each of which a string
 *  prefix would accept.
 *
 *  ONE RULE, TWO ENFORCEMENT POINTS - and they must agree.
 *
 *  It is enforced HERE because only this process sees a navigation: the web view
 *  tests every navigation it is asked to make, in ANY frame, and the transaction
 *  completes BEFORE the matched navigation loads. It is enforced AGAIN in the frontend --
 *  xdg-desktop-portal, desktop-portal/web-authentication.c,
 *  completion_uri_matches(), on the branch
 *  experimental/integration -- which re-parses the
 *  "completion_uri" this backend returns and refuses to hand the application a
 *  URI that is not the one it asked for: on a mismatch the response becomes 2
 *  with reason "backend_completion_mismatch" and the URI is discarded. Neither
 *  check makes the other redundant: this one decides when to stop the browser,
 *  that one decides what the application is told.
 *
 *  This file is that implementation, translated one function at a time, and
 *  tests/test-completion.c runs the frontend's own fixture table against it.
 *
 *  Note a third check that is NOT redundant with either and lives in neither:
 *  the application's own validation of the URI as a protocol response - the
 *  OAuth "state", the single "code" - which uses a secret no part of this
 *  design ever sees.
 */

/** The longest URI either side will parse, from the frontend's
 *  WEB_AUTHENTICATION_MAX_URI_LENGTH. */
#define WEBAUTH_MAX_URI_LENGTH 4096

/** Whether @uri may be opened: absolute, https, a host, no userinfo, well
 *  formed. The frontend has checked this already; this backend checks it again,
 *  because its safety must not depend on a frontend having been correct. */
gboolean webauth_completion_start_uri_is_valid(const char* uri, GError** error);

/** Whether @uri is usable as a completion URI: absolute; a host; no userinfo;
 *  well formed. A custom scheme is matched structurally and is never dispatched
 *  to another application - naming a scheme does not prove owning it, and the
 *  navigation is intercepted before any attempt at external protocol handling. */
gboolean webauth_completion_uri_is_valid(const char* uri, GError** error);

/** Whether @candidate, a navigation in any frame, completes a transaction
 *  expecting @completion_uri. WebKitGTK 6 exposes no frame identity on a policy
 *  decision, so every navigation reaches this function and a match in a subframe
 *  ends the flow too -- which is what the public XML promises. */
gboolean webauth_completion_matches(const char* candidate, const char* completion_uri);

#endif /* WEBAUTH_COMPLETION_H */
