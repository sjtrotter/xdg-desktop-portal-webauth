/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef WEBAUTH_HARDEN_H
#define WEBAUTH_HARDEN_H

#include <glib.h>

/** Hardening covers this process; execve resets dumpability in WebKit children. See docs/SECURITY.md. */

/** @allow_core disables hardening for debugging; only the command line can enable it. */
void webauth_harden(gboolean allow_core);

/** Nested holders temporarily enable /proc caller identification for portal calls; see docs/SECURITY.md. */
void webauth_harden_identifiable_begin(void);
void webauth_harden_identifiable_end(void);

gboolean webauth_harden_is_identifiable(void);

#endif /* WEBAUTH_HARDEN_H */
