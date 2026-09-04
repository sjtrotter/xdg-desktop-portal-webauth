# 2. No loopback redirect; host the login page and intercept `nativeclient`

Date: 2026-09-03
Status: accepted (forced)

## Context

For a native application, the recommended OAuth 2.0 redirect (RFC 8252) is a loopback URI —
`http://127.0.0.1:<random port>/` — which lets the application open the system browser, let the
user authenticate in the browser they already trust, and receive the authorization code on a
local socket. It is better than an embedded web view in every way that matters: the user sees a
real address bar, the browser's own smart-card handling does the card work, and the application
never touches the credential exchange.

We cannot use it.

The Azure Virtual Desktop public client id `a85cf173-4192-42f8-81fa-777a763e6e2c` is registered by
Microsoft, not by us. Its only registered redirect URI is:

```
https://login.microsoftonline.com/common/oauth2/nativeclient
```

There is no loopback redirect registered, and there is no `.us` variant: requesting
`https://login.microsoftonline.us/common/oauth2/nativeclient` against the US Government authority
is rejected with `AADSTS50011` (verified). Adding a redirect would require editing a
Microsoft-owned application registration, which is not something a tenant administrator can do
and not something available to us.

Registering our *own* public client in the tenant was also considered. It would need tenant
administrator consent for the AVD resources on every tenant it is used in, which turns a
piece of client software into a per-tenant deployment project — and it does not help the
non-enrolled devices in tenants we do not control, which are the whole reason this exists.

## Decision

The login page is hosted on this machine, in a WebKitGTK web view owned by the web authentication
service, and the navigation to `https://login.microsoftonline.com/common/oauth2/nativeclient` is
intercepted in the navigation policy decision — **closing the window before the page loads**.

Note the asymmetry this forces and which must not be "fixed": when authenticating against the US
Government authority `login.microsoftonline.us`, the final redirect is still to the **commercial**
`login.microsoftonline.com` `nativeclient` URL. That is not a bug and not a misconfiguration; it
is what the application registration says, and anything that "corrects" it to `.us` gets
`AADSTS50011`. Note that this asymmetry is Entra-specific knowledge and therefore lives in the
Entra client's cloud table — the service is simply handed a completion URI and never learns why it
is on a different host from the one it is showing.

## Consequences

**Forced consequences.**

- We must host a web view, with everything that implies: WebKitGTK as a hard runtime dependency,
  a TLS client-certificate challenge handler, a PKCS#11 certificate chooser and a PIN prompt —
  none of which any toolkit provides. This is the single largest cost in the project, and it
  exists solely because of this constraint. It is also the whole reason layer 1 exists as a
  separate, reusable thing: if this cost has to be paid, it should be paid once.
- The interception must be in the navigation *policy* decision, not in a load-finished handler.
  The redirect URL carries the authorization code in its query string; letting the web view fetch
  it would send the code to a remote Microsoft-operated page that has no part in this exchange,
  and would show the user a page flash that means nothing.
- The strict redirect classifier described in [SECURITY.md](../SECURITY.md) is not
  belt-and-braces. Because our expected redirect is a real, remote, commercial-cloud HTTPS URL
  rather than a loopback socket only we can bind, "did this navigation come from the authorization
  server" is a question we must answer by inspection: exact scheme/host/port/path match, no
  userinfo, no fragment, `state` matched in constant time, exactly one of `code` or `error`,
  strict percent-decoding.
- Spike S2 becomes a go/no-go for the project rather than an implementation detail: if the
  WebKitGTK certificate path is not portable, this design has no fallback that does not involve
  a browser extension.

**What this rules out.**

- `xdg-open` plus a local listener. Not registered; the code would never arrive.
- Any design where the user authenticates in their own browser. The terminal paste flow is the
  degenerate case that still works — the user authenticates in Firefox, which has its own card
  UI, and pastes the redirect URL back — and it is retained as the headless fallback precisely
  because it is the only browser-based path available.

**If this ever changes.** Should a loopback redirect become registered for this client, or should a
first-party client id with one become usable, this decision should be revisited immediately: the
AVD flow could then use a system-browser session instead — which is the preferred implementation
wherever it is possible — and the web view, chooser and PIN subsystem would stop being on the
critical path for the one consumer that exists. That is worth restating in any future review rather
than treating a service-owned web view as settled architecture.
