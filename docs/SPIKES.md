# Go / no-go spikes

Status: design sketch. None of these have been run.

Two questions decide whether this project is worth building as described — one per layer. Both are
answerable in days, with code that already exists, and both must be answered **before** any
polished work starts. A third, smaller question is worth answering early because it changes the
packaging story.

| | Layer | Question | Decides |
|---|---|---|---|
| **S1** | 3, the Entra client | Can a refresh token mint a PoP token for a new key, silently? | The product promise |
| **S2** | 2, the web auth service | Can WebKit complete mutual TLS with a certificate brokered by the smart card service? | The support floor, and whether the preferred certificate path exists at all |
| **S3** | 3, the Entra client | What happens when there is no keyring? | How much the caching design is worth |

S2 is the one that decides whether *layer 2* is buildable as described, so it is the more
fundamental of the two; S1 decides what the *client* can promise. Run them in parallel if there are
two people; run S2 first if there is one, because a service that cannot do cards is not worth a
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
no point building an authentication service to make one card prompt per connection if the answer
is two — though note that S2's outcome is what decides whether the service exists at all, and a
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
   and confirm it is accepted. A token that parses is not the same as a token the service takes.
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

- The promise becomes **one card interaction per *token*, mitigated by the service's `shared`
  session store** — the second acquisition still opens a transaction, but the user should not have
  to re-present the card if the Entra session cookie is still valid. That mitigation becomes
  load-bearing, and its lifetime must itself be measured: how long does the Entra session cookie
  survive, and does Conditional Access shorten it?
- Caching remains worth building (it still saves the *first* interaction on reconnect), but the
  README and [ENTRA-CLIENT-CLI.md](ENTRA-CLIENT-CLI.md) must not promise silent PoP acquisition.
- The service's persistent shared data store moves from "nice to have" to "required", and
  `session_mode: shared` stops being a convenient default and becomes something the client
  depends on — which in turn raises the stakes on everything in
  [SECURITY.md](SECURITY.md) about shared state amplifying a malicious caller.

### Partial

If it works for commercial but not Government, or works but only within a short window after the
interactive sign-in, record the exact boundary. A promise with a documented limit is fine; an
undocumented one is not.

---

## S2 (service) — Can WebKit complete a mutual-TLS handshake with a brokered certificate?

**The question.** The preferred certificate path takes the chooser and the PIN out of this service
and into the smart card service. Whether that is *possible* comes down to one unproven step:

> Can a `GTlsCertificate` built from a credential the smart card service brokered — a p11-kit
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

Until this passes, the service keeps the in-process adapter and does **not** hard-depend on the
smart card service ([decisions/0007-certificate-adapter.md](decisions/0007-certificate-adapter.md)).

**This is a joint spike** with the smart card service's repository: that project must be able to
produce an endpoint or signer at all before this one can consume it. Stand in for it with a
hand-run `p11-kit server` for the first pass — worth doing regardless, because it isolates whether a
failure is in the producing or the consuming.

**Why it is the more fundamental of the two.** It is the largest uncertainty in the effort estimate
(see [ROADMAP.md](ROADMAP.md)), it sets the support floor, and it decides whether the three-layer
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

Then the `portal` adapter becomes the preferred implementation, and retiring the in-process fallback
becomes a scheduled decision rather than an aspiration.

### Fail

The service ships with the **in-process adapter** and works. That is the point of having built the
adapter, and it is why this failure is survivable rather than fatal.

Then, in order of plausibility:

- **One permanently registered broker module exposing synthetic grant-bound slots.** Registered once
  at startup, multiplexing grants behind it. This changes the smart card service's contract from
  "return a new remote module" to "return a URI an already-registered module resolves" — a change to
  the other project's interface, not to this adapter's shape.
- **A dedicated WebKit network process or environment per transaction**, so registration happens
  before the process that needs it exists.
- **Brokered signing through a GnuTLS external-signer path**, avoiding module registration entirely —
  more attractive on security grounds anyway, and dependent on GLib exposing enough control.
- **Integrating the broker into glib-networking/GnuTLS**, which is a much longer road.

Publishing the smart card service's API should wait for one of these to work. An API claiming
object-scoped modules, service-owned login, broad application compatibility or connection-bound
lifetime, published before any of it is demonstrated, is a promise that will have to be broken.

If instead **interception** is what fails, that is a different problem: the browser-extension
mechanism from the "Why not X" section stops being a rejected alternative and becomes a second
browser session implementation behind `browser_session.h`, and neither
[SERVICE-INTERFACE.md](SERVICE-INTERFACE.md), nor [ENTRA-CLIENT-CLI.md](ENTRA-CLIENT-CLI.md), nor the
FreeRDP integration changes.

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
