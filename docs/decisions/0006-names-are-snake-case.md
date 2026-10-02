# 0006: Names are stored in snake_case and shown however the reader likes

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

Every language's naming style is chosen by its author, and for some readers it
isn't taste: many people with dyslexia find words run together, likeThisOne, hard
to read. A word processor never shows its file format; you choose the font and the
size. Code can work the same way.

## Decision

Every name in a `.one` file is snake_case, keywords included (`signed_in`), with no
option to write it another way. How names look is the reader's choice: code on
uione.io shows `due_at` as `dueAt`, `DueAt` or `due-at` if the reader prefers, and
the studio will do the same.

snake_case is the one to store because every other style follows from it without
guessing. `parseHTTPResponse` could be `parse_http_response` or
`parse_h_t_t_p_response`; `parse_http_response` is only ever one thing.

Names of folders, files, modules and namespaces in this repository are whole
words, for the same reason keywords are (0005): `generators`, not `gen`.

## Consequences

The compiler refuses any name that isn't snake_case and suggests the fix. Data in
Firestore uses the names as written. Generated Go follows Go's rules (`due_at`
becomes `DueAt`), and generated TypeScript uses the data's names as they are.

A name typed in another style has to be turned back into snake_case before it's
saved, and the studio shows what it will store when the split isn't obvious.
