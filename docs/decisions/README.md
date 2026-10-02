# Decisions

The decisions that shape uione, one per file: what was decided, why, and what it
costs. If something looks arbitrary, the reason is probably here, so read the
relevant record before changing what it covers.

| Record | Decision |
|---|---|
| [0001](0001-one-architecture.md) | There is one architecture |
| [0002](0002-generated-code-on-thin-libraries.md) | Generated code stays short, on thin libraries |
| [0003](0003-the-compiler-is-cpp23.md) | The compiler is written in C++23 |
| [0004](0004-licenses.md) | The compiler is AGPL, the libraries are LGPL, and the code is original |
| [0005](0005-the-syntax.md) | The syntax |
| [0006](0006-names-are-snake-case.md) | Names are stored in snake_case and shown however the reader likes |
| [0007](0007-work-lands-on-main.md) | Work lands on main, checked before every push |
| [0008](0008-deployed-with-pulumi.md) | Projects are deployed with Pulumi, by hand at first |
| [0009](0009-ids-from-keys-and-serials.md) | An entity's id is made from its keys, and numbers are counted per parent |
| [0010](0010-who-may-do-and-read-what.md) | Who may do and read what |

## Writing one

Each record is a file numbered in order, with its status, date and deciders, then
Context, Decision and Consequences.

Changing a decision means a new record that supersedes the old one, in the same
commit as the change, never quietly working around it.
