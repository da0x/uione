// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Highlighting from the same TextMate grammar as VS Code and the code on uione.io,
// so a file looks the same everywhere. Each token is colored for a theme's light and
// dark side at once; the editor shows whichever the page is in. A theme other than
// GitHub, the page's own colors, brings its own background and text color too.

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
import { defaultTheme } from "./themes.js";
import type { CodeTheme } from "./themes.js";

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

// Whether a theme's two sides are loaded, so it can be shown.
function loaded(theme: CodeTheme): boolean {
  const h = highlighterOrNothing();
  if (!h) return false;
  const have = h.getLoadedThemes();
  return have.includes(theme.light) && have.includes(theme.dark);
}

// Loads a theme the first time it's chosen. Showing it before then shows GitHub.
export async function loadTheme(theme: CodeTheme): Promise<void> {
  const h = highlighterOrNothing();
  if (!h || loaded(theme)) return;
  for (const registration of await theme.load()) {
    if (!h.getLoadedThemes().includes(registration.name ?? "")) h.loadThemeSync(registration);
  }
}

function shown(theme: CodeTheme): CodeTheme {
  return loaded(theme) ? theme : defaultTheme;
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

function decorate(view: EditorView, theme: CodeTheme): DecorationSet {
  const builder = new RangeSetBuilder<Decoration>();
  const text = view.state.doc.toString();
  const h = highlighterOrNothing();
  if (!h) return builder.finish();
  let tokens;
  try {
    tokens = h.codeToTokens(text, { lang: "uione", themes: { light: theme.light, dark: theme.dark } }).tokens;
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

function plugin(theme: CodeTheme) {
  return ViewPlugin.fromClass(
    class {
      decorations: DecorationSet;
      constructor(view: EditorView) {
        this.decorations = decorate(view, theme);
      }
      update(update: ViewUpdate) {
        if (update.docChanged) this.decorations = decorate(update.view, theme);
      }
    },
    { decorations: (v) => v.decorations },
  );
}

// A theme's own background and text color, light and dark, for the editor to use.
function surface(theme: CodeTheme): Extension {
  const h = highlighterOrNothing();
  if (!h || theme === defaultTheme) return [];
  const light = h.getTheme(theme.light);
  const dark = h.getTheme(theme.dark);
  return EditorView.editorAttributes.of({
    class: "uione-themed",
    style: `--one-bg:${light.bg};--one-fg:${light.fg};--one-bg-dark:${dark.bg};--one-fg-dark:${dark.fg}`,
  });
}

const dark = EditorView.baseTheme({
  "&dark .cm-content [style*='--one-dark']": { color: "var(--one-dark) !important" },
});

// Highlighting in a theme, GitHub's unless another is given. A theme not yet loaded
// shows as GitHub; load it with loadTheme, and highlight again.
export function highlighting(theme: CodeTheme = defaultTheme): Extension {
  const showing = shown(theme);
  return [plugin(showing), surface(showing), dark];
}

// The colors a token gets, in light and dark: the nth piece of `code` reading
// `token`, highlighted as any .one file is. It's how the legend shows each kind of
// word in exactly the colors the editor gives it.
export function colorsOf(code: string, token: string, nth = 0, theme: CodeTheme = defaultTheme): { light: string; dark: string } | undefined {
  const h = highlighterOrNothing();
  if (!h) return undefined;
  const showing = shown(theme);
  let seen = 0;
  for (const line of h.codeToTokens(code, { lang: "uione", themes: { light: showing.light, dark: showing.dark } }).tokens) {
    for (const piece of line) {
      if (piece.content.trim() !== token) continue;
      if (seen++ < nth) continue;
      const style = (piece.htmlStyle ?? {}) as Record<string, string>;
      if (!style.color) return undefined;
      return { light: style.color, dark: style["--shiki-dark"] ?? style.color };
    }
  }
  return undefined;
}
