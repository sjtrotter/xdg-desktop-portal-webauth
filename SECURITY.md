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

This is a **design sketch**. Nothing is implemented, nothing is released, and there is no deployment
to report a vulnerability against. If you find a problem in the *design* — and the interesting parts
are the completion matcher (which has two implementations of one rule, only one of which is
written), everything downstream of the app id the frontend derives, the impl boundary itself, the
certificate adapter's lifetime handling, and the OAuth response classifier — please open an issue.

When there is something running, this file will name a contact and a disclosure window.
