# Overview

uione describes a whole application feature in one short file: its data, its rules,
the commands that change it, the views that show it, who may do what, and its
screens. The compiler, `one`, turns a folder of `.one` files into projects you build
and deploy with the tools you already use:

| Output | What it is | Built with |
|---|---|---|
| `web/` | a React app on `@uione/react`, unstyled, drawn by a component set such as `@uione/radix` | `yarn build` |
| `api/` | a Go service on the `one` library | `go build` |
| `infrastructure/` | a Pulumi program for the cloud project | `pulumi up` |
| `firestore.rules` | who may read which view | deployed by `infrastructure/` |
| `cpp/<app>.hpp` | one header for a native program or desktop app: the app's records, the ids they're stored under, its commands, and its views read live | `g++ -std=c++23`, with [libember](https://github.com/da0x/libember) |

Everything generated is short enough to read, and nothing generated is edited by
hand. When the output needs changing, the `.one` file or the library changes.

## One architecture

Every uione app works the same way, so no file ever has to say how:

- **Commands are the only writes.** A screen calls a command. The Go service checks
  the caller's permission and the entity's rules, then writes.
- **Every write publishes an event.** `project::create` publishes `project.updated`.
- **Views are built from events.** A view is a document assembled ahead of time for
  the screen that shows it, and rebuilt when anything it reads changes.
- **Screens subscribe to views.** A screen never asks an endpoint for data and never
  polls. When someone else changes something, it changes on your screen, without a
  refresh.

## What you write, and what you don't

You write what is particular to the feature. You never write the things that are
true of every feature:

- ids, created and updated stamps, and the audit log;
- the event each command publishes;
- the permission a command needs: `book::withdraw` needs `book:withdraw` unless the
  command says otherwise;
- the permission a view needs: `book:view` for a view whose rows are books;
- when a view is rebuilt: whenever an entity it reads changes;
- validation: a rule written once on the entity is checked in the form and in the
  command;
- hiding a button from someone who may not press it, and asking for sign-in on a
  screen whose views need it.
