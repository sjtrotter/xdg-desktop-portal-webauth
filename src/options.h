/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_OPTIONS_H
#define WEBAUTH_OPTIONS_H

#include <glib.h>


/** Zero, absent or mistyped timeouts use @fallback; positive values are clamped to @ceiling. */
guint webauth_options_timeout(GVariant* options, guint fallback, guint ceiling);

/** The returned string is borrowed from @options. */
const char* webauth_options_string(GVariant* options, const char* key);

#endif /* WEBAUTH_OPTIONS_H */
