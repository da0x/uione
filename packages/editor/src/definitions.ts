// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What each name means, from the compiler: hovering over a name says what it is,
// like "field project of report, a project", and going to it, with Ctrl or Cmd and
// a click, or F12, goes where it's declared, in this file or another. One of the
// language's own words opens the reference's section about it instead.

import type { Extension } from "@codemirror/state";
import { EditorView, hoverTooltip, keymap } from "@codemirror/view";
import type { Compiler, Definition, Files } from "@uione/compiler";

export const reference = "https://www.uione.io/language/reference";

export interface DefinitionsOptions {
  compiler: Pick<Compiler, "define">;
  path: string;
  files: () => Files; // the rest of the project; this file's text is the editor's own
  // Going to a definition: its file, line and column, each counted from 1. Without
  // it, or when it returns false, a definition in this file moves the cursor there.
  onGo?: (path: string, line: number, column: number) => boolean | void;
}

type Found = Extract<Definition, { found: true }>;

async function defined(view: EditorView, at: number, options: DefinitionsOptions): Promise<Found | undefined> {
  const line = view.state.doc.lineAt(at);
  const project = { ...options.files(), [options.path]: view.state.doc.toString() };
  try {
    const d = await options.compiler.define(project, options.path, line.number, at - line.from + 1);
    return d.found ? d : undefined;
  } catch {
    return undefined;
  }
}

function go(view: EditorView, d: Found, options: DefinitionsOptions) {
  if (!d.path) {
    window.open(`${reference}#${d.section}`, "_blank", "noopener");
    return;
  }
  if (options.onGo && options.onGo(d.path, d.line, d.column) !== false) return;
  if (d.path !== options.path || d.line < 1 || d.line > view.state.doc.lines) return;
  const line = view.state.doc.line(d.line);
  const at = Math.min(line.to, line.from + Math.max(0, d.column - 1));
  view.dispatch({ selection: { anchor: at }, scrollIntoView: true });
  view.focus();
}

export function definitions(options: DefinitionsOptions): Extension {
  return [
    hoverTooltip(async (view, at) => {
      const d = await defined(view, at, options);
      if (!d) return null;
      const line = view.state.doc.lineAt(at);
      return {
        pos: line.from + d.from - 1,
        end: line.from + d.to - 1,
        above: true,
        create: () => {
          const dom = document.createElement("div");
          dom.className = "uione-definition";
          const says = document.createElement("span");
          says.textContent = d.says;
          dom.append(says);
          const link = document.createElement("a");
          if (d.path) {
            link.textContent = d.path === options.path ? `line ${d.line}` : `${d.path}:${d.line}`;
            link.href = "#";
            link.title = "Go to it (Ctrl or Cmd and a click, or F12)";
            link.addEventListener("click", (event) => {
              event.preventDefault();
              go(view, d, options);
            });
          } else {
            link.textContent = "In the reference";
            link.href = `${reference}#${d.section}`;
            link.target = "_blank";
            link.rel = "noreferrer";
          }
          dom.append(link);
          return { dom };
        },
      };
    }),
    EditorView.domEventHandlers({
      mousedown(event, view) {
        if (!(event.ctrlKey || event.metaKey) || event.button !== 0) return false;
        const at = view.posAtCoords({ x: event.clientX, y: event.clientY });
        if (at === null) return false;
        event.preventDefault();
        void defined(view, at, options).then((d) => d && go(view, d, options));
        return true;
      },
    }),
    keymap.of([
      {
        key: "F12",
        run(view) {
          void defined(view, view.state.selection.main.head, options).then((d) => d && go(view, d, options));
          return true;
        },
      },
    ]),
  ];
}
