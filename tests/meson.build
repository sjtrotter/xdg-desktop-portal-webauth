# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# Unit tests need neither a display nor a session bus; UI tests use tools/ui-smoke.sh; see docs/TESTING.md.
webauth_unit_tests = {
  'completion': 'test-completion.c',
  'options': 'test-options.c',
  'storage': 'test-storage.c',
  'redact': 'test-redact.c',
  'harden': 'test-harden.c',
}

foreach name, source : webauth_unit_tests
  test(
    name,
    executable(
      'webauth-test-' + name,
      source,
      dependencies: [webauth_core_dep],
      include_directories: config_inc,
      install: false,
    ),
    suite: 'unit',
    protocol: 'tap',
    args: ['--tap'],
  )
endforeach
