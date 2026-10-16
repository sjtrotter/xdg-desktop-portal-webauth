# Tests

`meson test -C build` runs the `unit` suite's tests: `completion`, `options`, `storage`,
`redact` and `harden`, as registered in `tests/meson.build`. They need no display or bus.
[Testing](../docs/TESTING.md) covers setup, sanitizers, end-to-end commands and dated evidence.

The frontend's tests live on xdg-desktop-portal's `experimental/integration` branch:
`tests/templates/webauthentication.py` provides a python-dbusmock backend and
`tests/test_webauthentication.py` exercises host and Flatpak identities. From that checkout:

```console
$ cd /path/to/xdg-desktop-portal/tests
$ BUILDDIR=../build ./run-test.sh ./test_webauthentication.py
```

The client tests belong to its repository
([ADR 0006](../docs/decisions/0006-two-repositories.md)).

## Completion fixtures

`test-completion.c` includes frontend fixtures marked `FRONTEND`. Test the same rule against
live navigation interception here and returned-URI verification in the frontend
([impl contract](../docs/IMPL-INTERFACE.md)). Port additional backend cases to the frontend;
any disagreement blocks release. [Upstreaming](../docs/UPSTREAMING.md) describes coordination.

Required cases include exact matches, added query/fragment, case and default ports, IDNA hosts,
private-use schemes with and without authority, and exact paths. Near misses include
`/callback.evil`, `/callbacker`, `/callback/sub`, host mismatches, userinfo, changed ports,
empty versus slash paths and trailing-dot hosts. Expand malformed-percent, encoded NUL,
control-character, backslash and cross-parser coverage. Navigation-policy tests must verify
interception in every frame; subframe candidates participate in matching.

## Boundary and race coverage

The frontend suite should cover dropped unknown keys, invalid values/types, clamped timeouts,
title limits, handle-token validation, URI rejection before backend calls, derived app identity,
missing export without backend or opt-in, mismatched and malformed backend results, backend
death, timeout and forwarded Close. Backend integration needs direct-sender refusal and window
teardown on frontend loss. Treat these as requirements; the local unit suite does not cover
all D-Bus paths.

Race tests should assert one terminal result across completion/Close, close/timeout, repeated
completion, caller loss, backend loss and frontend-timeout/late-reply orderings. Public Close
suppresses a later Response. A committed backend result remains terminal.

## Redaction and hardening

Exercise each typed logging field against the [rendering rules](../docs/SECURITY.md#logging).
Assert that secret fields render exactly `<secret>` without length.
A broader regression should pass realistic completion URIs and codes through logging entry
points, capture output and assert that credentials are absent. Error-text tests should verify
truncation before embedded URIs in GnuTLS and WebKit failures. Hardening tests cover balanced,
nested identifiable intervals without changing the test process's dumpability.

## Hardware and tenant checks

Certificate-adapter behavior needs real modules, PIN prompts, card removal, concurrency and
version-matrix checks. A stub can verify ownership and release rules without establishing real
card compatibility. [S2](../docs/SPIKES.md) records the mechanism and remaining matrix.
S1 covers refresh-to-PoP against a real tenant; S3 covers keyring availability.
