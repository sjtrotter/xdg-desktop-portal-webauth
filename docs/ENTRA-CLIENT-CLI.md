# Entra client CLI contract

Status: **implemented**. `entra-token-helper` signs in through the web authentication portal,
exchanges the code, stores an account in the Secret Service, serves later requests from its cache,
and mints proof-of-possession tokens bound to a key its caller generated. Everything below has been
exercised end to end against a mock authority (`tools/entra-e2e.sh`); **no token has yet been
acquired from a real identity provider by this code**, and that is the one thing the "Current
capabilities" table in [../README.md](../README.md) will keep saying until it has.

This is the contract for **layer 3**, the Entra ID / AVD token client — the interface FreeRDP
frontends and other programs are expected to depend on. Layer 2, the web authentication portal the
client calls when it needs a window, has its own contract in
[PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md); the two are independent and versioned separately.

The client calls xdg-desktop-portal, on `org.freedesktop.portal.Desktop`, and nothing
else. That the portal is internally a frontend and a backend
([decisions/0008](decisions/0008-build-to-the-upstream-shape.md)) is invisible here: no verb, exit
code or field below changed when the split was made, and none would change again if a machine
installed a different backend.

It is versioned: the JSON objects carry `"schema": 1`, and the compatibility promise at the end of
this document says what may change without a bump.

## Verbs

| Verb | Purpose | Interactive? |
|---|---|---|
| `login` | Establish an account: run an interactive sign-in through the portal and store the resulting refresh token. Does not print an access token. | Always |
| `token` | Acquire an access token. Silent if possible, interactive if permitted. | Depends on `--prompt` and cache state |
| `accounts` | List stored accounts. | Never |
| `logout` | Forget one account or all accounts: remove refresh tokens and account records from the keyring. | Never |

`token` is the verb FreeRDP's callback shim calls. `login`, `accounts` and `logout` exist so a
human can manage state without a connection in flight.

## Options

| Option | Applies to | Meaning |
|---|---|---|
| `--authority <a>` | `login`, `token` | The Entra ID authority. Normally a **host**: `login.microsoftonline.com` or `login.microsoftonline.us`. The `https://<host>/<tenant>` URL form is also accepted, because that is what Microsoft's own documentation calls the authority; only its host and its first path segment are ever read. A query, a fragment, userinfo, a non-`https` scheme or a deeper path is a usage error, and **no endpoint is ever taken from it**. Must be in the allowlist unless a *user* has added it in the configuration file. |
| `--tenant <id>` | `login`, `token` | Tenant identifier or `common`/`organizations`. Validated: ASCII alphanumerics, `-` and `.`, bounded length. Defaults to `common`, or to the tenant in the URL form of `--authority`. Naming a different tenant in both is a usage error. |
| `--cloud <name>` | `login`, `token` | `commercial` or `usgov`. A shortcut that fills in the authority host and, when no `--scope` is given, the four AVD scopes. It never overrides an explicit `--authority`, and naming a cloud that disagrees with one is a usage error. |
| `--client-id <guid>` | `login`, `token` | Public client id. Defaults to the AVD client `a85cf173-4192-42f8-81fa-777a763e6e2c`. Must be in the allowlist unless a *user* has added it in the configuration file. |
| `--scope <scope>` | `login`, `token` | Repeatable, given decoded. One occurrence may also carry a space-separated list; several occurrences and one space-separated occurrence mean exactly the same request and hit the same cache entry. Order is not significant; the cache key is the sorted, de-duplicated set. |
| `--req-cnf <b64url>` | `token` | Base64url-encoded JSON confirmation object, e.g. `{"kid": "<key-id>"}`, produced by FreeRDP. Its presence makes the request a proof-of-possession request; its absence makes it a bearer request. |
| `--account <id>` | `token`, `logout` | Which stored account to use. Omitted on `token`: the only account matching authority+tenant+client, or exit `30` if there is none or more than one — never a guess. Omitted on `logout`: every account. |
| `--prompt {auto,always,never}` | `login`, `token` | `auto` (default): silent if possible, call the portal if not. `always`: force an interactive transaction even if a cached token would do. `never`: silent only; exit `10` rather than calling the portal. `login` treats `never` as an error (exit `64`). |
| `--parent-window <handle>` | `login`, `token` | Optional and advisory. Passed straight through to the portal's `Start` so the sign-in window can be parented to the application that asked. Never trusted for authorization by any layer. |
| `--session-mode {shared,ephemeral}` | `login`, `token` | Optional. Passed through to the portal. The portal decides: an unidentified caller is narrowed to `ephemeral` whatever it asks for. |
| `--timeout <seconds>` | `login`, `token` | How long the sign-in window may stay up. Passed to the portal as its `timeout` option and enforced locally as well. Default 300. |
| `--json` | all | Emit a JSON object on stdout instead of the plain form. |
| `--format {raw,json}` | all | `--format json` is `--json`; `raw` is the default. Accepted so a caller can be explicit. |
| `--config <path>` | all | Alternative configuration file. Overrides `ENTRA_TOKEN_HELPER_CONFIG`. |
| `--verbose` | all | Raise the log level on **stderr**. Never changes what stdout contains, and never relaxes redaction. |
| `--help`, `--version` | all | Print and exit `0`. |

## stdout

stdout carries **only** the result. Diagnostics, prompts and progress go to stderr. A caller may
therefore capture stdout directly.

**Plain form.** `token` prints the access token and a newline, nothing else:

```
eyJ0eXAiOiJKV1Qi...
```

`login` prints one line naming the account. `accounts` prints one line per account: account id,
authority, tenant, tab separated. `logout` prints one line per removed account. On any non-zero
exit, stdout is **empty**.

**JSON form (`--json`).** A single object on stdout:

```json
{
  "schema": 1,
  "status": "ok",
  "token": "eyJ0eXAiOiJhdCtqd3Qi...",
  "access_token": "eyJ0eXAiOiJhdCtqd3Qi...",
  "token_type": "pop",
  "expires_in": 3599,
  "scope": "ms-device-service://termsrv.wvd.microsoft.com/name/<host>/user_impersonation",
  "account": "<user>@<tenant-domain>"
}
```

- `status` — `"ok"` on success; otherwise the symbolic name of the exit condition
  (`"interaction_required"`, `"cancelled"`, `"no_account"`, `"unavailable"`, `"server_error"`,
  `"usage"`, `"internal"`).
- `token` and `access_token` — the same string under both names: `token` is the name this contract
  fixed, `access_token` the name every OAuth consumer already reads. Present only when `status` is
  `"ok"` and the verb is `token`.
- `token_type` — `"Bearer"` or `"pop"`, as the authority spelled it.
- `expires_in` — seconds remaining, relative to when the response was written.
- `scope` — the scope the authority granted, which is not always the scope that was asked for.
- `account` — the account the token belongs to.
- `error` — present when `status` is not `"ok"`: a short, stable, machine-readable symbol.
- `message` — present when `status` is not `"ok"`: a human-readable sentence, already redacted.
  It never contains a token, a code, or an authorization-server `error_description`.

For `accounts --json`, and for `login --json` and `logout --json`, the object carries an
`"accounts"` array instead of the token fields.

## Exit codes

| Code | Name | Meaning |
|---|---|---|
| `0` | success | The request succeeded. For `token`, stdout holds the token. |
| `10` | interaction required | An interactive transaction would have been needed and `--prompt never` was given. The caller may retry with `--prompt auto`. Not an error condition; it is the documented way to ask "can you do this silently?" |
| `20` | cancelled | The portal responded `1`: the user closed the sign-in window, or cancelled the certificate chooser or the PIN prompt. A caller should **not** immediately retry interactively — the user just said no. |
| `30` | no such account | No account matched, `--account` named one that is not stored, or more than one matched and none was named. The caller should run `login`. |
| `40` | provider unavailable | The client cannot do its job in this environment: `org.freedesktop.portal.experimental.WebAuthentication` is not exported, so there is nothing to call for an interactive request. **That is the default state of a machine**: the interface is experimental and absent unless xdg-desktop-portal was started with `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`. The same code covers a portal with the gate on but no backend configured (it exports nothing either), no session bus, no Secret Service keyring, and an authority that cannot be reached at all — indistinguishable by design. **The message names the environment variable**, because on a developer's machine that is almost always what is wrong. A dispatcher should treat this as "decline" and fall through to the next provider (e.g. FreeRDP's terminal paste flow). |
| `50` | authorization server error | The authority refused for a reason a window will not fix: a bad client, a bad request, a server fault. The details are on stderr, redacted to the OAuth error code. |
| `64` | usage | Bad arguments. (`64` is `EX_USAGE` from `sysexits.h`.) |
| `70` | internal | An unexpected failure in the client itself — including a portal `Response` of `2` for a reason other than unavailability, such as a timeout or a `backend_completion_mismatch`, and a completion URI that failed the callback classifier. (`70` is `EX_SOFTWARE`.) |

The distinction that matters to a dispatcher is `40` (decline, try someone else) versus `20`
(stop, the user said no) versus `50` (stop, the server said no). Collapsing these into a boolean
is exactly the defect the FreeRDP provider interface has today.

**`invalid_grant` is not `50`.** An expired, revoked or Conditional-Access-challenged refresh token
is the authority saying "ask the human", not "no". It becomes a sign-in when `--prompt` allows one
and exit `10` when it does not.

## What each verb actually does

### `login`

1. Resolves the authority to a host and a tenant, checks both allowlists, derives the
   authorization and token endpoints from its own cloud table, and refines them with the
   authority's OpenID configuration if it answers. **Discovery refines, it does not gate**: a
   failed fetch is a DEBUG line and the derived endpoints stand.
2. Builds the authorization URL: `response_type=code`, PKCE `S256`, a fresh 256-bit `state`,
   `prompt=select_account` (`login` under `--prompt always`), and the nativeclient redirect.
3. Calls `org.freedesktop.portal.experimental.WebAuthentication.Start` with the completion URI set
   to that same redirect, having subscribed to the `Response` signal first so a fast completion
   cannot race the subscription.
4. Classifies what comes back (see below) and exchanges the code with the verifier.
5. Stores the account: refresh token, ID token and the access token just issued, in the Secret
   Service.
6. Prints `Signed in as <upn>`. The UPN comes from the ID token's `preferred_username`, `upn`,
   `unique_name`, `email` or `sub`, whichever is there first. **The signature is not checked and
   nothing is authorized by it**: the ID token arrived over TLS from an endpoint this client chose,
   in the answer to a request it made with a verifier no one else has, and the name is a label so a
   human can tell two sign-ins apart.

### `token`

A **bearer** request, in order:

1. The cache, unless `--prompt always`. A cached access token is used if it is more than five
   minutes from expiry. **This costs no network at all** — not even a discovery fetch.
2. The `refresh_token` grant.
3. A full interactive sign-in, if `--prompt` allows one; otherwise exit `10`, or exit `30` when
   there is no account to refresh from.

A **proof-of-possession** request (`--req-cnf`) never uses the cache — a PoP token is bound to one
key, and a cache that ignored the binding would hand back a token the caller cannot use — and adds
`token_type=pop` and `req_cnf` to the grant. It goes:

1. The `refresh_token` grant, with the confirmation object.
2. If the authority answers `interaction_required`, `consent_required`, `login_required` or
   `invalid_grant` (in `error` or in `suberror`), **a full interactive authorize for that scope**,
   exchanged with `token_type=pop` and `req_cnf` on the authorization code grant. Any rotated
   refresh token that comes back is stored; the PoP token itself is not.

**Expect a window here, and expect it to say something odd.** The RDS-AAD scope is
`ms-device-service://termsrv.wvd.microsoft.com/name/<host>/user_impersonation`, and Entra routinely
puts an interstitial in front of it — "you are connecting to a remote desktop", "make sure you
trust this client". That page appears **inside the portal's sign-in window** at this step, is
answered there, and is the reason no `prompt` parameter is sent on this request: naming one of ours
would only fight with it. A run against real hardware
(`FreeRDP-plan/test-avd-20260903-080717.log`) shows FreeRDP doing exactly this — a second
authorization for the device-service scope, then `grant_type=authorization_code` with `req_cnf`.

### The callback classifier

The portal guarantees only that the URI it returns matched the completion URI it was given. Whether
that URI is *this transaction's authorization response* is OAuth knowledge and lives in the client.
A URI is accepted only when:

- scheme, host, port and path equal the transaction's redirect (empty path and `/` are the same
  resource);
- there is no userinfo and no fragment;
- `state` occurs exactly once and matches in constant time;
- exactly one of `code` or `error` is present, each occurring exactly once — **a parameter present
  without a value still counts as an occurrence**, so `?code=good&code` is refused;
- every percent escape is well formed and none decodes to `%00`.

A transaction answers once. A second response, whatever it carries, is a replay or an answer to a
request this process did not make.

## Where things are stored

One Secret Service item per account, schema `io.github.sjtrotter.entra-token-helper`, with three
attributes:

| Attribute | Value |
|---|---|
| `authority` | `https://<host>/<tenant>` — the full base, so the same person in two tenants is two items rather than one overwriting the other |
| `client_id` | the public client id |
| `account` | the UPN |

The secret is a JSON record holding the refresh token, the ID token, and the access tokens cached
under the sorted scope set. The refresh token is the only thing in it that is months of standing
access; the access token is in there too because the CLI is a one-shot process and an in-memory
cache would live for the length of one call. Neither is ever written to a file, a log, or the JSON
response. See [SECURITY.md](SECURITY.md).

## Request / response schema

The same objects, described once, so that the CLI and a future socket or D-Bus transport carry
the same payload.

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
- `parent_window` is optional and advisory. It is passed straight through to the portal's `Start`
  — and by the frontend, uninterpreted, to whichever backend parses it — so the sign-in window can
  be parented to the application that asked, and it is never trusted for authorization by any
  layer.
- Fields a caller may **not** set, in any transport: token endpoint, authorization endpoint,
  redirect URI, or any URL at all. The client derives every URL from the authority and its own
  cloud table, and it is the client — not its caller — that gives the portal its `completion_uri`.
  See [SECURITY.md](SECURITY.md).

### Response

As the JSON form above, or:

```json
{
  "schema": 1,
  "status": "interaction_required",
  "error": "interaction_required",
  "message": "a sign-in window is required and --prompt never was given"
}
```

The response never contains a refresh token, an authorization code, a PKCE verifier, a `state`
value, a PIN, or a raw authorization-server `error_description`.

## Environment

**No environment variable is required.** The client runs correctly with an empty environment apart
from what the desktop session itself provides. It needs `DBUS_SESSION_BUS_ADDRESS` to reach the
portal and the Secret Service; a display is the *backend's* requirement, not the client's, which is
why a client invoked from a headless context gets a clean `40` rather than a crash.

| Variable | Meaning |
|---|---|
| `ENTRA_TOKEN_HELPER_CONFIG` | Optional. Path to a configuration file, overriding the default under `$XDG_CONFIG_HOME`. `--config` overrides it. |

Behaviour is deliberately **not** taken from other environment variables. A library whose
behaviour is set by process environment is hard for an embedding client to control, and an
environment variable is an easy thing for a hostile parent to set. In particular there is no
environment variable that widens an allowlist or trusts a certificate.

## The configuration file

A GKeyFile, at `$XDG_CONFIG_HOME/entra-token-helper/config` unless `--config` or
`ENTRA_TOKEN_HELPER_CONFIG` says otherwise. A missing file is not an error.

```ini
[allow]
authorities = login.example.invalid;
client_ids  = 00000000-0000-0000-0000-000000000000;

[testing]
trust_certificate = /path/to/fixture.pem
```

`[allow]` is how a **user** widens the two allowlists, one entry at a time, never with a wildcard.
`[testing] trust_certificate` names a PEM to trust **instead of** the system store; it exists for
the mock authority in `tools/entra-e2e.sh` and is the client's only trust override, mirroring the
backend's single `--debug-trust-certificate` option. Both are in a file rather than a flag so that
a hostile parent process cannot set them.

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
project has acquired a single real token, the schema should be considered provisional.

The portal interface version is separate. A caller of this CLI never sees it, and the client is
expected to work against anything implementing version 1 of
[`org.freedesktop.portal.experimental.WebAuthentication`](PUBLIC-INTERFACE.md) — including, one day, a
desktop-native backend rather than this project's own, or the frontend having moved into
xdg-desktop-portal entirely ([UPSTREAMING.md](UPSTREAMING.md)). The backend interface version is
separate again and is nobody's business here.
