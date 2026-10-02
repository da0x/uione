// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Highlighting from the same TextMate grammar as VS Code and the code on uione.io,
// so a file looks the same everywhere. Each token is colored for the light and the
// dark theme at once; the editor shows whichever it's in.

import { RangeSetBuilder } from "@codemirror/state";
import type { Extension } from "@codemirror/state";
import { Decoration, EditorView, ViewPlugin } from "@codemirror/view";
import type { DecorationSet, ViewUpdate } from "@codemirror/view";
import githubDark from "@shikijs/themes/github-dark";
import githubLight from "@shikijs/themes/github-light";
import { createHighlighterCoreSync } from "shiki/core";
import type { HighlighterCore, LanguageRegistration } from "shiki/core";
import { createJavaScriptRegexEngine } from "shiki/engine/javascript";
import grammar from "./grammar/uione.json" with { type: "json" };

// Built on first use; undefined when it can't be, and the code is then left plain.
let highlighter: HighlighterCore | undefined | null = null;

function highlighterOrNothing(): HighlighterCore | undefined {
  if (highlighter === null) {
    try {
      highlighter = createHighlighterCoreSync({
        themes: [githubLight, githubDark],
        langs: [grammar as unknown as LanguageRegistration],
        engine: createJavaScriptRegexEngine(),
      });
    } catch {
      highlighter = undefined;
    }
  }
  return highlighter;
}

const marks = new Map<string, Decoration>();
function mark(light: string, dark: string): Decoration {
  const key = `${light} ${dark}`;
  let m = marks.get(key);
  if (!m) {
    m = Decoration.mark({ attributes: { style: `color:${light};--one-dark:${dark}` } });
    marks.set(key, m);
  }
  return m;
}

function decorate(view: EditorView): DecorationSet {
  const builder = new RangeSetBuilder<Decoration>();
  const text = view.state.doc.toString();
  const h = highlighterOrNothing();
  if (!h) return builder.finish();
  let tokens;
  try {
    tokens = h.codeToTokens(text, { lang: "uione", themes: { light: "github-light", dark: "github-dark" } }).tokens;
  } catch {
    return builder.finish();
  }
  let lineFrom = 0;
  for (const line of tokens) {
    let at = lineFrom;
    for (const token of line) {
      const style = (token.htmlStyle ?? {}) as Record<string, string>;
      const end = at + token.content.length;
      if (style.color && token.content.trim() !== "") builder.add(at, end, mark(style.color, style["--shiki-dark"] ?? style.color));
      at = end;
    }
    lineFrom = at + 1;
  }
  return builder.finish();
}

const plugin = ViewPlugin.fromClass(
  class {
    decorations: DecorationSet;
    constructor(view: EditorView) {
      this.decorations = decorate(view);
    }
    update(update: ViewUpdate) {
      if (update.docChanged) this.decorations = decorate(update.view);
    }
  },
  { decorations: (v) => v.decorations },
);

const dark = EditorView.baseTheme({
  "&dark .cm-content [style*='--one-dark']": { color: "var(--one-dark) !important" },
});

export function highlighting(): Extension {
  return [plugin, dark];
}
