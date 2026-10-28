# 0023: A calendar is a list on its days

- Status: Accepted
- Date: 2026-10-28
- Deciders: Daher Alfawares

## Context

A list whose rows have a date, like tasks with when they're due or loans with when
they're due back, is often best seen on a calendar: the week ahead, or a month, each
row on its day. A table sorted by the date shows the order but not the shape of the
week, and a board's columns are stages, not days.

Days are named by the calendar, not counted from now: "tomorrow" is the day after
today wherever the reader is, and a date with no time of day is that day wherever
it's read (uione keeps such a date at midnight UTC).

## Decision

**A `calendar` is an element of its own**, beside `board`:

```one
calendar planner.tasks by due {
	month when month
	move task::plan
	tint by priority
	title
	board.title
}
```

- Its rows are a view's list, or the view's own, each on the day its date field, `by`,
  says, as it's read on the reader's calendar.
- It shows a week from Monday, its days side by side, or, while `month when` holds, a
  month's grid, the weeks its days fall in, its first few rows a day and how many
  more.
- On a day, the rows that are a day alone come first, then the rest by their time of
  day.
- It shows what it's on, like October 2026, and the ways back, to today and on; Page
  Up and Page Down go a week or a month at a time.
- A row is picked with a click, when the screen picks a row of its list, so a toolbar
  acts on it as on a table's.
- With `move`, a row dragged to another day is sent to that command with its date
  there: a day alone stays one, and a time of day stays the same, on the new day.
- What it shows of a row are its columns, its title first.

The days and the rows on them are worked out the same way by the desktop's
`calendar_of` and the web's `calendarOf`, each held to the same cases by its tests.

## Consequences

Any app with dates gets a calendar for one line: tasks, loans, bookings, releases.

A terminal shows each day in turn, its rows under it, picked with Enter; a row is
moved there by a command of its own, not dragged.

A calendar has no hours. A row with a time of day says it, but a day isn't a column
of hours, so a meeting's length isn't shown. That's for a later decision, when an app
needs one.
