# 5. URL in, completion out: no protocol semantics in the authentication service

Date: 2026-09-03
Status: accepted (for the sketch); its "one service, not a frontend and a backend" position is
superseded by [0008](0008-build-to-the-upstream-shape.md), and its "Naming" section by
[0010](0010-backend-only-frontend-lives-upstream.md)

> **Amendment (0010).** One line changes: the shipped name is no longer this project's to pick.
> The frontend is a branch of xdg-desktop-portal and the interface is
> `org.freedesktop.portal.experimental.WebAuthentication` — `experimental` being the namespace
> upstream set aside for unfinished portals, which is not the ownership claim the "Naming" section
> below argues against. Everything else in this decision stands untouched.

> **Amendment (0008).** Everything this decision is *about* — URL in, completion out, no tokens, no
> accounts, no caching, exact matching, the nine objections, the exit criterion — stands unchanged
> and is unaffected by the frontend/backend split. What is superseded is the process shape recorded
> under "Backend preference" below: the alternatives listed there are no longer implementations
> behind an in-process vtable but separate **backends** selected by `portals.conf`, and the
> preference order becomes an administrator's configuration rather than a runtime choice. The
> incubating name in "Naming" also gains a `portal` component
> (`io.github.sjtrotter.portal.WebAuthentication1`) to mirror upstream's namespace layout; it is no
> more a freedesktop name than it was.

## Context

Layer 1 needs a boundary. Two shapes were considered seriously.

**A token service.** The caller says "give me a token for this resource" and the service returns
one. Genuinely attractive: it would keep refresh tokens away from applications entirely, coalesce
concurrent requests, provide real cross-application SSO, enforce authority and client allowlists
centrally, and give applications a far safer API than "navigate anywhere I ask".

But it immediately needs provider-specific client registration rules; token cache keys and
account-selection semantics; refresh-token rotation and revocation; consent and incremental
scopes; claims challenges and Conditional Access; PoP, DPoP and device-bound key behaviour;
logout and session removal; broker enrollment and compliance semantics; policy about which
application may request which token; and protocol-specific error handling. That is not a web
authentication service. It is an identity broker, comparable to GNOME Online Accounts, MSAL's
broker, or Microsoft's Linux broker — a much larger thing, with a much larger governance problem,
and one that already has incumbents.

**A transaction service.** The caller supplies a URI to open and the exact URI whose navigation
ends the flow; the service returns the completion, uninterpreted.

## Decision

Layer 1 performs **one interactive web authentication transaction and returns an uninterpreted
completion artifact**. It has no token exchange, no accounts, and no caching.

Call the output a **completion**, not a "final URL". Version 1 supports navigation (GET)
completions and returns `completion_uri`; a future protocol may complete through an intercepted
POST, and the name should not have to change when it does.

## Consequences

**What stays with the caller**, which is the point:

- Generating and validating `state`.
- Generating the PKCE verifier and performing the exchange.
- Understanding OAuth errors and claims challenges.
- Client registration, authority, scopes, PoP, refresh-token rotation, logout.

The service never becomes a universal credential store, and never becomes an incomplete MSAL.

**What the service gets in exchange:** one coherent job — open a URI, host a trustworthy
browser-like experience, perform browser and TLS functions the caller cannot, recognise an agreed
completion navigation, and return it to the initiating transaction. It also supports non-OAuth
interactions that finish through navigation without falsely claiming to understand them.

**Costs accepted.**

- Every consumer needs its own OAuth code. For the AVD case that code already exists; for a second
  consumer it is real work that a token service would have saved.
- No cross-application SSO at the *token* level. Session-level sharing (the `shared` website data
  store) is what remains, and it is much weaker.
- `response_mode=form_post` and SAML HTTP-POST do not fit and are explicitly unsupported in
  version 1. Version 1 is honestly "web authorization navigation", not universal protocol-agnostic
  authentication. See [../PUBLIC-INTERFACE.md](../PUBLIC-INTERFACE.md).

**Boundaries that follow, and must not be crossed casually.** No token exchange, no credential
storage beyond web session state, no choice of identity provider or tenant, no policy about who
may sign in to what, no prefix matching, no caller-controlled TLS or certificate trust. Each would
be easy to cross once and impossible to uncross.

## Why this could be a bad idea

Nine serious objections, recorded here so that nobody has to rediscover them:

1. **Identity providers may reject embedded engines.** RFC 8252 recommends an external user-agent
   for native applications and rejects app-embedded ones because of credential-interception risk.
   A service-owned WebKit process is meaningfully better than a web view inside the requesting
   application — the requester cannot reach the DOM — but it is not the system browser, and some
   providers may still classify or block it.
2. **The API is a phishing launcher.** Any same-UID application can ask for a convincing
   Microsoft, Google or corporate sign-in page. The only mitigation is service-controlled chrome
   showing the real origin and an independently established caller identity. Caller-supplied
   titles are actively dangerous and are treated as untrusted text.
3. **Shared state amplifies malicious callers.** A hostile application can start a flow using an
   already-authenticated session. OAuth `state` protects transaction correlation; it does nothing
   for the user's understanding of *which native application* asked.
4. **Client certificates raise the stakes.** The service does not merely render a page: it can ask
   a hardware token to authenticate. The chooser must therefore show the requesting application,
   the target origin, the certificate identity and the purpose *before* any PIN is requested.
5. **A web engine becomes security-critical infrastructure.** Distributions must update WebKitGTK
   promptly, and this project must handle process isolation, downloads, popups, permissions,
   storage, TLS errors and navigation policy safely — forever.
6. **"Protocol-agnostic" can become unbounded scope.** SAML POST, WebAuthn, passkeys, broker
   enrollment, downloads, external schemes, multiple windows and provider quirks can turn this
   into a browser. Version 1 is deliberately narrow, and staying narrow is a continuing decision.
7. **There may be only one real consumer.** If AVD is the only one, the generic layer adds
   governance and API obligations without reuse. A second credible consumer is a precondition for
   pursuing standardisation, not a nice-to-have.
8. **It cannot reproduce real browser SSO.** A service-global profile is shared only within the
   service. It does not inherit Firefox or Chrome accounts, enterprise browser policies,
   extensions, device registration or browser-bound credentials, and the UI must not imply
   otherwise.
9. **Same-UID isolation is weak on an unrestricted desktop.** This materially helps sandboxed
   applications. It cannot claim strong separation between mutually hostile unsandboxed ones.

**Exit criterion.** If the implementation cannot make caller identity, displayed origin, storage
partitioning and certificate consent convincing, **collapse the browser layer back into the Entra
client**. A narrowly scoped Entra/AVD helper is better than a generic authentication service with
an ill-defined trust model. This is a real outcome to plan for, not a formality.

## Naming

The shipped name is project-controlled and versioned:
`io.github.sjtrotter.portal.WebAuthentication1`. `org.freedesktop.portal.WebAuthentication` appears in
this repository only as the name that might eventually be proposed, and only alongside the
acceptance path in [../ROADMAP.md](../ROADMAP.md). Shipping a `org.freedesktop.portal.*` name from
an independent project asserts an ownership that does not exist, and the D-Bus specification
recommends a controlled reverse-domain namespace with a major interface version anyway.

## Backend preference

Not every flow needs a service-owned engine, and the ones that do not should not get one:

1. **The system browser**, when the completion mechanism lets it securely return the result —
   loopback HTTP, claimed HTTPS app links, registered custom schemes.
2. **A service-owned WebKitGTK session**, when redirect interception or service-controlled PKCS#11
   handling requires it. This is the AVD/PIV case, and it is why the project exists.
3. **Manual paste**, as the recovery and headless fallback.
4. **A browser extension**, only as an experimental, explicitly installed integration — never a
   reference backend, given browser-family fragmentation, broad URL-observation permissions,
   concurrent-transaction correlation, profile selection, extension update trust, native-host
   packaging, and outright failure under private browsing or enterprise policy.

Under [0008](0008-build-to-the-upstream-shape.md) each of these is a separate **backend** — a
process implementing `org.freedesktop.impl.portal.experimental.WebAuthentication`, declaring itself
in a `.portal` file, and selected in `portals.conf` — rather than an implementation behind an
in-process vtable with a capability mask. The list above is unchanged; only who chooses, and when,
has moved. See [`backend/src/webkit-session.h`](../../backend/src/webkit-session.h)
and [../IMPL-INTERFACE.md](../IMPL-INTERFACE.md).
