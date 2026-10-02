// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The compiler's problems, underlined where they are, a moment after typing stops.
// The whole project is checked, since a mistake in one file can show in another, and
// only this file's problems are shown here.

import type { Extension, Text } from "@codemirror/state";
import { linter } from "@codemirror/lint";
import type { Diagnostic } from "@codemirror/lint";
import type { Compiler, Files, Problem } from "@uione/compiler";

export interface ProblemsOptions {
  compiler: Pick<Compiler, "check">;
  path: string; // this file, within the project, like main.one
  files: () => Files; // the project's other files; this one's text is taken from the editor
  delay?: number; // milliseconds after the last change, 500 unless set
}

// Where a problem is in a document: from its column to the end of the word there, or
// one character when it points at nothing that's a word.
export function placed(doc: Text, problem: Pick<Problem, "line" | "column">): { from: number; to: number } {
  const number = Math.min(Math.max(problem.line, 1), doc.lines);
  const line = doc.line(number);
  const from = Math.min(line.from + Math.max(problem.column, 1) - 1, line.to);
  let to = from;
  while (to < line.to && /[\w:]/.test(doc.sliceString(to, to + 1))) to++;
  return { from, to: to > from ? to : Math.min(from + 1, line.to) };
}

export function problems({ compiler, path, files, delay = 500 }: ProblemsOptions): Extension {
  return linter(
    async (view) => {
      const project = { ...files(), [path]: view.state.doc.toString() };
      const checked = await compiler.check(project);
      return checked.problems
        .filter((p) => p.path === path)
        .map((p): Diagnostic => ({ ...placed(view.state.doc, p), severity: "error", message: p.message, source: "one" }));
    },
    { delay },
  );
}
