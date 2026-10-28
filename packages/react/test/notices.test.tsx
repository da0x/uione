// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What's said as a row's time comes, whatever screen is open: once, with its buttons,
// each acting on its row, and remembered in the browser so it isn't said again.

import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { vi } from "vitest";
import { App, memorySource, screen as defineScreen, timeNamed } from "../src/index.js";
import { plain } from "../src/plain.js";

describe("a notice", () => {
  // The browser's storage, kept here.
  beforeEach(() => {
    const kept = new Map<string, string>();
    vi.stubGlobal("localStorage", { getItem: (key: string) => kept.get(key) ?? null, setItem: (key: string, value: string) => void kept.set(key, value) });
  });
  afterEach(() => vi.unstubAllGlobals());

  it("is said once its row's time has come, its button acting on the row, and isn't said again", async () => {
    const source = memorySource({ commands: { "orbit::task::remind": () => ({ id: "t1" }) } });
    const past = new Date(Date.now() - 60 * 1000);
    const later = new Date(Date.now() + 3600 * 1000);
    source.set("orbit::planner", {
      reminders: [
        { id: "t1", title: "Call the bank", board: "Home", remind_at: past },
        { id: "t2", title: "Dentist", remind_at: later },
      ],
    });
    const notices = [
      {
        view: "orbit::planner",
        list: "reminders",
        at: "remind_at",
        shown: ["title", "board"],
        buttons: [{ command: "orbit::task::remind", label: "In 10 minutes", given: () => ({ remind_at: timeNamed("+10m") }) }],
      },
    ];
    const only = defineScreen({ title: "Today", route: "/" }, () => <p>Nothing due</p>);
    const first = render(<App name="orbit" screens={[only]} ui={plain} data={source} location="/" notices={notices} />);
    const said = await waitFor(() => screen.getByRole("status", { name: "Call the bank" }));
    expect(said.textContent).toContain("Home");
    expect(screen.queryByRole("status", { name: "Dentist" })).toBeNull();
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "In 10 minutes" })));
    expect(source.runs.length).toBe(1);
    expect(source.runs[0]!.command).toBe("orbit::task::remind");
    expect((source.runs[0]!.input as Record<string, unknown>)["id"]).toBe("t1");
    expect((source.runs[0]!.input as Record<string, unknown>)["remind_at"]).toBeInstanceOf(Date);
    expect(screen.queryByRole("status", { name: "Call the bank" })).toBeNull();
    first.unmount();
    // Opened again: it was said, so it isn't again.
    render(<App name="orbit" screens={[only]} ui={plain} data={source} location="/" notices={notices} />);
    await screen.findByText("Nothing due");
    expect(screen.queryByRole("status", { name: "Call the bank" })).toBeNull();
  });
});
