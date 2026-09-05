# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# The tests that need neither a bus nor a network. What they cover is the rules:
# PKCE and the state comparison, the callback classifier, the ID token claim the
# account name comes from, discovery parsing, the cache key, and the logging that
# must never emit a credential. The sign-in itself is tools/entra-e2e.sh, which is
# not a meson test because it needs Xvfb, a frontend build and a mock authority.
entra_unit_tests = {
  'pkce': 'test-pkce.c',
  'callback': 'test-callback.c',
  'jwt': 'test-jwt.c',
  'discovery': 'test-discovery.c',
  'cache': 'test-cache.c',
  'redact': 'test-redact.c',
}

foreach name, source : entra_unit_tests
  test(
    name,
    executable(
      'entra-test-' + name,
      source,
      c_args: entra_c_args,
      dependencies: [entra_core_dep],
      install: false,
    ),
    suite: 'unit',
    protocol: 'tap',
    args: ['--tap'],
  )
endforeach
