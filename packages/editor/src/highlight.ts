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
import { defaultThemes } from "./themes.js";
import type { CodeTheme, Themes } from "./themes.js";

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

// Whether a theme is loaded, so it can be shown.
function loaded(theme: CodeTheme): boolean {
  return highlighterOrNothing()?.getLoadedThemes().includes(theme.id) ?? false;
}

// Loads the themes the first time they're chosen. Showing one before then shows
// GitHub's for its page.
export async function loadThemes(themes: Themes): Promise<void> {
  const h = highlighterOrNothing();
  if (!h) return;
  for (const theme of [themes.light, themes.dark]) {
    if (!loaded(theme)) h.loadThemeSync(await theme.load());
  }
}

function shown(themes: Themes): Themes {
  return { light: loaded(themes.light) ? themes.light : defaultThemes.light, dark: loaded(themes.dark) ? themes.dark : defaultThemes.dark };
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

function decorate(view: EditorView, themes: Themes): DecorationSet {
  const builder = new RangeSetBuilder<Decoration>();
  const text = view.state.doc.toString();
  const h = highlighterOrNothing();
  if (!h) return builder.finish();
  let tokens;
  try {
    tokens = h.codeToTokens(text, { lang: "uione", themes: { light: themes.light.id, dark: themes.dark.id } }).tokens;
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

function plugin(themes: Themes) {
  return ViewPlugin.fromClass(
    class {
      decorations: DecorationSet;
      constructor(view: EditorView) {
        this.decorations = decorate(view, themes);
      }
      update(update: ViewUpdate) {
        if (update.docChanged) this.decorations = decorate(update.view, themes);
      }
    },
    { decorations: (v) => v.decorations },
  );
}

// The themes' own background and text color, for the editor to use; GitHub's are
// the page's own.
function surface(themes: Themes): Extension {
  const h = highlighterOrNothing();
  if (!h || (themes.light === defaultThemes.light && themes.dark === defaultThemes.dark)) return [];
  const colors = (theme: CodeTheme, page: string, ink: string) => {
    if (theme === defaultThemes.light || theme === defaultThemes.dark) return { bg: page, fg: ink };
    const registered = h.getTheme(theme.id);
    return { bg: registered.bg, fg: registered.fg };
  };
  const light = colors(themes.light, "var(--uione-surface)", "var(--uione-ink)");
  const dark = colors(themes.dark, "var(--uione-surface)", "var(--uione-ink)");
  return EditorView.editorAttributes.of({
    class: "uione-themed",
    style: `--one-bg:${light.bg};--one-fg:${light.fg};--one-bg-dark:${dark.bg};--one-fg-dark:${dark.fg}`,
  });
}

const dark = EditorView.baseTheme({
  "&dark .cm-content [style*='--one-dark']": { color: "var(--one-dark) !important" },
});

// Highlighting in a light and a dark theme, each shown on its page, GitHub's unless
// others are given. One not yet loaded shows as GitHub's; load it with loadThemes,
// and highlight again.
export function highlighting(themes: Themes = defaultThemes): Extension {
  const showing = shown(themes);
  return [plugin(showing), surface(showing), dark];
}

// The colors a token gets, in light and dark: the nth piece of `code` reading
// `token`, highlighted as any .one file is. It's how the legend shows each kind of
// word in exactly the colors the editor gives it.
export function colorsOf(code: string, token: string, nth = 0, themes: Themes = defaultThemes): { light: string; dark: string } | undefined {
  const h = highlighterOrNothing();
  if (!h) return undefined;
  const showing = shown(themes);
  let seen = 0;
  for (const line of h.codeToTokens(code, { lang: "uione", themes: { light: showing.light.id, dark: showing.dark.id } }).tokens) {
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

// Loads one theme, as a gallery of them does to show each.
export async function loadTheme(theme: CodeTheme): Promise<void> {
  const h = highlighterOrNothing();
  if (h && !loaded(theme)) h.loadThemeSync(await theme.load());
}

// A piece of .one drawn in a theme, as HTML with the theme's own background, for a
// preview of it; nothing until it's loaded.
export function previewOf(theme: CodeTheme, code: string): string | undefined {
  const h = highlighterOrNothing();
  if (!h || !loaded(theme)) return undefined;
  try {
    return h.codeToHtml(code, { lang: "uione", theme: theme.id });
  } catch {
    return undefined;
  }
}
