# 7. Certificate handling behind an adapter: portal preferred, in-process retained until proven

Date: 2026-09-03
Status: accepted, and **substantially amended on 2026-09-04 by the S2 result**: the adapter has two
providers, `portal` and `pkcs11`, and there is no `inproc` provider at all. The adapter lives in the
BACKEND per [0008](0008-build-to-the-upstream-shape.md) and calls a different interface since
[0010](0010-backend-only-frontend-lives-upstream.md)

> **Amendment (S2 result, 2026-09-04). The mechanism is settled, and it is not the one this
> decision assumed.**
>
> Spike [S2](../SPIKES.md) ran. A `GTlsCertificate` whose private key is a **PKCS#11 URI**
> satisfies a WebKitGTK 2.52 client-certificate challenge and completes a mutual-TLS handshake
> against a server that requires one; the key stayed on the token (`CKA_SENSITIVE`), so the
> signature was made through the module inside WebKit's **network process**, addressed by URI.
> WebKit ships the URI, not key material — `libwebkitgtk-6.0.so` carries the
> `private-key-pkcs11-uri` property name — and asks for the token PIN itself through a second
> `authenticate` signal with `CLIENT_CERTIFICATE_PIN_REQUESTED`. There is no
> `webkit_network_session_set_tls_interaction()`: the `GTlsInteraction` route this decision assumed
> does not exist.
>
> **So brokered `Sign` is not the seam, and a PKCS#11 module is.** Three consequences:
>
> 1. **The `portal` provider becomes module-based.** It resolves a URI naming a token the
>    certificate portal's own client-side PKCS#11 module presents. The card, the chooser, the
>    consent and the PIN all stay in that service; what crosses into this process is a URI. The
>    agreement is [`src/tls/portal-token.h`](../../src/tls/portal-token.h), and it is
>    a contract with another repository: the token's label, manufacturer and model, its
>    `CKF_PROTECTED_AUTHENTICATION_PATH`, and the name of its p11-kit module file. That module does
>    not exist yet, so the provider reports itself unavailable and `auto` falls through.
> 2. **There is no `inproc` provider.** The second implementation is `pkcs11`: any p11-kit token,
>    named by `--client-cert-uri`, with the PIN read from a file the operator named. It has **no
>    chooser and no PIN prompt**, because a chooser and a PIN prompt inside a web browser process is
>    the design the certificate portal exists to replace, and building one here would make this
>    backend a second place where card handling lives. The sketch headers for one
>    (`tls/chooser.h`, `tls/pin.h`, `tls/pkcs11.h`, `tls/client_cert_inproc.h`) are deleted rather
>    than left as an intention. Everything in "What it costs" below about two certificate UIs is
>    therefore obsolete: there is one, and it is not in this repository.
> 3. **The delegation gap is unchanged and now has a mechanism to be solved through.** What this
>    backend can prove to the certificate portal is still nothing about the requesting application;
>    the module sees this process, and the portal sees the module's client. Never cross-process
>    attestation.
>
> What S2 did **not** answer stays open: dynamic module registration after the network process
> starts, concurrency, card removal mid-handshake, and the version matrix. See
> [SPIKES.md](../SPIKES.md).

> **Amendment (0010).** The adapter's *shape* is unchanged and the decision below still holds. What
> changed is what it calls, and what that call can do.
>
> **The names.** The `portal` adapter now calls
> `org.freedesktop.portal.experimental.Certificate` on `org.freedesktop.portal.Desktop` at
> `/org/freedesktop/portal/desktop`. Both this project's portal and the certificate portal are now
> hosted by one frontend — the xdg-desktop-portal branch
> `experimental/certificate-webauthentication` — and the separate repository still called
> `xdg-desktop-portal-certificate` ships only the certificate **backend**,
> `xdg-desktop-portal-certificate`.
>
> **`OpenPkcs11Endpoint` is gone.** It is on neither the public nor the impl Certificate interface:
> the branch deferred it as a follow-up, because an fd-returning method needs its own review and the
> mock backend the frontend is tested against cannot hand back a usable fd. That is a material loss
> to this decision's "two ways to use the grant". Only **brokered `Sign`** is left, and brokered
> `Sign` is viable here only if WebKitGTK/glib-networking expose an external-signer path — which is
> not known to exist. So the `inproc` fallback is not merely retained, it is currently the only
> implementation that can work at all, and the sentence below about retiring it is further away
> rather than nearer.
>
> **`context` is gone too.** The challenging origin was to be passed in a `context` option; there is
> no such option, so it can only travel in `reason`, which is application-supplied text presented as
> such.
>
> **The delegation gap is solvable now, in-process only.** Under one frontend hosting both
> interfaces, the app id derived for the web-authentication request is already in hand when that
> frontend calls its own certificate side, so it can be passed without crossing a bus. The frontend
> does not do this yet. The caveat is permanent: **never cross-process attestation** — across a
> process boundary, passing an app id along is an unattested assertion of someone else's identity,
> which is exactly the identity-laundering [../SECURITY.md](../SECURITY.md) forbids, and it is not
> to be built as a stopgap.

> **Amendment (2026-09-04, second).** Two sentences quoted above have been overtaken.
>
> - "That module does not exist yet, so the provider reports itself unavailable and `auto` falls
>   through" — **the module exists.** `tools/portal-stack.sh` runs both portals on one private bus,
>   headless, and completes a real WebKitGTK mutual-TLS handshake signed by the card's key. The
>   `portal` provider is the primary path; it reports itself unavailable only where the portal is
>   not running or its module is not in p11-kit's configuration.
> - "The delegation gap is solvable now, **in-process only**" — the rule is narrower than that
>   wording. What must never happen is **believing a caller about a third party's identity**;
>   delegating across a boundary is not itself forbidden, and authenticated IPC or a
>   frontend-issued capability would satisfy the rule. Neither is built, and in-process remains the
>   cheapest way to satisfy it. See [0010](0010-backend-only-frontend-lives-upstream.md).
> - "So the `inproc` fallback is not merely retained, it is currently the only implementation that
>   can work at all" — there is no `inproc` provider; there never was one to retain. The module the
>   `portal` provider needs exists and is tested, so `portal` is the working, preferred path, and
>   `pkcs11` (a token named on the command line, no chooser) is the fallback.
>
> And one thing the amendment above did not anticipate: a finished transaction cannot revoke the
> grants it caused, because they belong to two PKCS#11 module instances and one of them is in
> another process. [../SECURITY.md](../SECURITY.md), "What closing a transaction does NOT do".

> **Amendment (0008).** Nothing in this decision changed, but its home did: the adapter and both
> its implementations are now in the backend, because the TLS handshake and the window belong to
> the backend. Two consequences worth stating. First, the `portal` adapter calls the certificate
> portal's **public** interface as an ordinary client — a backend never calls another project's
> backend. Second, the identity that portal sees is this backend's rather than the application's,
> which was true before and is merely impossible to overlook now; see the delegation note in
> [../SECURITY.md](../SECURITY.md).

## Context

The original plan had this service own the whole smart-card path: enumerate the PKCS#11 tokens, show
a certificate chooser, prompt for the PIN, load the certificate, answer the TLS challenge. That is
the code Remmina already has, and it works.

There is a better place for it. Choosing a certificate from a card and unlocking it with a PIN is
something a mail client, a VPN dialog, a code-signing tool and a browser all need; putting the only
good implementation inside a *web authentication* service means every non-web consumer writes its
own — the situation this project set out to end, reproduced one layer up. And the PIN prompt is the
moment the user authorises a hardware token to authenticate on their behalf: a user asked for a PIN
by a different window in every application has no way to learn which window to trust. There should
be one, and it should not belong to whichever component happened to need a certificate first.

So: a separate project (repository `xdg-desktop-portal-certificate`, shipping the backend
`xdg-desktop-portal-certificate`) owns the chooser and the PIN, behind the public
`org.freedesktop.portal.experimental.Certificate` interface.

**But it cannot be a hard dependency for v0, because the mechanism that would connect it to this
service is unproven at exactly the point that matters.**

The ends of the chain are documented and fine; the middle is not. The three unproven steps between
a brokered credential and a WebKitGTK handshake are stated in [S2](../SPIKES.md).

"Call the service and build a `GTlsCertificate` from a returned URI" is therefore an assumption, not
a mechanism. Stock `p11-kit server` also scopes to a *token*, not to an object, so the security
property that makes delegation attractive — a module scoped to the one certificate the user picked —
needs a restricted facade that does not exist yet.

## Decision

Model the certificate path as an **internal adapter interface**
([`src/tls/client_cert.h`](../../src/tls/client_cert.h)):

```c
select_and_present(challenge) → GTlsCertificate*
```

with two implementations, chosen at run time. **Read the S2 amendment above first: the second one
is `pkcs11`, not `inproc`, and the first is a module rather than a brokered `Sign`.** What follows
is the decision as it stood before the spike, kept because the reasoning that survived it is here:

- **`portal`** — call the Certificate portal: `CreateSession` (a Request, whose Response carries
  the session handle) and then `AcquireCredential` (not "RequestCertificate": it grants private-key
  use and the name should say so), with `purpose: "client_auth"`, returning a grant carrying the
  certificate, its chain, the permitted operations and mechanisms, and an expiry. The operation is
  then satisfied by **brokered `Sign`** through a GnuTLS external-signer path — unproven, and
  dependent on WebKitGTK/glib-networking actually exposing one. **Preferred when available**, and
  see the amendment above: since `OpenPkcs11Endpoint` is not on the interface, this is the only
  option rather than the first of two.
- **`inproc`** — the proven in-process path: enumerate with p11-kit, show this service's own chooser
  and PIN prompt, and build the certificate with `g_tls_certificate_new_from_pkcs11_uris()` against
  the **system** p11-kit configuration. No forwarded module, no dynamic registration, nothing
  unproven between the chooser and the handshake. **Retained as the fallback until the portal path
  has completed a real WebKitGTK mutual-TLS handshake** ([S2](../SPIKES.md)).

When neither adapter can run, the challenge is declined and the transaction ends with response `2`
and reason `credential_unavailable`.

## Consequences

**What the adapter buys.**

- The service works on a machine with no smart card service installed. There is no release in which
  the AVD case is blocked on another project shipping.
- The unproven part is isolated behind four function pointers. If S2 fails, one implementation is
  disabled and nothing else in the service changes.
- The two implementations can be compared against each other on the same hardware, which is the only
  honest way to find out whether the portal path is actually equivalent.
- The smart card service can be published when it is ready and proven, not when this service needs
  it. That matches the advice in [SPIKES.md](../SPIKES.md) against publishing an API whose object
  scoping, login model, application compatibility and grant lifetime have not been demonstrated.

**What it costs.**

- **Two certificate UIs exist**, which is the duplication the delegation was supposed to end. It is
  accepted only as a transition: the in-process chooser is a fallback, not a feature, and the
  moment the portal path is proven across the support matrix, retiring it is the next decision. If
  that never happens, this decision should be reopened rather than left to drift.
- **A user could see either window** depending on what is installed, which is exactly the "which
  window do I trust" problem in miniature. Mitigation: both must state the same things — requesting
  application, origin, certificate identity, purpose — so what the user learns transfers.
- The in-process path keeps the PIN inside this process, and with it a whole row of
  [SECURITY.md](../SECURITY.md)'s secrets table that the portal path would have removed. That is the
  clearest single argument for finishing the portal path.
- Two spikes to coordinate rather than one, and a protocol to agree between projects being sketched
  in parallel.

**If the portal path fails at dynamic registration**, the most plausible workaround is **one
permanently registered broker module exposing synthetic grant-bound slots** — which changes the
contract from "return a new remote module" to "return a URI an already-registered module resolves".
That is a change to the *other* project's interface, not to this adapter's shape, which is the point
of having the adapter.

**Prefer the brokered-signing API to the module endpoint** in the certificate portal's own design —
which is, as it turns out, exactly what its frontend branch shipped, by leaving the endpoint out
entirely. A
module endpoint grants a generic cryptographic interface whose calls carry little trustworthy
context: once a client has a sign-capable PKCS#11 session, "client authentication" is largely
indistinguishable from arbitrary signing. Brokered `Sign`/`Decrypt` cannot prove its input came from
a TLS handshake either — the caller can lie — but it buys precise grant accounting, revocation,
policy enforcement and per-operation consent. The module endpoint is a *compatibility transport*,
requested by callers that need it, discoverable by capability, and not returned automatically.

**Precedent and neighbours.** Returning a device *endpoint* rather than device *data* is the shape
`org.freedesktop.portal.Camera` already uses. But note the limits: this is not yet a credible
freedesktop API — object and operation scoping are unresolved, application identity versus a
delegated network subprocess is unresolved, and the proposal mixes credential selection, PIN agent
behaviour, cryptographic operations and module transport. The `experimental` namespace is the right
place for it while that is true, and the conversation to have is with the
[linux-credentials](https://github.com/linux-credentials) maintainers about whether
certificate-backed signing belongs as a credential type under their proposal — a "ClientCertificate"
or "CryptographicCredential" boundary is better than one named after a physical device, since the
key might be a TPM, a software token, a phone or a remote HSM.

Firefox and Chromium are **not** automatically solved by "NSS and the OpenSSL provider can load
PKCS#11". Their sandboxing, module lifecycle and certificate-selection paths each need explicit
integration, and neither should be listed as an early consumer.

## What stays here either way

The *web* half of the challenge: recognising it, determining and displaying the origin that raised
it, refusing challenges from hosts unrelated to the page being shown, binding the whole thing to one
cancellable transaction, and releasing whatever the adapter held on every exit path. That last one
is small in code and disproportionate in risk, and it is on the independent-review list in
[SECURITY.md](../SECURITY.md).
