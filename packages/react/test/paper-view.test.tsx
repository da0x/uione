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
import { kindOf } from "../src/paper/index.js";
import { renderToStaticMarkup } from "react-dom/server";

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

  it("makes each paragraph of words pasted a block of its own, the first at the caret, as one step", async () => {
    const source = sourceWith([
      { id: "b1", kind: "title", words: "A harbor", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "After", position: 2, align: "left" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    const block = blocks(drawn)[0]!;
    block.focus();
    caretAt(block, 8);
    fireEvent.paste(block, { clipboardData: { files: [], getData: () => " at dawn\n\nThe boats go out.\r\nThe gulls follow.\n" } });
    await waitFor(() => expect(source.runs).toHaveLength(3));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "A harbor at dawn" } });
    expect(source.runs[1]).toMatchObject({ command: "studio::passage::create", input: { words: "The boats go out.", kind: "text", position: 1.5, "one:history": "joined" } });
    expect(source.runs[2]).toMatchObject({ command: "studio::passage::create", input: { words: "The gulls follow.", kind: "text", position: 1.75, "one:history": "joined" } });
  });

  it("writes an equation apart on a phone, its equation over its words, kept only when accepted", async () => {
    const was = window.matchMedia;
    window.matchMedia = ((query: string) => ({ matches: true, media: query, addEventListener() {}, removeEventListener() {} })) as unknown as typeof window.matchMedia;
    try {
      const source = sourceWith([
        { id: "b1", kind: "text", words: "Before", position: 1, align: "left" },
        { id: "b2", kind: "math", words: "x^2", position: 2, align: "center" },
      ]);
      const drawn = renderPaper(source);
      await waitFor(() => expect(blocks(drawn).length).toBe(2));
      act(() => blocks(drawn)[0]!.focus());
      act(() => blocks(drawn)[1]!.focus());
      const sheet = await drawn.findByRole("dialog", { name: "Equation" });
      const words = sheet.querySelector("textarea")!;
      expect(words.value).toBe("x^2");
      fireEvent.change(words, { target: { value: "x = a/b^2" } });
      expect(sheet.querySelector("svg[role=img]")?.getAttribute("aria-label")).toBe("x = a/b^2");
      fireEvent.change(words, { target: { value: "x = (a" } });
      expect(sheet.textContent).toContain("a ) to close");
      fireEvent.click(drawn.getByRole("button", { name: "Cancel" }));
      expect(drawn.queryByRole("dialog")).toBeNull();
      expect(source.runs).toHaveLength(0);
      act(() => blocks(drawn)[0]!.focus());
      act(() => blocks(drawn)[1]!.focus());
      fireEvent.change((await drawn.findByRole("dialog")).querySelector("textarea")!, { target: { value: "x = a/b^2" } });
      fireEvent.click(drawn.getByRole("button", { name: "Accept" }));
      await waitFor(() => expect(source.runs).toHaveLength(1));
      expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b2", words: "x = a/b^2" } });
    } finally {
      window.matchMedia = was;
    }
  });

  it("writes an equation apart with keys for all the notation writes, each put at the caret or around what's chosen", async () => {
    const was = window.matchMedia;
    window.matchMedia = ((query: string) => ({ matches: true, media: query, addEventListener() {}, removeEventListener() {} })) as unknown as typeof window.matchMedia;
    try {
      const source = sourceWith([
        { id: "b1", kind: "text", words: "Before", position: 1, align: "left" },
        { id: "b2", kind: "math", words: "x = ", position: 2, align: "center" },
      ]);
      const drawn = renderPaper(source);
      await waitFor(() => expect(blocks(drawn).length).toBe(2));
      act(() => blocks(drawn)[0]!.focus());
      act(() => blocks(drawn)[1]!.focus());
      const sheet = await drawn.findByRole("dialog", { name: "Equation" });
      const words = sheet.querySelector("textarea")!;
      words.setSelectionRange(4, 4);
      fireEvent.click(drawn.getByRole("tab", { name: "Powers" }));
      fireEvent.click(drawn.getByRole("button", { name: "Square root" }));
      expect(words.value).toBe("x = sqrt()");
      expect(words.selectionStart).toBe(9);
      fireEvent.click(drawn.getByRole("tab", { name: "Greek" }));
      fireEvent.click(drawn.getByRole("button", { name: "pi" }));
      expect(words.value).toBe("x = sqrt(pi)");
      // Around what's chosen; a name after a letter apart from it.
      words.setSelectionRange(9, 11);
      fireEvent.click(drawn.getByRole("tab", { name: "Powers" }));
      fireEvent.click(drawn.getByRole("button", { name: "Squared" }));
      expect(words.value).toBe("x = sqrt((pi)^2)");
      words.setSelectionRange(0, 1);
      fireEvent.click(drawn.getByRole("tab", { name: "Greek" }));
      fireEvent.click(drawn.getByRole("button", { name: "theta" }));
      expect(words.value).toBe("theta = sqrt((pi)^2)");
      expect(sheet.querySelector("svg[role=img]")?.getAttribute("aria-label")).toBe("theta = sqrt((pi)^2)");
    } finally {
      window.matchMedia = was;
    }
  });

  it("writes a chemical equation apart, saying as it's typed whether it balances, or what a mole of one thing weighs", async () => {
    const was = window.matchMedia;
    window.matchMedia = ((query: string) => ({ matches: true, media: query, addEventListener() {}, removeEventListener() {} })) as unknown as typeof window.matchMedia;
    try {
      const source = sourceWith([
        { id: "b1", kind: "text", words: "Before", position: 1, align: "left" },
        { id: "b2", kind: "chem", words: "H2 + O2 -> H2O", position: 2, align: "center" },
      ]);
      const drawn = renderPaper(source);
      await waitFor(() => expect(blocks(drawn).length).toBe(2));
      act(() => blocks(drawn)[0]!.focus());
      act(() => blocks(drawn)[1]!.focus());
      const sheet = await drawn.findByRole("dialog", { name: "Chemistry" });
      const words = sheet.querySelector("textarea")!;
      expect(sheet.textContent).toContain("not balanced: 2 O on the left, 1 on the right");
      words.setSelectionRange(0, 0);
      fireEvent.click(drawn.getByRole("tab", { name: "Common" }));
      fireEvent.click(drawn.getByRole("button", { name: "H" }));
      // Letters put together, as a formula's are.
      expect(words.value).toBe("HH2 + O2 -> H2O");
      fireEvent.change(words, { target: { value: "2H2 + O2 -> 2H2O" } });
      expect(sheet.textContent).toContain("Balanced");
      fireEvent.change(words, { target: { value: "H2O" } });
      expect(sheet.textContent).toContain("18.02 g/mol");
      fireEvent.change(words, { target: { value: "Xy" } });
      expect(sheet.textContent).toContain("there's no element Xy");
    } finally {
      window.matchMedia = was;
    }
  });

  it("sets the face of every chemistry key, as ce( ) reads it", () => {
    const tabs = kindOf("chem").apart?.keys ?? [];
    expect(tabs.map((t) => t.name)).toEqual(["Common", "Arrows", "States", "Charges", "Elements"]);
    const unset = tabs.flatMap((t) => t.keys.filter((k) => !renderToStaticMarkup(<>{k.face}</>).includes('role="img"')).map((k) => `${t.name}: ${k.name}`));
    expect(unset).toEqual([]);
  });

  it("sets the face of every equation key, as the notation reads it", () => {
    const tabs = kindOf("math").apart?.keys ?? [];
    expect(tabs.map((t) => t.name)).toEqual(["Basic", "Powers", "Greek", "Calculus", "Functions", "Layout"]);
    const unset = tabs.flatMap((t) => t.keys.filter((k) => !renderToStaticMarkup(<>{k.face}</>).includes('role="img"')).map((k) => `${t.name}: ${k.name}`));
    expect(unset).toEqual([]);
  });

  it("keeps the lines of words pasted into code", async () => {
    const source = sourceWith([{ id: "b1", kind: "code", words: "", position: 1, align: "left" }]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(1));
    const block = blocks(drawn)[0]!;
    block.focus();
    fireEvent.paste(block, { clipboardData: { files: [], getData: () => "one\ntwo" } });
    expect(source.runs.some((r) => r.command === "studio::passage::create")).toBe(false);
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

  it("inserts where the caret was before the button took it: between a block's halves from its middle, before it from its start", async () => {
    const source = sourceWith([
      { id: "b1", kind: "text", words: "One two", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "Three", position: 2, align: "left" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    const caretAt = (el: HTMLElement, at: number) => {
      fireEvent.focus(el);
      const range = document.createRange();
      range.setStart(el.firstChild!, at);
      range.collapse(true);
      document.getSelection()!.removeAllRanges();
      document.getSelection()!.addRange(range);
      fireEvent(document, new Event("selectionchange"));
      // The button takes the selection with it, as a phone's does.
      document.getSelection()!.removeAllRanges();
    };
    caretAt(blocks(drawn)[0]!, 4);
    fireEvent.click(drawn.getByRole("button", { name: "Equation" }));
    await waitFor(() => expect(source.runs).toHaveLength(3));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "One " } });
    expect(source.runs[1]).toMatchObject({ command: "studio::passage::create", input: { kind: "math", words: "", position: 1.5, "one:history": "joined" } });
    expect(source.runs[2]).toMatchObject({ command: "studio::passage::create", input: { kind: "text", words: "two", position: 1.75, "one:history": "joined" } });
    caretAt(blocks(drawn).at(-1)!, 0);
    fireEvent.click(drawn.getByRole("button", { name: "Equation" }));
    await waitFor(() => expect(source.runs).toHaveLength(4));
    expect(source.runs[3]!.input).toMatchObject({ kind: "math", words: "" });
    // Before Three, after One two, as the page holds only those.
    expect(source.runs[3]!.input["position"]).toBe(1.5);
    expect(source.runs).toHaveLength(4);
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
    expect(document.getSelection()!.anchorOffset).toBe(0);
    // A heading is ruled under, across the line.
    expect((drawn.container.querySelector("[data-kind=heading]") as HTMLElement).style.borderBottom).toContain("1px solid");
  });

  it("is a drawer at its side while its outline says, there only then", async () => {
    const source = sourceWith([{ id: "b1", kind: "title", words: "A harbor", position: 1, align: "left" }]);
    function Notes({ open }: { open: boolean }) {
      const page = useView("studio::notes_page", "p1");
      return <Paper view={page} list="passages" fields={parts} write="studio::passage::write" outline={open} outlineDrawer />;
    }
    const closed = defineScreen({ title: "Notes", route: "/" }, () => <Notes open={false} />);
    const shut = render(<App name="studio" screens={[closed]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(blocks(shut).length).toBe(1));
    expect(shut.queryByRole("navigation", { name: "On this page" })).toBeNull();
    shut.unmount();
    const opened = defineScreen({ title: "Notes", route: "/" }, () => <Notes open />);
    const open = render(<App name="studio" screens={[opened]} ui={plain} data={source} location="/" />);
    const drawer = await waitFor(() => open.getByRole("navigation", { name: "On this page" }));
    // As wide as a workspace's side, and nothing on the page's other side to balance it.
    expect(drawer.style.width).toBe("288px");
    expect(drawer.parentElement!.children.length).toBe(2);
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

  it("shows a picture as wide as it says, its caption under it, and makes dropped pictures picture blocks after the caret's", async () => {
    globalThis.createImageBitmap = (async () => ({ width: 400, height: 200, close() {} })) as unknown as typeof createImageBitmap;
    const source = memorySource({ files: true, commands: { "studio::passage::create": () => ({ id: "b9" }) } });
    source.set("studio::notes_page", { passages: [
      { id: "b1", kind: "text", words: "Before", position: 1, align: "left" },
      { id: "p1", kind: "picture", words: "A caption", position: 2, align: "center", picture: "harbor.png", picture_width: 900, picture_height: 450, width: 0.5, corners: 0 },
    ] }, "p1");
    const all = { ...parts, picture: "picture", picture_width: "picture_width", picture_height: "picture_height", width: "width", corners: "corners" };
    function Notes() {
      const page = useView("studio::notes_page", "p1");
      return <Paper view={page} list="passages" fields={all} add="studio::passage::create" parent="poster" subject="p1" paste />;
    }
    const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    const photo = drawn.container.querySelector("[data-kind=picture]")!;
    // Half the page's width, its own shape, and its caption is its words.
    const box = photo.firstElementChild as HTMLElement;
    expect(parseFloat(box.style.width) / parseFloat(box.style.height)).toBeCloseTo(2);
    expect(blocks(drawn)[1]!.textContent).toBe("A caption");
    // Pressed, it's picked, its caret in the caption, and ringed so that shows; and not
    // once the caret's elsewhere.
    act(() => blocks(drawn)[0]!.focus());
    await waitFor(() => expect(box.style.outline).toBe(""));
    fireEvent.mouseDown(box);
    expect(document.activeElement).toBe(blocks(drawn)[1]);
    await waitFor(() => expect(box.style.outline).toContain("solid"));
    // A picture dropped while the caret's in the first block goes after it.
    fireEvent.focus(blocks(drawn)[0]!);
    const file = new File(["a"], "a.png", { type: "image/png" });
    fireEvent.drop(drawn.container.querySelector(".one-paper")!, { dataTransfer: { files: [file], getData: () => "", types: ["Files"] } });
    await waitFor(() => expect(source.runs.some((r) => r.command === "studio::passage::create")).toBe(true));
    const made = source.runs.find((r) => r.command === "studio::passage::create")!.input as Record<string, unknown>;
    expect(made).toMatchObject({ kind: "picture", picture_width: 400, picture_height: 200, position: 1.5, poster: "p1" });
    expect(source.uploads.map((u) => u.file.name)).toEqual(["a.png"]);
  });

  it("makes a tall picture no taller than a page has room for, with a line of caption, narrower instead", async () => {
    const source = memorySource({ files: true });
    source.set("studio::notes_page", { passages: [
      { id: "p1", kind: "picture", words: "", position: 1, align: "center", picture: "tower.png", picture_width: 400, picture_height: 1600, width: 1, corners: 0 },
    ] }, "p1");
    const all = { ...parts, picture: "picture", picture_width: "picture_width", picture_height: "picture_height", width: "width", corners: "corners" };
    function Notes() {
      const page = useView("studio::notes_page", "p1");
      return <Paper view={page} list="passages" fields={all} />;
    }
    const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("[data-kind=picture]")).not.toBeNull());
    const box = drawn.container.querySelector("[data-kind=picture]")!.firstElementChild as HTMLElement;
    const w = parseFloat(box.style.width), h = parseFloat(box.style.height);
    // A letter page 816 wide is 1056 tall, an inch of margin each side, and the gap and
    // the caption's line are under it, as large as the reader has the words.
    expect(h).toBeLessThan(1056 - 144);
    expect(h).toBeGreaterThan(1056 - 144 - 2 * (8 + 21));
    expect(h / w).toBeCloseTo(4);
  });

  it("opens the device's file chooser for pictures from an add pictures button, and makes those chosen picture blocks after the caret's", async () => {
    globalThis.createImageBitmap = (async () => ({ width: 300, height: 300, close() {} })) as unknown as typeof createImageBitmap;
    const source = memorySource({ files: true, commands: { "studio::passage::create": () => ({ id: "b9" }) } });
    source.set("studio::notes_page", { passages: [
      { id: "b1", kind: "text", words: "Before", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "After", position: 2, align: "left" },
    ] }, "p1");
    const all = { ...parts, picture: "picture", picture_width: "picture_width", picture_height: "picture_height" };
    function Notes() {
      const page = useView("studio::notes_page", "p1");
      return (
        <>
          <PaperButton paper="/ paper" label="Photo" chooses />
          <Paper name="/ paper" view={page} list="passages" fields={all} add="studio::passage::create" parent="poster" subject="p1" paste />
        </>
      );
    }
    const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(blocks(drawn).length).toBe(2));
    fireEvent.focus(blocks(drawn)[0]!);
    // The chooser is a file input for pictures, opened by the press, and gone once chosen.
    let opened: HTMLInputElement | undefined;
    const click = HTMLInputElement.prototype.click;
    HTMLInputElement.prototype.click = function (this: HTMLInputElement) {
      opened = this;
    };
    try {
      fireEvent.click(drawn.getByRole("button", { name: "Photo" }));
    } finally {
      HTMLInputElement.prototype.click = click;
    }
    expect(opened).toMatchObject({ type: "file", accept: "image/*", multiple: true });
    const file = new File(["a"], "beach.jpg", { type: "image/jpeg" });
    Object.defineProperty(opened!, "files", { value: [file] });
    fireEvent.change(opened!);
    expect(opened!.isConnected).toBe(false);
    await waitFor(() => expect(source.runs.some((r) => r.command === "studio::passage::create")).toBe(true));
    const made = source.runs.find((r) => r.command === "studio::passage::create")!.input as Record<string, unknown>;
    expect(made).toMatchObject({ kind: "picture", picture_width: 300, picture_height: 300, position: 1.5, poster: "p1" });
    expect(source.uploads.map((u) => u.file.name)).toEqual(["beach.jpg"]);
  });

  it("runs a selection across blocks with Shift, copies it a line a block, and erases it as one step, the blocks between deleted", async () => {
    const source = sourceWith([
      { id: "b1", kind: "title", words: "A harbor", position: 1, align: "left" },
      { id: "b2", kind: "text", words: "The light was warm.", position: 2, align: "left" },
      { id: "b3", kind: "text", words: "The boats were still.", position: 3, align: "left" },
    ]);
    const drawn = renderPaper(source);
    await waitFor(() => expect(blocks(drawn).length).toBe(3));
    const [title, middle, last] = blocks(drawn) as [HTMLElement, HTMLElement, HTMLElement];
    // From after "A " to the title's end, then on into the next block, and the one after.
    act(() => title.focus());
    fireEvent.focus(title);
    document.getSelection()!.setBaseAndExtent(title.firstChild!, 2, title.firstChild!, 8);
    fireEvent.keyDown(title, { key: "ArrowRight", shiftKey: true });
    expect(document.activeElement).toBe(middle);
    fireEvent.focus(middle);
    fireEvent.keyDown(middle, { key: "ArrowDown", shiftKey: true });
    expect(document.activeElement).toBe(last);
    fireEvent.focus(last);
    // The caret's block is chosen from its start; Shift and Right moves the caret on in it.
    document.getSelection()!.setBaseAndExtent(last.firstChild!, 0, last.firstChild!, 9);
    const copied: Record<string, string> = {};
    fireEvent.copy(last, { clipboardData: { setData: (type: string, words: string) => (copied[type] = words) } });
    expect(copied["text/plain"]).toBe("harbor\nThe light was warm.\nThe boats");
    fireEvent.keyDown(last, { key: "Backspace" });
    await waitFor(() => expect(source.runs.length).toBe(3));
    expect(source.runs[0]).toMatchObject({ command: "studio::passage::write", input: { id: "b1", words: "A  were still." } });
    expect(source.runs[1]).toMatchObject({ command: "studio::passage::delete", input: { id: "b2", "one:history": "joined" } });
    expect(source.runs[2]).toMatchObject({ command: "studio::passage::delete", input: { id: "b3", "one:history": "joined" } });
    expect(title.textContent).toBe("A  were still.");
    expect(document.activeElement).toBe(title);
  });

  it("shows how far along it's been read, across its top", async () => {
    const source = sourceWith([{ id: "b1", kind: "text", words: "Words", position: 1, align: "left" }]);
    function Notes() {
      const page = useView("studio::notes_page", "p1");
      return <Paper view={page} list="passages" fields={parts} progress fills />;
    }
    const only = defineScreen({ title: "Notes", route: "/" }, () => <Notes />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    const bar = await waitFor(() => drawn.getByRole("progressbar", { name: "Read" }));
    const scroller = drawn.container.querySelector(".one-paper") as HTMLElement;
    Object.defineProperty(scroller, "scrollHeight", { value: 2000, configurable: true });
    Object.defineProperty(scroller, "clientHeight", { value: 1000, configurable: true });
    scroller.scrollTop = 500;
    fireEvent.scroll(scroller);
    await waitFor(() => expect(bar.getAttribute("aria-valuenow")).toBe("50"));
  });
});
