# 0005: The syntax

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

uione should read like a plain script to people who read TypeScript, Go and C++
all day. Unfamiliar syntax slows them down on every line; copying TypeScript brings
back the clutter uione removes.

## Decision

- Comments are `//` and `/* */`. Blocks go in `{ }`, and only blocks get braces,
  so `command book::create` is one line.
- No semicolons and no commas between fields: a line break ends a line. A field is
  its name, its type, then its rules.
- Strings use double quotes, and `{...}` in a string shows a value.
- A namespace is a feature. It gives its screens their route prefix and their
  place in the navigation.
- `::` reaches inside a namespace or an entity. Commands are named on their entity,
  `command loan::checkin`, and `book::withdraw` needs the permission
  `book:withdraw` unless it says otherwise.
- Every keyword is a whole word: `function`, not `fn`; `boolean`, not `bool`.
  Acronyms read as words, like `ui`, and short whole words, like `me` and `per`,
  are fine.
- Lines are indented with tabs, one per level, and a line indented with spaces is
  an error. How wide a tab looks is the reader's choice; the editors and the code
  on uione.io default to four.

## Consequences

Any editor folds and matches `.one` blocks, and the highlighting stays small,
though some words mean something only in one position, like a type after a field
name.

`docs/grammar.ebnf` is the grammar. The parser is tested against it both ways, and
both editor grammars must know exactly its words.
