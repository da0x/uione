// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The editor's toolbar: the text's size, a font made for code, a color theme, how
// wide a tab is, and a legend of what each color in the highlighting means. The choices are the
// reader's, kept in their browser, so the editor looks the way they left it.

import { useEffect, useRef, useState } from "react";
import { colorsOf, loadTheme } from "./highlight.js";
import { codeTheme, codeThemes } from "./themes.js";
import type { TabWidth } from "./tabs.js";

export interface Look {
  size: number; // the text's size, in pixels
  font: string; // one of fonts' names
  tabWidth: TabWidth;
  theme?: string; // one of codeThemes' names; GitHub when it's none
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
  const fallback: Look = { size: sizes.start, font: fonts[0].name, tabWidth, theme: codeThemes[0].name };
  try {
    const saved = JSON.parse(localStorage.getItem(key) ?? "null") as Partial<Look> | null;
    if (!saved) return fallback;
    return {
      size: typeof saved.size === "number" ? Math.min(sizes.most, Math.max(sizes.least, saved.size)) : fallback.size,
      font: fonts.some((f) => f.name === saved.font) ? saved.font! : fallback.font,
      tabWidth: [2, 4, 6, 8].includes(saved.tabWidth as number) ? (saved.tabWidth as TabWidth) : tabWidth,
      theme: codeTheme(saved.theme).name,
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
// the word, so its colors are read from the highlighter itself, and the section of
// the language reference that says what that kind of word is.
const reference = "https://www.uione.io/language/reference";
const kinds: { kind: string; says: string; code: string; word: string; nth?: number; section: string }[] = [
  { kind: "Declarations", says: "start what a file declares", code: "entity book {\n}\n", word: "entity", section: "declarations" },
  { kind: "Declared names", says: "what's declared", code: "entity book {\n}\n", word: "book", section: "names" },
  { kind: "Fields", says: "an entity's fields, by name", code: "entity book {\n\ttitle  text  required\n}\n", word: "title", section: "entity" },
  { kind: "Built-in types", says: "what a field holds", code: "entity book {\n\ttitle  text  required\n}\n", word: "text", section: "built-in-types" },
  { kind: "Entities as types", says: "a field holding another entity", code: "entity loan {\n\tbook  book  required\n}\n", word: "book", nth: 1, section: "built-in-types" },
  { kind: "Rules", says: "what a field must be", code: "entity book {\n\ttitle  text  required\n}\n", word: "required", section: "field-rules" },
  { kind: "Keywords", says: "the language's own words", code: "view shelf {\n\teach book where title != none {\n\t}\n}\n", word: "where", section: "keywords" },
  { kind: "Values", says: "built-in values", code: "command loan::checkin {\n\treturned_at = now\n}\n", word: "now", section: "built-in-values" },
  { kind: "Qualifiers", says: "what a name belongs to", code: "view shelf {\n\teach library::book {\n\t}\n}\n", word: "library", section: "namespace" },
  { kind: "Strings", says: "text as written", code: 'screen "Shelf" /shelf {\n}\n', word: '"Shelf"', section: "strings" },
  { kind: "Addresses", says: "where a screen is", code: 'screen "Shelf" /shelf {\n}\n', word: "/shelf", section: "screen" },
  { kind: "Numbers", says: "", code: "view shelf {\n\teach book {\n\t\tlimit 20\n\t}\n}\n", word: "20", section: "numbers-and-operators" },
  { kind: "Operators", says: "", code: "view shelf {\n\teach book where title != none {\n\t}\n}\n", word: "!=", section: "numbers-and-operators" },
  { kind: "Comments", says: "notes for people", code: "// A note\n", word: "// A note", section: "comments" },
];

// The reader's look, from their last visit, and a way to change it that keeps it.
export function useLook(tabWidth: TabWidth = 4): [Look, (look: Look) => void] {
  const [look, setLook] = useState<Look>(() => savedLook(tabWidth));
  return [
    look,
    (next: Look) => {
      save(next);
      setLook(next);
    },
  ];
}

// The editor's toolbar, for an app with no bar of its own to put the controls in.
export function Toolbar({ look, onLook }: { look: Look; onLook: (look: Look) => void }) {
  return (
    <div className="uione-toolbar" role="toolbar" aria-label="Editor">
      <LookControls look={look} onLook={onLook} />
    </div>
  );
}

// The controls themselves, for an app to put in a bar of its own: the text's size,
// as a word processor sizes it, with a large and a small A, the font, the tab width,
// and the legend.
export function LookControls({ look, onLook }: { look: Look; onLook: (look: Look) => void }) {
  const [legend, setLegend] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  const change = (next: Partial<Look>) => {
    const changed = { ...look, ...next };
    save(changed);
    onLook(changed);
  };
  const larger = Math.min(sizes.most, look.size + 1);
  const smaller = Math.max(sizes.least, look.size - 1);
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
    <div className="uione-look">
      <span className="uione-look-group" role="group" aria-label="Text size">
        <button type="button" className="uione-look-grow" onClick={() => change({ size: larger })} disabled={look.size >= sizes.most} aria-label="Larger text" title={`Larger text (${look.size}px)`}>
          A
        </button>
        <button type="button" className="uione-look-shrink" onClick={() => change({ size: smaller })} disabled={look.size <= sizes.least} aria-label="Smaller text" title={`Smaller text (${look.size}px)`}>
          A
        </button>
      </span>
      <label className="uione-look-group">
        <span>Font</span>
        <select value={look.font} onChange={(e) => change({ font: e.target.value })}>
          {fonts.map((f) => (
            <option key={f.name} value={f.name}>
              {f.name}
            </option>
          ))}
        </select>
      </label>
      <label className="uione-look-group">
        <span>Theme</span>
        <select
          value={codeTheme(look.theme).name}
          onChange={(e) => {
            const chosen = codeTheme(e.target.value);
            // Loaded before it's chosen, so the legend has its colors at once.
            void loadTheme(chosen).then(() => change({ theme: chosen.name }));
          }}
        >
          {codeThemes.map((t) => (
            <option key={t.name} value={t.name}>
              {t.name}
            </option>
          ))}
        </select>
      </label>
      <label className="uione-look-group">
        <span>Tab width</span>
        <select value={look.tabWidth} onChange={(e) => change({ tabWidth: Number(e.target.value) as TabWidth })}>
          {[2, 4, 6, 8].map((w) => (
            <option key={w} value={w}>
              {w}
            </option>
          ))}
        </select>
      </label>
      <div ref={box} className="uione-look-legend">
        <button type="button" aria-expanded={legend} onClick={() => setLegend(!legend)}>
          Legend
        </button>
        {legend && (
          <div role="dialog" aria-label="What the colors mean" className="uione-legend">
            <ul>
              {kinds.map((k) => {
                const colors = colorsOf(k.code, k.word, k.nth ?? 0, codeTheme(look.theme));
                return (
                  <li key={k.kind}>
                    <a href={`${reference}#${k.section}`} target="_blank" rel="noreferrer" title={`${k.kind} in the language reference`}>
                      <span className="uione-legend-sample" style={colors ? { color: colors.light, ["--one-dark" as string]: colors.dark } : undefined}>
                        {k.word.startsWith("//") ? "//" : k.word}
                      </span>
                      <span>
                        <strong>{k.kind}</strong>
                        {k.says && <span className="uione-legend-says">, {k.says}</span>}
                      </span>
                    </a>
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
