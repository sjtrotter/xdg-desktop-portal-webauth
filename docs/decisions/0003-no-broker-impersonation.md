# 3. Do not impersonate the Microsoft Identity Broker

Date: 2026-09-03
Status: accepted

## Context

`sso-mib` already exists, is already integrated in FreeRDP behind `WITH_SSO_MIB`, and already
obtains AVD tokens. It does so by talking over D-Bus to the Microsoft Identity Broker, a service
Microsoft ships for Intune-enrolled Linux devices which holds a device-bound primary refresh
token (PRT).

The tempting shortcut is obvious: implement the broker's D-Bus name and object paths in this
token client. Then every existing `sso-mib` caller — including FreeRDP as it stands today, with no
integration work at all — would silently start using us on non-enrolled devices.

The apparent justification is that MS-OAPXBC is a published Microsoft specification. It is not a
justification. MS-OAPXBC specifies the *OAuth protocol extensions* a broker client uses and the
associated token exchanges; it does not define Microsoft's Linux D-Bus service ABI. `sso-mib`
itself carries its own D-Bus descriptions, reverse-engineered rather than standardised, and its
own public API is not guaranteed stable before 1.0.

## Decision

Do not take, implement, or shadow Microsoft's Identity Broker D-Bus name. Use a new, narrowly
scoped interface — for the token client, the contract in
[ENTRA-CLIENT-CLI.md](../ENTRA-CLIENT-CLI.md); for web authentication,
`org.freedesktop.portal.experimental.WebAuthentication`, summarised in
[PUBLIC-INTERFACE.md](../PUBLIC-INTERFACE.md) and defined by the xdg-desktop-portal branch
([0010](0010-backend-only-frontend-lives-upstream.md)). Note that this ADR's argument is about
*impersonating somebody else's* bus name, and is untouched by which namespace our own interface
ends up in.

## Consequences

**Why the shortcut is refused.**

- **There is no contract to implement.** The local D-Bus ABI is not what MS-OAPXBC standardises,
  so "implementing the broker" means implementing today's observed behaviour of a service we do
  not control. Microsoft can change activation, object paths, methods or payload conventions
  without notice or breakage on their side.
- **The name asserts something untrue.** A caller reaching the Microsoft broker may reasonably
  infer device enrollment, compliance state, a PRT, and hardware-backed key properties. A generic
  browser flow on a non-enrolled device has none of those. Conditional Access policies are
  written against exactly those properties. Answering as the broker is a lie told to a security
  control.
- **A generic browser flow and an enrolled-device PRT broker are different providers.** They
  differ in what they can promise, what they can refresh silently, and what happens when policy
  tightens. Collapsing them behind one name makes both harder to reason about.
- **Collision.** If a real broker is later installed — and on a managed fleet it will be — two
  services contend for one bus name. Whichever wins, the user gets a coin flip between two
  different identity providers, and diagnosing it means knowing that we did this.

**Costs accepted.**

- Integration work is required that impersonation would have avoided: a callback shim in each
  client, or the typed provider API proposed in [ROADMAP.md](../ROADMAP.md).
- Two providers exist rather than one, and something has to dispatch between them. That is the
  right shape: try the broker first (it is better where it applies, because a PRT is stronger than
  a browser session), fall through to this token client, fall through to the frontend callback, fall
  through to the terminal paste flow. Each step is a deliberate degradation, visible in the exit
  code.

**A related caution, which turned out to apply twice.** The same reasoning governs naming
generally: nothing here should be named or described in a way that implies an endorsement or a
semantics it does not have. That is the argument against impersonating Microsoft's broker, and it is
exactly the argument in [0005](0005-service-shape.md) against shipping an
`org.freedesktop.portal.*` name before anyone has accepted one. The token client is a browser-based
sign-in path for non-enrolled devices; the service is an incubating project-controlled interface;
the documentation should keep saying both.
