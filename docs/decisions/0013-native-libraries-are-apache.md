# 0013: The libraries native apps are built on are Apache-2.0

- Status: Accepted
- Date: 2026-10-12
- Deciders: Daher Alfawares
- Extends: [0004](0004-licenses.md)

## Context

`one build` writes desktop and terminal apps as well as web ones, and those are
native programs: the libraries they're built on are linked into a single binary
that's handed to people, not served. The LGPL works for the web's libraries, which
are bundled as they are; a native app linked statically against an LGPL library
has to let its users relink it, which most makers of desktop software won't sign
up for. Markdown was GPL-3.0 at first, which made every distributed app that shows
Markdown GPL, including a proprietary one.

## Decision

**`desktop/` (`one::desktop` and `one::desktop::runtime`), `terminal/`
(`one::terminal`), `client/` (`one::client`) and `markdown/` (`one::markdown`) are
Apache-2.0.** Any program may use them, under any license, as long as it keeps
their notices. Each carries the Apache license in `LICENSE`.

Everything else in 0004 stands: the compiler, tools and site are AGPL-3.0-only, the
four web libraries LGPL-3.0-only, and generated code is its maker's.

## Consequences

A desktop or terminal app uione writes can be shipped under its maker's own
license, proprietary or not, whatever its screens show. Changes to these libraries
needn't be shared back, which the LGPL would have asked; they're small, and the
compiler that writes the apps stays AGPL. `tools/check-license` holds each of these
folders to Apache-2.0.
