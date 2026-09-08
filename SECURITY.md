# Security

The security model for this project is in **[docs/SECURITY.md](docs/SECURITY.md)**. It covers the
web authentication portal **backend** in this repository, and states alongside it the rules the
**frontend** enforces so that the backend's obligations make sense. That frontend is not here: it is
xdg-desktop-portal, on the branch `experimental/integration`; see
[docs/decisions/0010](docs/decisions/0010-backend-only-frontend-lives-upstream.md). The Entra ID /
AVD token client that first used this backend is in its own repository,
[entra-token-helper](https://github.com/sjtrotter/entra-token-helper); its own rules are kept in
part 2 of that document until it has a security document of its own.

Two things worth saying here, where people look first.

**The interface is experimental.** `org.freedesktop.portal.WebAuthentication.X1` is exported on
`/org/freedesktop/portal/desktop/experimental`, and only when a backend for it is configured.
Installing this backend on a machine whose portal does not know the interface adds no attack
surface: the `.portal` file names an interface nothing matches, and the backend is never
activated.

**Nothing is released and there is no deployment to report a vulnerability against.** The backend is
implemented and has been run against a real Entra ID tenant and real hardware
([docs/TESTING.md](docs/TESTING.md)). If you find a problem in the *design* or in that code — and
the interesting parts are the completion matcher (two implementations of one rule), the impl
boundary and its peer check, everything downstream of the app id the frontend derives, and the
certificate portal's lifetime handling — please open an issue.

When there is something running, this file will name a contact and a disclosure window.
