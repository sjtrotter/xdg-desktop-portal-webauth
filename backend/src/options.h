/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_OPTIONS_H
#define WEBAUTH_OPTIONS_H

#include <glib.h>

/** @file
 *  Reading the options vardict the frontend forwarded.
 *
 *  Everything in it has already been validated and, where policy applies,
 *  already narrowed: unknown keys are dropped, handle_token is not forwarded,
 *  session_mode is a decision and timeout is clamped. This backend reads it
 *  again anyway, because its behaviour must not depend on a frontend having been
 *  correct - and because the ceiling is a property of what this process is
 *  willing to keep a window open for, not of what it was told.
 */

/** The deadline in seconds: the forwarded "timeout", clamped to this backend's
 *  own ceiling, or the default when it is absent or of the wrong type. Zero is
 *  not a timeout and is treated as absent. */
guint webauth_options_timeout(GVariant* options, guint fallback, guint ceiling);

/** A string option, or NULL. The value is borrowed from @options. */
const char* webauth_options_string(GVariant* options, const char* key);

#endif /* WEBAUTH_OPTIONS_H */
