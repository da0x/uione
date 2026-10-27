// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A paper: blocks in the order their positions say, set as their kinds say, written in
// place; Enter splitting one and Backspace joining it to the one before, each one step;
// "# " making a heading; and an empty page's first words making its first block.

import { act, fireEvent, render, waitFor } from "@testing-library/react";
import { App, Paper, PaperButton, memorySource, screen as defineScreen, useView } from "../src/index.js";
import type { MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";
import { pagesOf } from "../src/paper-view.js";

const parts = { words: "words", kind: "kind", align: "align", position: "position", face: "face" };

function renderPaper(source: MemorySource) {
  function Notes() {
    const page = useView("studio::notes_page", "p1");
    return (
      <>
      <PaperButton paper="/ paper" label="Equation" given={{ kind: "math" }} />
      <Paper
        name="/ paper"
        view={page}
        list="passages"
        fields={parts}
        write="studio::passage::write"
        writesKind
        add="studio::passage::create"
        remove="studio::passage::delete"
        parent="poster"
        subject="p1"
        pick="/ passage"
        empty="Title"
      />
      </>
    );
  }
  const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
  return render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
}

const answered = () => ({ id: "b9" });
function sourceWith(passages: Record<string, unknown>[]) {
  const source = memorySource({ commands: { "studio::passage::write": answered, "studio::passage::create": answered, "studio::passage::delete": answered } });
  source.set("studio::notes_page", { passages }, "p1");
  return source;
}

// The caret put at a place in a block's words.
function caretAt(el: HTMLElement, at: number) {
  const selection = document.getSelection()!;
  const text = el.firstChild ?? el;
  selection.collapse(text, at);
}

const blocks = (drawn: ReturnType<typeof render>) => [...drawn.container.querySelectorAll<HTMLElement>("[role=textbox]")];

describe("a paper", () => {
  it("sets its blocks in the order their positions say, each as its kind says and aligned as it says", async () => {
    const source = sourceWith([
      { id: "b2", kind: "text", words: "The water holds the sun.", position: 2, align: "left" },
      { id: "b1", kind: "title", words: "A harbor", position: 1, align: "center" },
      { id: "b3", kind: "math", words: "x^2", position: 3, align: "center" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(3));
    expect(blocks(drawn).map((b) => b.textContent)).toEqual(["A harbor", "The water holds the sun.", "x^2"]);
    expect(blocks(drawn)[0]!.style.textAlign).toBe("center");
    expect(drawn.container.querySelector("[data-kind=title]")).not.toBeNull();
    // An equation is set, as math.
    expect(drawn.container.querySelector("svg[role=img]")?.getAttribute("aria-label")).toBe("x^2");
  });

  it("sends what's typed once typing pauses", async () => {
    const source = sourceWith([{ id: "b1", kind: "text", words: "Hello", position: 1, align: "left" }]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const block = blocks(drawn)[0]!;
    block.textContent = "Hello there";
    fireEvent.input(block);
    expect(source.runs).toHaveLength(0);
    await waitFor(() => expect(source.runs).toHaveLength(1), { timeout: 2000 });
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "Hello there" } });
  });

  it("splits a block with Enter, the words after the caret made a block after it, as one step", async () => {
    const source = sourceWith([
      { id: "b1", kind: "quote", words: "Paint what you see", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "After", position: 2, align: "left" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    const block = blocks(drawn)[0]!;
    block.focus();
    caretAt(block, 5);
    fireEvent.keyDown(block, { key: "Enter" });
    await waitFor(() => expect(source.runs).toHaveLength(2));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "Paint" } });
    expect(source.runs[1]).toMatchObject({ command: "studio::passage::create", input: { words: " what you see", kind: "quote", position: 1.5, poster: "p1", "one:history": "joined" } });
  });

  it("joins a block to the one before with Backspace at its start, deleting it, as one step", async () => {
    const source = sourceWith([
      { id: "b1", kind: "text", words: "One ", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "two", position: 2, align: "left" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    const block = blocks(drawn)[1]!;
    block.focus();
    caretAt(block, 0);
    fireEvent.keyDown(block, { key: "Backspace" });
    await waitFor(() => expect(source.runs).toHaveLength(2));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "One two" } });
    expect(source.runs[1]).toMatchObject({ command: "studio::passage::delete", input: { id: "b2", "one:history": "joined" } });
  });

  it("makes a text block a heading when it starts with \"# \"", async () => {
    const source = sourceWith([{ id: "b1", kind: "text", words: "", position: 1, align: "left" }]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const block = blocks(drawn)[0]!;
    block.textContent = "# ";
    fireEvent.input(block);
    await waitFor(() => expect(source.runs).toHaveLength(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "", kind: "heading" } });
  });

  it("makes an empty page's first block, a title, from the first words typed on it", async () => {
    const source = sourceWith([]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    expect(drawn.container.textContent).toContain("Title");
    const block = blocks(drawn)[0]!;
    block.textContent = "A";
    await act(async () => {
      fireEvent.input(block);
    });
    await waitFor(() => expect(source.runs).toHaveLength(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::create", input: { words: "A", kind: "title", position: 1, poster: "p1" } });
  });

  it("inserts an empty block after the caret's from a button, given what it gives", async () => {
    const source = sourceWith([
      { id: "b1", kind: "text", words: "One", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "Two", position: 2, align: "left" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    fireEvent.focus(blocks(drawn)[0]!);
    fireEvent.click(drawn.getByRole("button", { name: "Equation" }));
    await waitFor(() => expect(source.runs).toHaveLength(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::create", input: { kind: "math", words: "", position: 1.5, poster: "p1" } });
  });

  it("starts a new line with Enter in code, and leaves it from an empty last line", async () => {
    const source = sourceWith([{ id: "b1", kind: "code", words: "let x = 1\n", position: 1, align: "left" }]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const block = blocks(drawn)[0]!;
    block.focus();
    caretAt(block, 4);
    fireEvent.keyDown(block, { key: "Enter" });
    expect(source.runs).toHaveLength(0);
    caretAt(block, "let x = 1\n".length);
    fireEvent.keyDown(block, { key: "Enter" });
    await waitFor(() => expect(source.runs).toHaveLength(2));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "let x = 1" } });
    expect(source.runs[1]).toMatchObject({ command: "studio::passage::create", input: { kind: "text", words: "" } });
  });

  it("shows a math block's words while the caret's in it, and its equation once it leaves, in the same place", async () => {
    const source = sourceWith([{ id: "b1", kind: "math", words: "x^2", position: 1, align: "center" }]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const block = blocks(drawn)[0]!;
    const equation = drawn.container.querySelector("svg[role=img]")!.parentElement!;
    fireEvent.blur(block);
    await waitFor(() => expect(block.style.opacity).toBe("0"));
    expect(equation.style.visibility).toBe("visible");
    expect(block.style.gridArea).toBe(equation.style.gridArea);
    // Pressing the equation puts the caret in its words, which then show instead.
    fireEvent.mouseDown(equation);
    expect(document.activeElement).toBe(block);
    fireEvent.focus(block);
    await waitFor(() => expect(block.style.opacity).toBe(""));
    expect(equation.style.visibility).toBe("hidden");
  });

  it("sets the page in its document's face, but for code and an equation's words", async () => {
    const source = memorySource({});
    source.set("studio::notes_page", { face: "book", passages: [
      { id: "b1", kind: "text", words: "Words", position: 1, align: "left" },
      { id: "b2", kind: "code", words: "let x = 1", position: 2, align: "left" },
    ] }, "p1");
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    expect((drawn.container.querySelector("[data-kind=text]") as HTMLElement).style.fontFamily).toContain("Literata");
    expect((drawn.container.querySelector("[data-kind=code]") as HTMLElement).style.fontFamily).toContain("mono");
  });

  it("lists its title and headings in an outline, marks the section the caret is in, and goes to one pressed", async () => {
    const source = sourceWith([
      { id: "b1", kind: "title", words: "A harbor", position: 1, align: "left" },
      { id: "b2", kind: "heading", words: "The light", position: 2, align: "left" },
      { id: "b3", kind: "text", words: "Warm.", position: 3, align: "left" },
      { id: "b4", kind: "heading", words: "The water", position: 4, align: "left" },
    ]);
    function Notes() {
      const page = useView("studio::notes_page", "p1");
      return <Paper view={page} list="passages" fields={parts} write="studio::passage::write" outline />;
    }
    const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    const nav = await waitFor(() => drawn.getByRole("navigation", { name: "On this page" }));
    const links = [...nav.querySelectorAll("a")];
    expect(links.map((a) => a.textContent)).toEqual(["A harbor", "The light", "The water"]);
    fireEvent.focus(blocks(drawn)[2]!);
    await waitFor(() => expect(links[1]!.getAttribute("aria-current")).toBe("location"));
    fireEvent.click(links[2]!);
    expect(document.activeElement).toBe(blocks(drawn)[3]);
    // A heading is ruled under, across the line.
    expect((drawn.container.querySelector("[data-kind=heading]") as HTMLElement).style.borderBottom).toContain("1px solid");
  });

  it("places blocks page by page: one that doesn't fit starts the next, and a heading goes with what it heads", () => {
    // Pages 100 tall with 10 of margin: 80 to write in.
    const text = (height: number) => ({ kind: "text", height });
    expect(pagesOf([text(30), text(30), text(30)], 100, 10)).toEqual({ tops: [10, 52, 10 + 100 + 28], pages: 2 });
    // The heading fits, but what it heads wouldn't after it, so both go to the next page.
    const placed = pagesOf([text(30), { kind: "heading", height: 10 }, text(30)], 100, 10);
    expect(placed.tops[1]).toBe(10 + 100 + 28);
    expect(placed.pages).toBe(2);
  });

  it("is read, not written on, while it's read only", async () => {
    const source = sourceWith([{ id: "b1", kind: "text", words: "Read me", position: 1, align: "left" }]);
    function Notes() {
      const page = useView("studio::notes_page", "p1");
      return <Paper view={page} list="passages" fields={parts} write="studio::passage::write" readOnly />;
    }
    const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    expect(blocks(drawn)[0]!.getAttribute("contenteditable")).toBe("false");
  });

  it("is made larger and smaller by the reader, a step at a time, until it can't be any more", async () => {
    const source = sourceWith([{ id: "b1", kind: "text", words: "Words", position: 1, align: "left" }]);
    function Notes() {
      const page = useView("studio::notes_page", "p1");
      return (
        <>
          <PaperButton paper="/ paper" label="Larger" sizes={1} />
          <PaperButton paper="/ paper" label="Smaller" sizes={-1} />
          <Paper view={page} list="passages" fields={parts} />
        </>
      );
    }
    const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const size = () => parseFloat((drawn.container.querySelector("[data-kind=text]") as HTMLElement).style.fontSize);
    const before = size();
    fireEvent.click(drawn.getByRole("button", { name: "Larger" }));
    await waitFor(() => expect(size()).toBeGreaterThan(before));
    for (let i = 0; i < 8; i++) fireEvent.click(drawn.getByRole("button", { name: "Smaller" }));
    await waitFor(() => expect((drawn.getByRole("button", { name: "Smaller" }) as HTMLButtonElement).disabled).toBe(true));
    expect(size()).toBeLessThan(before);
  });

  it("keeps what's typed on an empty page while its first block is made, making it once, and in the same place", async () => {
    const source = sourceWith([]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const block = blocks(drawn)[0]!;
    block.focus();
    block.textContent = "H";
    fireEvent.input(block);
    block.textContent = "He";
    fireEvent.input(block);
    block.textContent = "Hello";
    fireEvent.input(block);
    // Made once, with what it was first, and written with all of it once it's made.
    await waitFor(() => expect(source.runs.filter((r) => r.command === "studio::passage::create")).toHaveLength(1));
    await waitFor(() => expect(source.runs.at(-1)).toMatchObject({ command: "studio::passage::write", input: { id: "b9", words: "Hello" } }));
    // The view arrives with only what was first saved: what's typed stays, in the same element.
    await act(async () => {
      source.set("studio::notes_page", { passages: [{ id: "b9", kind: "title", words: "H", position: 1, align: "left" }] }, "p1");
    });
    expect(blocks(drawn)[0]).toBe(block);
    expect(block.textContent).toBe("Hello");
  });

  it("puts the caret in the block Enter made, even beside another at the same position", async () => {
    const source = sourceWith([
      { id: "b1", kind: "text", words: "One", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "Two", position: 1, align: "left" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    const block = blocks(drawn)[0]!;
    block.focus();
    caretAt(block, 3);
    fireEvent.keyDown(block, { key: "Enter" });
    await waitFor(() => expect(source.runs.some((r) => r.command === "studio::passage::create")).toBe(true));
    await act(async () => {
      source.set("studio::notes_page", { passages: [
        { id: "b1", kind: "text", words: "One", position: 1, align: "left" },
        { id: "b2", kind: "text", words: "Two", position: 1, align: "left" },
        { id: "b9", kind: "text", words: "", position: 1, align: "left" },
      ] }, "p1");
    });
    await waitFor(() => expect(blocks(drawn).length).toBe(3));
    expect(document.activeElement).toBe(blocks(drawn)[2]);
  });

  it("stays empty when its words are all erased, and saves them erased, never bringing back what was there", async () => {
    const source = sourceWith([{ id: "b1", kind: "title", words: "Title", position: 1, align: "left" }]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const block = blocks(drawn)[0]!;
    block.focus();
    for (const left of ["Titl", "Tit", "Ti", "T", ""]) {
      block.textContent = left;
      fireEvent.input(block);
    }
    expect(block.textContent).toBe("");
    await waitFor(() => expect(source.runs.at(-1)).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "" } }), { timeout: 2000 });
    expect(block.textContent).toBe("");
    block.textContent = "New";
    fireEvent.input(block);
    expect(block.textContent).toBe("New");
  });
});
