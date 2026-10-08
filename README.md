# uione

> **0.6.21.** The language is young and designed in the open. The compiler and its
> libraries are released together; see [uione.io/releases](https://uione.io/releases)
> and [getting started](https://uione.io/language/start).

A way to write a whole application feature once: its data, rules, commands,
events, live views, permissions and screens, in one short file. Today the same
feature is spread over a dozen React, TypeScript and backend files that repeat
each other.

The syntax aims to be as plain as a script, in a shape a TypeScript developer
already reads: `//` and `/* */` comments, `{ }` around blocks and nothing else. It
has no semicolons, no commas between fields, no type annotations where the meaning
is already clear, and nothing said twice.

There is one architecture, and a feature never has to name it. Commands are the
only writes, and each publishes an event. Projections turn events into Firestore
view documents. Screens subscribe to those documents, so every screen is live.

## What it builds

`one build` turns a folder of `.one` files into projects built with the usual tools:
a React app on `@uione/react` (yarn), a Go service on the `one` library, a Pulumi
program, and the Firestore rules. Generated files are short enough to read, because
everything they would repeat lives in those libraries.

## Layout

```
compiler/        one, the compiler, in C++23 (make -C compiler test)
one/             the Go library generated backends are built on (go test, with the emulators)
infrastructure/  the Go library generated Pulumi programs are built on (go test, with mocks)
packages/        the libraries generated apps are built on, @uione/react and @uione/radix, and
                 the compiler in the browser, @uione/compiler and @uione/editor (yarn)
examples/        features written in uione: library uses every part of the language,
                 tasks is the smallest that needs sign-in
site/            uione.io, written in uione (see site/README.md)
editors/         highlighting for Vim, Neovim and VS Code (see editors/README.md)
docs/            decisions that shape uione, and why (docs/decisions/)
tools/           repository checks, run by the pre-commit hook and on every push to main
```

## License

Copyright 2026 Daher Alfawares. The compiler, tools and site are licensed under the
GNU Affero General Public License v3 (`LICENSE`). The libraries apps are built on,
`one/`, `infrastructure/`, `packages/react/` and `packages/radix/`, are licensed
under the GNU Lesser General Public License v3 (their own `LICENSE` and
`COPYING`). Code uione generates belongs to whoever generated it.
