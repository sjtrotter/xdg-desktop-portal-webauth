# Entra client CLI contract

Status: design sketch. The `entra-token-helper` binary in this repository parses these options and
then exits `70`.

This is the contract for **layer 2**, the Entra ID / AVD token client — the interface FreeRDP
frontends and other programs are expected to depend on. Layer 1, the web authentication service the
client calls when it needs a window, has its own contract in
[SERVICE-INTERFACE.md](SERVICE-INTERFACE.md); the two are independent and versioned separately.

It is versioned: the JSON objects carry `"schema": 1`, and the compatibility promise at the end of
this document says what may change without a bump.

## Verbs

| Verb | Purpose | Interactive? |
|---|---|---|
| `login` | Establish an account: run an interactive sign-in through the service and store the resulting refresh token. Does not print an access token. | Always (unless a usable account already exists and `--prompt auto`) |
| `token` | Acquire an access token. Silent if possible, interactive if permitted. | Depends on `--prompt` and cache state |
| `accounts` | List stored accounts. | Never |
| `logout` | Forget one account or all accounts: remove refresh tokens and account records from the keyring. | Never |

`token` is the verb FreeRDP's callback shim calls. `login`, `accounts` and `logout` exist so a
human can manage state without a connection in flight.

## Options

| Option | Applies to | Meaning |
|---|---|---|
| `--authority <host>` | `login`, `token` | The Entra ID authority host, e.g. `login.microsoftonline.com` or `login.microsoftonline.us`. Must be in the allowlist unless overridden (see [SECURITY.md](SECURITY.md)). **Never** a full token endpoint URL. |
| `--tenant <id>` | `login`, `token` | Tenant identifier or `common`/`organizations`. Validated: ASCII alphanumerics, `-` and `.`, not all dots, bounded length. |
| `--client-id <guid>` | `login`, `token` | Public client id. Defaults to the AVD client `a85cf173-4192-42f8-81fa-777a763e6e2c`. Must be in the allowlist unless overridden. |
| `--scope <scope>` | `login`, `token` | Repeatable. One scope per occurrence, given decoded. Order is not significant; the cache key uses the sorted set. |
| `--req-cnf <b64url>` | `token` | Base64url-encoded JSON confirmation object, e.g. `{"kid": "<key-id>"}`, produced by FreeRDP. Presence of this option makes the request a proof-of-possession request; absence makes it a bearer request. |
| `--account <id>` | `token`, `logout` | Which stored account to use. Omitted on `token`: use the only account matching authority+tenant+client, or fail with exit `30` if there is more than one. |
| `--prompt {auto,always,never}` | `login`, `token` | `auto` (default): silent if possible, call the service if not. `always`: force an interactive transaction even if a cached token would do. `never`: silent only; exit `10` rather than calling the service. `login` treats `never` as an error (exit `64`). |
| `--json` | all | Emit a JSON object on stdout instead of the plain form. |
| `--config <path>` | all | Alternative configuration file. Overrides `ENTRA_TOKEN_HELPER_CONFIG`. |
| `--verbose` | all | Raise the log level on **stderr**. Never changes what stdout contains. |
| `--help`, `--version` | all | Print and exit `0`. |

## stdout

stdout carries **only** the result. Diagnostics, prompts and progress go to stderr. A caller may
therefore capture stdout directly.

**Plain form.** `token` prints the access token and a newline, nothing else:

```
eyJ0eXAiOiJKV1Qi...
```

`login` prints one line naming the account. `accounts` prints one line per account:
account id, authority, tenant, separated by whitespace. `logout` prints one line per removed
account. On any non-zero exit, stdout is **empty**.

**JSON form (`--json`).** A single object, one line or pretty-printed, on stdout:

```json
{
  "schema": 1,
  "status": "ok",
  "token": "eyJ0eXAiOiJhdCtqd3Qi...",
  "token_type": "pop",
  "expires_in": 3599,
  "account": "<user>@<tenant-domain>"
}
```

- `status` — `"ok"` on success; otherwise the symbolic name of the exit condition
  (`"interaction_required"`, `"cancelled"`, `"no_account"`, `"unavailable"`, `"server_error"`,
  `"usage"`, `"internal"`).
- `token` — present only when `status` is `"ok"` and the verb is `token`.
- `token_type` — `"bearer"` or `"pop"`.
- `expires_in` — seconds remaining, relative to when the response was written.
- `account` — the account the token belongs to.
- `error` — present when `status` is not `"ok"`: a short, stable, machine-readable symbol.
- `message` — present when `status` is not `"ok"`: a human-readable sentence, already redacted.
  It never contains a token, a code, or an authorization-server `error_description`.

For `accounts --json` the object carries an `"accounts"` array instead of `token` fields.

## Exit codes

| Code | Name | Meaning |
|---|---|---|
| `0` | success | The request succeeded. For `token`, stdout holds the token. |
| `10` | interaction required | An interactive transaction would have been needed and `--prompt never` was given. The caller may retry with `--prompt auto`. Not an error condition; it is the documented way to ask "can you do this silently?" |
| `20` | cancelled | The service responded `1`: the user closed the sign-in window, or cancelled the certificate chooser or the PIN prompt. A caller should **not** immediately retry interactively — the user just said no. |
| `30` | no such account | No account matched, or `--account` named one that is not stored, or the account exists but has no usable refresh token (signed out, expired, revoked). The caller should run `login`. |
| `40` | provider unavailable | The client cannot do its job in this environment: nothing implementing `io.github.sjtrotter.WebAuthentication1` to call for an interactive request, no session bus, or no Secret Service keyring. Also the mapping for a service `Response` of `2` when it means no window could be shown at all. A dispatcher should treat this as "decline" and fall through to the next provider (e.g. FreeRDP's terminal paste flow). |
| `50` | authorization server error | The authority refused: `invalid_grant`, `interaction_required` from the server, a Conditional Access claims challenge, a consent problem, `AADSTS50011`. The details are on stderr, redacted. |
| `64` | usage | Bad arguments. (`64` is `EX_USAGE` from `sysexits.h`.) |
| `70` | internal | An unexpected failure in the client itself — including a service `Response` of `2` for a reason other than unavailability, such as a timeout. (`70` is `EX_SOFTWARE`.) **Every verb currently returns this** with the message `not implemented (design sketch)`. |

The distinction that matters to a dispatcher is `40` (decline, try someone else) versus `20`
(stop, the user said no) versus `50` (stop, the server said no). Collapsing these into a boolean
is exactly the defect the FreeRDP provider interface has today.

## Request / response schema

The same objects, described once, so that the CLI and a future socket or D-Bus transport carry
the same payload. `size`/`schema` is present so a field can be added without a new positional
convention.

### Request

```json
{
  "schema": 1,
  "verb": "token",
  "authority": "login.microsoftonline.us",
  "tenant": "<tenant-id>",
  "client_id": "a85cf173-4192-42f8-81fa-777a763e6e2c",
  "scopes": [
    "https://www.wvd.azure.us/.default",
    "openid",
    "profile",
    "offline_access"
  ],
  "token_kind": "pop",
  "req_cnf": "<base64url JSON confirmation object>",
  "account": "<user>@<tenant-domain>",
  "prompt": "auto",
  "parent_window": "wayland:<handle>"
}
```

- `token_kind` is `"bearer"` or `"pop"`; `"pop"` requires `req_cnf`.
- `scopes` are **decoded**. Whether a scope arrives encoded or decoded is exactly the kind of
  ambiguity the current FreeRDP varargs convention leaves open, so it is pinned here.
- `parent_window` is optional and advisory. It is passed straight through to the service's
  `Start` so the sign-in window can be parented to the application that asked, and it is never
  trusted for authorization by either layer.
- Fields a caller may **not** set, in any transport: token endpoint, authorization endpoint,
  redirect URI, or any URL at all. The client derives every URL from the authority and its own
  cloud table, and it is the client — not its caller — that gives the service its `completion_uri`.
  See [SECURITY.md](SECURITY.md).

### Response

```json
{
  "schema": 1,
  "status": "ok",
  "token": "<access token>",
  "token_type": "pop",
  "expires_in": 3599,
  "account": "<user>@<tenant-domain>"
}
```

or

```json
{
  "schema": 1,
  "status": "interaction_required",
  "error": "prompt_never",
  "message": "a sign-in window is required and --prompt never was given"
}
```

The response never contains a refresh token, an authorization code, a PKCE verifier, a `state`
value, a PIN, or a raw authorization-server `error_description`.

## Environment

**No environment variable is required.** The client runs correctly with an empty environment apart
from what the desktop session itself provides. It needs `DBUS_SESSION_BUS_ADDRESS` to reach the
service and `XDG_RUNTIME_DIR` for its per-account lock; a display is the *service's* requirement,
not the client's, which is why a client invoked from a headless context gets a clean `40` rather
than a crash.

| Variable | Meaning |
|---|---|
| `ENTRA_TOKEN_HELPER_CONFIG` | Optional. Path to a configuration file, overriding the default under `$XDG_CONFIG_HOME`. `--config` overrides it. |

Behaviour is deliberately **not** taken from other environment variables. A library whose
behaviour is set by process environment is hard for an embedding client to control, and an
environment variable is an easy thing for a hostile parent to set.

## Compatibility promise

Within `"schema": 1`:

- The four verbs, their option names, and their meanings do not change.
- Exit codes do not change meaning, and no exit code is reused for a different condition.
- The plain stdout form of `token` stays "the token and a newline, nothing else".
- New optional options may be added. New optional fields may be added to the JSON request and
  response. A consumer must ignore fields it does not recognise.
- New exit codes may be added for conditions that do not fit an existing one; a consumer should
  treat an unknown non-zero code as a hard error rather than as a decline.
- `status` and `error` symbols are stable. `message` text is not — do not parse it.

A change to any of the above requires `"schema": 2` and a documented migration. Until this
project has acquired a single real token, the schema should be considered provisional: `1` is
what it will be *when it works*, not a promise made about a sketch.

The service interface version is separate. A caller of this CLI never sees it, and the client is
expected to work against anything implementing version 1 of
[`io.github.sjtrotter.WebAuthentication1`](SERVICE-INTERFACE.md) — including, one day, a
desktop-native implementation rather than this project's own.
