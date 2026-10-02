# Security

## Reporting a vulnerability

Please report security problems privately, not in a public issue.

Until neotrac.org is running, report them through GitHub: open the repository's
**Security** tab and choose **Report a vulnerability**. Only the maintainer sees the
report. You can also email security@uione.io.

Once neotrac.org is running, reports go to uione's private security project there,
where only its maintainers and the person who reported each issue can read it.

A useful report says what is affected (the compiler, one of the libraries, or a
generated app), the version, how to reproduce it, and what an attacker gains.

## What happens next

You'll hear back once the report has been read. The problem is fixed privately,
a release is published, and the report is made public after the fix is out, with
credit to you unless you'd rather not be named.

## Supported versions

Fixes go into the latest release only. uione is in its 0.x series: the compiler
and its four libraries (`one`, `infrastructure`, `@uione/react`, `@uione/radix`)
are released together under one version.

## Scope

In scope: the compiler, the libraries, the code the compiler generates, and the
Firestore rules it writes. The hosted studio and hosting on uione.io are run
separately; report problems with them the same way.
