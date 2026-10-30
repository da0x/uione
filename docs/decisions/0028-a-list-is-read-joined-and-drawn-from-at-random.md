# 0028: A list is read, joined and drawn from at random

- Status: Accepted
- Date: 2026-10-30
- Deciders: Daher Alfawares
- Extends: [0027](0027-a-function-may-be-called-from-a-command-on-every-target.md)

## Context

A list field could be added to, removed from, or given a list written out, `[a,
b]`. Nothing could take part of one, join two, or put one in an order worked out by
the app, like a queue rotated or items mixed. Those are what an app says in its own
functions once it has a few general pieces, and none of them is particular to one
kind of app.

## Decision

**What reads text reads a list**, in a function or a command:

- `first(list, n)`: its first n, all of it when it holds fewer;
- `drop(list, n)`: all of it after its first n, none when it holds no more;
- `count(list)`: how many it holds;
- `+`: two lists joined, the first's then the second's; `[]` holds nothing.

Each gives a list of its own, never changing the one it's given. A list field is
given a whole list, so `deck = drop(deck, 13)` is allowed as `deck = [a, b]` was.

**`random(low, high)`** is a whole number from low to high, both included:

- drawn as a command runs, from the operating system's secure source: `crypto/rand`
  on the backend, `crypto.getRandomValues` in the browser and `std::random_device`
  on the desktop. On the cloud target that's the backend, so no one using the app can
  see or steer a draw;
- what's kept is what the command made of it, so replaying or undoing it never draws
  again;
- never in a view, directly or through a function, since a view shows everyone the
  same each time.

**A command's input may be one of an enum's choices**, `input how  cut_by`, and a
command sent anything else is refused before its body runs. So a function's match on
it never meets a choice it doesn't know.

Each target's tests can draw in an order they choose, to hold them to the same
results.

## Consequences

How a list is put in order is the app's own, said in its functions, rather than
an operation of the language: one app riffles, another rotates a queue, another
picks a winner.

A function in Go that's named like a command, a view or an entity of its
namespace is written with Function after its name, `cut` and `deal::cut` becoming
`CutFunction` and `Cut`.

Each list operation copies what it gives, which is plain to read and cheap for the
lists an app keeps in one record, like a few dozen ids. A list of thousands, worked
on item by item, would want something else.
