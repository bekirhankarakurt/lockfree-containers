# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| 1.x     | Yes       |
| < 1.0   | No        |

## Reporting a vulnerability

Please do **not** open a public issue for security problems.

Report privately through GitHub's
[private vulnerability reporting](https://github.com/bekirhankarakurt/lockfree-containers/security/advisories/new)
and include:

- affected version or commit,
- a minimal reproducer (container type, element type, thread layout),
- the observed behaviour (crash, data race, lost or duplicated element) and
  the sanitizer output if available.

You can expect an acknowledgement within 7 days and a fix or mitigation plan
within 30 days. Credit is given in the changelog unless you prefer otherwise.

## Scope

Memory-safety bugs, data races, and violations of the documented progress or
ordering guarantees (for example an element delivered twice or lost) are all
treated as security-relevant, because these containers are intended for
safety- and mission-critical software.
