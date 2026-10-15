# Upstreaming: the frontend is already upstream-shaped, and what is left

Status: **nothing has been proposed to anyone.** No issue has been opened, no pull request
exists, and no maintainer has been contacted beyond two comments the author posted on
2026-09-05 (flatpak/xdg-desktop-portal#662 and FreeRDP#13328). The branch this document is
about lives in the author's checkout of https://github.com/sjtrotter/xdg-desktop-portal, and
the backend has been run against it end to end ([TESTING.md](TESTING.md)), so the claim
"this shape works" is an observation rather than a design argument.

The frontend is no longer a plan with a mapping table attached: it **is** in
xdg-desktop-portal, on a branch, under upstream's experimental-portal convention
([decisions/0010-backend-only-frontend-lives-upstream.md](decisions/0010-backend-only-frontend-lives-upstream.md)).

The point of this document is still that it is **short**. If it ever grows a section called
"and then restructure X", [decisions/0008](decisions/0008-build-to-the-upstream-shape.md)
has stopped paying for itself.

## Where the frontend is

```
repository   a local checkout of xdg-desktop-portal
remote       upstream → https://github.com/flatpak/xdg-desktop-portal.git
             origin   → https://github.com/sjtrotter/xdg-desktop-portal.git
branch       experimental/integration
base         upstream/main = 8eba842
public XML   data/org.freedesktop.portal.experimental.WebAuthentication1.xml
impl XML     data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml
```

The frontend branch hosts WebAuthentication and Certificate in one process. The backend's
`portal` certificate adapter calls `org.freedesktop.portal.experimental.Certificate1` on
`org.freedesktop.portal.Desktop` at `/org/freedesktop/portal/desktop`.

The convention is documented in the frontend's `doc/experimental-portals.rst`:

- Frontend interfaces use `org.freedesktop.portal.experimental.<Name><N>`; backend interfaces
  use `org.freedesktop.impl.portal.experimental.<Name><N>`.
- Both use `/org/freedesktop/portal/desktop`. Request and Session handles retain their own paths.
- Breaking changes increment the trailing number; non-breaking changes increment `version`.
- The frontend requires a configured backend and the portal name in
  `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` at startup. For both portals, set
  `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=WebAuthentication1,Certificate1` in the frontend's
  environment. Names are comma separated, with no spaces. Preserve existing names and append
  only missing names.

**What this interface lost when the branch was rewritten**: the AVD vocabulary. The generic
XML no longer names `no_certificate_adapter` or `unrelated_certificate_challenge`, which are
certificate concepts in a portal that has nothing to do with certificates; this backend emits
`credential_unavailable` for the first and keeps the second as one of its own additions. The
identity option is `app_identity_level` with `sandboxed` / `host` / `unidentified`, shared
with the Certificate interface, which used to have its own three names for the same
distinction. The completion rule now states the equivalences the URI parser applies, and a
wildcard host is refused rather than matched literally. A private-use scheme redirect URI
(RFC 8252 section 7.1), the shape a native OAuth client registers, is accepted and matched
on its scheme and its path, as [PUBLIC-INTERFACE.md](PUBLIC-INTERFACE.md) sets out; this
backend's `completion.c` carries the same rule and `tests/test-completion.c` runs the
frontend's cases against it.

**The deadline is now the frontend's.** It races the backend call against a timeout, and
when the timeout wins it calls `Close()` on the impl `Request` and answers the application
`2` with `reason` `timeout`. This backend still owns the window and its own deadline
([IMPL-INTERFACE.md](IMPL-INTERFACE.md)); whichever end reaches it first, the window goes
away. **And a sandboxed
application with no network access can no longer call `Start()` at all**: the frontend
refuses it with `org.freedesktop.portal.Error.NotAllowed`, because the response to that
call is an answer fetched from the network.

Test results: `meson test --suite integration --suite unit` green upstream except a
pre-existing `usb` failure (`umockdev-run` is not installed there),
`tests/test_webauthentication.py` 70 passed, `tests/test_certificate.py` 98 passed,
`gitlint --commits upstream/main..HEAD` passes, `black --check` passes.

## Experimental status

The experimental-portal mechanism is merged upstream (PR #2129, fixed by #2173).
WebAuthentication and Certificate remain on the `experimental/integration` branch.
The experimental namespace carries no stability guarantee or claim of interface acceptance.

## What this repository is now

An out-of-tree backend, and nothing else:

| Here | What it is |
|---|---|
| `src/main.c` | the D-Bus activated executable |
| `src/webauthentication-impl.h`, `request-impl.h` | the impl skeleton, one file per portal interface |
| `src/transaction.c`, `webkit-session.c`, `chrome.c`, `external-window.c`, `storage.c`, `completion.c`, `options.c`, `redact.c` | the window, the engine, the chrome, the partition, the matcher, the logging rules |
| `src/tls/` | the certificate adapter and its two providers, `portal` and `pkcs11` |
| `tests/` | the rules, tested with no display and no bus |
| `data/webauth.portal.in` | `DBusName`, `Interfaces`, `UseIn`; installed into `$datadir/xdg-desktop-portal/portals` |
| `data/org.freedesktop.impl.portal.desktop.webauth.service.in` | D-Bus activation |
| `data/org.freedesktop.impl.portal.experimental.WebAuthentication1.xml` | a **verbatim tracking copy** of the branch's file; deleted the day the branch lands and the file ships in xdg-desktop-portal's interfaces directory |
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
| the `reason` symbols were this repository's list | fixed by the XML: impl side `timeout`, `no_display`, `no_engine`, `user_cancelled`, `session_terminated`, `credential_unavailable`; public side adds `backend_disappeared`, `backend_completion_mismatch`, `backend_protocol_error` | Settled, and the split between the two lists is itself informative: a backend cannot report `backend_disappeared` about itself. The list is generic on purpose: `no_certificate_adapter` named a piece of this backend |
| `timeout` default and ceiling were prose | 300 default, 900 ceiling, always forwarded | Settled in code |
| `title` "length-limited" | ≤ 256 characters and single-line | Settled in code |
| interface versioning and export | **experimental**: breaking changes increment the trailing number; export on `/org/freedesktop/portal/desktop` requires a configured backend and frontend opt-in | Consumers must handle an unavailable interface |

And what the **Certificate** interface, on the same branch, forced on the certificate
adapter — which matters more here than anything in the paragraph above:

| `client_cert_portal.h` said | The branch says | Consequence |
|---|---|---|
| `CreateSession(a{sv}) → o session_handle` | `CreateSession(a{sv}) → o handle`, a **Request**; the session handle arrives in its `Response` | The adapter must subscribe before calling, and must not treat the return value as a session |
| `OpenPkcs11Endpoint(o session, a{sv}) → h fd, s, s, u` | **not on the interface at all** | The compatibility transport does not exist — and after [S2](SPIKES.md) it is not what was needed. WebKit resolves a client certificate from a **PKCS#11 URI** in its network process, so the seam is a permanently registered module the Certificate portal publishes, not an fd handed over per grant. `src/tls/portal-token.h` is the resulting contract, and it is now the thing that needs agreeing between the two repositories |
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
2. ~~**Fix the commit trailer.**~~ **Done.** All ten commits carry
   `Assisted-by: Claude:claude-fable-5-1`, which is what
   `.gitlint.conf/co-authored-by-coding-agent.py` asks for instead of a `Co-Authored-By`
   AI trailer.
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
7. **The branch's own open items**, which are not this repository's: it has been run
   against a real web engine and real hardware (WebKitGTK 2026-09-04/05, live Entra ID
   2026-09-05), but only by this repository's backend, not by an independent
   implementation.

## Retiring the experimental names

When and if a non-experimental `org.freedesktop.portal.WebAuthentication` ships, the
experimental one is **retired, not aliased**. A compatibility period in which both
interface names work would double the surface being reasoned about at exactly the moment
the security argument matters most, and this project has no deployed users to protect. The
`.portal` file, the D-Bus service file and the client's constants change in one commit.
