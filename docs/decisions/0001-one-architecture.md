# 0001: There is one architecture

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

Most code in a web feature moves data around: handlers, fetch wrappers, caches,
loading states, and the code that keeps a screen from showing something stale.
Every project does this its own way, so none of it can be left out or generated.
A language that also had to describe the architecture couldn't be short.

## Decision

Every uione app works the same way, so a `.one` file never says how.

- A screen changes data only by running a command. The backend checks who is
  asking and the entity's rules, writes, and publishes an event.
- A screen reads only views. A view is a Firestore document built ahead of time
  from those events, and the screen reads it live, so every screen showing
  something updates the moment it changes. A screen never fetches and never polls.
- A view is one document for everyone, one per person (`per user`), or one per
  entity (`per book`), read for the entity the screen's address names. It holds
  values and lists, and each row holds exactly what its block lists.
- Data no view provides means a new view, never a query in the screen.
- An entity marked `history` keeps every change to it, written in the same
  transaction as the change, so views can show its timeline.

## Consequences

Ids, timestamps, events, history and live updates are the same in every app, so
the compiler writes them. A feature file holds only what's particular to it.

Every app runs on Firebase with a Go service on Cloud Run. Other stacks can't use
uione yet, and no syntax is added that only makes sense for another one.

Views are copies, so they catch up a moment after their data changes. A view has
to fit in one Firestore document, so long lists are split by entity (a page per
project) and kept short with `limit`.
