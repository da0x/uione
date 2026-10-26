// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace's parts: keys that run what's on a line, a toolbar's groups drawing
// their buttons as tools, toggles remembered and read by name, undo and redo said in
// the messages, and a command turned down said there too.

import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { App, Command, Group, History, Layout, Messages, Region, Toggle, keyShown, memorySource, screen as defineScreen, useToggled } from "../src/index.js";
import type { MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";

function renderScreen(source: MemorySource, body: () => React.ReactNode) {
  const only = defineScreen({ title: "Studio", route: "/" }, body);
  return render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
}

describe("a workspace", () => {

  it("runs a command by its key, and not while the person is typing", async () => {
    const source = memorySource({ commands: { "studio::poster::create": () => ({ id: "p1" }) } });
    renderScreen(source, () => (
      <>
        <input aria-label="Title" />
        <Command name="studio::poster::create" label="New" keys={["ctrl+n"]} />
      </>
    ));
    expect(screen.getByRole("button", { name: "New" }).getAttribute("title")).toBe("New (Ctrl+N)");
    fireEvent.keyDown(screen.getByLabelText("Title"), { key: "n", ctrlKey: true });
    expect(source.runs).toEqual([]);
    fireEvent.keyDown(window, { key: "n", ctrlKey: true });
    await waitFor(() => expect(source.runs.length).toBe(1));
    fireEvent.keyDown(window, { key: "n" });
    expect(source.runs.length).toBe(1);
  });

  it("says a key as people read it", () => {
    expect(keyShown("ctrl+shift+z")).toBe("Ctrl+Shift+Z");
    expect(keyShown("f11")).toBe("F11");
    expect(keyShown("minus")).toBe("-");
  });

  it("draws a toolbar's buttons as tools, in a group named for what they do", () => {
    renderScreen(memorySource(), () => (
      <Group name="Poster">
        <Command name="studio::poster::create" label="New" icon="add" />
      </Group>
    ));
    expect(screen.getByRole("group", { name: "Poster" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "New" })).toBeTruthy();
  });

  it("remembers a toggle, which the screen reads by its name, and shows a region while it's on", () => {
    function Panel() {
      const on = useToggled("/ list");
      return (
        <Layout name="two_columns">
          <Region name="main">
            <Toggle name="/ list" label="List" keys={["l"]} />
          </Region>
          {on && (
            <Region name="side">
              <p>The list</p>
            </Region>
          )}
        </Layout>
      );
    }
    const first = renderScreen(memorySource(), () => <Panel />);
    expect(screen.queryByText("The list")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "List" }));
    expect(screen.getByText("The list")).toBeTruthy();
    expect(screen.getByRole("button", { name: "List" }).getAttribute("aria-pressed")).toBe("true");
    first.unmount();
    renderScreen(memorySource(), () => <Panel />);
    expect(screen.getByText("The list")).toBeTruthy();
    fireEvent.keyDown(window, { key: "l" });
    expect(screen.queryByText("The list")).toBeNull();
  });

  it("says what an undo took back, and why a command was turned down", async () => {
    const source = memorySource({
      commands: {
        "one::undo": () => ({ id: "studio::poster::place" }),
        "studio::poster::delete": () => {
          throw new Error("There's no such poster.");
        },
      },
    });
    renderScreen(source, () => (
      <>
        <History label="Undo" keys={["ctrl+z"]} />
        <Command name="studio::poster::delete" label="Delete" />
        <Messages />
      </>
    ));
    fireEvent.keyDown(window, { key: "z", ctrlKey: true });
    expect(await screen.findByText("Undid place.")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Delete" }));
    await waitFor(() => expect(screen.getByRole("status").textContent).toBe("There's no such poster."));
  });
});
