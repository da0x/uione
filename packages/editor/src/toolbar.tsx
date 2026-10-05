// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The editor's toolbar: the text's size, a font made for code, color themes for a
// light page and a dark one, how wide a tab is, and a legend of what each color in the highlighting means. The choices are the
// reader's, kept in their browser, so the editor looks the way they left it.

import { useCallback, useEffect, useRef, useState } from "react";
import { colorsOf, loadTheme, loadThemes, previewOf } from "./highlight.js";
import { Popover } from "./popover.js";
import { darkThemes, fromOneChoice, lightThemes, themesOf } from "./themes.js";
import type { CodeTheme } from "./themes.js";
import type { TabWidth } from "./tabs.js";

export interface Look {
  size: number; // the text's size, in pixels
  font: string; // one of fonts' names
  tabWidth: TabWidth;
  lightTheme?: string; // the theme for a light page, one of lightThemes' names; GitHub Light when none
  darkTheme?: string; // and for a dark page, one of darkThemes'; GitHub Dark when none
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
    const saved = JSON.parse(localStorage.getItem(key) ?? "null") as (Partial<Look> & { theme?: string }) | null;
    if (!saved) return fallback;
    // One theme for both pages, as earlier versions kept it, becomes one of each.
    const earlier = fromOneChoice(saved.theme);
    const themes = themesOf(saved.lightTheme ?? earlier.light, saved.darkTheme ?? earlier.dark);
    return {
      size: typeof saved.size === "number" ? Math.min(sizes.most, Math.max(sizes.least, saved.size)) : fallback.size,
      font: fonts.some((f) => f.name === saved.font) ? saved.font! : fallback.font,
      tabWidth: [2, 4, 6, 8].includes(saved.tabWidth as number) ? (saved.tabWidth as TabWidth) : tabWidth,
      lightTheme: themes.light.name,
      darkTheme: themes.dark.name,
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
// as a word processor sizes it, with a large and a small A, the font, the theme, the
// tab width, and the legend. The font and the theme are shown by what they look like,
// and named once they're opened.
export function LookControls({ look, onLook }: { look: Look; onLook: (look: Look) => void }) {
  const change = (next: Partial<Look>) => {
    const changed = { ...look, ...next };
    save(changed);
    onLook(changed);
  };
  const larger = Math.min(sizes.most, look.size + 1);
  const smaller = Math.max(sizes.least, look.size - 1);

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
      <FontPicker look={look} onPick={(font) => change({ font })} />
      <ThemePicker
        look={look}
        onPick={(theme) => change(theme.dark ? { darkTheme: theme.name } : { lightTheme: theme.name })}
      />
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
      <Legend look={look} />
    </div>
  );
}

// Choosing the font from a list of them, each written in itself; the button shows a
// T, as editors show the font control.
function FontPicker({ look, onPick }: { look: Look; onPick: (font: string) => void }) {
  const [open, setOpen] = useState(false);
  const button = useRef<HTMLButtonElement>(null);
  const close = useCallback(() => setOpen(false), []);
  useEffect(() => {
    if (open) for (const f of fonts) void f.load?.().catch(() => {});
  }, [open]);
  return (
    <span className="uione-look-font">
      <button
        ref={button}
        type="button"
        className="uione-look-icon"
        aria-expanded={open}
        aria-haspopup="dialog"
        aria-label={`Font: ${look.font}`}
        title={`Font: ${look.font}`}
        onClick={() => setOpen(!open)}
      >
        <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
          <path d="M5 7V5h14v2M12 5v14M9 19h6" />
        </svg>
      </button>
      {open && (
        <Popover anchor={button} label="Font" className="uione-font-list" onClose={close}>
          <div role="radiogroup" aria-label="Font">
            {fonts.map((f) => (
              <button
                key={f.name}
                type="button"
                role="radio"
                aria-checked={f.name === look.font}
                className="uione-font-choice"
                style={{ fontFamily: `${f.family}, ui-monospace, monospace` }}
                onClick={() => {
                  onPick(f.name);
                  setOpen(false);
                }}
              >
                {f.name}
              </button>
            ))}
          </div>
        </Popover>
      )}
    </span>
  );
}

// What each color means, each kind of word in the colors of the theme shown now.
function Legend({ look }: { look: Look }) {
  const [open, setOpen] = useState(false);
  const button = useRef<HTMLButtonElement>(null);
  const close = useCallback(() => setOpen(false), []);
  return (
    <span className="uione-look-legend">
      <button ref={button} type="button" aria-expanded={open} aria-haspopup="dialog" onClick={() => setOpen(!open)}>
        Legend
      </button>
      {open && (
        <Popover anchor={button} label="What the colors mean" className="uione-legend" onClose={close}>
          <ul>
            {kinds.map((k) => {
              const colors = colorsOf(k.code, k.word, k.nth ?? 0, themesOf(look.lightTheme, look.darkTheme));
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
        </Popover>
      )}
    </span>
  );
}

// Whether the page is dark now: as the reader picked it, or as their system says
// when they didn't, followed as either changes.
export function usePageDark(): boolean {
  const read = () => {
    if (typeof document === "undefined") return false;
    const picked = document.documentElement.getAttribute("data-theme");
    if (picked === "dark" || picked === "light") return picked === "dark";
    return typeof matchMedia !== "undefined" && matchMedia("(prefers-color-scheme: dark)").matches;
  };
  const [dark, setDark] = useState(read);
  useEffect(() => {
    const update = () => setDark(read());
    const watcher = typeof MutationObserver !== "undefined" ? new MutationObserver(update) : undefined;
    watcher?.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
    const query = typeof matchMedia !== "undefined" ? matchMedia("(prefers-color-scheme: dark)") : undefined;
    query?.addEventListener("change", update);
    return () => {
      watcher?.disconnect();
      query?.removeEventListener("change", update);
    };
  }, []);
  return dark;
}

// A few lines of .one that show each kind of word, for a theme's preview.
const sample = `entity book {
	title   text  required
	status  enum  on_shelf | lent
}
command book::lend
`;

// Choosing the theme for the page as it is now, light or dark, from a gallery of
// them, each shown in its own colors; the theme for the other is kept as it was,
// and comes back when the page does.
function ThemePicker({ look, onPick }: { look: Look; onPick: (theme: CodeTheme) => void }) {
  const dark = usePageDark();
  const list = dark ? darkThemes : lightThemes;
  const themes = themesOf(look.lightTheme, look.darkTheme);
  const current = dark ? themes.dark : themes.light;
  const [open, setOpen] = useState(false);
  const [ready, setReady] = useState(0);
  const button = useRef<HTMLButtonElement>(null);
  const close = useCallback(() => setOpen(false), []);
  const kind = dark ? "Dark theme" : "Light theme";
  // The current theme's swatch, and every theme of the list once the gallery opens.
  useEffect(() => {
    let live = true;
    void loadThemes(themes).then(() => live && setReady((n) => n + 1));
    return () => {
      live = false;
    };
  }, [look.lightTheme, look.darkTheme]);
  useEffect(() => {
    if (!open) return;
    let live = true;
    void Promise.all(list.map((t) => loadTheme(t))).then(() => live && setReady((n) => n + 1));
    return () => {
      live = false;
    };
  }, [open, dark]);
  const swatch = previewOf(current, sample);
  return (
    <span className="uione-look-themes" data-ready={ready}>
      <button
        ref={button}
        type="button"
        className="uione-look-icon"
        aria-expanded={open}
        aria-haspopup="dialog"
        aria-label={`${kind}: ${current.name}`}
        title={`${kind}: ${current.name}`}
        onClick={() => setOpen(!open)}
      >
        <span className="uione-theme-swatch" aria-hidden="true" dangerouslySetInnerHTML={swatch ? { __html: swatch } : undefined} />
      </button>
      {open && (
        <Popover anchor={button} label={dark ? "Dark themes" : "Light themes"} className="uione-theme-gallery" onClose={close}>
          <p className="uione-theme-gallery-heading">{dark ? "Dark themes" : "Light themes"}</p>
          <div className="uione-theme-grid" role="radiogroup" aria-label={kind}>
            {list.map((theme) => {
              const shown = previewOf(theme, sample);
              return (
                <button
                  key={theme.id}
                  type="button"
                  role="radio"
                  aria-checked={theme === current}
                  className="uione-theme-card"
                  onClick={() => {
                    onPick(theme);
                    setOpen(false);
                  }}
                >
                  <span className="uione-theme-preview" aria-hidden="true" dangerouslySetInnerHTML={shown ? { __html: shown } : undefined} />
                  <span className="uione-theme-name">{theme.name}</span>
                </button>
              );
            })}
          </div>
        </Popover>
      )}
    </span>
  );
}
