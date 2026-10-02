// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Holds the compiler built for the browser to the native one: for every project in
// the repository, the same files, byte for byte, the same line sources, and the
// same errors, word for word.
//
//   make -C compiler && tools/wasm/build && node tools/wasm/test.mjs

import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const native = join(root, "compiler/build/one");
const { default: load } = await import(join(root, "compiler/build-wasm/one.mjs"));
const one = await load();

// The repository, and the temporary folder for broken projects, are seen by the
// module at the same paths as on disk, so every path in the output is the same.
const scratch = mkdtempSync(join(tmpdir(), "uione-wasm-"));
for (const folder of [root, scratch]) {
  one.FS.mkdirTree(folder);
  one.FS.mount(one.FS.filesystems.NODEFS, { root: folder }, folder);
}

const call = (name, ...args) => JSON.parse(one.ccall(name, "string", args.map(() => "string"), args));
const run = (args) => {
  try {
    return { status: 0, out: execFileSync(native, args, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }) };
  } catch (e) {
    return { status: e.status, out: String(e.stdout), err: String(e.stderr) };
  }
};

let failed = 0;
const fail = (message) => {
  failed++;
  console.log(`FAIL  ${message}`);
};

if (one.ccall("one_version", "string", [], []) !== run(["--version"]).out.trim().replace(/^one /, "")) fail("the versions differ");

for (const project of ["site", "examples/library", "examples/tasks", "examples/tracker"]) {
  const dir = join(root, project);
  const out = join(dir, "build");
  const built = call("one_build", dir, out);
  if (built.refusal) {
    fail(`${project}: the browser's build refused: ${built.refusal}\n${built.problems.map((p) => p.text).join("\n")}`);
    continue;
  }
  const result = run(["build", dir, "--out", out]);
  if (result.status !== 0) {
    fail(`${project}: the native build failed:\n${result.err}`);
    continue;
  }
  for (const file of built.files) {
    const written = readFileSync(join(out, file.path), "utf8");
    if (written !== file.content) fail(`${project}: ${file.path} differs from the native build's`);
    const lines = file.content.split("\n").length - (file.content.endsWith("\n") ? 1 : 0);
    if (file.sources.length !== lines) fail(`${project}: ${file.path} has ${file.sources.length} sources for ${lines} lines`);
  }
  const listed = result.out.match(/wrote (\d+) files/)?.[1];
  if (Number(listed) !== built.files.length) fail(`${project}: the native build wrote ${listed} files, the browser's ${built.files.length}`);
  console.log(`ok    ${project}: ${built.files.length} files the same`);
}

// The same mistakes, said the same way: one the parser stops at, and several the
// checker finds.
const mistakes = {
  "a file that doesn't parse": "entity book {\n\ttitle text requird\n}\n",
  "a file with mistakes in its names": "entity book {\n\ttitle  text\n\tdueAt  date\n}\nview shelf {\n\teach book where statu == lent {\n\t\ttitel\n\t}\n}\n",
};
for (const [name, source] of Object.entries(mistakes)) {
  const broken = join(scratch, name.replaceAll(" ", "-").replaceAll("'", ""));
  one.FS.mkdirTree(broken);
  writeFileSync(join(broken, "main.one"), source);
  const checked = call("one_check", broken);
  const wanted = run(["check", broken]).err.split("\n").filter((line) => line.includes(": error: "));
  const got = checked.problems.map((p) => p.text);
  if (JSON.stringify(got) !== JSON.stringify(wanted) || got.length === 0) {
    fail(`${name}: the errors differ:\n  native  ${wanted.join("\n          ")}\n  browser ${got.join("\n          ")}`);
  } else {
    console.log(`ok    ${name}: ${got.length} ${got.length === 1 ? "error" : "errors"}, word for word`);
  }
}

// And one show finds the same lines.
const line = readFileSync(join(root, "examples/tracker/main.one"), "utf8").split("\n").findIndex((l) => l.includes("add me to assignees")) + 1;
const where = `${join(root, "examples/tracker/main.one")}:${line}`;
const shown = call("one_show", where);
const printed = run(["show", where]).out;
const rendered = shown.files.map((f) => `${f.path}\n${f.lines.map(([n, text]) => `${String(n).padStart(5)}  ${text}`).join("\n")}\n`).join("\n");
if (rendered !== printed) fail(`one show differs:\n${printed}\n---\n${rendered}`);
else console.log(`ok    one show: ${shown.files.reduce((n, f) => n + f.lines.length, 0)} lines the same`);

rmSync(scratch, { recursive: true, force: true });
console.log(failed ? `${failed} differences` : "the browser's compiler matches the native one");
process.exit(failed ? 1 : 0);
