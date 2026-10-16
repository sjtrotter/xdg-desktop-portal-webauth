# Roadmap

The reference backend, both certificate providers and the first Entra client are implemented.
The frontend is maintained on `experimental/integration`; [Upstreaming](UPSTREAMING.md) covers
its proposal requirements. [Testing](TESTING.md) records the evidence and limits.
[ADR 0010](decisions/0010-backend-only-frontend-lives-upstream.md) assigns frontend ownership.

## Current evidence

[Feasibility checks](SPIKES.md) records the S1, S2 and S3 results and their remaining
keyring, card and distribution matrices.

[The impl guide](IMPL-INTERFACE.md) defines backend behavior;
[Security](SECURITY.md#implemented-controls-and-gaps) lists implemented controls and gaps.

[entra-token-helper](https://github.com/sjtrotter/entra-token-helper) owns authorization-code
exchange, PKCE S256, response validation, commercial/Government authority allowlists, ARM bearer
and RDS-AAD PoP acquisition, refresh, OAuth error parsing and Secret Service account storage.
Access-token cache entries share the keyring secret with refresh tokens. The client exposes
`accounts` and `logout` in its own CLI. Per-account refresh serialization, cancellation races
and portal web-session clearing on logout remain outstanding. The split is recorded in
[ADR 0006](decisions/0006-two-repositories.md).

## Backend work

- Review security chrome for caller identity, origin, contrast and scaling. Complete the
  [accessibility criteria](PUBLIC-INTERFACE.md#accessibility-requirements), including screen-reader
  use, focus order and focus return.
- Resolve certificate attribution, grant lifetime and authenticated connection reuse. Both
  providers need card-removal, wrong-PIN, cancellation and timeout coverage across the support
  matrix. [Security](SECURITY.md) defines the constraints.
- Add frontend per-connection rate limiting and test the impl sender boundary.
- Expand [offline fixtures](../tests/README.md), keep both matcher tables aligned and review
  completion/Close/death races independently.
- Package the backend with its D-Bus activation file, `.portal` declaration and documented
  `portals.conf`. The branch-built frontend remains required. Recommend the Certificate portal;
  retain `pkcs11` as a fallback for an operator-named token.
- Test GNOME and KDE, Wayland and X11, including parent-window handling and activation.

Planning allowances are 1-2 person-weeks for security chrome/accessibility, 1 week for
integration and 2-3 weeks for tests and review, assuming one experienced Linux/C developer
familiar with the Remmina patches. Remaining total effort is unestimated; the support matrix
is the largest uncertainty. [ADR 0008](decisions/0008-build-to-the-upstream-shape.md) records
the split's costs.

## Consumers and proposal

The OneDrive wrapper has [fixture evidence](TESTING.md#headless); live OneDrive integration
remains unproven. Seek an unrelated use case and an independent
backend to validate the generic contract. [ADR 0005](decisions/0005-service-shape.md) records
why these are acceptance criteria. The interface could still be rejected.

Before proposing stable interfaces:

1. Complete the backend, accessibility and hardware validation above.
2. Document the threat model, caller identity, completion rule, storage and chrome.
   Test the GLib/WebKit/GnuTLS support matrix.
3. Demonstrate independent consumers and desktop/backend implementations.
4. Open a design discussion in
   [flatpak/xdg-desktop-portal](https://github.com/flatpak/xdg-desktop-portal).
5. Agree the public and impl contracts, then add conformance tests and independent review.

## Related interfaces and prior art

- Related xdg-desktop-portal issues include
  [#987 "FIDO2 Portal Proposal"](https://github.com/flatpak/xdg-desktop-portal/issues/987) and
  [#989 "FIDO U2F/WebAuthn abstraction/permission/portal"](https://github.com/flatpak/xdg-desktop-portal/issues/989)
  and closed
  [#311 "Add password portal"](https://github.com/flatpak/xdg-desktop-portal/issues/311). The
  shipped neighboring interfaces are
  [`org.freedesktop.portal.Account`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Account.html)
  (basic user information) and
  [`org.freedesktop.portal.Secret`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Secret.html).
- [linux-credentials/credentialsd](https://github.com/linux-credentials/credentialsd) proposes
  an `org.freedesktop.portal.Credentials` interface for passkeys and WebAuthn, with a reference
  implementation and experimental Firefox and Chromium integration. Its FIDO2-only scope excludes OAuth and
  hosted web-view sign-in.
- Platform analogues for the transaction shape:
  [`ASWebAuthenticationSession`](https://developer.apple.com/documentation/authenticationservices/aswebauthenticationsession),
  [Android Custom Tabs](https://developer.android.com/develop/ui/views/layout/webapps/overview-of-android-custom-tabs)
  with [AppAuth](https://github.com/openid/AppAuth-Android), and
  [`WebAuthenticationBroker`](https://learn.microsoft.com/en-us/uwp/api/windows.security.authentication.web.webauthenticationbroker)
  on Windows.
- Microsoft's Linux identity broker is a per-user D-Bus-activated service, with
  [sso-mib](https://github.com/siemens/sso-mib) as its C client.


## Deferred work

- Capability negotiation requires an upstream contract addition such as GetCapabilities.
- System-browser and paste implementations are separate backends selected by `portals.conf`.
  Browser-returnable completions include loopback HTTP, claimed HTTPS app links and registered
  custom schemes; the AVD client registration does not offer these alternatives.
- Browser extensions require explicit installation and remain experimental.
- POST completions (`response_mode=form_post`, SAML HTTP-POST) require a separately reviewed
  result shape and a consumer.
- A separate `app_persistent` policy requires identity strong enough to key it. Current `shared`
  storage already uses app IDs, including host labels, whose trust limits need review.
- A KDE/Qt backend can implement the same impl contract; callers need not use the backend's toolkit.
- Persisting certificate choice across transactions requires separate policy and must not persist PINs.
- Device enrollment, PRTs and broker device claims belong to the Microsoft broker and `sso-mib`.
  The Entra client implements the AVD flows on its authority allowlist; general MSAL coverage
  is outside its scope.
- Additional keyring providers and an explicit no-persistent-cache mode depend on the S3 matrix.
- A client daemon and FreeRDP provider registry remain deferred pending demonstrated demand.

Revisit [ADR 0007](decisions/0007-certificate-adapter.md) if the portal module fails across
the support matrix. There is no in-process certificate adapter to retire.

## FreeRDP integration

[FreeRDP #13340](https://github.com/FreeRDP/FreeRDP/pull/13340), opened 2026-09-04 and reviewed
favorably for 3.32, externalizes the AAD web view into a helper speaking newline-delimited
JSON-RPC over two pipes. `navigate` takes a URL, redirect-URI prefix and timeout and returns
the redirect URL verbatim. FreeRDP retains OAuth. The helper is selected with
`/azure:auth-helper:<path|autodetect>`. A follow-up agreed in that review would pass OAuth
parameters as JSON so the helper can construct the authorize URL.

The proposed portal helper forwards navigation to Start and returns the completion URI.
The helper protocol's redirect prefix must be reconciled with the portal's exact matching rule.
A token-returning helper extension would allow `entra-token-helper` to supply cached refresh
and proof-of-possession tokens too. `sso-mib` remains the provider for Intune-enrolled devices
with device-bound PRTs; this project cannot mint those.

The fork-only `client/entra-token-helper` GetAccessToken shim is the proof of concept from
2026-09-05. The intended integration uses the helper protocol above. The Remmina
GetCommonAccessToken callback seam supports saving, chaining and restoring another provider,
as `sso-mib` does; a reference client can exercise it without a FreeRDP code change.
