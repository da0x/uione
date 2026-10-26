# 0018: Undo and redo are for local storage, for now

- Status: Accepted
- Date: 2026-10-25
- Deciders: Daher Alfawares

## Context

A local project ([0015](0015-an-app-may-keep-its-data-on-the-machine.md)) keeps
its events in order on one machine, so each step it takes knows what it changed and
can put it back: `one::undo` and `one::redo`, kept as events like any other, the
history the page's own while it's open.

A project in the cloud has many people writing at once. Undoing a step there means
deciding what happens to what others changed since, and who may undo what. That's
a decision of its own, not a side effect of a button.

## Decision

**`undo` and `redo` buttons, and `apart from undo` in a command, need `storage
local`.** The checker says so on the line, as it does for what local storage can't
do yet.

- `undo "Undo" icon undo key ctrl+z` takes back the page's last step; its tooltip
  names it, like "Undo place".
- A command saying `apart from undo` is kept but never a step, like a preview drawn
  again, or housekeeping when a page opens.

## Consequences

A drawing app and a notebook get undo with one line each, on the desktop and on the
web alike.

A cloud project that wants undo will get it from a new record that supersedes this
one, saying what an undo does to the changes of others.
