/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_ERROR_H
#define ENTRA_ERROR_H

#include <glib.h>

/** @file
 *  One error domain for the whole client, whose codes are the exit conditions of
 *  docs/ENTRA-CLIENT-CLI.md. A failure is classified where it happens, by the
 *  component that knows what it means, and main() only has to translate.
 *
 *  A GError message reaching the user has already been redacted by whoever set
 *  it: no token, no code, no state, no authorization-server error_description.
 */

#define ENTRA_ERROR (entra_error_quark())

typedef enum
{
	ENTRA_ERROR_INTERACTION_REQUIRED, /**< exit 10 */
	ENTRA_ERROR_CANCELLED,            /**< exit 20 */
	ENTRA_ERROR_NO_ACCOUNT,           /**< exit 30 */
	ENTRA_ERROR_UNAVAILABLE,          /**< exit 40 */
	ENTRA_ERROR_SERVER,               /**< exit 50 */
	ENTRA_ERROR_USAGE,                /**< exit 64 */
	ENTRA_ERROR_INTERNAL              /**< exit 70 */
} EntraError;

GQuark entra_error_quark(void);

#endif /* ENTRA_ERROR_H */
