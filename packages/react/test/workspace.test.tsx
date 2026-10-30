// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace's parts: keys that run what's on a line, a toolbar's groups drawing
// their buttons as tools, toggles remembered and read by name, an enum's choices pressed
// one at a time, undo and redo said in the messages, and a command turned down said
// there too.

import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { useLocation } from "react-router";
import { App, Choose, Command, Form, FullScreen, Group, History, Layout, Live, Messages, OnOpen, Region, Table, Options, Toggle, Tools, keyShown, memorySource, screen as defineScreen, useChosen, useToggled, useView } from "../src/index.js";
import { lastGiven, styleThese, usePicked } from "../src/workspace.js";
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

  it("starts its bar with the app's name, as the site's bar would", async () => {
    renderScreen(memorySource(), () => (
      <Layout name="workspace">
        <Region name="bar">
          <History label="Undo" />
        </Region>
        <Region name="main">
          <p>The posters</p>
        </Region>
      </Layout>
    ));
    await waitFor(() => expect(document.querySelector("[data-region=bar]")?.textContent).toBe("studio Undo"));
  });

  it("fills the window by itself where the browser won't give a page the screen, only its work and a way out", async () => {
    renderScreen(memorySource(), () => (
      <Layout name="workspace">
        <Region name="bar">
          <FullScreen label="Full screen" icon="fullscreen" />
        </Region>
        <Region name="main">
          <p>The letter</p>
        </Region>
      </Layout>
    ));
    expect(document.documentElement.requestFullscreen).toBeUndefined();
    fireEvent.click(screen.getByRole("button", { name: "Full screen" }));
    await waitFor(() => expect(screen.queryByRole("button", { name: "Full screen" })).toBeNull());
    expect(screen.getByText("The letter")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Leave full screen" }));
    await waitFor(() => expect(screen.getByRole("button", { name: "Full screen" })).toBeTruthy());
    fireEvent.click(screen.getByRole("button", { name: "Full screen" }));
    await waitFor(() => expect(screen.getByRole("button", { name: "Leave full screen" })).toBeTruthy());
    fireEvent.keyDown(window, { key: "Escape" });
    await waitFor(() => expect(screen.queryByRole("button", { name: "Leave full screen" })).toBeNull());
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

  it("presses one of an enum's choices at a time, its first until another's chosen, by a press or its key", () => {
    function Tools() {
      const tool = useChosen("/ tool", "select");
      return (
        <>
          <Choose name="/ tool" choice="select" first="select" label="Select" keys={["v"]} />
          <Choose name="/ tool" choice="pen" first="select" label="Pen" keys={["p"]} />
          <p>Drawing with {tool}</p>
        </>
      );
    }
    renderScreen(memorySource(), () => <Tools />);
    const pressed = () => screen.getAllByRole("button").filter((b) => b.getAttribute("aria-pressed") === "true").map((b) => b.textContent);
    expect(pressed()).toEqual(["Select"]);
    fireEvent.click(screen.getByRole("button", { name: "Pen" }));
    expect(pressed()).toEqual(["Pen"]);
    expect(screen.getByText("Drawing with pen")).toBeTruthy();
    fireEvent.keyDown(window, { key: "v" });
    expect(pressed()).toEqual(["Select"]);
  });

  it("draws a workspace's tools as small tools, each its words and key said as its hint", () => {
    renderScreen(memorySource(), () => (
      <Tools>
        <Choose name="/ tool" choice="pen" first="pen" label="Pen" keys={["p"]} />
      </Tools>
    ));
    expect(screen.getByRole("button", { name: "Pen" }).getAttribute("title")).toBe("Pen (P)");
  });

  it("opens a tool's options, each a choice with what it's for, closing once one's chosen", async () => {
    const source = memorySource({ commands: { "studio::poster::resize": () => ({ id: "p1" }) } });
    renderScreen(source, () => (
      <Group name="Page">
        <Options label="Page" icon="fit">
          <Command name="studio::poster::resize" label="Wide" about="16:9, for screens" swatch="page 1920 1080" given={{ width: 1920, height: 1080 }} />
          <Command name="studio::poster::resize" label="Tall" given={{ width: 1080, height: 1920 }} pressed />
        </Options>
      </Group>
    ));
    expect(screen.queryByText("16:9, for screens")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Page" }));
    expect(screen.getByText("16:9, for screens")).toBeTruthy();
    expect(screen.getByRole("menuitemradio", { name: /Tall/ }).getAttribute("aria-checked")).toBe("true");
    fireEvent.click(screen.getByRole("menuitemradio", { name: /Wide/ }));
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::poster::resize", input: { width: 1920, height: 1080 } });
    expect(screen.queryByText("16:9, for screens")).toBeNull();
  });

  it("slides a button's value from what it is now, running its command with where it's let go", async () => {
    const source = memorySource({ commands: { "studio::layer::paint": () => ({ id: "l1" }) } });
    renderScreen(source, () => (
      <Group name="Opacity">
        <Options label="Opacity" icon="opacity">
          <Command name="studio::layer::paint" id="l1" label="Opacity" slide={{ field: "opacity", from: 0, to: 1, by: 0.01, now: 0.5 }} />
        </Options>
      </Group>
    ));
    fireEvent.click(screen.getByRole("button", { name: "Opacity" }));
    const slider = screen.getByRole("slider") as HTMLInputElement;
    expect(slider.value).toBe("0.5");
    fireEvent.change(slider, { target: { value: "0.75" } });
    expect(source.runs.length).toBe(0);
    fireEvent.pointerUp(slider);
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::paint", input: { id: "l1", opacity: 0.75 } });
  });

  it("picks any color for a button's value, running its command with the one chosen", async () => {
    const source = memorySource({ commands: { "studio::layer::paint": () => ({ id: "l1" }) } });
    renderScreen(source, () => <Command name="studio::layer::paint" id="l1" label="Fill" color={{ field: "fill", now: "#2e86de" }} />);
    const picker = screen.getByLabelText("Fill") as HTMLInputElement;
    expect(picker.value).toBe("#2e86de");
    fireEvent.change(picker, { target: { value: "#ff0000" } });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::paint", input: { id: "l1", fill: "#ff0000" } });
  });

  it("remembers what a button gives for what's drawn next, only that with nothing to act on, and changes what's picked too", async () => {
    const source = memorySource({ commands: { "studio::layer::paint": () => ({ id: "l1" }) } });
    renderScreen(source, () => (
      <>
        <Command name="studio::layer::paint" label="Red" given={{ stroke: "#e4572e" }} remembers />
        <Command name="studio::layer::paint" id="l1" label="Ink" given={{ stroke: "#1d1d1f" }} remembers />
      </>
    ));
    fireEvent.click(screen.getByRole("button", { name: "Red" }));
    expect(lastGiven("studio::layer::paint")).toEqual({ stroke: "#e4572e" });
    expect(source.runs.length).toBe(0);
    await waitFor(() => expect(screen.getByRole("button", { name: "Red" }).getAttribute("aria-pressed")).toBe("true"));
    fireEvent.click(screen.getByRole("button", { name: "Ink" }));
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(lastGiven("studio::layer::paint")).toEqual({ stroke: "#1d1d1f" });
  });

  it("gives a style to each of several chosen, in one step", async () => {
    const source = memorySource({ commands: { "studio::layer::paint": (input) => ({ id: String(input["id"]) }) } });
    act(() => styleThese("/ layer", ["a", "b"]));
    renderScreen(source, () => <Command name="studio::layer::paint" label="Red" given={{ stroke: "#e4572e" }} remembers picks="/ layer" />);
    fireEvent.click(screen.getByRole("button", { name: "Red" }));
    await waitFor(() => expect(source.runs.length).toBe(2));
    expect(source.runs.map((r) => [r.input["id"], r.input["stroke"], r.input["one:history"]])).toEqual([["a", "#e4572e", undefined], ["b", "#e4572e", "joined"]]);
    act(() => styleThese("/ layer", []));
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

  it("picks a row from a table, which a command acts on with what it's given, drawn pressed while it already holds", async () => {
    const source = memorySource({ commands: { "studio::layer::scale": () => ({ id: "l2" }) } });
    source.set("studio::poster_page", { layers: [{ id: "l1", size: 10 }, { id: "l2", size: 20 }] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      const picked = usePicked("/ layer", page, "layers");
      return (
        <>
          <Table view={page} list="layers" columns={{ size: "Size" }} pick="/ layer" />
          <p>
            Picked <Live row={picked} field="size" />
          </p>
          <Command name="studio::layer::scale" id={picked?.id as string | undefined} label="Twice" given={{ by: 2 }} when={picked !== null} />
          <Command name="studio::layer::scale" id={picked?.id as string | undefined} label="Twenty" given={{ size: 20 }} pressed={picked?.["size"] === 20} when={picked !== null} />
        </>
      );
    }
    renderScreen(source, () => <Poster />);
    expect(screen.queryByRole("button", { name: "Twice" })).toBeNull();
    fireEvent.click(screen.getByText("20"));
    expect(screen.getByText(/Picked/).textContent).toBe("Picked 20");
    expect(screen.getByRole("button", { name: "Twenty" }).getAttribute("aria-pressed")).toBe("true");
    fireEvent.click(screen.getByRole("button", { name: "Twice" }));
    await waitFor(() => expect(source.runs).toEqual([{ command: "studio::layer::scale", input: { by: 2, id: "l2" } }]));
  });

  it("opens what a command made, at the route it says", async () => {
    const source = memorySource({ commands: { "studio::poster::create": () => ({ id: "p9" }) } });
    function Where() {
      return <p>At {useLocation().pathname}</p>;
    }
    const studio = defineScreen({ title: "Studio", route: "/" }, () => <Command name="studio::poster::create" label="New" given={{ width: 1920 }} thenOpen="/posters/:poster" />);
    const poster = defineScreen({ title: "Poster", route: "/posters/:poster" }, () => <Where />);
    render(<App name="studio" screens={[studio, poster]} ui={plain} data={source} location="/" />);
    fireEvent.click(screen.getByRole("button", { name: "New" }));
    await waitFor(() => expect(screen.getByText(/^At/).textContent).toBe("At /posters/p9"));
  });

  it("opens what a form opened from its button made, at the route it says", async () => {
    const source = memorySource({ commands: { "studio::poster::create": () => ({ id: "p9" }) } });
    function Where() {
      return <p>At {useLocation().pathname}</p>;
    }
    const studio = defineScreen({ title: "Studio", route: "/" }, () => <Form command="studio::poster::create" fields={["title"]} button opener="New" thenOpen="/posters/:poster" />);
    const poster = defineScreen({ title: "Poster", route: "/posters/:poster" }, () => <Where />);
    render(<App name="studio" screens={[studio, poster]} ui={plain} data={source} location="/" />);
    fireEvent.click(screen.getByRole("button", { name: "New" }));
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "Harbor" } });
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await waitFor(() => expect(screen.getByText(/^At/).textContent).toBe("At /posters/p9"));
    expect(source.runs).toEqual([{ command: "studio::poster::create", input: { title: "Harbor" } }]);
  });

  it("on open, opens the first row its condition holds for, or else runs a command, apart from undo, and opens what it made", async () => {
    function Where() {
      return <p>At {useLocation().pathname}</p>;
    }
    function Opening() {
      const posters = useView("studio::posters");
      const rows = (posters.data?.["rows"] as Record<string, unknown>[] | undefined) ?? [];
      return (
        <OnOpen
          ready={posters.status === "live"}
          lines={[
            { open: "/posters/:poster", rows, where: (row) => row["width"] === 1920 },
            { run: { name: "studio::poster::create", given: { width: 1920 }, thenOpen: "/posters/:poster" } },
          ]}
        />
      );
    }
    const opening = defineScreen({ title: "New", route: "/" }, () => <Opening />);
    const poster = defineScreen({ title: "Poster", route: "/posters/:poster" }, () => <Where />);
    const fits = memorySource({ commands: { "studio::poster::create": () => ({ id: "p9" }) } });
    fits.set("studio::posters", { rows: [{ id: "p1", width: 1280 }, { id: "p2", width: 1920 }] });
    const first = render(<App name="studio" screens={[opening, poster]} ui={plain} data={fits} location="/" />);
    await waitFor(() => expect(screen.getByText(/^At/).textContent).toBe("At /posters/p2"));
    expect(fits.runs).toEqual([]);
    first.unmount();
    const none = memorySource({ commands: { "studio::poster::create": () => ({ id: "p9" }) } });
    none.set("studio::posters", { rows: [] });
    render(<App name="studio" screens={[opening, poster]} ui={plain} data={none} location="/" />);
    await waitFor(() => expect(screen.getByText(/^At/).textContent).toBe("At /posters/p9"));
    // Never a step undo takes back: the person didn't do it.
    expect(none.runs).toEqual([{ command: "studio::poster::create", input: { width: 1920, "one:history": "apart" } }]);
  });
});
