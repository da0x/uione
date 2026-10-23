# 0014: Everything is built once, tested, and released as it was tested

- Status: Accepted
- Date: 2026-10-23
- Deciders: Daher Alfawares

## Context

The checks built the same things again and again. The client was compiled on its
own, again inside the screens, the desktop toolkit and the terminal toolkit, and
again for the print shop's press; each test that needed a backend built it for
itself; and a release built the compiler from its tag, with flags no test had run
against, after the checks had passed on a different build. What was tested was not
what was shipped, and most of the checks' time went to building what was already
built.

## Decision

The checks have three stages, in order, and nothing is built after the first.

1. **Build**, once: the compiler for each platform as it's released, with its tests,
   which run on that build; the compiler for the browser; what the compiler writes;
   each generated backend; all of uione's C++ as one build, so each library is
   compiled once for everything that uses it; and the JavaScript packages and the
   site.
2. **Test** what was built. A test never builds; given nothing built, it says what
   to build and stops.
3. **Release** what was tested. A tag publishes the compiler the checks built and
   tested on that commit, for each platform, and the npm packages are published from
   the clone the checks built and tested, their `dist` as it was.

A platform that's released is a platform that's tested: the macOS build runs the
compiler's tests too.

## Consequences

The checks take as long as their longest build plus their tests, not every build
added up. A release can't ship a build that wasn't tested, and it waits for the
checks on its commit to pass.

Tools that used to build what they test now need it built first, so running one by
hand is two steps, and the tool says which.

The client is still built twice in GitHub's checks: once on Ubuntu, for the tests
that run against the emulators, and once on Arch, inside the toolkits' build, whose
g++ has the C++26 they need. Those are two platforms' builds, not one built again. A build for a platform that isn't tested isn't released.
