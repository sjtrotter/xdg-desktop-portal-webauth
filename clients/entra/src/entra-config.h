/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_CONFIG_H
#define ENTRA_CONFIG_H

#include <glib.h>

/** @file
 *  The user's configuration file: the only way to widen an allowlist.
 *
 *  docs/SECURITY.md, "Access control": a caller may not extend the authority or
 *  client-id allowlists. A *user* may, by naming each addition individually in a
 *  file — never a wildcard, never an environment variable, never a command line
 *  flag a hostile parent could set. --config and ENTRA_TOKEN_HELPER_CONFIG only
 *  choose WHICH file is read; nothing in the environment can add an entry.
 *
 *  The file is a GKeyFile:
 *
 *      [allow]
 *      authorities = localhost:8443;
 *      client_ids  = 00000000-0000-0000-0000-000000000000;
 *
 *      [testing]
 *      trust_certificate = /path/to/fixture.pem
 *
 *  The [testing] group exists for the fixture identity provider in
 *  tools/entra-e2e.sh and is the client's ONLY trust override, mirroring the
 *  backend's single --debug-trust-certificate option. It is a file, not a flag,
 *  for the reason above.
 */

typedef struct _EntraConfig EntraConfig;

/** Load @path, or $ENTRA_TOKEN_HELPER_CONFIG, or the default under
 *  $XDG_CONFIG_HOME. A missing file is not an error: it yields an empty
 *  configuration. A malformed one is. */
EntraConfig* entra_config_load(const char* path, GError** error);

gboolean entra_config_allows_authority(EntraConfig* self, const char* host);
gboolean entra_config_allows_client_id(EntraConfig* self, const char* client_id);

/** A PEM file to trust in addition to the system store, or NULL. */
const char* entra_config_trust_certificate(EntraConfig* self);

void entra_config_free(EntraConfig* self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EntraConfig, entra_config_free)

#endif /* ENTRA_CONFIG_H */
