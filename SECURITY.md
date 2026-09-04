# Security

The security model for this project is in **[docs/SECURITY.md](docs/SECURITY.md)**. It covers all
three components: the web authentication portal's frontend in `service/frontend/` and its reference
backend in `service/backends/gtk/`, and the Entra ID / AVD token client in `clients/entra/`.

This is a **design sketch**. Nothing is implemented, nothing is released, and there is no deployment
to report a vulnerability against. If you find a problem in the *design* — and the interesting parts
are the completion matcher (which now has two implementations of one rule), app-id derivation and
everything downstream that trusts it, the frontend/backend boundary itself, the certificate
adapter's lifetime handling, and the OAuth response classifier — please open an issue.

When there is something running, this file will name a contact and a disclosure window.
