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
| [0011](0011-services-with-keys.md) | Services that work beside people, with keys |
| [0012](0012-programs-sign-in-through-the-browser.md) | Programs sign in as a person through their browser |
| [0013](0013-everything-is-agpl.md) | Everything in uione is AGPL-3.0-only |
| [0014](0014-built-once-tested-released.md) | Everything is built once, tested, and released as it was tested |
| [0015](0015-an-app-may-keep-its-data-on-the-machine.md) | An app may keep all its data on the machine it runs on |
| [0016](0016-pictures-are-read-by-a-pinned-stb.md) | Pictures are read and written by a pinned copy of stb |
| [0017](0017-a-screen-keeps-what-is-picked.md) | A screen keeps what's picked, and nothing else |
| [0018](0018-undo-is-for-local-storage-for-now.md) | Undo and redo are for local storage, for now |
| [0019](0019-a-collage-is-a-document.md) | A collage is a picture made of layers, not a canvas |
| [0020](0020-a-screen-may-act-as-it-opens.md) | A screen may act as it's opened |
| [0021](0021-every-picture-in-a-collage-is-a-layer.md) | Every picture in a collage is a layer like the rest |
| [0022](0022-a-paper-is-a-page-written-in-place.md) | A paper is a page written on in place |
| [0023](0023-a-calendar-is-a-list-on-its-days.md) | A calendar is a list on its days |
| [0024](0024-a-notice-is-said-when-a-rows-time-comes.md) | A notice is said when a row's time comes |
| [0025](0025-a-collage-layer-may-be-a-shape.md) | A collage's layer may be a shape, drawn by a pinned copy of nanosvg |
| [0026](0026-a-function-says-its-parameters-types.md) | A function says its parameters' types, as fields do |
| [0027](0027-a-function-may-be-called-from-a-command-on-every-target.md) | A function may be called from a command, on every target |

## Writing one

Each record is a file numbered in order, with its status, date and deciders, then
Context, Decision and Consequences.

Changing a decision means a new record that supersedes the old one, in the same
commit as the change, never quietly working around it.
