// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Running the compiler over one project: its files go into the module's in-memory
// file system, under /project, and the answer comes back as data. The worker uses
// this, and so do the tests, in Node, without a worker.

// A project's files, by path within the project, like main.one or docs/intro.md.
export type Files = Record<string, string>;

export interface Problem {
  path: string; // within the project, like main.one
  line: number;
  column: number;
  message: string;
}

// Where a generated line came from: a line of a .one file, "same" for a line every
// project gets, or null for a blank line.
export type Source = { path: string; line: number } | "same" | null;

export interface GeneratedFile {
  path: string; // within the build, like api/library/library.go
  content: string;
  executable: boolean;
  sources: Source[]; // one per line of content
}

// Where a project block's parts are, by line, so an editor can change them: each
// setting, and each environment from its first line to its closing brace's.
export interface OutlinedSetting {
  key: string;
  value: string;
  line: number;
}

export interface OutlinedEnvironment {
  name: string;
  line: number;
  end: number;
  settings: OutlinedSetting[];
}

export interface Outline {
  path: string; // the file the project block is in
  name: string;
  line: number;
  end: number;
  settings: OutlinedSetting[]; // shared by every environment
  environments: OutlinedEnvironment[];
}

// A screen's parts, by line, so an editor can lay it out: its layout, and each
// item, named by its kind, what it shows or runs and what it says; a region, like
// main, holds items of its own.
export interface OutlinedItem {
  kind: string; // table, form, button, text, markdown, region, ...
  subject: string; // projects::issue_page.comments, issue::close, main
  label: string; // "Close issue", when it says something
  line: number;
  items: OutlinedItem[];
  table: OutlinedTable | null; // a table's columns and settings
}

// A table's columns, and how it's divided, searched, sorted and paged, empty or 0
// when it isn't; and what its rows hold, which is what it can show, the ones with
// choices marked, since those are what it can be divided by.
export interface OutlinedTable {
  columns: { value: string; label: string | null; when: string; line: number }[]; // when: a row button's, as written, or ""
  rows: { name: string; choices: boolean }[];
  by: string;
  search: string[];
  sort: string; // -number, for the largest first
  page: number;
  link: string;
}

export interface OutlinedScreen {
  path: string;
  title: string;
  route: string;
  line: number;
  layout: string; // as written; empty when the project's is used
  layoutLine: number; // where it's written, or 0
  items: OutlinedItem[];
}

// What the name at a place in a file means: what it says, and where it's declared,
// or the language reference's section for one of the language's own words.
export type Definition =
  | { found: false }
  | {
      found: true;
      says: string; // field project of report, a project
      path: string; // the file it's declared in; empty for the language's own
      line: number;
      column: number;
      section: string; // the reference's section, like built-in-values
      from: number; // the columns the name spans on its line, the end not included
      to: number;
    };

// What can be written at a place in a file, from uione's library: a project's
// settings in its block, and an enum setting's choices after its name.
export interface Completions {
  from: number; // the column the word being written starts at
  items: { label: string; detail: string; info: string }[]; // detail: its type, or how a choice is shown
}

export interface Checked {
  problems: Problem[];
  files: number;
  project: Outline | null; // when a project block parsed
  screens: OutlinedScreen[]; // when everything parsed
}

export interface Built {
  problems: Problem[];
  refusal: string; // why nothing was built, or empty
  note: string;
  environment: string; // the one built for, when the project has them
  files: GeneratedFile[];
}

export interface ShownFile {
  path: string;
  lines: [number, string][];
}

export interface Shown {
  problems: Problem[];
  files: ShownFile[];
}

export type Request =
  | { kind: "version" }
  | { kind: "check"; files: Files }
  | { kind: "build"; files: Files; environment?: string }
  | { kind: "define"; files: Files; path: string; line: number; column: number }
  | { kind: "show"; files: Files; path: string; from: number; to?: number }
  | { kind: "complete"; text: string; line: number; column: number };

export type Answer = string | Checked | Built | Shown | Definition | Completions;

// The parts of the Emscripten module this uses.
export interface Module {
  FS: {
    mkdirTree(path: string): void;
    writeFile(path: string, data: string): void;
    readdir(path: string): string[];
    isDir(mode: number): boolean;
    stat(path: string): { mode: number };
    unlink(path: string): void;
    rmdir(path: string): void;
  };
  ccall(name: string, returns: "string", types: ("string" | "number")[], args: (string | number)[]): string;
}

const project = "/project";

export function run(one: Module, request: Request): Answer {
  if (request.kind === "version") return one.ccall("one_version", "string", [], []);
  if (request.kind === "complete") {
    return JSON.parse(one.ccall("one_complete", "string", ["string", "number", "number"], [request.text, request.line, request.column])) as Completions;
  }
  place(one, request.files);
  const call = (name: string, ...args: string[]) => JSON.parse(one.ccall(name, "string", args.map(() => "string"), args));
  if (request.kind === "check") {
    const checked = within(call("one_check", project)) as Checked;
    if (checked.project) checked.project.path = inside(checked.project.path);
    for (const screen of checked.screens) screen.path = inside(screen.path);
    return checked;
  }
  if (request.kind === "define") {
    const defined = JSON.parse(
      one.ccall("one_define", "string", ["string", "string", "number", "number"], [project, `${project}/${request.path}`, request.line, request.column]),
    ) as Definition;
    if (defined.found && defined.path) defined.path = inside(defined.path);
    return defined;
  }
  if (request.kind === "build") {
    const built = within(call("one_build", project, `${project}/build`, request.environment ?? "")) as Built;
    for (const file of built.files) {
      file.sources = file.sources.map((s) => (s && s !== "same" ? { path: inside(s.path), line: s.line } : s));
    }
    return built;
  }
  const lines = request.to && request.to !== request.from ? `${request.from}-${request.to}` : `${request.from}`;
  const shown = call("one_show", `${project}/${request.path}:${lines}`) as Shown & { understood: boolean };
  return within({ problems: shown.problems, files: shown.files });
}

// Replaces whatever was in /project with these files.
function place(one: Module, files: Files) {
  clear(one, project);
  one.FS.mkdirTree(project);
  for (const [path, text] of Object.entries(files)) {
    if (path.startsWith("/") || path.split("/").includes("..")) throw new Error(`a project's file is named within it, not ${path}`);
    const full = `${project}/${path}`;
    one.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
    one.FS.writeFile(full, text);
  }
}

function clear(one: Module, path: string) {
  let entries: string[];
  try {
    entries = one.FS.readdir(path);
  } catch {
    return;
  }
  for (const name of entries) {
    if (name === "." || name === "..") continue;
    const child = `${path}/${name}`;
    if (one.FS.isDir(one.FS.stat(child).mode)) {
      clear(one, child);
      one.FS.rmdir(child);
    } else {
      one.FS.unlink(child);
    }
  }
}

// Paths in what the compiler says are made relative to the project again.
function inside(path: string): string {
  return path.startsWith(`${project}/`) ? path.slice(project.length + 1) : path;
}

function within<T extends { problems: { path: string; line: number; column: number; message: string }[] }>(answer: T): T {
  answer.problems = answer.problems.map(({ path, line, column, message }) => ({ path: inside(path), line, column, message }));
  return answer;
}
