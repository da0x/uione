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

export interface Checked {
  problems: Problem[];
  files: number;
  project: Outline | null; // when a project block parsed
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
  | { kind: "show"; files: Files; path: string; from: number; to?: number };

export type Answer = string | Checked | Built | Shown;

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
  ccall(name: string, returns: "string", types: string[], args: string[]): string;
}

const project = "/project";

export function run(one: Module, request: Request): Answer {
  if (request.kind === "version") return one.ccall("one_version", "string", [], []);
  place(one, request.files);
  const call = (name: string, ...args: string[]) => JSON.parse(one.ccall(name, "string", args.map(() => "string"), args));
  if (request.kind === "check") {
    const checked = within(call("one_check", project)) as Checked;
    if (checked.project) checked.project.path = inside(checked.project.path);
    return checked;
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
