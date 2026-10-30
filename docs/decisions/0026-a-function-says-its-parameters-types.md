# 0026: A function says its parameters' types, as fields do

- Status: Accepted
- Date: 2026-10-30
- Deciders: Daher Alfawares

## Context

A function was written for one place, a view's `order by`, like
`sort_title(title)`. Its parameters had no types; the backend took each one's from
the field it was called with there.

A function worth having elsewhere, in a command, calling another function or
itself, has no such field to look at. Working a type out from every call, through
calls of calls and a function calling itself, is guesswork the compiler would get
wrong in ways it couldn't explain.

## Decision

**A parameter may say its type, written as a field's is**: `function cut(deck list
of item, at number)`. Any type a field takes is allowed, an enum declared on its
own too, except `serial`, which is counted as a record is made.

A parameter written bare, like `title`, keeps working where it worked: its type is
still taken from the field it's called with in a view's order.

## Consequences

A function reads like an entity: each name beside its type, in words already in the
language, with nothing new to learn.

The generators know every typed parameter's type without looking at a call, which
is what lets a function be used beyond ordering.
