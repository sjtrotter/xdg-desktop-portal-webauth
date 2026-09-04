# Security

The security model for this project is in **[docs/SECURITY.md](docs/SECURITY.md)**. It covers both
components: the web authentication service in `service/` and the Entra ID / AVD token client in
`clients/entra/`.

This is a **design sketch**. Nothing is implemented, nothing is released, and there is no deployment
to report a vulnerability against. If you find a problem in the *design* — and the interesting parts
are the completion matcher, caller-identity resolution, the certificate adapter's lifetime handling,
and the OAuth response classifier — please open an issue.

When there is something running, this file will name a contact and a disclosure window.
