# 1. Standalone processes, not a library and not a client feature

Date: 2026-09-03
Status: accepted (for the sketch)

## Context

Connecting to Azure Virtual Desktop requires two Entra ID access tokens per connection, and
obtaining them on a smart-card tenant requires a browser that can answer a TLS client-certificate
challenge and collect a PKCS#11 PIN. Four candidate homes for that work were considered.

**In `libfreerdp-client`.** FreeRDP already has a minimal OAuth implementation there (an
authorization-URL builder, a terminal paste flow, a token exchange). Extending it was attempted:
the resulting change was roughly 2,300 lines, and the maintainer's position is that FreeRDP
should not grow that implementation. That position is defensible on its own terms — an embedded
browser, a certificate chooser, a PIN prompt and a keyring integration are not RDP protocol
functionality, and they would become a dependency burden for every packager of the library.

**In Remmina.** The chooser and PIN prompt already work there. But it strands KRDC, sdl-freerdp,
gtk-frdp and anything else behind the same missing UI, and it buries a desktop identity capability
inside one client's RDP plugin.

**As a shared library** that clients link. This still forces every client to link WebKit, p11-kit
and libsecret, forces a toolkit choice on callers, and puts refresh tokens in the client's address
space — which means the credential boundary is only as good as the least careful client.

**As a standalone per-user process** with a narrow, versioned interface.

The `nativeclient` redirect constraint (see [0002](0002-no-loopback-redirect.md)) rules out any
design that delegates to the user's own browser, so *something* local must host a web view. The
question is only what that something is.

## Decision

Build standalone, per-user processes in their own project, with versioned contracts designed as
IPC from day one.

(This decision predates the split into two layers recorded in
[0005](0005-service-shape.md) and [0006](0006-two-repositories.md). What follows is the reasoning
for *not putting this work inside FreeRDP or inside one RDP client*, and it still applies to both
layers; the boundary between them is 0005's subject, not this one's.)

The boundary:

> FreeRDP describes the token it needs — authority, tenant, client id, scope, and for
> proof-of-possession requests the `req_cnf` for a key it generated itself — and consumes the
> returned access token. Everything between — OAuth, browser hosting, certificate selection, PIN
> entry, redirect interception, account state, refresh tokens and caching — happens outside
> FreeRDP, in processes of our own.

The first integration uses FreeRDP's existing `GetCommonAccessToken` seam and requires **no
FreeRDP changes**: a frontend saves the current callback, installs one that invokes the client,
and chains to the saved one on decline — exactly the pattern `sso-mib` already uses.

## Consequences

**Good.**

- One implementation of the hard part serves every client. Remmina's proven code becomes the
  input to a shared component instead of a feature one client has.
- The credential boundary is a process boundary, enforced by the OS at UID granularity, rather
  than a discipline every linking client has to maintain.
- Clients need no new dependencies. A caller needs `fork`/`exec` and a pipe.
- The toolkit question disappears: a separate process may use GTK/WebKitGTK regardless of whether
  the caller is GTK, Qt or SDL.
- FreeRDP does not grow an OAuth stack, which is what its maintainer asked for.

**Costs.**

- A process spawn per token acquisition, and two acquisitions per connection. Acceptable: the
  interactive path is already dominated by a human and a card.
- More things to package, install and version. A client without them installed must degrade
  gracefully — hence exit code `40` (provider unavailable) and the retained terminal paste flow.
- The contracts are now public and must be kept. That is the point of
  [ENTRA-CLIENT-CLI.md](../ENTRA-CLIENT-CLI.md), of
  [SERVICE-INTERFACE.md](../SERVICE-INTERFACE.md), and of their version numbers.
- The token client is one-shot, so per-account serialization has to be a lock in the runtime
  directory rather than a queue in memory. The related worry — keeping a browser session warm
  across connections — turned out to belong to layer 1, which is D-Bus-activated and long-lived
  anyway. See [ARCHITECTURE.md](../ARCHITECTURE.md).
