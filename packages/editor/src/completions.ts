// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What can be written as it's typed, from uione's library through the compiler: in
// a project's block, its settings, each with its type and what it's for; after an
// enum setting's name, its choices, each as it's shown; and at the top of a file,
// import one.

import { autocompletion } from "@codemirror/autocomplete";
import type { CompletionContext, CompletionResult } from "@codemirror/autocomplete";
import type { Extension } from "@codemirror/state";
import type { Compiler } from "@uione/compiler";

export interface CompletionsOptions {
  compiler: Pick<Compiler, "complete">;
}

export function completionsFrom(options: CompletionsOptions) {
  return async (context: CompletionContext): Promise<CompletionResult | null> => {
    const line = context.state.doc.lineAt(context.pos);
    const typed = context.matchBefore(/\w*/);
    // Offered as a word is typed, or when asked for with Ctrl and Space.
    if (!context.explicit && (!typed || typed.from === typed.to)) return null;
    try {
      const found = await options.compiler.complete(context.state.doc.toString(), line.number, context.pos - line.from + 1);
      if (context.aborted || found.items.length === 0) return null;
      return {
        from: line.from + found.from - 1,
        options: found.items.map((item) => ({ label: item.label, detail: item.detail, ...(item.info ? { info: item.info } : {}) })),
        validFor: /^\w*$/,
      };
    } catch {
      return null;
    }
  };
}

export function completions(options: CompletionsOptions): Extension {
  return autocompletion({ override: [completionsFrom(options)] });
}
