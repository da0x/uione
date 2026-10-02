# 0004: The compiler is AGPL, the libraries are LGPL, and the code is original

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

uione is a language whose libraries go into every app built with it, and
uione.io is to sell hosting for those apps. People won't adopt a language they
can't read or keep using, and businesses won't build on one that claims their
apps. A competitor could also take uione, improve it, and run it as a rival hosted
service.

## Decision

- **The compiler, tools, editor support, examples and site are AGPL-3.0-only.**
  Anyone who runs a changed uione for others over a network has to offer the
  source of their changes.
- **The four libraries apps are built on, `one`, `infrastructure`, `@uione/react`
  and `@uione/radix`, are LGPL-3.0-only.** An app can use them as they are
  without becoming LGPL itself; changes to the libraries are shared. Each library
  carries the LGPL in `LICENSE` and the GPL it builds on in `COPYING`.
- **Code uione generates belongs to whoever generated it.** The generators never
  add this repository's copyright or license to it.
- **The studio and hosting are proprietary,** in separate private repositories.
  Copyright is held by Daher Alfawares, who isn't bound by these licenses in his
  own products.

Every source file starts with two lines, its copyright and its license:

```
// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only
```

`tools/check-license` checks each file's license by where it lives, and fails on a
kind of file it doesn't know.

Nothing is copied in from another codebase, not code, text or examples, because
anything copied brings someone else's copyright with it.

## Consequences

Apps built with uione stay their makers', and a hosted copy of uione has to share
what it changes.

Outside contributions are accepted only with a contributor agreement that lets the
copyright holder relicense them, so a later change of license, or a commercial
license beside these, stays possible.

The libraries are linked into apps that are shipped as single bundles. Before
charging customers, the licensing should be reviewed by a lawyer.
