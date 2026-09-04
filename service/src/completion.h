/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEBAUTH_COMPLETION_H
#define WEBAUTH_COMPLETION_H

#include <glib.h>

/** @file
 *  Deciding whether a navigation is the completion the caller is waiting for.
 *
 *  This is the security critical routine of the service: it decides which URI, out of
 *  everything a sign-in flow navigates to, is handed to the caller. Matching is
 *  EXACT and is performed on parsed URIs, never on strings, and there is deliberately
 *  no prefix mode in version 1. Scheme compared case-insensitively; host compared
 *  case-insensitively after IDNA normalisation; port normalised so that an explicit
 *  443 and a default 443 are the same port; path compared exactly; userinfo forbidden;
 *  query and fragment carry the result and take no part in matching.
 *
 *  So https://example.com/callback matches neither /callback.evil nor /callbacker nor
 *  https://example.com@evil.invalid/ — each of which a string prefix would accept.
 *  Control characters, malformed percent escapes, %00, backslashes in
 *  authority-sensitive positions, and URIs that parse differently between libraries
 *  are all rejected outright rather than normalised.
 *
 *  Sketch only; nothing here is implemented. See docs/SERVICE-INTERFACE.md.
 */

/** Whether @uri may be opened: absolute, https, a host, no userinfo, well formed. */
gboolean webauth_completion_start_uri_is_valid(const char* uri, GError** error);

/** Whether @uri is usable as a completion URI: absolute; https or an exactly named
 *  custom scheme; a host where the scheme has one; no userinfo; no wildcard. A custom
 *  scheme is matched structurally and is never dispatched to another application —
 *  naming a scheme does not prove owning it. */
gboolean webauth_completion_uri_is_valid(const char* uri, GError** error);

/** Whether @candidate completes a transaction expecting @completion_uri. */
gboolean webauth_completion_matches(const char* candidate, const char* completion_uri);

#endif /* WEBAUTH_COMPLETION_H */
