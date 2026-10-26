# 0017: A screen keeps what's picked, and nothing else

- Status: Accepted
- Date: 2026-10-25
- Deciders: Daher Alfawares

## Context

0001 says a screen changes data only by running a command and reads only views.
That's what makes every screen live and every change checked.

A drawing app, a file manager or an editor needs a little more: which row is
picked, whether a panel is open, which mode the board is in. None of it is data.
Nobody else needs to see it, nothing should check it, and it shouldn't outlive the
window. Written by hand, it's where a component starts, and a component is code the
language can't read.

## Decision

**A screen may keep what's picked and what's toggled, and only that.**

- `pick layer from poster_page.layers` names a row of a view's list. A table or a
  collage on that list picks it with a click, and the screen reads it by its name:
  `layer.size`, `layer != none`. A command on that entity, run from the screen,
  acts on the picked row.
- `toggle layers "Layers"` names something on or off, like a side panel, read the
  same way: `side when layers`.

**Neither is stored or sent.** A pick lives as long as the page. A toggle is
remembered for the person on that device, as the table and board switch already
is. Neither is in a view, an event, or a command's fields; a command is still sent
only its fields, with the picked row's id as the one it acts on.

## Consequences

Screens with a workspace can be written in the language, with the same rule as
every other screen: what changes data is a command, and what's shown is a view or
what's picked from one.

Anything more, like a value typed and kept between screens, is still not a screen's
to keep; it's an entity with a command.
