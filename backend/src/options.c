/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 *
 * xdg-desktop-portal-webauth
 */

#include "options.h"

guint webauth_options_timeout(GVariant* options, guint fallback, guint ceiling)
{
	guint32 timeout = 0;

	if (options == NULL || !g_variant_lookup(options, "timeout", "u", &timeout))
		return MIN(fallback, ceiling);

	if (timeout == 0)
		return MIN(fallback, ceiling);

	return MIN((guint) timeout, ceiling);
}

const char* webauth_options_string(GVariant* options, const char* key)
{
	const char* value = NULL;

	if (options == NULL || !g_variant_lookup(options, key, "&s", &value))
		return NULL;

	return value;
}
