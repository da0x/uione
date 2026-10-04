// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The editor's toolbar: the text's size, a font made for code, how wide a tab is,
// and a legend of what each color in the highlighting means. The choices are the
// reader's, kept in their browser, so the editor looks the way they left it.

import { useEffect, useRef, useState } from "react";
import { colorsOf } from "./highlight.js";
import type { TabWidth } from "./tabs.js";

export interface Look {
  size: number; // the text's size, in pixels
  font: string; // one of fonts' names
  tabWidth: TabWidth;
}

// Fonts drawn for code, each loaded the first time it's chosen.
export const fonts: { name: string; family: string; load?: () => Promise<unknown> }[] = [
  { name: "IBM Plex Mono", family: '"IBM Plex Mono"', load: () => Promise.all([import("@fontsource/ibm-plex-mono/400.css"), import("@fontsource/ibm-plex-mono/500.css")]) },
  { name: "JetBrains Mono", family: '"JetBrains Mono"', load: () => import("@fontsource/jetbrains-mono/400.css") },
  { name: "Fira Code", family: '"Fira Code"', load: () => import("@fontsource/fira-code/400.css") },
  { name: "Source Code Pro", family: '"Source Code Pro"', load: () => import("@fontsource/source-code-pro/400.css") },
  { name: "The system's", family: "ui-monospace" },
];

const sizes = { least: 10, most: 24, start: 14 };
const key = "uione-editor-look";

export function savedLook(tabWidth: TabWidth): Look {
  const fallback: Look = { size: sizes.start, font: fonts[0].name, tabWidth };
  try {
    const saved = JSON.parse(localStorage.getItem(key) ?? "null") as Partial<Look> | null;
    if (!saved) return fallback;
    return {
      size: typeof saved.size === "number" ? Math.min(sizes.most, Math.max(sizes.least, saved.size)) : fallback.size,
      font: fonts.some((f) => f.name === saved.font) ? saved.font! : fallback.font,
      tabWidth: [2, 4, 6, 8].includes(saved.tabWidth as number) ? (saved.tabWidth as TabWidth) : tabWidth,
    };
  } catch {
    return fallback;
  }
}

function save(look: Look) {
  try {
    localStorage.setItem(key, JSON.stringify(look));
  } catch {
    // Without storage, the look lasts as long as the page.
  }
}

// The font a look names, as CSS, loading it when it isn't yet.
export function fontFamily(look: Look): string {
  const font = fonts.find((f) => f.name === look.font) ?? fonts[0];
  void font.load?.().catch(() => {});
  return `${font.family}, ui-monospace, "SFMono-Regular", Menlo, monospace`;
}

// What each color means: a kind of word, a piece of .one where it's that kind, and
// the word, so its colors are read from the highlighter itself.
const reference = "https://www.uione.io/language/reference";
const kinds: { kind: string; says: string; code: string; word: string; nth?: number; more?: string }[] = [
  { kind: "Declarations", says: "start what a file declares", code: "entity book {\n}\n", word: "entity" },
  { kind: "Declared names", says: "what's declared", code: "entity book {\n}\n", word: "book" },
  { kind: "Fields", says: "an entity's fields, by name", code: "entity book {\n\ttitle  text  required\n}\n", word: "title" },
  { kind: "Built-in types", says: "what a field holds", code: "entity book {\n\ttitle  text  required\n}\n", word: "text", more: `${reference}#built-in-types` },
  { kind: "Entities as types", says: "a field holding another entity", code: "entity loan {\n\tbook  book  required\n}\n", word: "book", nth: 1 },
  { kind: "Rules", says: "what a field must be", code: "entity book {\n\ttitle  text  required\n}\n", word: "required" },
  { kind: "Keywords", says: "the language's own words", code: "view shelf {\n\teach book where title != none {\n\t}\n}\n", word: "where" },
  { kind: "Values", says: "built-in values", code: "command loan::checkin {\n\treturned_at = now\n}\n", word: "now" },
  { kind: "Qualifiers", says: "what a name belongs to", code: "view shelf {\n\teach library::book {\n\t}\n}\n", word: "library" },
  { kind: "Strings", says: "text as written", code: 'screen "Shelf" /shelf {\n}\n', word: '"Shelf"' },
  { kind: "Addresses", says: "where a screen is", code: 'screen "Shelf" /shelf {\n}\n', word: "/shelf" },
  { kind: "Numbers", says: "", code: "view shelf {\n\teach book {\n\t\tlimit 20\n\t}\n}\n", word: "20" },
  { kind: "Operators", says: "", code: "view shelf {\n\teach book where title != none {\n\t}\n}\n", word: "!=" },
  { kind: "Comments", says: "notes for people", code: "// A note\n", word: "// A note" },
];

export function Toolbar({ look, onLook }: { look: Look; onLook: (look: Look) => void }) {
  const [legend, setLegend] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  const change = (next: Partial<Look>) => {
    const changed = { ...look, ...next };
    save(changed);
    onLook(changed);
  };
  useEffect(() => {
    if (!legend) return;
    const close = (e: MouseEvent | KeyboardEvent) => {
      if (e instanceof KeyboardEvent ? e.key === "Escape" : !box.current?.contains(e.target as Node)) setLegend(false);
    };
    document.addEventListener("mousedown", close);
    document.addEventListener("keydown", close);
    return () => {
      document.removeEventListener("mousedown", close);
      document.removeEventListener("keydown", close);
    };
  }, [legend]);

  return (
    <div className="uione-toolbar" role="toolbar" aria-label="Editor">
      <span className="uione-toolbar-group" role="group" aria-label="Text size">
        <button type="button" onClick={() => change({ size: Math.max(sizes.least, look.size - 1) })} disabled={look.size <= sizes.least} aria-label="Smaller text" title="Smaller text">
          −
        </button>
        <span className="uione-toolbar-value" aria-live="polite">
          {look.size}
        </span>
        <button type="button" onClick={() => change({ size: Math.min(sizes.most, look.size + 1) })} disabled={look.size >= sizes.most} aria-label="Larger text" title="Larger text">
          +
        </button>
      </span>
      <label className="uione-toolbar-group">
        <span>Font</span>
        <select value={look.font} onChange={(e) => change({ font: e.target.value })}>
          {fonts.map((f) => (
            <option key={f.name} value={f.name}>
              {f.name}
            </option>
          ))}
        </select>
      </label>
      <label className="uione-toolbar-group">
        <span>Tab width</span>
        <select value={look.tabWidth} onChange={(e) => change({ tabWidth: Number(e.target.value) as TabWidth })}>
          {[2, 4, 6, 8].map((w) => (
            <option key={w} value={w}>
              {w}
            </option>
          ))}
        </select>
      </label>
      <div ref={box} className="uione-toolbar-legend">
        <button type="button" aria-expanded={legend} onClick={() => setLegend(!legend)}>
          Legend
        </button>
        {legend && (
          <div role="dialog" aria-label="What the colors mean" className="uione-legend">
            <ul>
              {kinds.map((k) => {
                const colors = colorsOf(k.code, k.word, k.nth ?? 0);
                return (
                  <li key={k.kind}>
                    <span className="uione-legend-sample" style={colors ? { color: colors.light, ["--one-dark" as string]: colors.dark } : undefined}>
                      {k.word.startsWith("//") ? "//" : k.word}
                    </span>
                    <span>
                      <strong>{k.kind}</strong>
                      {k.says && <span className="uione-legend-says">, {k.says}</span>}
                      {k.more && (
                        <a href={k.more} target="_blank" rel="noreferrer">
                          all of them
                        </a>
                      )}
                    </span>
                  </li>
                );
              })}
            </ul>
          </div>
        )}
      </div>
    </div>
  );
}
