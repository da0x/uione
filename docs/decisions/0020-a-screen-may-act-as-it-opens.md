# 0020: A screen may act as it's opened

- Status: Accepted
- Date: 2026-10-25
- Deciders: Daher Alfawares

## Context

Some apps open straight into work, as a paint program opens on a blank picture
rather than a list of files. Which piece of work opens depends on what's kept: the
newest one nothing's been done to yet, as big as this screen, or else a new one.

Until now a screen only showed things and waited for a press. Doing this meant a
component written by hand twice, once for the desktop and once for the web.

## Decision

**A screen may say what it does as it's opened**, a line at a time:

```one
screen "New poster" /new {
	on open open /posters/:poster for posters where poster.first_layer == none && poster.width == screen.width
	on open poster::create { title = "Untitled"  width = screen.width } then open /posters/:poster
}
```

- `on open open ROUTE for VIEW where CONDITION` opens the first row of the view's
  list the condition holds for. The row is named by what it's of, `poster`, as a
  pick is read.
- `on open` before a command's line runs it as its button would: with its given
  values, its `when`, and `then open`.
- The lines are tried in order, and the first that applies is the only one done.
- It's decided once a visit, once every view the lines read is there, with
  "Opening…" shown until then.
- What it opens takes the screen's place, so going back doesn't open it again.

**`screen.width` and `screen.height`** are the screen the app is shown on, in its
own pixels, wherever a value or a condition is read. A terminal has no screen of its
own to measure, so it reads a common one, 1920 by 1080.

## Consequences

An app that opens into work, like a wallpaper maker, is written in the language,
the same on every target.

A command run on open is a step like any other. One that shouldn't be undone says
`apart from undo` itself.

An `each` in a command still picks rows by one field's value, so every storage can
find them the same way. Tidying rows by a richer condition, like "untitled with
nothing on it", isn't something a command can say yet.
