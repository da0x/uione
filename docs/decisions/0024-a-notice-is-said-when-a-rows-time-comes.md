# 0024: A notice is said when a row's time comes

- Status: Accepted
- Date: 2026-10-28
- Deciders: Daher Alfawares

## Context

A reminder app is no use if it only says a reminder while the screen that lists it is
open. What's due has to be said whatever the person is looking at, by the system, the
way any other app tells them, and they should be able to act on it from there: done,
or later.

A screen's elements live while the screen is open. Something that watches all the
time is a part of the app, not of a screen.

## Decision

**`notify` is a declaration of its own**, beside `screen`, in a namespace:

```one
notify planner.reminders at remind_at {
	title
	board.title
	task::finish "Done"
	task::remind "In 10 minutes" { remind_at = 10 minutes from now }
}
```

- It names a view of the whole app, not one per something, and a list of it, or the
  view's own, and the date saying when each row is due.
- Whatever screen is open, a row whose time has come is said once: its first column
  as the title, the rest under it, and its buttons, each a command on the row, given
  values written out, worked out as it's pressed.
- What's been said is remembered on the machine, each row at its time, so it's said
  once; a time moved later is said again then, and one that passed while the app was
  closed is said when it's next open. Nothing of it is stored or sent, and it's no
  step undo takes back.
- On the desktop it's the desktop's own notification, org.freedesktop.Notifications,
  its buttons on it. On the web it's a note in the page's corner with its buttons, and
  the browser's notification too once the person has let the page notify them, which
  it asks the first time they press one. In a terminal, it's the bell and its words.

The desktop and the terminal share one watcher, `notice_watch`; the web's is its
twin.

## Consequences

Any app with a date that matters, a reminder, a loan due back, a booking, says it
for one declaration.

A notice is said only while the app is running. Saying one while it's closed needs a
part of the system that runs on its own, which is for a later decision.
