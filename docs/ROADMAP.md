# Roadmap

Status: design sketch. The sketch itself is done; nothing after it has started.

Effort figures are person-weeks for **one experienced Linux/C developer already familiar with the
working Remmina patches**, taken from the Codex estimate in the design review. They describe a
*credible MVP*, not something ready for general distribution. The largest single uncertainty in
all of them is S2 (the WebKitGTK client-certificate path across distro versions).

## Before anything: the two spikes

Run [S1](SPIKES.md) (refresh token → PoP token for a new key, silently) and [S2](SPIKES.md) (does
WebKit + GLib TLS accept a certificate on a p11-kit-forwarded module) before committing to the rest.
S1 decides what the client can promise; S2 decides whether the smart-card delegation works at all
and where the support floor sits. S3 (keyring availability) can run alongside. **Days, not weeks** —
and nothing below starts until both have answers.

S2 has an external dependency: the smart card portal's own spike, in its own repository, must first
establish that a scoped forwarded module can be produced. Run that one first, or stand in for it
with a hand-run `p11-kit server` — which is worth doing regardless, because it isolates whether a
failure is in the forwarding or in the consuming.

---

## Phase 0 — Reference frontend, reference backend and Entra client — **10–17 person-weeks**

One portal frontend, one backend and one token client, all working, on the machines the author
controls. Not packaged for the world, not proposed to anyone.

The estimate rose by about a person-week against the single-service plan, and that increase is the
price of [decisions/0008](decisions/0008-build-to-the-upstream-shape.md): a second D-Bus interface,
a second activation path, backend discovery, and the failure paths that only exist when the two
halves can die independently. It is spent here rather than spent twice later.

### 0a. Frontend: the portal — **1–2 weeks**

The public interface exactly as in [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md): `Start`, the
`Request` object, `Close()`, one `Response`. App-id derivation and its three honesty levels. Option
filtering and argument validation. Storage-mode policy. Rate limiting. The re-check of the
`completion_uri` a backend returns. Exact parsed completion matching and its rejection rules — the
frontend's copy. Structural redaction discipline.

Deliberately toolkit-free: GLib and GIO only, forever.

### 0b. Frontend: backend discovery and lifetime — **0.5–1 week**

`.portal` file parsing, the `portals.conf` `[preferred]` search, not exporting the interface when
nothing implements it, proxying with a `G_MAXINT` timeout, forwarding `Close()`, and the answers the
frontend owes when a backend cannot start, dies, or misbehaves
([IMPL-INTERFACE.md](IMPL-INTERFACE.md)). Small, and entirely new work that the single-service plan
did not contain.

### 0c. Backend: WebKitGTK web view — **1–2 weeks**

The GTK4/WebKitGTK 6.0 backend: the impl skeleton and its Request object, `parent_window` parsing
and parenting, window, navigation policy, interception before load, the backend's copy of the
completion matcher, storage partitioning across *all* engine state, disabled downloads and autofill,
no TLS-error bypass, structural redaction.

A web view with a strict navigation policy, and nothing about certificates.

### 0d. Backend: certificate adapter — **2–4 weeks**

`tls/client_cert.h` and both implementations. The adapter itself is trivial; the two things behind
it are not.

**`inproc` first**, because it is the path known to work and it is what makes the rest of phase 0
independent of another project's schedule: lift the chooser and PIN prompt out of the Remmina plugin,
port them from GTK 3 to GTK 4, replace `gtk_dialog_run()`'s nested main loops with asynchronous
responses, and — if practical — replace the `p11tool` subprocess with the p11-kit and GnuTLS APIs,
dropping the runtime dependency on `gnutls-utils`/`gnutls-bin`.

**`portal` when S2 says it works**, not before. Preferred once proven, because it takes the PIN and
the chooser out of this process entirely.

Common to both, and disproportionately risky for its size: recognising the challenge, displaying the
origin, refusing challenges from unrelated hosts, and releasing whatever the adapter held on **every**
exit path — the cancellation and timeout paths especially. On the independent-review list in
[SECURITY.md](SECURITY.md).

The wide range is S2's fault and narrows once S2 has run.

### 0e. Backend: security chrome and accessibility — **1–2 weeks**

The chrome that shows the verified caller and the real origin independently of caller text, and the
accessibility acceptance criteria from [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md) — AT-SPI
exposure including the in-process chooser and PIN prompt, keyboard-only certificate selection and
PIN entry, focus order and restoration across any hand-off to another portal's windows,
screen-reader announcement, contrast and scaling. Budgeted as its own item because treating it as
polish is how it does not happen, and because this chrome carries a security decision that WebKit's
own accessibility does not cover.

### 0f. Client: OAuth, clouds, refresh, cache — **2–3 weeks**

Authorization-code exchange with `state` and PKCE S256; the strict response classifier and its
percent-decoder; the commercial/Government cloud table; ARM bearer acquisition; RDS-AAD PoP
acquisition using the caller's `req_cnf`; refresh for both token kinds; token-response and
OAuth-error parsing with redaction; the in-memory access-token cache and its key.

### 0g. Client: secret storage, concurrency, cancellation — **1–2 weeks**

Secret Service storage for refresh tokens and account records; the explicit "no persistent cache"
mode; per-account serialization; cancellation and its races; `accounts` and `logout`, including
asking the portal to discard the account's web session.

### 0h. Integration — **1 week**

The Remmina adapter using FreeRDP's existing `GetCommonAccessToken` seam (save, install, chain on
decline — exactly as `sso-mib` does), and a small reference client proving non-Remmina use. No
FreeRDP changes required for either.

### 0i. Tests and security cleanup — **2–3 weeks**

Offline fixture tests (see [../tests/README.md](../tests/README.md)), including the completion
fixture table run against **both** copies of the matcher; end-to-end tests against a real Government
tenant for both ARM bearer and RDS-AAD PoP acquisition; and the independent review of the items
named in [SECURITY.md](SECURITY.md), which now includes the impl boundary itself.

**Phase 0 total: 10–17 person-weeks, in this repository. Allow 13–20 for something suitable for
general distribution** rather than a controlled-environment build. The gap is distro variance,
packaging, and the error paths that only appear on machines you do not own.

The estimate has not fallen even though the smart card portal exists, because the in-process
adapter is still built here — that is the whole point of retaining it. What the smart card portal
buys is not a smaller phase 0; it is that the card code is eventually written *once*, for every
application, rather than faster for this one.

**And note the coordination cost**, which is real even though the bus-name question is now settled.
The `AcquireCredential` contract, its grant semantics and the lifetime of anything it returns must
still be agreed between two sketches being written in parallel — and neither should be frozen until
S2 has said whether the module transport works at all. Each project now claims its own incubating
bus name, so there is nothing to negotiate there; see
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md), "Per-project bus names during
incubation". A shared `incubating-portal-frontend` is discussed there as one option to explore, not
a required next step, and is not budgeted above, because it is a third project and neither
interface is settled.

---

## Phase 1 — Distribution and a second consumer

Packaging for the distributions the S2 matrix identified, with **two** D-Bus service files, a
`.portal` file, a documented `portals.conf`, and a stated support floor. The smart card portal is a
*recommended*, not a required, dependency: without it the in-process adapter runs. Testing on GNOME
and KDE, on Wayland and X11, including `parent_window` parenting and `activation_token` behaviour.

Two packaging questions are new and neither has an answer yet: whether the frontend and the backend
are one package or two (two, if the point of the split is to be honoured), and what a distribution
does when both this frontend and the smart card portal's want the same bus name — which is the
argument for the shared incubating frontend in
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md).

And, more important than any of that: **a second, unrelated consumer.** Not a second RDP client —
something that is not AVD and not FreeRDP, that needs an interactive web sign-in and would rather
not build a browser. Until one exists, the generic layer is a generalisation from a single example,
and [0005](decisions/0005-service-shape.md) is explicit that this is one of the ways the whole idea
could be wrong. A second consumer is what turns the interface from a plausible shape into a tested
one, and it is a precondition for phase 2 rather than a nice-to-have.

Also in this phase: the repository split from
[0006](decisions/0006-two-repositories.md), at the first tagged interface release.

---

## Phase 2 — Propose the interface to freedesktop

Only after phases 0 and 1. The eventual names would be `org.freedesktop.portal.WebAuthentication` and
`org.freedesktop.impl.portal.WebAuthentication`; today's `io.github.sjtrotter.portal.*` and
`io.github.sjtrotter.impl.portal.*` are the incubating ones, and shipping a freedesktop name before
acceptance would assert an ownership that does not exist. The full mapping, file by file, is
[UPSTREAMING.md](UPSTREAMING.md).

The acceptance path, in order:

1. Implement the independent prototype and publish the introspection XML.
2. Document the threat model, completion matching, caller identity, cookie/storage model and UI
   security chrome. (Most of that is [SECURITY.md](SECURITY.md) and
   [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md) already, but written against a real
   implementation.)
3. Demonstrate AVD **and at least one unrelated consumer**.
4. Test on GNOME and KDE/Wayland, including parenting and activation.
5. Open a design discussion in [`flatpak/xdg-desktop-portal`](https://github.com/flatpak/xdg-desktop-portal),
   where the project directs requests for new portals.
6. Agree the public interface and what the frontend enforces. This project has taken a concrete
   position on both — [IMPL-INTERFACE.md](IMPL-INTERFACE.md) — precisely so there is something to
   disagree with rather than a blank page.
7. Move the frontend into xdg-desktop-portal and rename both interfaces. Because of
   [decisions/0008](decisions/0008-build-to-the-upstream-shape.md) this step is a rename and a
   move, not the design work it used to be; [UPSTREAMING.md](UPSTREAMING.md) is the patch, written
   in advance.
8. Obtain interest from another desktop, and a second backend. One backend already exists.
9. Add conformance tests and documentation before declaring the incubating interface obsolete.

The proposal will have to answer *"what protected host resource is being mediated?"* — portals
traditionally mediate access that sandboxed applications lack. The strongest available answer is
"a trusted system authentication user-agent, persistent sign-in state, and client-certificate
capability". That is a plausible case, and it has to be made explicitly rather than assumed.

### Prior art, stated accurately

- **xdg-desktop-portal has no OAuth or web-authentication portal**, existing or proposed. A search
  of its issue tracker finds only FIDO2/WebAuthn discussions —
  [#987 "FIDO2 Portal Proposal"](https://github.com/flatpak/xdg-desktop-portal/issues/987) and
  [#989 "FIDO U2F/WebAuthn abstraction/permission/portal"](https://github.com/flatpak/xdg-desktop-portal/issues/989)
  — plus a closed
  [#311 "Add password portal"](https://github.com/flatpak/xdg-desktop-portal/issues/311). The
  nearest *shipped* neighbours are
  [`org.freedesktop.portal.Account`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Account.html)
  (basic user information) and
  [`org.freedesktop.portal.Secret`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Secret.html).
- **[linux-credentials/credentialsd](https://github.com/linux-credentials/credentialsd)** proposes
  an `org.freedesktop.portal.Credentials` interface for passkeys and WebAuthn, with a reference
  implementation and experimental Firefox and Chromium integration. It is FIDO2-only: no OAuth, no
  hosted web-view sign-in. Adjacent work rather than overlapping work, and a useful precedent for
  how such an interface gets proposed and what it is asked to justify.
- **Platform analogues** for the transaction shape:
  [`ASWebAuthenticationSession`](https://developer.apple.com/documentation/authenticationservices/aswebauthenticationsession),
  [Android Custom Tabs](https://developer.android.com/develop/ui/views/layout/webapps/overview-of-android-custom-tabs)
  with [AppAuth](https://github.com/openid/AppAuth-Android), and
  [`WebAuthenticationBroker`](https://learn.microsoft.com/en-us/uwp/api/windows.security.authentication.web.webauthenticationbroker)
  on Windows.
- **Microsoft's Linux identity broker** is itself a per-user D-Bus-activated service, with
  [sso-mib](https://github.com/siemens/sso-mib) as its C client — evidence that the shape works on
  Linux, and a reminder that a vendor-specific service is not a platform primitive.

**None of these is adoption of this idea.** No maintainer has been asked, nothing has been
proposed, and the interface here may well be argued down.

---

## Deferred

Not "never" — "not yet, and not before something asks for it".

- ~~**A frontend/backend D-Bus split.**~~ **Done, deliberately and early**, against the advice
  recorded here and in the design review. The costs that advice named are real, are accepted, and
  are listed in [decisions/0008](decisions/0008-build-to-the-upstream-shape.md) rather than
  deleted. What has *not* been done, and remains exactly as deferred as it was, is claiming an
  `org.freedesktop.*` name.
- **A capability-negotiating impl interface.** The split cost the old vtable's capability mask, and
  version 1 replaces it with "a backend implements the whole interface or does not claim it". If
  that turns out to be too rigid — a paste backend that can serve some flows, say — the answer is a
  `GetCapabilities`-shaped addition argued upstream, not invented here.
- **A shared `incubating-portal-frontend`** hosting both this interface and the smart card
  portal's, so that two incubating portals can be installed at once. Proposed in
  [decisions/0008](decisions/0008-build-to-the-upstream-shape.md); needs agreement from both
  projects, which is why it is here and not in phase 0.
- **A system-browser backend, and a paste backend.** Formerly "a system-browser session": under the
  split these are separate backends selected by `portals.conf` rather than implementations behind a
  vtable.
- **A system-browser session.** The right first choice for flows whose completion the browser can
  return safely — loopback HTTP, claimed HTTPS app links, registered custom schemes. Deferred only
  because the AVD case cannot use it; it should be built as soon as a consumer can.
- **POST completions** (`response_mode=form_post`, SAML HTTP-POST). A separately reviewed result
  shape, added only when a real consumer needs it.
- **A per-application persistent session mode.** Blocked on caller identity being credible enough
  to key one. If it ever lands it should be called `app_persistent`.
- **A browser extension session.** Experimental, explicitly installed, never the reference.
- **A Qt user interface.** One GTK/WebKitGTK backend serves every caller, because the backend is a
  separate process and its window need not match the caller's toolkit. A KDE-native *backend* is a
  different matter and is now a straightforward one: implement the impl interface, ship a `.portal`
  file, name it in `portals.conf`. That it became straightforward is most of the point of
  [decisions/0008](decisions/0008-build-to-the-upstream-shape.md).
- **Retiring the in-process certificate adapter.** The intended end state, once the portal path has
  completed a real WebKitGTK mutual-TLS handshake across the support matrix. Deliberately *not*
  scheduled: doing it before then would make the backend depend on an unproven mechanism. If it
  never becomes possible, [0007](decisions/0007-certificate-adapter.md) should be reopened rather
  than left to drift.
- **Persisting a certificate choice across transactions.** A distinct, reviewable policy under
  either adapter, and one that never implies persisting a PIN.
- **Device enrollment and PRT behaviour.** The Microsoft Identity Broker's job; `sso-mib` already
  reaches it. This is the non-enrolled path, and the two are siblings.
- **A generic MSAL replacement.** The client implements exactly the flows AVD needs against exactly
  the authorities on its allowlist.
- **Additional keyring backends** beyond Secret Service, plus the explicit "no persistent cache"
  mode. Await S3.
- **A client-side daemon.** The layering already removed most of the reason for one: the warm
  browser session a client daemon would have bought is now the backend's shared store, and both
  portal processes are already long-lived.
- **A FreeRDP provider registry**, until the client's request contract has been exercised against a
  real client.

---

## FreeRDP-side follow-ups

Changes wanted in FreeRDP itself. None block phase 0f — the existing callback seam is enough for a
first integration — but each removes a rough edge that any out-of-tree provider hits.

1. **A typed, size-versioned token-provider API in `client/common`.** A request struct carrying
   `{ token_type, authority, tenant, client_id, decoded scope, req_cnf, parsed kid }`; a provider
   callback with **userdata**; register/unregister with priority; and an explicit
   success / declined / cancelled / error result, where `cancelled` stops the chain so cancelling
   one interactive provider does not immediately open another sign-in window.
   `instance->GetAccessToken` stays as the final frontend fallback for at least one ABI cycle.
   Today's mechanism is an interception hook, not a provider interface: the argument convention is
   undocumented positional varargs, every provider must decode `req_cnf` itself, there is no
   userdata (`sso-mib` gets a dedicated field in `rdpClientContext` instead), and `BOOL` conflates
   "not applicable", "temporarily unavailable", "user cancelled" and "hard failure".

2. **`freerdp_common_context()` returns `TRUE` without a token** when the frontend has no
   `GetAccessToken`. It should return `FALSE` (or the new `DECLINED`). A caller currently gets
   "success" and no credential, turning a missing provider into a confusing downstream failure.
   Small, independently justified, worth sending on its own.

3. **`sso-mib` hard-codes the commercial AVD scope** (`https://www.wvd.microsoft.com/.default`)
   instead of using the resolved `FreeRDP_GatewayAvdScope`. That breaks sovereign clouds outright:
   on a Government tenant the broker is asked for the wrong resource. Related: `sso-mib`'s single
   failure flag permanently stops trying the broker after *any* failure, including a transient one
   or one specific to only one token type.

4. **Discovery is fetched before the provider is asked.** Both callers fetch the OpenID
   configuration before invoking the provider, so a provider perfectly capable of resolving the
   authority itself is blocked when that fetch fails. The dispatcher should build the request from
   the *configured* authority and treat discovered endpoints as optional.

5. **Registration order and lifetime.** A provider installed during `ContextNew` ends up *under*
   `sso-mib` (installed later, in `freerdp_client_start()`); one installed during or after
   `ClientStart` *overwrites* `sso-mib` unless it manually saves and chains. There is no
   unregister, no ownership tracking, and no protection against callbacks destroyed in the wrong
   order. The register/unregister API in item 1 subsumes this.

6. **A single authority resolver.** The same authority is derived in at least three places today
   (`sso-mib`, RDS-AAD discovery, ARM discovery). One exported resolver returning normalized
   authority, tenant, client id, decoded scope and any discovered endpoints would stop provider
   code re-implementing sovereign-cloud and tenant-selection logic.

7. **Provider selection as a setting**, e.g. `/token-provider:auto|mib|helper|frontend|none`,
   rather than a library-global environment variable — library behaviour driven by process
   environment is hard for an embedding client to control. A sensible `auto` order is `sso-mib`
   (enrolled devices) → this client (everything else) → frontend `GetAccessToken` → the CLI paste
   flow.
