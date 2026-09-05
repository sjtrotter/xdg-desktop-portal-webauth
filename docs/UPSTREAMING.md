# Upstreaming: the frontend is already upstream-shaped, and what is left

Status: **nothing has been proposed to anyone.** No issue has been opened, no pull request
exists, no maintainer has been contacted, and the branch this document is about is
local-only — nothing was forked and nothing was pushed. What has changed since the last
version is that the backend is now implemented and has been run against the branch's
frontend end to end ([TESTING.md](TESTING.md)), so the claim "this shape works" is an
observation rather than a design argument.

What changed since the previous version of this document is that "the frontend, if
accepted, would move into xdg-desktop-portal" stopped being a plan with a mapping table
attached. The frontend **is** in xdg-desktop-portal now, on a branch, in the
`experimental` namespace upstream set aside for portals in exactly this state. See
[decisions/0010-backend-only-frontend-lives-upstream.md](decisions/0010-backend-only-frontend-lives-upstream.md).

The point of this document is still that it is **short**. If it ever grows a section called
"and then restructure X", [decisions/0008](decisions/0008-build-to-the-upstream-shape.md)
has stopped paying for itself.

## Where the frontend is

```
repository   a local checkout of xdg-desktop-portal
remote       upstream → https://github.com/flatpak/xdg-desktop-portal.git
branch       experimental/certificate-webauthentication
base         upstream/main = c95490a  settings: include xdp-dex.h for the
                                      dex_scheduler_spawnv fallback
commits      661e441  doc: List the experimental portals in the interface reference
             3a32e9b  web-authentication: Add an experimental WebAuthentication portal  ← this one
             703fb22  certificate: Add an experimental Certificate portal
             aa1d697  session-dex: Add xdp_session_dex_close()
             3f46e3c  xdp: Add a gate for experimental portals
```

`3a32e9b` is the commit this repository tracks. `3f46e3c` is the
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` gate. `703fb22` is the Certificate portal the
`portal` certificate adapter calls — **on the same branch, in the same frontend process**,
which is what makes the delegation gap solvable at all
([decisions/0007](decisions/0007-certificate-adapter.md)).

Test results, from the branch write-up: `meson test` green on the whole suite,
`tests/test_webauthentication.py` 38 passed, `tests/test_certificate.py` 40 passed,
`gitlint --commits upstream/main..HEAD` passes, `black --check` passes.

## Why `experimental` is not a claim of acceptance

[PR #1889](https://github.com/flatpak/xdg-desktop-portal/pull/1889) ("Introduce
Credentials portal (experimental)") is where the mechanism was settled. Sebastian Wick,
2026-01-28, verbatim:

> As for the interface name, let's call it something like
> `org.freedesktop.portal.experimental.Credentials`. It should also not be exposed by
> default and have a environment variable to turn it on (e.g.
> `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=credentials`).

and Isaiah Inuwa, minutes later:

> I noticed the other portals have singular names: should we do that here too?
> `org.freedesktop.portal.experimental.Credential`

So: singular name, `experimental` infix, not exported by default, turned on by an
environment variable holding portal names. That is what an unfinished portal looks like
upstream, and it is what the branch implements. It carries no more standing than the old
`io.github.sjtrotter.*` names did — it just carries it in the place where the people whose
opinion matters can see it. `WebAuthentication` is not singular-vs-plural ambiguous, so
the naming note above did not force a change here.

## What this repository is now

An out-of-tree backend, plus an application:

| Here | What it is |
|---|---|
| `backend/src/main.c` | the D-Bus activated executable |
| `backend/src/webauthentication-impl.h`, `request-impl.h` | the impl skeleton, one file per portal interface |
| `backend/src/transaction.c`, `webkit-session.c`, `chrome.c`, `external-window.c`, `storage.c`, `completion.c`, `options.c`, `redact.c` | the window, the engine, the chrome, the partition, the matcher, the logging rules |
| `backend/src/tls/` | the certificate adapter and its two providers, `portal` and `pkcs11` |
| `backend/tests/` | the rules, tested with no display and no bus |
| `backend/data/webauth.portal.in` | `DBusName`, `Interfaces`, `UseIn`; installed into `$datadir/xdg-desktop-portal/portals` |
| `backend/data/org.freedesktop.impl.portal.desktop.webauth.service.in` | D-Bus activation |
| `backend/data/org.freedesktop.impl.portal.experimental.WebAuthentication.xml` | a **verbatim tracking copy** of the branch's file; deleted the day the branch lands and the file ships in xdg-desktop-portal's interfaces directory |
| `clients/entra/` | the Entra ID / AVD token client. **Never moves.** It is a consumer. |
| `tools/` | the fixture, the fixture identity provider, the private-bus stack, the Xvfb smoke test, the public-interface client |
| `spikes/` | `webkit-client-cert.c`, S2's answer |

What is *gone*, and was deleted rather than moved: `service/frontend/src/webauthentication.h`,
`request.h`, `session.h`, `app-info.h`, `portal-impl.h`, `main.c`, its interface XML, its
`.service.in`, and its `meson.build`. The previous version of this document listed four of
those five headers as "**Deleted.** Upstream has …". It was right, and the deletion has now
happened.

## What the branch's XML forced this repository to change

The interface is the frontend's, and where the branch disagreed with what this repository
had written down, the branch won. Recorded because a claim that "acceptance is a rename"
is only worth anything with its exceptions attached — and these are the exceptions.

The headline is how few there are on this side: **`Start`'s signature did not change**, on
either the public or the impl interface, and neither did the response codes, the option
vocabulary, the matching rule or the backend's structure.

| This repository said | The branch says | Why |
|---|---|---|
| `completion_uri` must be "absolute, `https` or an exactly named custom scheme, with no userinfo and no wildcard" | "absolute URI with a host, no userinfo and no wildcard" | The custom-scheme carve-out is not in the XML. A custom scheme with a host still parses, but "an exactly named custom scheme" is not a documented category any more |
| the `reason` symbols were this repository's list | fixed by the XML: impl side `timeout`, `no_display`, `no_engine`, `session_terminated`, `no_certificate_adapter`, `unrelated_certificate_challenge`; public side adds `no_backend`, `backend_disappeared`, `backend_completion_mismatch`, `backend_protocol_error` | Settled, and the split between the two lists is itself informative: a backend cannot report `backend_disappeared` about itself |
| `timeout` default and ceiling were prose | 300 default, 900 ceiling, always forwarded | Settled in code |
| `title` "length-limited" | ≤ 256 characters and single-line | Settled in code |
| the interface was ours to version | **experimental**: it can change or be removed without a version bump, and is not exported unless the gate is set | This is the largest change to what a consumer must expect |

And what the **Certificate** interface, on the same branch, forced on the certificate
adapter — which matters more here than anything in the paragraph above:

| `client_cert_portal.h` said | The branch says | Consequence |
|---|---|---|
| `CreateSession(a{sv}) → o session_handle` | `CreateSession(a{sv}) → o handle`, a **Request**; the session handle arrives in its `Response` | The adapter must subscribe before calling, and must not treat the return value as a session |
| `OpenPkcs11Endpoint(o session, a{sv}) → h fd, s, s, u` | **not on the interface at all** | The compatibility transport does not exist — and after [S2](SPIKES.md) it is not what was needed. WebKit resolves a client certificate from a **PKCS#11 URI** in its network process, so the seam is a permanently registered module the Certificate portal publishes, not an fd handed over per grant. `backend/src/tls/portal-token.h` is the resulting contract, and it is now the thing that needs agreeing between the two repositories |
| `context` carrying the challenging origin | **no such option** | The origin can only travel in `reason`, as application-supplied text |
| a `pkcs11_endpoint` capability bit | `GetCapabilities` has no such key | Gone from the adapter, along with the capability mask itself: a provider is available or it is not |
| brokered `Sign` would satisfy the handshake | the interface is unchanged, but WebKit has no seam for it | S2: there is no external-signer path and no `GTlsInteraction` on a `WebKitNetworkSession`. This is the one place where the branch's interface is *not* the constraint — the engine is |

## What remains before this could be a pull request

1. **Open the "new portals" discussion upstream first.** xdg-desktop-portal directs
   requests for new portals to an issue in `flatpak/xdg-desktop-portal`, with the question
   "what protected host resource is being mediated?" answered first. The strongest
   available answer here: **a trusted system authentication user-agent, persistent
   sign-in state, and client-certificate capability**, and it has to be argued rather than
   assumed. Nothing has been opened.
2. **Fix the commit trailer.** The branch's commits carry
   `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. `gitlint` passes on that,
   but `.gitlint.conf/co-authored-by-coding-agent.py` exists precisely to reject AI
   co-author trailers and asks for `Assisted-by: AGENT_NAME:MODEL_VERSION` instead. A real
   PR should use `Assisted-by: Claude:Fable-5.1`. This is noted in the branch write-up and
   is a rewrite-the-commits job, not a code change.
3. **A second, unrelated consumer.** [ROADMAP.md](ROADMAP.md) makes this a precondition,
   and no amount of correct plumbing substitutes for it. One consumer that is a sibling
   project by the same author is weaker evidence still.
4. **A second backend, and interest from another desktop.** The paste-only and
   system-browser backends the design keeps promising are the cheapest way to find out
   whether the impl interface is actually implementable by somebody who did not design it.
5. **Agreement on what the frontend enforces** — which is precisely the content of
   [IMPL-INTERFACE.md](IMPL-INTERFACE.md), and precisely the thing this repository took a
   position on early so that there is something concrete to disagree with.
6. **Conformance tests and documentation** before the interface stops being experimental.
7. **The branch's own open items**, which are not this repository's: nothing has been run
   against a real web engine or real hardware, and the python-dbusmock templates are the
   only implementations that have ever answered these interfaces.

## Retiring the experimental names

When and if a non-experimental `org.freedesktop.portal.WebAuthentication` ships, the
experimental one is **retired, not aliased**. A compatibility period in which both
interface names work would double the surface being reasoned about at exactly the moment
the security argument matters most, and this project has no deployed users to protect. The
`.portal` file, the D-Bus service file and the client's constants change in one commit.
