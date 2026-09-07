# Architecture decision records

0001 to 0003 are in the client repository,
[entra-token-helper](https://github.com/sjtrotter/entra-token-helper), which they belong to since
the split of 2026-09-07 ([0006](0006-two-repositories.md)); 0009 was never written.

| | |
|---|---|
| [0004](0004-license.md) | LGPL-2.1-or-later |
| [0005](0005-service-shape.md) | URL in, completion out: no protocol semantics in the authentication service |
| [0006](0006-two-repositories.md) | One repository now, two repositories at the first tagged interface release |
| [0007](0007-certificate-adapter.md) | Certificate handling behind an adapter: portal preferred, in-process retained until proven |
| [0008](0008-build-to-the-upstream-shape.md) | Build to the upstream shape now: a portal frontend and a portal backend |
| [0010](0010-backend-only-frontend-lives-upstream.md) | Be a backend only: the frontend lives in xdg-desktop-portal |
