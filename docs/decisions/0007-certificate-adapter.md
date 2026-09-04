# 7. Certificate handling behind an adapter: portal preferred, in-process retained until proven

Date: 2026-09-03
Status: accepted (for the sketch); the adapter now lives in the BACKEND, per
[0008](0008-build-to-the-upstream-shape.md)

> **Amendment (0008).** Nothing in this decision changed, but its home did: the adapter and both
> its implementations are now in `service/backends/gtk/src/tls/`, because the TLS handshake and the
> window belong to the backend. Two consequences worth stating. First, the `portal` adapter calls
> the smart card portal's **public** interface as an ordinary client — a backend never calls
> another project's backend. Second, the identity that portal sees is now unambiguously
> *webauth-portal-gtk*'s rather than the application's, which was true before and is merely
> impossible to overlook now; see the delegation note in [../SECURITY.md](../SECURITY.md).

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

So: a separate project, the **smart card portal** (repository `smartcard-portal`; public interface
`io.github.sjtrotter.portal.Smartcard1`, on the same `io.github.sjtrotter.portal.Desktop` bus name
this project's own frontend claims, now that its restructuring has landed), owns the chooser and the
PIN.

**But it cannot be a hard dependency for v0, because the mechanism that would connect it to this
service is unproven at exactly the point that matters.**

The ends of the chain are documented and fine. `g_tls_certificate_new_from_pkcs11_uris()` accepts a
certificate and a private key named by PKCS#11 URIs, with the key accessed only later during use.
`webkit_credential_new_for_certificate()` accepts a `GTlsCertificate`. What is *not* established is
the middle:

- **A PKCS#11 URI cannot name a socket.** The documented p11-kit remoting path needs
  `p11-kit-client.so`, a `P11_KIT_SERVER_ADDRESS`, and the client module registered in p11-kit
  *configuration* for GnuTLS and OpenSSL consumers to find it.
- **GLib's constructor has no module parameter.** GnuTLS can load providers programmatically; GLib's
  public constructor does not expose that control.
- **WebKit's network process may not see a module registered after it started**, and it is not even
  obvious which process opens the socket, or when.

"Call the service and build a `GTlsCertificate` from a returned URI" is therefore an assumption, not
a mechanism. Stock `p11-kit server` also scopes to a *token*, not to an object, so the security
property that makes delegation attractive — a module scoped to the one certificate the user picked —
needs a restricted facade that does not exist yet.

## Decision

Model the certificate path as an **internal adapter interface**
([`service/backends/gtk/src/tls/client_cert.h`](../../service/backends/gtk/src/tls/client_cert.h)):

```c
select_and_present(challenge) → GTlsCertificate*
```

with two implementations, chosen at run time:

- **`portal`** — call the smart card service: `AcquireCredential` (not "RequestCertificate": it
  grants private-key use and the name should say so), with `purpose: "client_auth"` and `context`
  set to the destination host, returns a grant carrying the certificate, its chain, the permitted
  operations and mechanisms, and an expiry. The operation is then satisfied either by **brokered
  `Sign`** through a GnuTLS external-signer path — unproven, and dependent on WebKitGTK/glib-networking
  actually exposing one — or by the **experimental `OpenPkcs11Endpoint`** compatibility endpoint.
  **Preferred when available.**
- **`inproc`** — the proven in-process path: enumerate with p11-kit, show this service's own chooser
  and PIN prompt, and build the certificate with `g_tls_certificate_new_from_pkcs11_uris()` against
  the **system** p11-kit configuration. No forwarded module, no dynamic registration, nothing
  unproven between the chooser and the handshake. **Retained as the fallback until the portal path
  has completed a real WebKitGTK mutual-TLS handshake** ([S2](../SPIKES.md)).

When neither adapter can run, the challenge is declined and the transaction ends with response `2`
and reason `no_certificate_adapter`.

## Consequences

**What the adapter buys.**

- The service works on a machine with no smart card service installed. There is no release in which
  the AVD case is blocked on another project shipping.
- The unproven part is isolated behind four function pointers. If S2 fails, one implementation is
  disabled and nothing else in the service changes.
- The two implementations can be compared against each other on the same hardware, which is the only
  honest way to find out whether the portal path is actually equivalent.
- The smart card service can be published when it is ready and proven, not when this service needs
  it. That matches the advice not to publish an API claiming object-scoped modules, service-owned
  login, broad application compatibility or connection-bound lifetime until each has been
  demonstrated.

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

**Prefer the brokered-signing API to the module endpoint** in the smart card service's own design. A
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
behaviour, cryptographic operations and module transport. `io.github.sjtrotter.portal.Smartcard1` is the
right namespace for incubation, and the conversation to have is with the
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
