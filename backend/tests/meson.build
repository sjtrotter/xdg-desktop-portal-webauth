# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# The tests that need neither a display nor a session bus. What they cover is
# the rules: the completion match, the options, the storage partition's name and
# the logging that must never emit a credential. Everything between a D-Bus call
# and a Return key is tools/ui-smoke.sh, which is not a meson test because it
# needs Xvfb, a frontend build and a SoftHSM fixture.
webauth_unit_tests = {
  'completion': 'test-completion.c',
  'options': 'test-options.c',
  'storage': 'test-storage.c',
  'redact': 'test-redact.c',
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
