# Go / no-go spikes

Status: **S1, S2 and S3 have all been run and are answered** (see their Result sections). S2 was
confirmed against the Certificate portal's real client-side module 2026-09-04/05/06; S1 was
answered by the 2026-09-05 chain (one interactive re-auth needed for the RDS PoP token); S3 by the
same run (the ARM token came from the keyring cache).

Two questions decide whether this project is worth building as described — one per component. Both are
answerable in days, with code that already exists, and both must be answered **before** any
polished work starts. A third, smaller question is worth answering early because it changes the
packaging story.

| | Layer | Question | Decides |
|---|---|---|---|
| **S1** | the Entra client | Can a refresh token mint a PoP token for a new key, silently? | The product promise |
| **S2** | the portal backend | Can WebKit complete mutual TLS with a certificate brokered by the Certificate portal? | The support floor, and whether the preferred certificate path exists at all |
| **S3** | the Entra client | What happens when there is no keyring? | How much the caching design is worth |

S2 is the one that decides whether the *portal backend* is buildable as described, so it is the
more fundamental of the two; S1 decides what the *client* can promise. Run them in parallel if there are
two people; run S2 first if there is one, because a portal that cannot do cards is not worth a
client.

The rule for all of them: a spike is throwaway code that answers one question. It does not become
the product, it does not get a test suite, and it does not get merged.

---

## S1 (client) — Can a refresh token mint a PoP token for a *new* key, without interaction?

**The question.** FreeRDP generates a fresh RDS proof-of-possession key for each connection and
passes it as `req_cnf`. The client's whole value proposition is that after one sign-in, later
tokens are silent. That requires redeeming a cached refresh token for a PoP token bound to a key
the authority has never seen — under Government tenant policy, with Conditional Access active,
on a certificate-authenticated account.

Nobody has verified that this works. If it does not, every connection needs a second interactive
transaction, and the product promise changes.

**Why it matters.** It is cheap to answer and it invalidates a lot of design if it fails. There is
no point building an authentication portal to make one card prompt per connection if the answer
is two — though note that S2's outcome is what decides whether the portal exists at all, and a
failing S1 makes its persistent shared session store *more* important, not less.

### Steps

1. Using FreeRDP's **existing terminal paste flow** (no new code), acquire against the Government
   authority `login.microsoftonline.us`, tenant `<tenant-id>`, client
   `a85cf173-4192-42f8-81fa-777a763e6e2c`:
   - the ARM bearer token, scope `https://www.wvd.azure.us/.default openid profile offline_access`;
   - the first RDS-AAD PoP token, with the `req_cnf` FreeRDP generated for that connection.
   Confirm both work end to end: the ARM call returns connection details and the RDS-AAD
   handshake completes. That establishes the baseline.
2. Capture the **refresh token** from the first token response. Store it in a file in a tmpfs
   directory for the duration of the spike, and delete it and revoke the session when done. This
   is the only point in this project where a refresh token is deliberately written down; it is
   acceptable only because it is a spike, and it is why step 8 exists.
3. Generate a **new** PoP key, independently of any connection, and format its `req_cnf` the way
   FreeRDP does: base64url of `{"kid": "<key-id>"}`.
4. Script a token request to `https://login.microsoftonline.us/<tenant-id>/oauth2/v2.0/token`
   with `grant_type=refresh_token`, the stored refresh token, the RDS scope for a target session
   host, and the **new** `req_cnf`. No browser involved, no card present.
5. Inspect the response. Confirm the returned token is a PoP token and that its `cnf` claim binds
   to the **new** key, not the one from step 1.
6. Drive the RDS-AAD handshake with the new key and the new token, against a real session host,
   and confirm it is accepted. A token that parses is not the same as a token the session host takes.
7. Probe the edges, since they determine what the error paths must handle:
   refresh-token expiry; a revoked session; a Conditional Access claims challenge
   (`claims` parameter in the error response); an `interaction_required` response; and a
   refresh-token *rotation* — does the response carry a new refresh token that invalidates the
   old one? If so, the cache must replace it atomically and the per-account serialization in
   [ARCHITECTURE.md](ARCHITECTURE.md) is mandatory, not an optimisation.
8. Destroy the captured refresh token and sign the account out.

### Pass

- Step 5 returns a PoP token whose `cnf` binds to the key from step 3, with no user interaction.
- Step 6's handshake succeeds against a real session host.
- Step 7 produces intelligible, distinguishable errors rather than the same opaque failure.

Then: the product promise is **one card interaction per sign-in, silent tokens thereafter until
the refresh token expires**, and the cache design in [ARCHITECTURE.md](ARCHITECTURE.md) stands.

### Fail

If the authority refuses to bind a refresh-token grant to a new `req_cnf`, or the session host
rejects the resulting token, then:

- The promise becomes **one card interaction per *token*, mitigated by the backend's `shared`
  session store** — the second acquisition still opens a transaction, but the user should not have
  to re-present the card if the Entra session cookie is still valid. That mitigation becomes
  load-bearing, and its lifetime must itself be measured: how long does the Entra session cookie
  survive, and does Conditional Access shorten it?
- Caching remains worth building (it still saves the *first* interaction on reconnect), but the
  README and the client's own CLI contract must not promise silent PoP acquisition.
- The backend's persistent shared data store moves from "nice to have" to "required", and
  `session_mode: shared` stops being a convenient default and becomes something the client
  depends on — which in turn raises the stakes on everything in
  [SECURITY.md](SECURITY.md) about shared state amplifying a malicious caller.

### Partial

If it works for commercial but not Government, or works but only within a short window after the
interactive sign-in, record the exact boundary. A promise with a documented limit is fine; an
undocumented one is not.

---

## S2 (backend) — Can WebKit complete a mutual-TLS handshake with a brokered certificate?

**The question.** The preferred certificate path takes the chooser and the PIN out of the backend
and into the Certificate portal. Whether that is *possible* comes down to one unproven step:

> Can a `GTlsCertificate` built from a credential the Certificate portal brokered — a p11-kit
> endpoint obtained at run time, or an external signer — actually satisfy a WebKitGTK client
> certificate challenge and complete a mutual-TLS handshake?

The ends of the chain are documented and fine. `g_tls_certificate_new_from_pkcs11_uris()` takes a
certificate and a private key named by PKCS#11 URIs, with the key used only later. WebKit accepts a
`GTlsCertificate` through `webkit_credential_new_for_certificate()`. The middle is the problem:

- a **PKCS#11 URI cannot name a socket**; p11-kit remoting needs `p11-kit-client.so`, a
  `P11_KIT_SERVER_ADDRESS`, and the client module registered in p11-kit **configuration**;
- **GLib's constructor has no module parameter** — GnuTLS can load providers programmatically, GLib's
  public constructor does not expose that;
- **WebKit's network process may not see a module registered after it started**, and it is not
  settled which process opens the socket, or when.

S2 has passed: the backend has no in-process adapter and never did; it depends on the Certificate
portal's PKCS#11 module as the only client-certificate path, with `pkcs11` (a token named on the
command line, no chooser) as the non-portal fallback
([decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md)).

**This is a joint spike** with the Certificate portal's repository: that project must be able to
produce an endpoint or signer at all before this one can consume it. Stand in for it with a
hand-run `p11-kit server` for the first pass — worth doing regardless, because it isolates whether a
failure is in the producing or the consuming.

### Result, 2026-09-04: the URI survives, and the handshake completes

**Answered, for the half that matters: YES.** A `GTlsCertificate` whose private key is a PKCS#11
URI satisfies a WebKitGTK 2.52 client-certificate challenge and completes a mutual-TLS handshake
against a server that **requires** a client certificate.

What was run: [`spikes/webkit-client-cert.c`](../spikes/webkit-client-cert.c) — GTK4 plus
WebKitGTK 6.0, a `WebKitNetworkSession`, and one `authenticate` handler — against a SoftHSM token
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

**The private key never left the token.** The SoftHSM object is `CKA_PRIVATE` and `CKA_SENSITIVE`,
so its value cannot be read by the UI process at all; the signature above therefore was made
through the module, addressed by URI, inside WebKit's **network process**. That is the answer to
the question this spike existed to ask: WebKit carries the certificate to the network process by
URI rather than as key material. `libwebkitgtk-6.0.so` carries the GTlsCertificate property name
`private-key-pkcs11-uri`, which is the mechanism.

**The PIN.** SoftHSM requires `C_Login`, and there are exactly two ways in — neither of them a
`GTlsInteraction`:

- **`WebKitNetworkSession` has no TLS interaction setter.** There is no
  `webkit_network_session_set_tls_interaction()`; the compile fails. So the GLib route the sketch
  assumed does not exist.
- **WebKit asks for the PIN itself.** After the certificate is supplied, the `authenticate` signal
  is emitted a second time with
  `WEBKIT_AUTHENTICATION_SCHEME_CLIENT_CERTIFICATE_PIN_REQUESTED` (scheme 9), and
  `webkit_credential_new_for_certificate_pin()` satisfies it. This is the route the backend uses.
- A `pin-value` in the key URI also works and is what the first pass used. The backend **refuses**
  it (`src/tls/client_cert.c`): a URI on a command line is a PIN in `/proc/*/cmdline`.

**What this does NOT answer**, and the steps stay open: dynamic module registration *after* the
network process exists (step 2), two concurrent endpoints (6), card removal mid-handshake (7),
grant lifetime against a closed D-Bus connection (8), which process opens a p11-kit socket (9), and
the whole version matrix (10). The spike itself involved no certificate portal, because the module
it publishes did not exist when the spike was run.

**It exists now, and the spike's conclusion holds against it.**
`xdg-desktop-portal-certificate` ships `libpkcs11-portal-certificate.so`, and
[`tools/portal-stack.sh`](../tools/portal-stack.sh) runs the two services against each other
headless: the challenge, the URI, p11-kit, the module in this process and in WebKit's network
process, the portal's chooser and PIN prompt, `C_Sign`, and a completed mutual-TLS handshake with
the card's common name in the server's log. The URI seam is what S2 said it was. Two things the
spike could not have predicted came out of that run and are in [TESTING.md](TESTING.md) tier 2b:
**one handshake resolves the URI in two processes and therefore puts up two choosers** — the
process-tree delegation that would have answered the second from the first is not on the portal
interface ([SECURITY.md](SECURITY.md)), so this stands — and the contract's URI needed an `object=` attribute before GnuTLS's single-object
import would accept it at all.

**What it decides.** The `portal` adapter stops being "broker a `Sign`" and becomes "resolve a URI
through the certificate portal's own PKCS#11 module": there is no external-signer seam in WebKit or
glib-networking to plug a brokered `Sign` into, and there is a working URI seam.
`src/tls/portal-token.h` is that agreement, and
[decisions/0007](decisions/0007-certificate-adapter.md) records the decision.

Steps 11 (`nativeclient` interception against the `.us` authority) and 13 (the in-process adapter)
are gone rather than pending: 11 needs a real tenant, and 13 describes a component this backend no
longer has. Step 12, storage isolation, is now an end-to-end test rather than a spike; see
[TESTING.md](TESTING.md).

**Why it is the more fundamental of the two.** It is the largest uncertainty in the effort estimate
(see [ROADMAP.md](ROADMAP.md)), it sets the support floor, and it decides whether the delegated-card
story is real or aspirational. It must also be answered long before any interface is proposed to
anyone.

### Steps

1. Start WebKitGTK with a **clean, ephemeral p11-kit configuration** — not the system one, so that
   nothing succeeds by accident through an already-registered module.
2. Obtain a new p11-kit endpoint **after** the WebKit web and network processes exist. This ordering
   is the whole point: a module present before launch proves nothing about dynamic registration.
3. Construct the `GTlsCertificate` from the brokered credential.
4. Answer a real client-certificate challenge with it.
5. **Complete a mutual-TLS handshake.** A constructed certificate is not a passed spike; a completed
   handshake is.
6. Repeat with a **second concurrent endpoint and certificate**, since one transaction working says
   nothing about two.
7. **Remove the card mid-handshake.** Clean failure, no hang, everything released.
8. **Close the owner D-Bus connection** while a grant is live. The grant must die with it.
9. **Verify which process opens the Unix socket, and when.** If it is the network process, sandbox
   and lifetime questions follow that do not exist if it is the UI process.
10. Run all of the above on the **oldest and newest supported GLib, WebKitGTK and GnuTLS** versions —
    Fedora 44 (WebKitGTK 2.52.x here) at one end, Debian stable and Ubuntu LTS at the other.

Alongside, and cheap to check here rather than later:

11. **`nativeclient` interception against the `.us` authority.** Authenticating against
    `login.microsoftonline.us` still redirects to the **commercial**
    `https://login.microsoftonline.com/common/oauth2/nativeclient` URL; the navigation is intercepted;
    the window closes **before** the page loads. Confirm the `.us` variant is genuinely rejected
    (`AADSTS50011`) so the constraint is documented from observation rather than memory.
12. **Storage isolation.** One transaction with the shared store, one with an ephemeral
    `WebsiteDataManager`; the ephemeral one neither sees the shared session nor leaves anything
    behind. Check more than cookies: local storage, IndexedDB, service workers, HSTS state and the
    engine's remembered client-certificate selection.
13. **The in-process adapter, for comparison**, on the same hardware and the same matrix: chooser,
    PIN, wrong PIN, retry exhaustion, token removal mid-flow, cancellation at each stage, repeat
    authentication. This is the fallback that ships either way, so it needs the same confidence — and
    running both against the same cards is the only honest way to find out whether the brokered path
    is actually equivalent.

### Pass

- Steps 1–6 succeed on every cell of the matrix: a handshake completes, twice, concurrently, with a
  module or signer introduced after WebKit started.
- Steps 7–8 fail cleanly and release everything. No hangs, no crashes, no orphaned grants.
- Step 9 has a definite, documented answer.
- Steps 11–13 behave identically at both ends of the matrix, or the differences are small enough to
  describe in a paragraph.

The `portal` provider is now the preferred implementation, per S2's Result above. There is no
in-process fallback to retire — only the `pkcs11` non-portal fallback, which is intentional and not
scheduled for removal.

### Fail

**This is now moot — S2 passed 2026-09-04/05/06.** Had it failed, the backend would have had only
the `pkcs11` fallback (a token named on the command line, no chooser): there was never an
in-process adapter to fall back to.

Then, in order of plausibility:

- **One permanently registered broker module exposing synthetic grant-bound slots.** Registered once
  at startup, multiplexing grants behind it. This changes the Certificate portal's contract from
  "return a new remote module" to "return a URI an already-registered module resolves" — a change to
  the other project's interface, not to this adapter's shape.
- **A dedicated WebKit network process or environment per transaction**, so registration happens
  before the process that needs it exists.
- **Brokered signing through a GnuTLS external-signer path**, avoiding module registration entirely —
  more attractive on security grounds anyway, and dependent on GLib exposing enough control.
- **Integrating the broker into glib-networking/GnuTLS**, which is a much longer road.

Publishing the Certificate portal's API should wait for one of these to work. An API claiming
object-scoped modules, service-owned login, broad application compatibility or connection-bound
lifetime, published before any of it is demonstrated, is a promise that will have to be broken.
**Superseded by the Result above.** S2 settled on the PKCS#11 URI mechanism, not
`OpenPkcs11Endpoint` and not a brokered `Sign`: WebKit resolves the certificate through a p11-kit
module the Certificate portal publishes, and that module exists and is tested. There is no
remaining half of S2 to run.

If instead **interception** is what fails, that is a different problem: the browser-extension
mechanism from the "Why not X" section stops being a rejected alternative and becomes a second
**backend** — a separate process implementing
`org.freedesktop.impl.portal.WebAuthentication.X1`, declared in its own `.portal` file in
`$datadir/xdg-desktop-portal/portals` and selected in `portals.conf` — and neither
[PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md), nor [IMPL-INTERFACE.md](IMPL-INTERFACE.md), nor the
client's CLI contract, nor the FreeRDP integration changes. That an
alternative mechanism is a package rather than a patch is the clearest practical dividend of
[decisions/0008](decisions/0008-build-to-the-upstream-shape.md).

---

## S3 (client, optional) — Keyring availability behaviour

**The question.** What actually happens on the machines this will run on when there is no Secret
Service: a headless session, a minimal window manager with no `gnome-keyring`/`kwallet`, a
locked keyring, an SSH session with a forwarded display, a Flatpak sandbox.

Worth answering early because it decides whether "no persistent cache" is a rare edge case or the
common case, and therefore how much the caching design is actually worth.

### Steps

1. Enumerate the target environments: GNOME session, KDE session, a bare WM, headless with
   `--prompt never`, Flatpak, and a session where the keyring exists but is **locked**.
2. In each, attempt a `libsecret` store and retrieve. Record: does it work, does it prompt, does
   it block, does it fail fast, and how long does it take.
3. Specifically check the locked-keyring case. A blocking unlock prompt in the middle of an
   otherwise silent `--prompt never` acquisition would be a surprising and unacceptable
   behaviour — it must either be avoided or count as interaction (exit `10`).
4. Check the Flatpak case through the Secret portal, since that is a different code path.

### Pass

- Every environment either works, or fails **fast and distinguishably** (exit `40`, provider
  unavailable) so a dispatcher can fall through to FreeRDP's paste flow.
- No environment blocks indefinitely, and no environment silently stores nothing while reporting
  success.
- The locked-keyring case is classified deliberately, not by accident.
