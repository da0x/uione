# 0015: An app may keep all its data on the machine it runs on

- Status: Accepted
- Date: 2026-10-24
- Deciders: Daher Alfawares
- Supersedes: in part, [0001](0001-one-architecture.md), for projects that say `storage local`

## Context

0001 runs every app on Firebase with a Go service. Some apps have one person and
data that should never leave their computer: a board of family photos, a notebook,
a tool that edits files at home. For them, Firebase means an account, a network and
a server, and gives nothing back.

What makes uione worth using is not Firebase. It's that a screen changes data only
through a command, reads only views, and is live. All of that holds on one machine
just as well.

## Decision

**A project may say `storage local`.** Its commands and views run inside the app,
and its data is kept on the machine the app runs on. A project that doesn't say it
works as 0001 says.

**The architecture is the same; only where it runs changes.**

- A command is still the only write. It checks its requires, changes entities, and
  records an event.
- A view is still built from those events ahead of time, and a screen still reads it
  live. A screen never fetches and never polls.

**On the desktop, everything is under `$XDG_DATA_HOME/<project>/`**
(`~/.local/share/<project>/` when it isn't set):

- `events.jsonl`: every event, one per line, appended and synced to disk before the
  command answers, so a command that answered is never lost.
- `files/`: each file a command is given, copied in once and named by its SHA-256.
  Events name the hash and never hold a file's bytes, so the log stays small and
  quick to read again.
- On start, the app reads the log through to rebuild its entities and views. Views
  live in memory, and each screen showing one is told the moment it changes.

**It's generated code, on a thin library** (0002). The native generator writes each
command and view of a local project as C++, on a new library, `one::local`, that
holds what repeats: the log, the files, the entities, and `local_source`, which
gives the screens the same `one::screens::source` they read from the cloud. Nothing
interprets a `.one` file at runtime.

**In the browser, everything is in an IndexedDB database named for the project**,
with the same two parts: the log of events, each kept before the command answers,
and each file, kept once by its SHA-256 and named `files/<sha-256>` as on the
desktop. On load, the app reads the log through, as the desktop app does. The web
generator writes the same description of each command and view, in TypeScript, on
`@uione/react/local`, which is to the browser what `one::local` is to the desktop:
`localSource` gives the screens the same data source they read from the cloud. So a
local project may target the web, the desktop and the terminal, each keeping its
own data where it runs.

**What a local project can't say yet, the checker refuses**, naming the line and
why: sign-in, roles and permissions other than `anyone`, `readers` and `public
when`, views `per user`, Stripe, webhooks, services, and environments. Each comes
back when local storage can do what it means.

**Nothing syncs.** A local project's data leaves the machine only when its person
takes it, as a file.

## Consequences

A command's views are current the moment it answers, since nothing has to catch up,
and a view isn't limited to what fits in one Firestore document.

The log only grows. Reading it on start is fast for the sizes expected; when it
isn't, measured, the app writes a snapshot of its views beside it.

Two copies of the app open on the same data would be two writers with no order
between them, so the second doesn't open, by a lock on the folder: it hands the
address it was started with to the first, which goes there, and leaves. In the
browser two tabs of the same local web app share one database, so they take turns
instead: each catches up with what the others kept before it runs a command, under
a Web Lock, and tells them through a BroadcastChannel when it keeps one, so each
shows what the others did. Each tab's undo is its own. Files nothing names are let
go of only by a tab that opens alone, since another's undo could name one again.

0001 now reads "every app that doesn't say `storage local` runs on Firebase with a
Go service".

A local web app can still be served from Firebase Hosting, when its project names
`firebase`, `region` and `domain` (2026-10-29). Hosting only hands out its files: the
deploy makes no service, database or rules, and each visitor's data stays in their
browser, so nothing here changes.
