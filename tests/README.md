# Tests

**The backend's tests are `backend/tests/`, run by `meson test -C build-backend`, and the
end-to-end runs are `tools/ui-smoke.sh`; what they cover and what they proved is
[../docs/TESTING.md](../docs/TESTING.md).** This directory holds no tests: it holds the strategy
they were written from, kept because the strategy outlived the sketch and the Entra client has
still not started.

The fixture tables below are the ones `backend/tests/test-completion.c` now carries, with the
frontend's own cases marked `FRONTEND` in that file.

**Half of it exists already, in the frontend's repository.** The xdg-desktop-portal branch
`experimental/certificate-webauthentication` ships `tests/templates/webauthentication.py` (a
python-dbusmock backend) and `tests/test_webauthentication.py` (38 passing cases, each run once as
`AppInfoHost` and once as `AppInfoFlatpak`). Everything below that is a frontend obligation is that
suite; what is left for this repository is the backend's half.

## Principle: the interesting parts are testable offline

Almost everything security-critical in both components is a pure function over strings: does this
URI match that one, is this a valid authorization response, may this field be logged, does this JSON
parse into that request. None of it needs a network, a card, a browser or a tenant. That is
deliberate — the parts that *do* need those are the parts a test cannot cover, so the boundary
between them should be sharp and the pure side should be large.

Anything requiring a real tenant, a real card or a real browser is a **spike**
([docs/SPIKES.md](../docs/SPIKES.md)) or an end-to-end test run by hand, not a unit test.

## Fixtures

### Completion matching (`backend/src/completion.h` **and the frontend's re-check**)

The single most valuable fixture set in the project: a table of
`(completion_uri, candidate, expected)` triples, each with a comment saying what it is testing.

**This table must be run against both implementations.** One rule is enforced in two places — the
backend, against live navigations, and the frontend, against the URI a backend returns
([../docs/IMPL-INTERFACE.md](../docs/IMPL-INTERFACE.md)) — and two implementations of one rule can
drift. **Both are now written and tested.** This repository's is `backend/src/completion.c`, covered by
`backend/tests/test-completion.c`; the frontend's is in another repository:
`desktop-portal/web-authentication.c:completion_uri_matches()` on the xdg-desktop-portal branch,
covered by `test_completion_mismatch_rejected` and `test_completion_normalisation_accepted`. The
fixtures have been ported one way, from the frontend to here; porting the additions back is
outstanding. Shared fixtures are the only thing standing between "they agree" and "they agreed when they
were written". A fixture that passes in one and fails in the other is a release blocker, not a
discrepancy to reconcile later.

Must include, at least: the exact match; the same URI with query and fragment added (matches, since
neither participates); explicit `:443` versus the default port; upper-case scheme and host; an
IDNA-normalised host; `/callback` against `/callback.evil`, `/callbacker` and `/callback/sub` (all
non-matches, since matching is exact and there is no prefix mode); userinfo in the candidate
(`https://good.example@evil.invalid/`); a subframe navigation (not offered to the matcher at all);
control characters; malformed percent escapes; `%00` in each component; a backslash in an
authority-sensitive position; and any URI known to parse differently between two libraries.

Every non-match needs its own line. A matcher that is *right* on the happy path and wrong on one
edge is worse than one that is obviously wrong, because it will be trusted.

### The impl boundary (upstream's half — `desktop-portal/web-authentication.c`)

Cheap, offline, and **largely written already**: the branch's `tests/test_webauthentication.py` has
38 passing cases against a python-dbusmock backend, covering the happy path, cancellation from both
directions, four invalid-option cases and six invalid-URI cases. What follows is the list this
repository wrote in advance; it is kept as the checklist to read that suite against. With a stub
backend that returns canned replies, assert that:

- an unknown option key is **dropped** and never reaches the backend, and an unknown *value* for a
  known key is rejected before the backend is called at all;
- `timeout` is clamped, `title` is length-limited, and `handle_token` is rejected when it is not a
  valid object path element;
- a malformed `start_uri` or `completion_uri` produces a D-Bus **error** and no impl call;
- `app_id` reaching the backend is the derived one, and a caller cannot influence it — there is no
  argument for it, so the test is that no option or argument path can set it;
- a backend returning a *different* `completion_uri` produces `2` /
  `backend_completion_mismatch` and the URI never reaches the application;
- a backend that vanishes mid-call produces exactly one `Response(2, backend_disappeared)`;
- a backend returning a malformed vardict produces `2` / `backend_protocol_error`;
- with no backend configured, the interface is **not exported**;
- a `Close()` from the application reaches the backend's impl Request.

And on the backend side, with a stub frontend: that a `Start` from any sender other than the
frontend is refused, and that dropping the frontend's connection destroys the window.

### Redaction (`backend/src/redact.h`, `clients/entra/src/log/redact.h`)

For each field kind, assert what `webauth_redact_field()` / `entra_redact_field()` renders. The
loggable kinds render their value; the non-loggable kinds render kind and length and **never** any
part of the value.

The test that matters most is the inverse one: take a realistic completion URI, a token, an
authorization code and a server `error_description`, push them through every logging entry point at
every level, capture the output, and assert that no substring of any of them appears anywhere. That
is a regression test against the failure mode this project actually fears — not a check that one
function works.

Also: `webauth_redact_error_text()` cuts before an embedded URI, on error strings taken from real
GnuTLS and WebKit failures.

### Request and response schema (`clients/entra/src/ipc/request.h`)

Round-trip every field. Then the rejections: an unknown `schema`; a request carrying a token
endpoint, an authorization endpoint or a redirect URI (forbidden in any transport, so a usage error
rather than a configuration); `"token_kind": "pop"` without `req_cnf`; a scope list that is not an
array; unknown keys (ignored, per the compatibility promise).

Assert the exit-code mapping for every `EntraStatus`, against the table in
[docs/ENTRA-CLIENT-CLI.md](../docs/ENTRA-CLIENT-CLI.md) — the code and the document disagreeing is
exactly the kind of drift a test should catch.

### OAuth response classification (`clients/entra/src/oauth/callback.h`)

A transaction with a known `state` and redirect, and a table of candidate URIs: valid `code`; valid
`error`; both present; neither; `code` twice; `code` present with no value (still an occurrence, so
a second one is a duplicate); wrong `state`; missing `state`; `state` twice; fragment present;
userinfo present; a different host; malformed escapes; `%00`. Expected outcomes are `CODE`, `ERROR`,
`UNRELATED` or `INVALID`, and the second call on a consumed transaction is always `INVALID`.

Constant-time comparison is asserted structurally — that the comparison function is the one used —
rather than by timing, which is not reproducible in CI.

### Cache keys (`clients/entra/src/cache/keyring.h`)

Two requests differing in exactly one field must produce different keys. One field per test, and one
test per field, including scope *order* (which must **not** matter, since the key uses the sorted
set) and the PoP binding (which must, since a token bound to one `kid` is useless for another). A
missing field here means a token returned to the wrong requester.

### Transaction races (`backend/src/transaction.h`, and upstream's `xdp-request-dex.c`)

Not a fixture set but a deterministic-scheduler test: exactly one terminal result and exactly one
`Response`, under a committed completion racing a `Close()`, a timeout firing after a close, a
second matching navigation after a completion, and a caller disconnect mid-flight. Each ordering
asserted explicitly rather than by running it a thousand times and hoping.

Now with a second axis, because the transaction spans two processes: a backend reply arriving after
the frontend has already answered a timeout; a `Close()` in flight when the backend returns success;
a backend death in the same instant as a completion. The specification did not change — a committed
completion wins, every other late event is discarded — but the number of orderings did, and each new
one needs its own line.

## What cannot be tested this way

The certificate adapter, in either implementation. A brokered credential, a p11-kit module
registered after WebKit started, a PIN prompt, a card removed mid-handshake — all of that is
[S2](../docs/SPIKES.md), on real hardware, on a matrix of distributions. The adapter *interface* can
be tested with a stub implementation that returns a canned certificate; that verifies the lifetime
and release rules, which is worth doing, and verifies nothing at all about whether a real card
works.

Likewise the refresh-to-PoP question is [S1](../docs/SPIKES.md), against a real tenant.

## Running

There is nothing to run **here**. The frontend's half runs in another repository:
`cd xdg-desktop-portal/tests && BUILDDIR=../build ./run-test.sh ./test_webauthentication.py`, on the
branch. When there is something to run in this one, tests belong in each component's own meson
project — `backend/tests/` and `clients/entra/tests/` — and not in this directory, which will be
deleted at the repository split
([docs/decisions/0006-two-repositories.md](../docs/decisions/0006-two-repositories.md)).

The completion fixture table is the exception that proves the rule: it is one table consumed by two
projects, which is exactly the duplication upstream removes by putting the matcher in shared code
([docs/UPSTREAMING.md](../docs/UPSTREAMING.md)). Until then it is copied, and a CI check that the
two copies are byte-identical is worth more than either copy.
