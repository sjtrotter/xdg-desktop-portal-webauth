/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 *  host compared case-insensitively after IDNA normalisation; port normalised so
 *  that an explicit 443 and a default 443 are the same port; path compared
 *  exactly; userinfo forbidden; query and fragment carry the result and take no
 *  part in matching.
 *
 *  So https://example.com/callback matches neither /callback.evil nor
 *  /callbacker nor https://example.com@evil.invalid/ - each of which a string
 *  prefix would accept. Control characters, malformed percent escapes, %00,
 *  backslashes in authority-sensitive positions, and URIs that parse differently
 *  between libraries are all rejected outright rather than normalised.
 *
 *  ONE RULE, TWO ENFORCEMENT POINTS - and they must agree.
 *
 *  It is enforced HERE because only this process sees a navigation: the web view
 *  tests every top level navigation and the transaction completes BEFORE the
 *  matched navigation loads. It is enforced AGAIN in the frontend -- which is
 *  now xdg-desktop-portal itself, in desktop-portal/web-authentication.c,
 *  completion_uri_matches(), on the branch
 *  experimental/certificate-webauthentication -- which re-parses the
 *  "completion_uri" this backend returns and refuses to hand the application a
 *  URI that is not the one it asked for: on a mismatch the response becomes 2
 *  with reason "backend_completion_mismatch" and the URI is discarded. Neither
 *  check makes the other redundant: this one decides when to stop the browser,
 *  that one decides what the application is told.
 *
 *  Read that implementation before changing this one. Its rule, exactly: scheme
 *  and host compared case insensitively, effective ports normalised so :443
 *  equals the default, paths compared exactly, no userinfo, query and fragment
 *  ignored.
 *
 *  The rule is defined by the branch's public XML; docs/PUBLIC-INTERFACE.md
 *  points at it. The two implementations of it can drift, and that is a real
 *  cost of the split: the shared fixture table in tests/README.md must be run
 *  against both. The frontend's half is at least tested already
 *  (tests/test_webauthentication.py: test_completion_mismatch_rejected and the
 *  negative control test_completion_normalisation_accepted); this half has no
 *  tests because it has no implementation.
 *
 *  Note a third check that is NOT redundant with either and lives in neither:
 *  the application's own validation of the URI as a protocol response - the
 *  OAuth "state", the single "code" - which uses a secret no part of this
 *  design ever sees.
 *
 *  Sketch only; nothing here is implemented. See docs/PUBLIC-INTERFACE.md.
 */

/** Whether @uri may be opened: absolute, https, a host, no userinfo, well
 *  formed. The frontend has checked this already; this backend checks it again,
 *  because its safety must not depend on a frontend having been correct. */
gboolean webauth_completion_start_uri_is_valid(const char* uri, GError** error);

/** Whether @uri is usable as a completion URI: absolute; https or an exactly
 *  named custom scheme; a host where the scheme has one; no userinfo; no
 *  wildcard. A custom scheme is matched structurally and is never dispatched to
 *  another application - naming a scheme does not prove owning it, and the
 *  navigation is intercepted before any attempt at external protocol handling. */
gboolean webauth_completion_uri_is_valid(const char* uri, GError** error);

/** Whether @candidate, a top level navigation, completes a transaction expecting
 *  @completion_uri. Subframe navigations are never offered to this function. */
gboolean webauth_completion_matches(const char* candidate, const char* completion_uri);

#endif /* WEBAUTH_COMPLETION_H */
