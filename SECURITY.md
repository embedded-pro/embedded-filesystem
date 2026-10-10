# Security Policy

## Supported Versions

The [latest](https://github.com/embedded-pro/embedded-filesystem/releases/latest) version of embedded-filesystem is supported with security updates.

## Reporting a Vulnerability

If you find a significant vulnerability, or evidence of one, please report it privately.

Vulnerabilities should be reported using [GitHub's mechanism for privately reporting a vulnerability](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing/privately-reporting-a-security-vulnerability#privately-reporting-a-security-vulnerability). Under the [repository's security tab](https://github.com/embedded-pro/embedded-filesystem/security), click "Report a vulnerability" to open the advisory form.

A maintainer of embedded-filesystem will triage the reported vulnerability and if the vulnerability is accepted a security advisory will be published and all further communication will be done via that security advisory.

Malformed storage contents are an attack surface: a filesystem that crashes, loops or overruns a buffer when it mounts a corrupted or crafted SD card or flash image is a vulnerability, and should be reported this way.
