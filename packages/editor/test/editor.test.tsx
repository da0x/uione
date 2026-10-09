// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { CompletionContext } from "@codemirror/autocomplete";
import { insertTab } from "@codemirror/commands";
import { diagnosticCount, forceLinting } from "@codemirror/lint";
import { EditorState, Text } from "@codemirror/state";
import type { Extension } from "@codemirror/state";
import { EditorView } from "@codemirror/view";
import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import type { Built, Checked, Compiler, Completions, Definition, Files } from "@uione/compiler";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { Diff, Editor, Generated, Workbench, completionsFrom, readSpot, writeSpot, fromLine, highlighting, placed, problems, setTabWidth, tabs, themesOf, lightThemes, darkThemes, loadThemes, LookControls } from "../src/index.js";
import { colorsOf } from "../src/highlight.js";

// jsdom lays nothing out, so the editor's measuring of text gets empty boxes.
Range.prototype.getClientRects ??= () => ({ length: 0, item: () => null, [Symbol.iterator]: [][Symbol.iterator] }) as unknown as DOMRectList;
Range.prototype.getBoundingClientRect ??= () => new DOMRect();

// Tests run from packages/editor.
const repository = resolve(process.cwd(), "../..") + "/";
const tasks = readFileSync(`${repository}examples/tasks/main.one`, "utf8");

function editor(doc: string, ...extensions: Extension[]) {
  return new EditorView({ parent: document.body, state: EditorState.create({ doc, extensions }) });
}

describe("highlighting", () => {
  it("colors a .one file as the grammar says, for light and dark", () => {
    const view = editor("entity book {\n\ttitle  text  required\n}\n", highlighting());
    const colored = [...view.contentDOM.querySelectorAll("span[style]")].map((s) => [s.textContent, s.getAttribute("style")]);
    expect(colored.find(([text]) => text === "entity")?.[1]).toMatch(/^color: [^;]+; --one-dark: [^;]+;?$/);
    expect(colored.find(([text]) => text === "text")).toBeDefined();
    view.destroy();
  });
});

describe("problems", () => {
  it("are placed on the word they're about", () => {
    const doc = Text.of(["entity book {", "\tdueAt  date", "}"]);
    expect(placed(doc, { line: 2, column: 2 })).toEqual({ from: 15, to: 20 });
    expect(placed(doc, { line: 9, column: 1 })).toEqual({ from: 27, to: 28 }); // past the end: the last line
  });

  it("show only this file's, from a check of the whole project", async () => {
    const asked: Files[] = [];
    const compiler = {
      check: async (files: Files): Promise<Checked> => {
        asked.push(files);
        return {
          files: 2,
          project: null,
          screens: [],
          problems: [
            { path: "main.one", line: 2, column: 2, message: "'dueAt' isn't snake_case; write it as due_at" },
            { path: "other.one", line: 1, column: 1, message: "elsewhere" },
          ],
        };
      },
    };
    const view = editor("entity book {\n\tdueAt  date\n}\n", problems({ compiler, path: "main.one", files: () => ({ "other.one": "x" }), delay: 0 }));
    forceLinting(view);
    await waitFor(() => expect(diagnosticCount(view.state)).toBe(1));
    expect(asked[0]).toEqual({ "other.one": "x", "main.one": "entity book {\n\tdueAt  date\n}\n" });
    view.destroy();
  });
});

describe("tabs", () => {
  it("insert a tab, shown as wide as the reader likes", () => {
    const view = editor("title", tabs(4));
    expect(view.state.tabSize).toBe(4);
    insertTab(view);
    expect(view.state.doc.toString()).toBe("\ttitle");
    setTabWidth(view, 8);
    expect(view.state.tabSize).toBe(8);
    expect(view.state.doc.toString()).toBe("\ttitle");
    view.destroy();
  });
});

describe("the code each line becomes", () => {
  const built = {
    files: [
      { path: "a.go", content: "x\ny\nz\n", executable: false, sources: [{ path: "main.one", line: 3 }, "same", { path: "main.one", line: 3 }] },
      { path: "b.tsx", content: "q\n", executable: false, sources: [{ path: "other.one", line: 3 }] },
    ],
  } as unknown as Built;

  it("is found by the line it came from", () => {
    expect(fromLine(built.files, "main.one", 3)).toEqual(new Map([["a.go", [1, 3]]]));
    expect(fromLine(built.files, "main.one", 4).size).toBe(0);
  });

  it("is shown with those lines marked", () => {
    const { container } = render(<Generated files={built.files} path="main.one" line={3} />);
    expect([...container.querySelectorAll(".uione-from-here")].map((l) => l.getAttribute("data-line"))).toEqual(["1", "3"]);
    fireEvent.change(screen.getByLabelText("Generated file"), { target: { value: "b.tsx" } });
    expect(container.querySelectorAll(".uione-from-here")).toHaveLength(0);
  });
});

describe("a workbench, with the real compiler", async () => {
  const { run } = await import("../../compiler/src/project.js");
  // The Emscripten build, made by tools/wasm/build.
  const { default: load } = await import(`${repository}compiler/build-wasm/one.mjs`);
  // Under jsdom the module would look for one.wasm at a web address, so it's handed
  // the file instead.
  const bytes = readFileSync(`${repository}compiler/build-wasm/one.wasm`);
  const one = await load({
    instantiateWasm: (imports: WebAssembly.Imports, receive: (instance: WebAssembly.Instance) => void) => {
      WebAssembly.instantiate(bytes, imports).then((made) => receive(made.instance));
      return {};
    },
  });
  const compiler: Pick<Compiler, "check" | "build" | "define" | "complete"> = {
    check: async (files) => run(one, { kind: "check", files }) as Checked,
    build: async (files) => run(one, { kind: "build", files }) as Built,
    define: async (files, path, line, column) => run(one, { kind: "define", files, path, line, column }) as Definition,
    complete: async (text, line, column) => run(one, { kind: "complete", text, line, column }) as Completions,
  };

  it("marks the code a line becomes, once the project is built", async () => {
    const line = tasks.split("\n").findIndex((l) => l.includes("done = true")) + 1;
    const { container } = render(<Workbench compiler={compiler} files={{ "main.one": tasks }} path="main.one" onChange={() => {}} delay={0} />);
    const view = EditorView.findFromDOM(container.querySelector(".cm-editor") as HTMLElement)!;
    act(() => view.dispatch({ selection: { anchor: view.state.doc.line(line).from } }));
    await waitFor(() => expect(container.querySelector(".uione-from-here")?.textContent).toContain("t.Done = true"));
    expect((screen.getByLabelText("Generated file") as HTMLSelectElement).value).toBe("api/tasks/tasks.go");
  });

  it("can be the editor alone, with line numbers, and builds nothing then", async () => {
    let built = 0;
    const counting = { ...compiler, build: async (files: Files) => (built++, compiler.build(files)) };
    const { container } = render(
      <Workbench compiler={counting} files={{ "main.one": tasks }} path="main.one" onChange={() => {}} delay={0} generated={false} />,
    );
    expect(container.querySelector(".uione-generated")).toBeNull();
    expect(container.querySelector(".cm-lineNumbers")).not.toBeNull();
    await new Promise((resolve) => setTimeout(resolve, 20));
    expect(built).toBe(0);
  });

  it("goes to where a name is declared, in another file or this one", async () => {
    const files = {
      "projects.one": "namespace tracker {\n\tentity project {\n\t\ttakes_reports  boolean = false\n\t}\n}\n",
      "reports.one":
        "namespace tracker {\n\tentity report {\n\t\tproject  project  required\n\t}\n" +
        '\tcommand report::create {\n\t\tby anyone signed in\n\t\trequire project.takes_reports  "no"\n\t}\n}\n',
    };
    const went: [string, number, number][] = [];
    const { container, unmount } = render(
      <Workbench compiler={compiler} files={files} path="reports.one" onChange={() => {}} generated={false} onGo={(...to) => went.push(to)} />,
    );
    const view = EditorView.findFromDOM(container.querySelector(".cm-editor") as HTMLElement)!;
    const line7 = view.state.doc.line(7);
    act(() => view.dispatch({ selection: { anchor: line7.from + line7.text.indexOf("takes_reports") + 2 } }));
    fireEvent.keyDown(container.querySelector(".cm-content")!, { key: "F12" });
    await waitFor(() => expect(went).toEqual([["projects.one", 3, 3]]));
    unmount();

    // With nowhere to go but this file, the cursor goes there itself.
    const alone = render(<Workbench compiler={compiler} files={files} path="reports.one" onChange={() => {}} generated={false} />);
    const editor = EditorView.findFromDOM(alone.container.querySelector(".cm-editor") as HTMLElement)!;
    act(() => editor.dispatch({ selection: { anchor: line7.from + line7.text.indexOf("project") + 1 } }));
    fireEvent.keyDown(alone.container.querySelector(".cm-content")!, { key: "F12" });
    await waitFor(() => expect(editor.state.doc.lineAt(editor.state.selection.main.head).number).toBe(3));
  });

  it("offers a project's settings as they're typed, and an enum setting's choices", async () => {
    const offered = async (doc: string, explicit = false) => {
      const state = EditorState.create({ doc });
      return completionsFrom({ compiler })(new CompletionContext(state, doc.length, explicit));
    };
    const settings = await offered("import one\nproject shop {\n\tcor");
    expect(settings?.from).toBe("import one\nproject shop {\n\t".length);
    expect(settings?.options).toContainEqual({ label: "corners", detail: "corners", info: "How corners are drawn." });
    const choices = await offered("import one\nproject shop {\n\tcorners  ", true);
    expect(choices?.options.map((o) => o.label)).toEqual(["square", "round"]);
    // Nothing is offered before a word is begun, unless it's asked for.
    expect(await offered("import one\nproject shop {\n\tcorners  ")).toBeNull();
    expect(await offered("entity book {\n\tti")).toBeNull();
  });

  it("colors only a .one file with uione's words", () => {
    const code = "entity book {\n\ttitle  text\n}\n";
    const colored = (path: string) => {
      const { container, unmount } = render(<Workbench compiler={compiler} files={{ [path]: code }} path={path} onChange={() => {}} generated={false} />);
      const count = container.querySelectorAll(".cm-content span[style]").length;
      unmount();
      return count;
    };
    expect(colored("main.one")).toBeGreaterThan(0);
    expect(colored("components/book.tsx")).toBe(0);
    expect(colored("README.md")).toBe(0);
  });

  it("can be only read, by someone who can't change the file", () => {
    let changed = false;
    const { container } = render(
      <Workbench compiler={compiler} files={{ "main.one": tasks }} path="main.one" onChange={() => (changed = true)} generated={false} readOnly />,
    );
    const view = EditorView.findFromDOM(container.querySelector(".cm-editor") as HTMLElement)!;
    expect(view.state.readOnly).toBe(true);
    expect(container.querySelector(".cm-content")?.getAttribute("contenteditable")).toBe("false");
    expect(changed).toBe(false);
  });
});

describe("diff", () => {
  it("shows the text after, with what was taken away above what replaced it", () => {
    render(<Diff path="main.one" before={"entity book {\n\ttitle  text\n}\n"} after={"entity book {\n\ttitle  text  required\n}\n"} />);
    const shown = screen.getByLabelText("Changes to main.one");
    expect(shown.textContent).toContain("title  text  required");
    const removed = document.querySelector(".cm-deletedChunk");
    expect(removed?.textContent).toContain("title  text");
    expect(removed?.textContent).not.toContain("required");
    expect(shown.getAttribute("contenteditable")).toBe("false");
  });
});

describe("themes", () => {
  it("colors each kind of word in the light and the dark theme chosen, once they're loaded", async () => {
    const code = "entity book {\n}\n";
    const github = colorsOf(code, "entity");
    expect(github).toBeDefined();
    const chosen = themesOf("PaperColor Light", "Nord");
    expect(colorsOf(code, "entity", 0, chosen)).toEqual(github); // not loaded yet, so GitHub's
    await loadThemes(chosen);
    const colored = colorsOf(code, "entity", 0, chosen);
    expect(colored?.light).toBe("#D70087"); // PaperColor Light's keyword
    expect(colored?.dark).not.toBe(github?.dark); // Nord's
  });

  it("shows Nord on its own blue-gray, as dark as the page", async () => {
    const nord = await darkThemes.find((t) => t.name === "Nord")!.load();
    expect(nord.bg).toBe("#1e2229");
    expect(nord.colors?.["editor.background"]).toBe("#1e2229");
  });

  it("keeps a list of themes for a light page and one for a dark page, and GitHub's for a name it doesn't know", () => {
    expect(lightThemes.every((t) => !t.dark)).toBe(true);
    expect(darkThemes.every((t) => t.dark)).toBe(true);
    expect(lightThemes.map((t) => t.name)).toContain("PaperColor Light");
    expect(darkThemes.map((t) => t.name)).toContain("Monokai");
    // A dark theme isn't a light page's, and the other way around.
    expect(themesOf("Monokai", "GitHub Light")).toEqual({ light: lightThemes[0], dark: darkThemes[0] });
  });

  it("gives the editor its themes' own backgrounds, or the site's ground for code when its theme gives one", async () => {
    const checker = { check: async () => ({ problems: [] }) } as unknown as Pick<Compiler, "check">;
    const text = "entity book {\n}\n";
    const { container } = render(<Editor path="main.one" value={text} onChange={() => {}} files={{ "main.one": text }} compiler={checker} themes={{ dark: "Dracula" }} />);
    await waitFor(() => expect(container.querySelector(".cm-editor.uione-themed")).not.toBeNull());
    const style = (container.querySelector(".cm-editor")!.getAttribute("style") ?? "").replace(/\s/g, "").toLowerCase();
    expect(style).toContain("--one-bg-dark:var(--uione-code-ground,#282a36)");
  });

  it("offers only the themes for the page as it is, and keeps the other page's choice", async () => {
    document.documentElement.setAttribute("data-theme", "light");
    const looks: unknown[] = [];
    render(<LookControls look={{ size: 14, font: "IBM Plex Mono", tabWidth: 4, darkTheme: "Nord" }} onLook={(l) => looks.push(l)} />);
    fireEvent.click(screen.getByRole("button", { name: /Light theme/ }));
    const offered = Array.from(document.querySelectorAll(".uione-theme-card .uione-theme-name")).map((c) => c.textContent);
    expect(offered).toEqual(lightThemes.map((t) => t.name));
    fireEvent.click(screen.getByRole("radio", { name: /One Light/ }));
    expect(looks.at(-1)).toMatchObject({ lightTheme: "One Light", darkTheme: "Nord" });
    document.documentElement.removeAttribute("data-theme");
  });

  it("shows the font as an icon, and names the fonts once it's opened", () => {
    const looks: unknown[] = [];
    render(<LookControls look={{ size: 14, font: "IBM Plex Mono", tabWidth: 4 }} onLook={(l) => looks.push(l)} />);
    const button = screen.getByRole("button", { name: "Font: IBM Plex Mono" });
    expect(button.textContent).toBe("");
    expect(screen.queryByRole("radio", { name: "Fira Code" })).toBeNull();
    fireEvent.click(button);
    fireEvent.click(screen.getByRole("radio", { name: "Fira Code" }));
    expect(looks.at(-1)).toMatchObject({ font: "Fira Code" });
    expect(screen.queryByRole("radio", { name: "Fira Code" })).toBeNull();
  });
});

describe("changes", () => {
  it("reports what's typed, and not the text it's handed", () => {
    const checker = { check: async () => ({ problems: [] }) } as unknown as Pick<Compiler, "check">;
    const changed: string[] = [];
    const shown = (value: string) => (
      <Editor path="main.one" value={value} onChange={(text) => changed.push(text)} files={{ "main.one": value }} compiler={checker} />
    );
    const { rerender } = render(shown(""));
    rerender(shown("entity book {\n}\n"));
    expect(changed).toEqual([]); // the file arriving isn't anyone changing it
    const view = EditorView.findFromDOM(document.querySelector(".cm-editor") as HTMLElement)!;
    act(() => view.dispatch({ changes: { from: 0, insert: "// A note\n" } }));
    expect(changed).toEqual(["// A note\nentity book {\n}\n"]);
  });
});

describe("spots", () => {
  it("reads and writes the place an address names, the way GitHub's do", () => {
    expect(readSpot("#L12")).toEqual({ from: { line: 12 } });
    expect(readSpot("L12C5")).toEqual({ from: { line: 12, column: 5 } });
    expect(readSpot("#L12-L20")).toEqual({ from: { line: 12 }, to: { line: 20 } });
    expect(readSpot("#L12C5-L14C2")).toEqual({ from: { line: 12, column: 5 }, to: { line: 14, column: 2 } });
    for (const nonsense of ["", "#", "#top", "#L0", "#L3C0", "#L1-L2-L3", "#L4-x"]) expect(readSpot(nonsense)).toBeUndefined();
    for (const written of ["L12", "L12C5", "L12-L20", "L12C5-L14C2"]) expect(writeSpot(readSpot(written)!)).toBe(written);
  });

  it("puts the cursor where the address says, marks its lines, and says where it moves", () => {
    const text = "entity book {\n\ttitle  text\n\tauthor  text\n}\n";
    const moved: string[] = [];
    const checker = { check: async () => ({ problems: [] }) } as unknown as Pick<Compiler, "check">;
    render(
      <Editor
        path="main.one"
        value={text}
        onChange={() => {}}
        files={{ "main.one": text }}
        compiler={checker}
        at={{ from: { line: 2 }, to: { line: 3 } }}
        onSelect={(spot) => moved.push(writeSpot(spot))}
      />,
    );
    const view = EditorView.findFromDOM(document.querySelector(".cm-editor") as HTMLElement)!;
    expect(view.state.doc.lineAt(view.state.selection.main.from).number).toBe(2);
    expect(view.state.doc.lineAt(view.state.selection.main.to).number).toBe(3);
    expect(document.querySelectorAll(".uione-marked").length).toBe(2);
    expect(moved).toEqual([]); // putting it there isn't the person moving it
    act(() => view.dispatch({ selection: { anchor: view.state.doc.line(4).from } }));
    expect(moved).toEqual(["L4C1"]);
    expect(document.querySelectorAll(".uione-marked").length).toBe(0);
  });

  it("puts the cursor where the address says once the file's text arrives", () => {
    const text = "one\ntwo\nthree\nfour\n";
    const checker = { check: async () => ({ problems: [] }) } as unknown as Pick<Compiler, "check">;
    const at = { from: { line: 3 }, to: { line: 4 } };
    const shown = (value: string) => (
      <Editor path="main.one" value={value} onChange={() => {}} files={{ "main.one": value }} compiler={checker} at={at} />
    );
    const { rerender } = render(shown(""));
    rerender(shown(text));
    const view = EditorView.findFromDOM(document.querySelector(".cm-editor") as HTMLElement)!;
    expect(view.state.doc.lineAt(view.state.selection.main.from).number).toBe(3);
    expect(document.querySelectorAll(".uione-marked").length).toBe(2);
  });
});

describe("toolbar", () => {
  // The reader's choices are kept in localStorage, which this test environment lacks.
  const kept = new Map<string, string>();
  Object.defineProperty(globalThis, "localStorage", {
    configurable: true,
    value: {
      getItem: (k: string) => kept.get(k) ?? null,
      setItem: (k: string, v: string) => void kept.set(k, v),
      removeItem: (k: string) => void kept.delete(k),
      clear: () => kept.clear(),
    },
  });
  const compiler = {
    check: async () => ({ problems: [] }),
    build: async () => ({ problems: [], files: [] }),
  } as unknown as Pick<Compiler, "check" | "build">;
  const files = { "main.one": "entity book {\n\ttitle  text  required\n}\n" };

  it("fills the window from its corner, and Escape or the same button puts it back", () => {
    render(<Workbench compiler={compiler} files={files} path="main.one" onChange={() => {}} generated={false} />);
    const workbench = document.querySelector(".uione-workbench") as HTMLElement;
    fireEvent.click(screen.getByRole("button", { name: "Full screen" }));
    expect(workbench.classList.contains("uione-workbench-full")).toBe(true);
    expect(document.documentElement.style.overflow).toBe("hidden");
    fireEvent.keyDown(document, { key: "Escape" });
    expect(workbench.classList.contains("uione-workbench-full")).toBe(false);
    fireEvent.click(screen.getByRole("button", { name: "Full screen" }));
    fireEvent.click(screen.getByRole("button", { name: "Exit full screen" }));
    expect(workbench.classList.contains("uione-workbench-full")).toBe(false);
    expect(document.documentElement.style.overflow).toBe("");
  });

  it("sizes the text, picks a font and a tab width, and remembers them", () => {
    localStorage.clear();
    const { unmount } = render(<Workbench compiler={compiler} files={files} path="main.one" onChange={() => {}} generated={false} />);
    const workbench = document.querySelector(".uione-workbench") as HTMLElement;
    expect(workbench.style.getPropertyValue("--uione-code-size")).toBe("14px");
    fireEvent.click(screen.getByRole("button", { name: "Larger text" }));
    fireEvent.click(screen.getByRole("button", { name: "Larger text" }));
    expect(workbench.style.getPropertyValue("--uione-code-size")).toBe("16px");
    fireEvent.click(screen.getByRole("button", { name: /^Font:/ }));
    fireEvent.click(screen.getByRole("radio", { name: "JetBrains Mono" }));
    expect(workbench.style.getPropertyValue("--uione-code-font")).toContain('"JetBrains Mono"');
    fireEvent.change(screen.getByRole("combobox", { name: "Tab width" }), { target: { value: "2" } });
    unmount();
    render(<Workbench compiler={compiler} files={files} path="main.one" onChange={() => {}} generated={false} />);
    expect((document.querySelector(".uione-workbench") as HTMLElement).style.getPropertyValue("--uione-code-size")).toBe("16px");
    expect(screen.getByRole("button", { name: "Font: JetBrains Mono" })).toBeTruthy();
    expect((screen.getByRole("combobox", { name: "Tab width" }) as HTMLSelectElement).value).toBe("2");
  });

  it("takes its look from the app, with no toolbar, when the app shows the controls itself", () => {
    render(
      <Workbench compiler={compiler} files={files} path="main.one" onChange={() => {}} generated={false} look={{ size: 18, font: "Fira Code", tabWidth: 8 }} />,
    );
    expect(screen.queryByRole("toolbar")).toBeNull();
    const workbench = document.querySelector(".uione-workbench") as HTMLElement;
    expect(workbench.style.getPropertyValue("--uione-code-size")).toBe("18px");
    expect(workbench.style.getPropertyValue("--uione-code-font")).toContain('"Fira Code"');
  });

  it("keeps the text between its smallest and largest", () => {
    localStorage.setItem("uione-editor-look", JSON.stringify({ size: 24 }));
    render(<Workbench compiler={compiler} files={files} path="main.one" onChange={() => {}} generated={false} />);
    expect((screen.getByRole("button", { name: "Larger text" }) as HTMLButtonElement).disabled).toBe(true);
    localStorage.clear();
  });

  it("says what each color means, in the editor's own colors, with the built-in types in the reference", () => {
    render(<Workbench compiler={compiler} files={files} path="main.one" onChange={() => {}} generated={false} />);
    fireEvent.click(screen.getByRole("button", { name: "Legend" }));
    const legend = screen.getByRole("dialog", { name: "What the colors mean" });
    expect(legend.textContent).toContain("Built-in types");
    expect(legend.textContent).toContain("Entities as types");
    // Each kind links to the section of the reference that says what it is.
    const links = [...legend.querySelectorAll("a")];
    expect(links.length).toBe(14);
    expect(links.every((a) => a.getAttribute("href")!.startsWith("https://www.uione.io/language/reference#"))).toBe(true);
    expect(screen.getByRole("link", { name: /Built-in types/ }).getAttribute("href")).toBe("https://www.uione.io/language/reference#built-in-types");
    expect(screen.getByRole("link", { name: /Comments/ }).getAttribute("href")).toBe("https://www.uione.io/language/reference#comments");
    const samples = [...legend.querySelectorAll(".uione-legend-sample")] as HTMLElement[];
    expect(samples.every((sample) => sample.style.color !== "")).toBe(true);
    const color = (word: string) => samples.find((sample) => sample.textContent === word)!.style.color;
    expect(color("text")).not.toBe(color("title")); // a type isn't colored as a field
  });
});
