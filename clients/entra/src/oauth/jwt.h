/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_JWT_H
#define ENTRA_OAUTH_JWT_H

#include <glib.h>

/** @file
 *  Reading the account name out of an ID token, and the one thing this must not
 *  be mistaken for.
 *
 *  THE SIGNATURE IS NOT CHECKED AND NOTHING IS AUTHORIZED BY WHAT IS READ HERE.
 *  The ID token arrives over TLS from the token endpoint this client itself chose,
 *  in the response to a request it itself made with a PKCE verifier no one else
 *  has; that transport is what makes it trustworthy, not a local signature check.
 *  The name is used for ONE purpose: to label the account in the keyring and on
 *  screen so a human can tell two sign-ins apart. It is never a permission.
 *
 *  A decoded claim is displayed, so it is checked for being valid UTF-8 and for
 *  carrying no control characters: a display name is a place to hide a terminal
 *  escape sequence.
 */

/** The payload segment of @jwt, decoded. NULL if it is not three base64url
 *  segments or the middle one is not a JSON object. */
char* entra_jwt_payload(const char* jwt);

/** A string claim from the payload, or NULL. */
char* entra_jwt_claim(const char* jwt, const char* claim);

/** The account name to display: preferred_username, upn, unique_name, email, sub,
 *  in that order. NULL if none of them is usable. */
char* entra_jwt_account_name(const char* id_token);

#endif /* ENTRA_OAUTH_JWT_H */
