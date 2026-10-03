// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { EditorState } from "@codemirror/state";
import { defaultKeymap, history, historyKeymap, indentWithTab } from "@codemirror/commands";
import { EditorView, drawSelection, highlightActiveLine, highlightActiveLineGutter, keymap, lineNumbers } from "@codemirror/view";
import type { Built, Compiler, Files, GeneratedFile } from "@uione/compiler";
import { useEffect, useMemo, useRef, useState } from "react";
import { fromLine } from "./generated.js";
import { highlighting } from "./highlight.js";
import { problems } from "./problems.js";
import { setTabWidth, tabs } from "./tabs.js";
import type { TabWidth } from "./tabs.js";

export interface EditorProps {
  path: string; // the file, within the project, like main.one
  value: string;
  onChange: (value: string) => void;
  files: Files; // the whole project, this file included
  compiler: Pick<Compiler, "check">;
  tabWidth?: TabWidth;
  onLine?: (line: number) => void; // the line the cursor is on, numbered from 1
}

// A .one file in CodeMirror. The editor owns its text while it's open; a value from
// outside replaces it only when it's different, so typing is never undone by an echo.
export function Editor({ path, value, onChange, files, compiler, tabWidth = 4, onLine }: EditorProps) {
  const host = useRef<HTMLDivElement>(null);
  const view = useRef<EditorView | null>(null);
  const latest = useRef({ onChange, onLine, files });
  latest.current = { onChange, onLine, files };

  useEffect(() => {
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
          highlighting(),
          tabs(tabWidth),
          problems({ compiler, path, files: () => latest.current.files }),
          EditorView.lineWrapping,
          EditorView.contentAttributes.of({ "aria-label": path }),
          EditorView.updateListener.of((update) => {
            if (update.docChanged) latest.current.onChange(update.state.doc.toString());
            if (update.docChanged || update.selectionSet) {
              latest.current.onLine?.(update.state.doc.lineAt(update.state.selection.main.head).number);
            }
          }),
        ],
      }),
    });
    view.current = editor;
    return () => {
      editor.destroy();
      view.current = null;
    };
    // A new file or compiler is a new editor; the rest is followed below.
  }, [path, compiler]);

  useEffect(() => {
    const editor = view.current;
    if (editor && editor.state.doc.toString() !== value) {
      editor.dispatch({ changes: { from: 0, to: editor.state.doc.length, insert: value } });
    }
  }, [value]);

  useEffect(() => {
    if (view.current) setTabWidth(view.current, tabWidth);
  }, [tabWidth]);

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
  compiler: Pick<Compiler, "check" | "build">;
  files: Files;
  path: string;
  onChange: (path: string, value: string) => void;
  tabWidth?: TabWidth;
  delay?: number; // milliseconds after the last change before building, 500 unless set
  generated?: boolean; // whether to show the code it becomes beside it; true unless set
}

// A .one file beside what it becomes. The project is built a moment after typing
// stops; the code from the last build that worked stays up while a mistake is fixed.
export function Workbench({ compiler, files, path, onChange, tabWidth = 4, delay = 500, generated = true }: WorkbenchProps) {
  const [line, setLine] = useState(1);
  const [built, setBuilt] = useState<Built | undefined>();
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
    <div className={generated ? "uione-workbench" : "uione-workbench uione-workbench-alone"}>
      <Editor
        path={path}
        value={files[path] ?? ""}
        onChange={(value) => onChange(path, value)}
        files={files}
        compiler={compiler}
        tabWidth={tabWidth}
        onLine={setLine}
      />
      {generated && <Generated files={built?.files ?? []} path={path} line={line} tabWidth={tabWidth} />}
    </div>
  );
}
