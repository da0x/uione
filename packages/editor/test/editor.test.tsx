// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { insertTab } from "@codemirror/commands";
import { diagnosticCount, forceLinting } from "@codemirror/lint";
import { EditorState, Text } from "@codemirror/state";
import type { Extension } from "@codemirror/state";
import { EditorView } from "@codemirror/view";
import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import type { Built, Checked, Compiler, Files } from "@uione/compiler";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { Generated, Workbench, fromLine, highlighting, placed, problems, setTabWidth, tabs } from "../src/index.js";

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
  const compiler: Pick<Compiler, "check" | "build"> = {
    check: async (files) => run(one, { kind: "check", files }) as Checked,
    build: async (files) => run(one, { kind: "build", files }) as Built,
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
