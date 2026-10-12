# 0013: Everything in uione is AGPL-3.0-only

- Status: Accepted
- Date: 2026-10-12
- Deciders: Daher Alfawares
- Supersedes: the libraries' licenses in [0004](0004-licenses.md)

## Context

uione had four licenses: AGPL for the compiler and site, LGPL for the web
libraries, Apache for the native toolkits, and GPL for Markdown. Each line between
them was a decision to keep making and a header to get right.

## Decision

**Everything in this repository is AGPL-3.0-only**: the compiler, tools, editor
support, examples and site, and every library apps are built on, web and native
alike. Each folder carrying a `LICENSE` carries the AGPL.

What 0004 says otherwise stands: code uione generates belongs to whoever generated
it, and the generators never add this repository's copyright or license to it;
the studio and hosting, the cloud part, are proprietary, in a private repository of
their own; and nothing is copied in from another codebase.

`client/include/ember.hpp` is a pinned copy of libember, another project, checked
by its hash, so it keeps that project's own license.

## Consequences

One license, one header, one rule in `tools/check-license`. An app built with uione
links its libraries, so an app that's distributed, or served to others over a
network, does so under the AGPL's terms, unless its maker has a license of its own
from the copyright holder, who isn't bound by these licenses and can grant one.
