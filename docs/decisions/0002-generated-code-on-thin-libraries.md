# 0002: Generated code stays short, on thin libraries

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

`one build` turns `.one` files into a React app, a Go service, Firestore rules and
a Pulumi program. Long, self-contained output is as hard to read as the code uione
replaces, and people don't trust code they can't read. Interpreting `.one` files at
runtime is worse: when something breaks, there's no code to look at.

## Decision

The compiler writes ordinary code and keeps it short by putting everything that
repeats into libraries:

- `one`, in Go: commands, sign-in and permission checks, validation, storage,
  events, views, history and webhooks.
- `infrastructure`, in Go: every cloud resource a project runs on.
- `@uione/react`: live views, commands, sign-in, routing, and the screen
  components (`Table`, `Form`, `Confirm` and the rest), drawn by a component set.
- `@uione/radix`: the component set, written for uione on Radix primitives and
  Tailwind, shipped with its own `styles.css`. Nothing is copied from shadcn/ui or
  anywhere else (0004). A project can choose another set with `ui`.

## Consequences

A generated file is about as long as the `.one` it came from, and can be read in a
minute. The examples in `site/target/` are the specification the generators are
tested against.

When the same code shows up in several generated files, it belongs in a library.

The libraries are public and every generated project depends on them, so their
APIs change with care. Generated files are rewritten on every build; changes go in
the `.one` file, a library, or the compiler.
