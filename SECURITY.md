# Security Policy

## Reporting a vulnerability

Please report vulnerabilities privately to the project maintainers rather than
opening a public issue. Include affected versions, reproduction steps, and the
possible impact. Do not include secrets or personal data in reports.

## Trust boundary

Story packages are untrusted input. They must pass structural validation,
semantic validation, and (in later phases) simulation before publication.
Generated content never executes code. API keys must be supplied through the
host environment and must not appear in packages, logs, tests, or commits.

The Phase 1 loader places a 16 MiB limit on an input JSON file. Hosts accepting
content from untrusted users should add their own filesystem and process limits.
