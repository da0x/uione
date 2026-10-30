# 0029: A list gives its nth, and says what it holds, in a function

- Status: Accepted
- Date: 2026-10-30
- Deciders: Daher Alfawares
- Extends: [0028](0028-a-list-is-read-joined-and-drawn-from-at-random.md)

## Context

A function could take part of a list, join two and count one, but it couldn't
look at any one thing a list held. `first(list, 1)` is a list of one, not the
thing in it. So a function couldn't ask anything about what a list holds, like
whether any of it starts with a letter, which of two of its things comes first,
or where in it the largest is. `has` asked whether a list held a value in a
command, and a function accepted it, but only the backend's commands could carry
it out.

## Decision

**`item(list, n)` is a list's nth**, counting from 1, as `first(list, 1)` counts,
so `item(list, count(list))` is its last. With n below 1 or past its end it's
nothing, as `first` and `drop` give all or none rather than fail. It reads a list
anywhere `first` does, in a function or a command.

**`list has value` works in a function on every target**, as it does in a
command: true when the list holds something == to the value. On storage local,
where commands had no `has` either, it works in both.

## Consequences

With the item and `has`, plus a function calling itself, a function can walk a
list and ask anything of each thing in it. It's how an app finds the first of a
kind, the largest, or whether any of it is something, in its own words, with no
search, sort or filter built into the language.

Walking a list this way copies what's left of it at each step, as 0028's
operations do. That's fine for the lists one record keeps, but not for thousands.
