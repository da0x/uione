// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A calendar: a week from Monday, or the weeks a month's days fall in, each row on its
// day, a day alone first; one picked with a click, and one dropped on another day
// moved there with its time of day kept, as the desktop's.

import { act, fireEvent, render, screen, within } from "@testing-library/react";
import { App, Calendar, memorySource, screen as defineScreen, useView } from "../src/index.js";
import { calendarOf, movedTo } from "../src/calendar.js";
import { plain } from "../src/plain.js";
import { usePickedId } from "../src/workspace.js";

describe("a calendar's days", () => {
  // A Wednesday afternoon, where the reader is.
  const now = new Date(2026, 8, 16, 15, 0);
  const rows = [
    { id: "t1", title: "Call the bank", due: new Date(2026, 8, 15, 9, 30) },
    { id: "t2", title: "Water plants", due: new Date(Date.UTC(2026, 8, 15)) },
    { id: "t3", title: "Dentist", due: new Date(2026, 8, 17, 14, 0) },
    { id: "t4", title: "Someday", due: null },
  ];

  it("are a week from Monday, each row on its day, a day alone first, then by its time of day", () => {
    const { title, days } = calendarOf(rows, (row) => row.due, now, false, now);
    expect(title).toBe("Week of Sep 14, 2026");
    expect(days.map((d) => d.date)).toEqual(["2026-09-14", "2026-09-15", "2026-09-16", "2026-09-17", "2026-09-18", "2026-09-19", "2026-09-20"]);
    expect(days[2]!.today).toBe(true);
    expect(days[1]!.cards.map((c) => [c.row.title, c.time])).toEqual([
      ["Water plants", ""],
      ["Call the bank", "09:30"],
    ]);
    expect(days[3]!.cards.map((c) => c.time)).toEqual(["14:00"]);
  });

  it("are, for a month, the weeks its days fall in, from the Monday before its first", () => {
    const { title, days } = calendarOf(rows, (row) => row.due, now, true, now);
    expect(title).toBe("September 2026");
    expect(days.length).toBe(35);
    expect(days[0]!.date).toBe("2026-08-31");
    expect(days[0]!.inMonth).toBe(false);
    expect(days[34]!.date).toBe("2026-10-04");
  });

  it("move a row to another day, a day alone staying one and a time of day kept", () => {
    expect(movedTo(new Date(2026, 8, 15, 9, 30), "2026-09-18")).toEqual(new Date(2026, 8, 18, 9, 30));
    expect(movedTo(new Date(Date.UTC(2026, 8, 15)), "2026-09-16")).toEqual(new Date(Date.UTC(2026, 8, 16)));
  });
});

describe("a calendar", () => {
  it("shows its rows on their days, picks one with a click, and moves one dropped on another day there", async () => {
    const source = memorySource({ commands: { "orbit::task::plan": () => ({ id: "t1" }) } });
    source.set("orbit::planner", {
      tasks: [
        { id: "t1", title: "Call the bank", due: new Date(2026, 8, 15, 9, 30) },
        { id: "t2", title: "Pay rent", due: new Date(Date.UTC(2026, 8, 18)) },
      ],
    });
    function Picked() {
      return <p data-testid="picked">{usePickedId("/ task") ?? ""}</p>;
    }
    function Planner() {
      const view = useView("orbit::planner");
      return (
        <>
          <Calendar view={view} list="tasks" by="due" columns={{ title: "Title" }} move="orbit::task::plan" pick="/ task" name="/ tasks" />
          <Picked />
        </>
      );
    }
    const only = defineScreen({ title: "Calendar", route: "/" }, () => <Planner />);
    render(<App name="orbit" screens={[only]} ui={plain} data={source} location="/" />);
    // It opens on today's week; back to the week of September 14, 2026, from there.
    const weeks = Math.round((new Date(2026, 8, 14).getTime() - mondayOf(new Date()).getTime()) / (7 * 24 * 3600 * 1000));
    for (let i = 0; i < Math.abs(weeks); i++) fireEvent.click(screen.getByRole("button", { name: weeks < 0 ? "Last week" : "Next week" }));
    expect(screen.getByRole("heading", { name: "Week of Sep 14, 2026" })).toBeTruthy();
    const day = (date: string) => screen.getByRole("region", { name: date });
    expect(within(day("2026-09-15")).getByText(/Call the bank/)).toBeTruthy();
    await act(async () => fireEvent.click(within(day("2026-09-15")).getByText(/Call the bank/).closest("li")!));
    expect(screen.getByTestId("picked").textContent).toBe("t1");
    // Dropped on Friday: there at the same time of day.
    const carried = { getData: () => "t1", setData: () => {} };
    await act(async () => fireEvent.drop(day("2026-09-18"), { dataTransfer: carried }));
    expect(source.runs).toEqual([{ command: "orbit::task::plan", input: { id: "t1", due: new Date(2026, 8, 18, 9, 30) } }]);
  });
});

function mondayOf(d: Date): Date {
  return new Date(d.getFullYear(), d.getMonth(), d.getDate() - ((d.getDay() + 6) % 7));
}
