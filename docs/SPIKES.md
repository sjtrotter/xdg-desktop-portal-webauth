# Feasibility checks

The broader hardware, distribution and keyring matrices remain open. [Testing](TESTING.md) holds runnable
stack commands, and [Roadmap](ROADMAP.md) tracks follow-up work.

| Check | Question | Recorded result |
|---|---|---|
| S1, client | Can a cached refresh token acquire PoP for a new key silently? | The 2026-09-05 Government-tenant run required one interactive re-authentication. |
| S2, backend | Can WebKit complete mutual TLS through the Certificate portal? | PKCS#11 URI transport completed on 2026-09-04 and through the portal module on 2026-09-04/05/06. |
| S3, client | Is Secret Service available in target environments? | The 2026-09-05 run retrieved the ARM token from the keyring cache; failure environments remain untested. |

## S1: refresh to a new proof-of-possession key

FreeRDP generates a new RDS PoP key per connection and supplies base64url `req_cnf` containing
`{"kid": "<key-id>"}`. The client must bind the returned token to that key and verify it with
an RDS-AAD handshake against a real session host.

The 2026-09-05 run required interaction under the Government tenant's Conditional Access policy.
The portal's shared Entra cookie reduced re-authentication to a click without another card
operation. This evidence does not support a promise of silent PoP acquisition for every key.
Commercial/Government differences and the lifetime of the shared Entra session remain to be
measured. [Architecture](ARCHITECTURE.md) describes component ownership and
[Security](SECURITY.md) describes shared-state risk.

### Reproduction and edge cases

Use the client repository for token experiments:

1. Establish an ARM bearer and RDS-AAD baseline with FreeRDP's terminal paste flow against
   `login.microsoftonline.us`, tenant `<tenant-id>`, public client
   `a85cf173-4192-42f8-81fa-777a763e6e2c`, and ARM scope
   `https://www.wvd.azure.us/.default openid profile offline_access`.
2. For a bounded experiment, hold the refresh token in tmpfs, generate a different PoP key and
   send a refresh-token grant to
   `https://login.microsoftonline.us/<tenant-id>/oauth2/v2.0/token` with the target RDS scope and
   new `req_cnf`. No browser or card should participate in this step.
3. Verify the returned token's `cnf` against the new key and complete the RDS handshake.
4. Exercise expiry, session revocation, Conditional Access `claims` challenges,
   `interaction_required` and refresh-token rotation. Rotation requires atomic cache replacement
   and per-account serialization in the consumer.
5. Destroy the captured refresh token and sign out the test session.

A silent result requires both the new-key token and the real handshake to succeed without
interaction. Otherwise retain interactive fallback and measure cookie lifetime. Record
commercial/Government and time-window limits explicitly.

## S2: WebKit and brokered certificates

### Result, 2026-09-04

A GTlsCertificate naming a PKCS#11 private key completed a required-client-certificate handshake
with WebKitGTK 2.52.

The run used [`spikes/webkit-client-cert.c`](../spikes/webkit-client-cert.c) with GTK4,
WebKitGTK 6.0, a `WebKitNetworkSession` and one `authenticate` handler against a SoftHSM token
holding an RSA-2048 client certificate and its key, on Fedora 44 with WebKitGTK 2.52.5,
GLib 2.88.3, glib-networking 2.80 (GnuTLS backend), GnuTLS 3.8.13 and p11-kit 0.26.5. Two servers:
`gnutls-serv --require-client-cert`, and `tools/mtls-server.py` with Python `ssl.CERT_REQUIRED` and
a CA file, which rejects a client that produces no certificate at the TLS layer
(`tlsv13 alert certificate required`).

Verbatim, from the spike against the CERT_REQUIRED server:

```
authenticate scheme=7 host=localhost
certificate outcome=built kind=pkcs11
load outcome=finished uri=https://localhost:18444/
```

and from `gnutls-serv`'s side of the same exercise:

```
Subject: CN=Portal Test User,O=Example Org
Client Signature: RSA-PSS-RSAE-SHA256
```


The SoftHSM object has CKA_PRIVATE and CKA_SENSITIVE, so the key value is unavailable to the UI
process. WebKit carries `private-key-pkcs11-uri` to its network process, where GnuTLS resolves
and signs through the module. The `portal` provider uses the Certificate portal's permanently
registered module; `pkcs11` is the non-portal fallback. There is no in-process card chooser.
[ADR 0007](decisions/0007-certificate-adapter.md) records the adapter decision.

### PIN and module constraints

WebKitNetworkSession has no `webkit_network_session_set_tls_interaction()` setter or external
signer hook. WebKit emits CLIENT_CERTIFICATE_PIN_REQUESTED (scheme 9) after the certificate
challenge; `webkit_credential_new_for_certificate_pin()` answers it. A `pin-value` URI worked in
the initial experiment. [The PKCS#11 provider documentation](SECURITY.md#pkcs11-provider)
describes current rejection and the unchecked key-URI gap. The portal token uses
CKF_PROTECTED_AUTHENTICATION_PATH and keeps PIN handling in its backend.

A PKCS#11 URI cannot name a socket. p11-kit remoting needs `p11-kit-client.so`,
P11_KIT_SERVER_ADDRESS and module configuration; GLib's constructor accepts no module parameter.
The implemented contract uses an installed module present before WebKit starts. Its URI requires
an `object=` attribute for GnuTLS single-object import. `src/tls/portal-token.h` specifies it.

### Portal confirmation, 2026-09-04/05/06

The Certificate backend supplies `libpkcs11-portal-certificate.so`.
[`portal-stack.sh`](../tools/portal-stack.sh) runs both portals on a private bus and headless X
server. It exercises the chooser, PIN prompt, C_Sign and a completed handshake whose server log
contains the certificate common name. Firefox and this backend exercised the module on 2026-09-06.
[Testing](TESTING.md#two-grants-per-fresh-handshake) records the grant and chooser counts;
[Security](SECURITY.md#certificate-grant-and-connection-lifetimes) explains grant lifetime,
attribution and the exclusion of process-tree delegation.

### Remaining matrix

- Register a module after WebKit's web and network processes start, using isolated p11-kit
  configuration. Compare with the supported permanently registered module path.
- Exercise two concurrent certificates/endpoints.
- Remove a card mid-handshake and close the grant owner's D-Bus connection. Require bounded
  failure and no leaked grant, window or PIN buffer.
- Identify which process opens a remoting Unix socket and when, if testing that transport.
- Repeat on oldest/newest supported GLib, WebKitGTK and GnuTLS versions, including Fedora 44,
  Debian stable and Ubuntu LTS. Establish a support floor from those measurements.
- Test wrong PINs, engine-initiated retry, exhaustion, empty tokens and trust-token exclusion
  without automatic retries that can lock a real card.
- Expand storage checks beyond cookies to local storage, IndexedDB, service workers, HSTS and
  client-certificate decisions.

The 2026-09-05 live run confirmed `certauth` challenge handling and commercial nativeclient
interception against the `.us` authority. The registered redirect is
`https://login.microsoftonline.com/common/oauth2/nativeclient`; the rejected `.us` spelling
produces AADSTS50011. Repeat across the authority and version matrix before generalizing.

If the module path fails in a target environment, review the support floor and transport contract.
Potential alternatives include separate network processes per transaction or changes to
WebKit/glib-networking exposing an external signer. Stock token remoting has broader authority
than a grant-scoped module. The existing pkcs11 provider remains available.

Interception alternatives are separate backends under the
[impl contract](IMPL-INTERFACE.md), with their own `.portal` declarations and `portals.conf`
selection. The [public contract](PUBLIC-INTERFACE.md) remains the application boundary;
[ADR 0008](decisions/0008-build-to-the-upstream-shape.md) records this arrangement.

## S3: keyring availability

The 2026-09-05 cache hit demonstrates an available keyring. It does not establish behavior when
Secret Service is absent or locked. The client repository owns this matrix:

| Environment | Check |
|---|---|
| GNOME and KDE sessions | libsecret store/retrieve and any prompt |
| Bare window manager and headless session | Fast unavailable result without a persistent-file fallback |
| Locked keyring | No unexpected interaction during `--prompt never`; interaction must be classified as exit 10 |
| SSH session with forwarded display | Availability, prompt behavior and latency |
| Flatpak | Secret portal path and its permissions |

Record success, prompt, blocking behavior, error and latency. Unavailable providers should exit
40 so a dispatcher can try another flow. No environment should block indefinitely or report
successful storage without storing the secret.
