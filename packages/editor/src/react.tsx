// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { unifiedMergeView } from "@codemirror/merge";
import { Compartment, EditorState, RangeSetBuilder, StateEffect, StateField } from "@codemirror/state";
import { defaultKeymap, history, historyKeymap, indentWithTab } from "@codemirror/commands";
import { Decoration, EditorView, drawSelection, highlightActiveLine, highlightActiveLineGutter, keymap, lineNumbers } from "@codemirror/view";
import type { DecorationSet } from "@codemirror/view";
import type { Built, Compiler, Files, GeneratedFile } from "@uione/compiler";
import { useEffect, useMemo, useRef, useState } from "react";
import type { CSSProperties } from "react";
import { fromLine } from "./generated.js";
import { highlighting, loadThemes } from "./highlight.js";
import { themesOf } from "./themes.js";
import { definitions } from "./definitions.js";
import { problems } from "./problems.js";
import { place, spotOf } from "./spot.js";
import type { Spot } from "./spot.js";
import { setTabWidth, tabs } from "./tabs.js";
import { Toolbar, fontFamily, savedLook } from "./toolbar.js";
import type { Look } from "./toolbar.js";
import type { TabWidth } from "./tabs.js";

export interface EditorProps {
  path: string; // the file, within the project, like main.one
  value: string;
  onChange: (value: string) => void;
  files: Files; // the whole project, this file included
  // With define, hovering over a name says what it is, and Ctrl or Cmd and a click,
  // or F12, goes to it.
  compiler: Pick<Compiler, "check"> & Partial<Pick<Compiler, "define">>;
  tabWidth?: TabWidth;
  onLine?: (line: number) => void; // the line the cursor is on, numbered from 1
  // Going to a definition in another file, or this one: its path, line and column.
  // Without it, one in this file moves the cursor there.
  onGo?: (path: string, line: number, column: number) => void;
  readOnly?: boolean; // shown to be read, by someone who can't change it
  at?: Spot; // where the cursor is, or what's selected, as an address says; its lines are marked
  onSelect?: (spot: Spot) => void; // where the cursor or selection moved to
  themes?: { light?: string; dark?: string }; // the color themes for a light page and a dark one, by name; GitHub's unless set
}

// The lines an address points at, marked the way GitHub marks them.
const mark = StateEffect.define<Spot | undefined>();
const marked = StateField.define<DecorationSet>({
  create: () => Decoration.none,
  update(lines, transaction) {
    // Once the person moves on, the lines an address marked aren't marked anymore.
    if (transaction.selection && !transaction.effects.some((effect) => effect.is(mark))) return Decoration.none;
    lines = lines.map(transaction.changes);
    for (const effect of transaction.effects) {
      if (!effect.is(mark)) continue;
      if (!effect.value) return Decoration.none;
      const doc = transaction.state.doc;
      const from = Math.min(Math.max(effect.value.from.line, 1), doc.lines);
      const to = Math.min(Math.max(effect.value.to?.line ?? from, from), doc.lines);
      const builder = new RangeSetBuilder<Decoration>();
      for (let n = from; n <= to; n++) builder.add(doc.line(n).from, doc.line(n).from, Decoration.line({ class: "uione-marked" }));
      lines = builder.finish();
    }
    return lines;
  },
  provide: (field) => EditorView.decorations.from(field),
});

// Putting the cursor or selection where a spot says, marking its lines, and bringing
// it into the middle of the view.
function go(view: EditorView, spot: Spot) {
  const { anchor, head } = place(view.state.doc, spot);
  view.dispatch({
    selection: { anchor, head },
    effects: [mark.of(spot), EditorView.scrollIntoView(head, { y: "center" })],
  });
}

// Lines wrap when there's room for wrapping to help, and scroll sideways when the
// editor is narrow, as on a phone, where wrapping would break code a few words to a
// line.
const wideEnough = 560;
function wrapping(width: number) {
  return width >= wideEnough ? EditorView.lineWrapping : [];
}

function same(a: Spot | undefined, b: Spot | undefined) {
  return JSON.stringify(a) === JSON.stringify(b);
}

// A .one file in CodeMirror. The editor owns its text while it's open; a value from
// outside replaces it only when it's different, so typing is never undone by an echo.
export function Editor({ path, value, onChange, files, compiler, tabWidth = 4, onLine, readOnly = false, at, onSelect, onGo, themes }: EditorProps) {
  const host = useRef<HTMLDivElement>(null);
  const view = useRef<EditorView | null>(null);
  const latest = useRef({ onChange, onLine, files, onSelect, onGo });
  latest.current = { onChange, onLine, files, onSelect, onGo };
  // Whether the selection is being put where `at` says, which isn't the person moving
  // it, and whether the person has moved it since the editor opened.
  const placing = useRef(false);
  const moved = useRef(false);
  const latestAt = useRef(at);
  latestAt.current = at;
  const wraps = useRef(new Compartment());
  const colors = useRef(new Compartment());

  useEffect(() => {
    const wrap = wraps.current;
    const editor = new EditorView({
      parent: host.current!,
      state: EditorState.create({
        doc: value,
        extensions: [
          lineNumbers(),
          highlightActiveLine(),
          highlightActiveLineGutter(),
          drawSelection(),
          history(),
          keymap.of([...defaultKeymap, ...historyKeymap, indentWithTab]),
          colors.current.of(highlighting(themesOf(themes?.light, themes?.dark), path.endsWith(".one"))),
          tabs(tabWidth),
          problems({ compiler, path, files: () => latest.current.files }),
          ...(compiler.define && path.endsWith(".one")
            ? [
                definitions({
                  compiler: compiler as Pick<Compiler, "define">,
                  path,
                  files: () => latest.current.files,
                  onGo: (to, line, column) => {
                    const goes = latest.current.onGo;
                    if (!goes) return false;
                    goes(to, line, column);
                  },
                }),
              ]
            : []),
          wrap.of(wrapping(host.current!.clientWidth || wideEnough)),
          // Someone who can't change the file can still select and copy it.
          EditorState.readOnly.of(readOnly),
          EditorView.editable.of(!readOnly),
          EditorView.contentAttributes.of({ "aria-label": path }),
          marked,
          EditorView.updateListener.of((update) => {
            // Only what's typed is a change; text the editor is handed is already the value.
            if (update.docChanged && !placing.current) latest.current.onChange(update.state.doc.toString());
            if (update.docChanged || update.selectionSet) {
              latest.current.onLine?.(update.state.doc.lineAt(update.state.selection.main.head).number);
            }
            if (update.selectionSet && !placing.current && update.transactions.some((t) => t.isUserEvent("select") || t.isUserEvent("input") || t.isUserEvent("delete"))) {
              moved.current = true;
            }
            if (update.selectionSet && !placing.current) {
              const { anchor, head } = update.state.selection.main;
              latest.current.onSelect?.(spotOf(update.state.doc, anchor, head));
            }
          }),
        ],
      }),
    });
    view.current = editor;
    moved.current = false;
    // Wrapping follows the editor's width as the window changes.
    let wide = (host.current!.clientWidth || wideEnough) >= wideEnough;
    const watch = typeof ResizeObserver === "undefined" ? undefined : new ResizeObserver(([entry]) => {
      const now = entry.contentRect.width >= wideEnough;
      if (now === wide) return;
      wide = now;
      editor.dispatch({ effects: wrap.reconfigure(wrapping(entry.contentRect.width)) });
    });
    watch?.observe(host.current!);
    if (at) {
      placing.current = true;
      go(editor, at);
      placing.current = false;
    }
    return () => {
      watch?.disconnect();
      editor.destroy();
      view.current = null;
    };
    // A new file or compiler is a new editor; the rest is followed below.
  }, [path, compiler, readOnly]);

  useEffect(() => {
    const editor = view.current;
    if (editor && editor.state.doc.toString() !== value) {
      placing.current = true;
      editor.dispatch({ changes: { from: 0, to: editor.state.doc.length, insert: value } });
      // The text the address's spot is in may only now have arrived, so until the
      // person moves the cursor, it's put there again.
      if (latestAt.current && !moved.current) go(editor, latestAt.current);
      placing.current = false;
    }
  }, [value]);

  useEffect(() => {
    if (view.current) setTabWidth(view.current, tabWidth);
  }, [tabWidth]);

  // The themes are shown once they're loaded, the first time they're chosen.
  useEffect(() => {
    let current = true;
    const chosen = themesOf(themes?.light, themes?.dark);
    void loadThemes(chosen).then(() => {
      if (current && view.current) view.current.dispatch({ effects: colors.current.reconfigure(highlighting(chosen, path.endsWith(".one"))) });
    });
    return () => {
      current = false;
    };
  }, [themes?.light, themes?.dark, path, compiler, readOnly]);

  // An address naming another place, like going back, moves the cursor there; the
  // address following the cursor, as it does, moves nothing.
  useEffect(() => {
    const editor = view.current;
    if (!editor || !at) return;
    const { anchor, head } = editor.state.selection.main;
    if (same(spotOf(editor.state.doc, anchor, head), at)) return;
    placing.current = true;
    go(editor, at);
    placing.current = false;
  }, [at ? JSON.stringify(at) : ""]);

  return <div className="uione-editor" ref={host} />;
}

export interface GeneratedProps {
  files: GeneratedFile[];
  path: string; // the .one file the cursor is in
  line: number; // and its line
  tabWidth?: TabWidth;
}

// The code a project becomes, one generated file at a time, with every line that came
// from the cursor's line marked. Moving to a line opens the first file it made code in.
export function Generated({ files, path, line, tabWidth = 4 }: GeneratedProps) {
  const found = useMemo(() => fromLine(files, path, line), [files, path, line]);
  const [chosen, setChosen] = useState<string | undefined>(files[0]?.path);
  const firstFound = found.keys().next().value as string | undefined;
  useEffect(() => {
    if (firstFound) setChosen(firstFound);
  }, [firstFound, line]);
  const file = files.find((f) => f.path === chosen) ?? files[0];
  const marked = new Set(file ? (found.get(file.path) ?? []) : []);
  const first = useRef<HTMLDivElement>(null);
  useEffect(() => {
    first.current?.scrollIntoView?.({ block: "nearest" });
  }, [file?.path, line]);
  if (!file) return <div className="uione-generated" />;
  let firstMarked = true;
  return (
    <div className="uione-generated">
      <label>
        <span>Generated file</span>
        <select value={file.path} onChange={(e) => setChosen(e.target.value)}>
          {files.map((f) => (
            <option key={f.path} value={f.path}>
              {f.path}
              {found.has(f.path) ? ` (${found.get(f.path)!.length})` : ""}
            </option>
          ))}
        </select>
      </label>
      <pre style={{ tabSize: tabWidth }} aria-label={file.path}>
        {file.content.split("\n").map((text, i) => {
          const number = i + 1;
          const here = marked.has(number);
          const ref = here && firstMarked ? ((firstMarked = false), first) : undefined;
          return (
            <div key={i} ref={ref} className={here ? "uione-from-here" : undefined} data-line={number}>
              <span className="uione-line-number">{number}</span>
              {text}
            </div>
          );
        })}
      </pre>
    </div>
  );
}

export interface WorkbenchProps {
  compiler: Pick<Compiler, "check" | "build"> & Partial<Pick<Compiler, "define">>;
  files: Files;
  path: string;
  onChange: (path: string, value: string) => void;
  tabWidth?: TabWidth;
  delay?: number; // milliseconds after the last change before building, 500 unless set
  generated?: boolean; // whether to show the code it becomes beside it; true unless set
  readOnly?: boolean; // shown to be read, by someone who can't change it
  at?: Spot; // where the cursor is in the file, as an address says
  onSelect?: (spot: Spot) => void; // where the cursor or selection moved to
  toolbar?: boolean; // the text's size, font, tab width and a legend of colors above it; true unless set
  look?: Look; // the text's look, when the app shows LookControls in a bar of its own, with no toolbar here
  onGo?: (path: string, line: number, column: number) => void; // going to a definition, as the Editor's
}

// While the workbench fills the window, the page beneath holds still, and Escape
// puts it back.
function useFullWindow(full: boolean, leave: () => void) {
  useEffect(() => {
    if (!full) return;
    const root = document.documentElement;
    const was = root.style.overflow;
    root.style.overflow = "hidden";
    const escape = (e: KeyboardEvent) => e.key === "Escape" && leave();
    document.addEventListener("keydown", escape);
    return () => {
      root.style.overflow = was;
      document.removeEventListener("keydown", escape);
    };
  }, [full]);
}

// In the workbench's top right corner: fill the window with it, or put it back,
// with the arrows pointing out or in.
function FullWindowButton({ full, onChange }: { full: boolean; onChange: (full: boolean) => void }) {
  const label = full ? "Exit full screen" : "Full screen";
  return (
    <button type="button" className="uione-full-window" aria-label={label} title={label} aria-pressed={full} onClick={() => onChange(!full)}>
      <svg viewBox="0 0 16 16" width="15" height="15" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
        {full ? <path d="M6 2v4H2M10 2v4h4M6 14v-4H2M10 14v-4h4" /> : <path d="M2 6V2h4M14 6V2h-4M2 10v4h4M14 10v4h-4" />}
      </svg>
    </button>
  );
}

// A .one file beside what it becomes. The project is built a moment after typing
// stops; the code from the last build that worked stays up while a mistake is fixed.
export function Workbench({ compiler, files, path, onChange, tabWidth = 4, delay = 500, generated = true, readOnly = false, at, onSelect, toolbar = true, look: given, onGo }: WorkbenchProps) {
  const [line, setLine] = useState(1);
  // How the reader likes the text: size, font and tab width, from their last visit,
  // or as the app's own bar sets it.
  const [kept, setLook] = useState<Look>(() => savedLook(tabWidth));
  const look = given ?? kept;
  const styled = toolbar || given !== undefined;
  const shown = toolbar && given === undefined;
  const [built, setBuilt] = useState<Built | undefined>();
  const [full, setFull] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  useFullWindow(full, () => setFull(false));
  // On a phone, tapping the code fills the screen with it first, rather than
  // opening the keyboard over a page zoomed in on it; a tap there then edits. A
  // finger that moves is scrolling the page, and goes on doing that.
  useEffect(() => {
    const element = box.current;
    if (!element || full || typeof matchMedia === "undefined" || !matchMedia("(pointer: coarse)").matches) return;
    let start: { x: number; y: number } | undefined;
    const began = (e: TouchEvent) => {
      const touch = e.touches[0];
      start = e.touches.length === 1 && touch ? { x: touch.clientX, y: touch.clientY } : undefined;
    };
    const ended = (e: TouchEvent) => {
      const touch = e.changedTouches[0];
      const target = e.target as HTMLElement | null;
      if (!start || !touch || !target?.closest(".uione-editor")) return;
      if (Math.hypot(touch.clientX - start.x, touch.clientY - start.y) > 10) return;
      e.preventDefault();
      setFull(true);
    };
    element.addEventListener("touchstart", began, { passive: true });
    element.addEventListener("touchend", ended, { passive: false });
    return () => {
      element.removeEventListener("touchstart", began);
      element.removeEventListener("touchend", ended);
    };
  }, [full]);
  useEffect(() => {
    if (!generated) return;
    let current = true;
    const timer = setTimeout(() => {
      compiler.build(files).then(
        (result) => {
          if (current && !result.refusal) setBuilt(result);
        },
        () => {},
      );
    }, delay);
    return () => {
      current = false;
      clearTimeout(timer);
    };
  }, [compiler, files, delay, generated]);
  return (
    <div
      ref={box}
      className={`uione-workbench${generated ? "" : " uione-workbench-alone"}${shown ? " uione-workbench-tools" : ""}${full ? " uione-workbench-full" : ""}`}
      style={styled ? ({ "--uione-code-size": `${look.size}px`, "--uione-code-font": fontFamily(look) } as CSSProperties) : undefined}
    >
      {shown && <Toolbar look={look} onLook={setLook} />}
      <FullWindowButton full={full} onChange={setFull} />
      <Editor
        path={path}
        value={files[path] ?? ""}
        onChange={(value) => onChange(path, value)}
        files={files}
        compiler={compiler}
        tabWidth={styled ? look.tabWidth : tabWidth}
        onLine={setLine}
        readOnly={readOnly}
        at={at}
        onSelect={onSelect}
        onGo={onGo}
        themes={styled ? { light: look.lightTheme, dark: look.darkTheme } : undefined}
      />
      {generated && <Generated files={built?.files ?? []} path={path} line={line} tabWidth={styled ? look.tabWidth : tabWidth} />}
    </div>
  );
}

export interface DiffProps {
  path: string; // the file, like main.one, which says how it's highlighted
  before: string; // its text before, empty when it's new
  after: string; // and after, empty when it's gone
  tabWidth?: TabWidth;
}

// What changed in a file: the text after, with each line taken away shown above
// what replaced it, and long stretches nothing changed in folded away. It's for
// reading; the text after can be selected and copied, but not changed.
export function Diff({ path, before, after, tabWidth = 4 }: DiffProps) {
  const host = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const editor = new EditorView({
      parent: host.current!,
      state: EditorState.create({
        doc: after,
        extensions: [
          lineNumbers(),
          drawSelection(),
          ...(path.endsWith(".one") ? [highlighting()] : []),
          tabs(tabWidth),
          EditorView.lineWrapping,
          EditorState.readOnly.of(true),
          EditorView.editable.of(false),
          EditorView.contentAttributes.of({ "aria-label": `Changes to ${path}` }),
          unifiedMergeView({ original: before, mergeControls: false, highlightChanges: true, gutter: true, collapseUnchanged: { margin: 3, minSize: 6 } }),
        ],
      }),
    });
    return () => editor.destroy();
  }, [path, before, after, tabWidth]);
  return <div className="uione-editor uione-diff" ref={host} />;
}
