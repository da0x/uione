// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A calendar's days and what's on them, worked out as the desktop's calendar_of does
// (decision 0023): a week from Monday, or the weeks a month's days fall in, each day
// with the rows on it, those that are a day alone first, then by their time of day.
// A day alone, kept at midnight UTC, is that day wherever it's read; any other time
// is placed on the reader's calendar.

import { dateOnly } from "./ui.js";

export interface CalendarCardOf<Row> {
  row: Row;
  time: string; // its time of day, like 09:00, or nothing for a day alone
}

export interface CalendarDayOf<Row> {
  date: string; // 2026-10-28
  weekday: string; // Wed
  day: number; // 28
  today: boolean;
  inMonth: boolean; // in a month's grid, a day of that month, not one around it
  cards: CalendarCardOf<Row>[];
}

const weekdays = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const months = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const pad = (n: number) => String(n).padStart(2, "0");

// A day where the reader is, as 2026-10-28.
export function dateOf(d: Date): string {
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

// The day 2026-10-28 names, at its start where the reader is.
export function dayOf(date: string): Date {
  const [y, m, d] = date.split("-").map(Number);
  return new Date(y!, m! - 1, d!);
}

// A time as it's read here: a day alone at midnight UTC, the start of that day; any
// other, as it is.
function readHere(at: Date): Date {
  return dateOnly(at) ? new Date(at.getUTCFullYear(), at.getUTCMonth(), at.getUTCDate()) : at;
}

export function calendarOf<Row>(rows: Row[], when: (row: Row) => unknown, on: Date, month: boolean, now = new Date()): { title: string; days: CalendarDayOf<Row>[] } {
  const start = new Date(on.getFullYear(), on.getMonth(), on.getDate());
  const fromMonday = (d: Date) => (d.getDay() + 6) % 7;
  let first: Date, last: Date, title: string;
  if (month) {
    const opening = new Date(start.getFullYear(), start.getMonth(), 1);
    const closing = new Date(start.getFullYear(), start.getMonth() + 1, 0);
    first = new Date(opening.getFullYear(), opening.getMonth(), 1 - fromMonday(opening));
    last = new Date(closing.getFullYear(), closing.getMonth(), closing.getDate() + 6 - fromMonday(closing));
    title = `${months[start.getMonth()]} ${start.getFullYear()}`;
  } else {
    first = new Date(start.getFullYear(), start.getMonth(), start.getDate() - fromMonday(start));
    last = new Date(first.getFullYear(), first.getMonth(), first.getDate() + 6);
    title = `Week of ${months[first.getMonth()]!.slice(0, 3)} ${first.getDate()}, ${first.getFullYear()}`;
  }
  // Each row on its day, a day alone first, then by its time of day.
  const placed = rows
    .map((row) => {
      const held = when(row);
      const at = held instanceof Date ? held : typeof held === "string" && held !== "" ? new Date(held) : undefined;
      if (!at || Number.isNaN(at.getTime())) return undefined;
      const alone = dateOnly(at);
      const here = readHere(at);
      return { row, date: dateOf(here), at: alone ? undefined : here };
    })
    .filter((p): p is { row: Row; date: string; at: Date | undefined } => p !== undefined)
    .sort((a, b) => (a.at === undefined ? (b.at === undefined ? 0 : -1) : b.at === undefined ? 1 : a.at.getTime() - b.at.getTime()));
  const today = dateOf(now);
  const days: CalendarDayOf<Row>[] = [];
  for (let d = first; d <= last; d = new Date(d.getFullYear(), d.getMonth(), d.getDate() + 1)) {
    const date = dateOf(d);
    days.push({
      date,
      weekday: weekdays[d.getDay()]!,
      day: d.getDate(),
      today: date === today,
      inMonth: !month || d.getMonth() === start.getMonth(),
      cards: placed.filter((p) => p.date === date).map((p) => ({ row: p.row, time: p.at ? `${pad(p.at.getHours())}:${pad(p.at.getMinutes())}` : "" })),
    });
  }
  return { title, days };
}

// The day a calendar goes to, a week or a month on or back.
export function calendarGo(on: Date, month: boolean, by: number): Date {
  return month ? new Date(on.getFullYear(), on.getMonth() + by, 1) : new Date(on.getFullYear(), on.getMonth(), on.getDate() + 7 * by);
}

// A row's time moved to another day: a day alone stays one, at midnight UTC; a time
// of day stays the same, on the day it's moved to, where the reader is.
export function movedTo(held: unknown, date: string): Date {
  const day = dayOf(date);
  const at = held instanceof Date ? held : typeof held === "string" ? new Date(held) : undefined;
  if (!at || Number.isNaN(at.getTime()) || dateOnly(at)) return new Date(Date.UTC(day.getFullYear(), day.getMonth(), day.getDate()));
  return new Date(day.getFullYear(), day.getMonth(), day.getDate(), at.getHours(), at.getMinutes(), at.getSeconds());
}
