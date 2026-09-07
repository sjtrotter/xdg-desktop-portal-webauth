# Security

The security model for this project is in **[docs/SECURITY.md](docs/SECURITY.md)**. It covers the
two components here — the web authentication portal **backend** in `backend/`, and the Entra ID /
AVD token client in `clients/entra/` — and states, alongside them, the rules the **frontend**
enforces so that the backend's obligations make sense. That frontend is not in this repository: it
is xdg-desktop-portal, on the branch `experimental/certificate-webauthentication`; see
[docs/decisions/0010](docs/decisions/0010-backend-only-frontend-lives-upstream.md).

Two things worth saying here, where people look first.

**The interface is experimental and gated.** `org.freedesktop.portal.experimental.WebAuthentication`
is not exported unless xdg-desktop-portal was started with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`. Installing this backend on a machine
whose portal does not know the interface adds no attack surface: the `.portal` file names an
interface nothing matches, and the backend is never activated.

**Nothing is released and there is no deployment to report a vulnerability against.** The portal
backend and the Entra client are both implemented and have been run against a real Entra ID tenant
and real hardware ([docs/TESTING.md](docs/TESTING.md)). If you find a problem in the *design* or in
that code — and the interesting parts are the completion matcher (two implementations of one rule),
the impl boundary and its peer check, everything downstream of the app id the frontend derives, the
certificate portal's lifetime handling, and the OAuth response classifier in
`clients/entra/src/oauth/callback.c` — please open an issue.

When there is something running, this file will name a contact and a disclosure window.
