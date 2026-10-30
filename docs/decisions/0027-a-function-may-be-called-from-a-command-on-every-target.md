# 0027: A function may be called from a command, on every target

- Status: Accepted
- Date: 2026-10-30
- Deciders: Daher Alfawares
- Extends: [0026](0026-a-function-says-its-parameters-types.md)

## Context

A function could only order a view, and only on the cloud target; a project that
says `storage local` refused one outright. Work a command does in several steps,
like a sum worked out the same way in two commands, or anything that repeats until
it's done, had to be written out in each command, or not at all.

## Decision

**A function is called from a command as from a view, on every target**: the Go
backend, and the browser and the desktop for `storage local`.

Its body, `if` and `return` as before, may now use:

- arithmetic and comparisons, as a command's body does, `/` and `%` worked out alike
  everywhere;
- calls of other functions the namespace declares, and of itself, which is how a
  function repeats;
- `return match way { … }`, on a parameter whose type is an enum, where only the
  arm chosen is worked out. A match goes alone after `return`, so a function that
  calls itself in one arm never runs the others.

A choice given to a parameter is named with its enum, `steps(w, pace::fast)`, as
everywhere else; a match's arms name them bare, as a command's do.

A function's return type is that of the first `return` whose type can be told
without guessing, so one that calls itself is typed by the return it ends on.

## Consequences

The generated Go is written as gofmt writes it, sums in a call of several
arguments tight, `n-1`, and alone open, `n - 1`, so the generated backends stay
formatted as CI checks them.

On the local targets a function works on values as rows keep them, as a command's
body does, and a C++ app declares each before it's defined, so functions may call
one another in any order.

A function still has no statements but `if` and `return`, and reads only its
parameters. It can't read or change records; that stays a command's work.
