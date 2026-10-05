// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { createCompiler } from "../src/index.js";
import type { Port } from "../src/index.js";
import { run } from "../src/project.js";
import type { Built, Checked, Module, Shown } from "../src/project.js";

const repository = fileURLToPath(new URL("../../..", import.meta.url));
// The Emscripten build, made by tools/wasm/build.
const { default: load } = await import(`${repository}compiler/build-wasm/one.mjs`);
const one: Module = await load();

const tasks = { "main.one": readFileSync(`${repository}examples/tasks/main.one`, "utf8") };

describe("the compiler in the browser", () => {
  it("checks a project and finds nothing wrong", () => {
    expect(run(one, { kind: "check", files: tasks })).toEqual({ problems: [], files: 1, project: null });
  });

  it("says what's wrong, where, within the project", () => {
    const checked = run(one, { kind: "check", files: { "main.one": "entity book {\n\tdueAt  date\n}\n" } }) as Checked;
    expect(checked.problems).toEqual([
      { path: "main.one", line: 2, column: 2, message: "'dueAt' isn't snake_case; write it as due_at" },
    ]);
  });

  it("builds every file, with the line of the .one each line came from", () => {
    const built = run(one, { kind: "build", files: tasks }) as Built;
    expect(built.refusal).toBe("");
    const backend = built.files.find((f) => f.path === "api/tasks/tasks.go");
    expect(backend).toBeDefined();
    const lines = backend!.content.split("\n");
    const at = lines.findIndex((l) => l.includes("t.Done = true"));
    expect(backend!.sources[at]).toEqual({ path: "main.one", line: expect.any(Number) });
    const written = tasks["main.one"].split("\n")[(backend!.sources[at] as { line: number }).line - 1];
    expect(written).toContain("done = true");
  });

  it("refuses to build a project with mistakes, and says why", () => {
    const built = run(one, { kind: "build", files: { "main.one": "entity book {\n\ttitle text requird\n}\n" } }) as Built;
    expect(built.files).toEqual([]);
    expect(built.refusal).toBe("nothing was built, because the project has errors");
    expect(built.problems).toHaveLength(1);
  });

  it("shows what a line becomes", () => {
    const line = tasks["main.one"].split("\n").findIndex((l) => l.includes("done = true")) + 1;
    const shown = run(one, { kind: "show", files: tasks, path: "main.one", from: line }) as Shown;
    expect(shown.files.map((f) => f.path)).toContain("api/tasks/tasks.go");
  });

  it("forgets a project's files when it's given another's", () => {
    run(one, { kind: "check", files: { "old.one": "entity broken {\n" } });
    expect((run(one, { kind: "check", files: tasks }) as Checked).problems).toEqual([]);
  });

  it("outlines the project block, and builds for the environment named", () => {
    const shop =
      'project shop {\n\tregion "us-east4"\n' +
      '\tenvironment production {\n\t\tdomain "shop.example"\n\t\tfirebase "shop"\n\t}\n' +
      '\tenvironment staging {\n\t\tdomain "staging.shop.example"\n\t\tfirebase "shop-staging"\n\t}\n}\n' +
      "namespace shop {\n\tentity order {\n\t\ttotal  number\n\t}\n}\n";
    const checked = run(one, { kind: "check", files: { "shop.one": shop } }) as Checked;
    expect(checked.project).toEqual({
      path: "shop.one",
      name: "shop",
      line: 1,
      end: 11,
      settings: [{ key: "region", value: "us-east4", line: 2 }],
      environments: [
        { name: "production", line: 3, end: 6, settings: [{ key: "domain", value: "shop.example", line: 4 }, { key: "firebase", value: "shop", line: 5 }] },
        { name: "staging", line: 7, end: 10, settings: [{ key: "domain", value: "staging.shop.example", line: 8 }, { key: "firebase", value: "shop-staging", line: 9 }] },
      ],
    });
    expect((run(one, { kind: "check", files: tasks }) as Checked).project).toBeNull();
    const staging = run(one, { kind: "build", files: { "shop.one": shop }, environment: "staging" }) as Built;
    expect(staging.environment).toBe("staging");
    expect(staging.files.find((f) => f.path === "deploy")?.content).toContain("--stack shop-staging --project shop-staging ");
    expect((run(one, { kind: "build", files: { "shop.one": shop } }) as Built).environment).toBe("production");
  });

  it("says what a name means, and where it's declared, in another file", () => {
    const files = {
      "projects.one": "namespace tracker {\n\tentity project {\n\t\ttakes_reports  boolean = false\n\t}\n}\n",
      "reports.one":
        "namespace tracker {\n\tentity report {\n\t\tproject  project  required\n\t}\n" +
        '\tcommand report::create {\n\t\tpermission authenticated\n\t\trequire project.takes_reports  "no"\n\t}\n}\n',
    };
    const through = run(one, { kind: "define", files, path: "reports.one", line: 7, column: 20 });
    expect(through).toEqual({ found: true, says: "field takes_reports of project, a boolean", path: "projects.one", line: 3, column: 3, section: "", from: 19, to: 32 });
    const built = run(one, { kind: "define", files, path: "reports.one", line: 6, column: 15 });
    expect(built).toMatchObject({ found: true, says: "built-in permission authenticated: anyone signed in", path: "", section: "command" });
    expect(run(one, { kind: "define", files, path: "reports.one", line: 1, column: 1 })).toEqual({ found: false });
  });

  it("keeps a project's files inside it", () => {
    expect(() => run(one, { kind: "check", files: { "../escape.one": "" } })).toThrow();
  });
});

describe("talking to the worker", () => {
  // Stands in for the worker, answering with the same run the worker uses.
  function port(): Port {
    let listener: ((event: MessageEvent) => void) | undefined;
    return {
      addEventListener: (_type, l) => {
        listener = l;
      },
      postMessage: (message) => {
        const { id, request } = message as { id: number; request: Parameters<typeof run>[1] };
        setTimeout(() => {
          try {
            listener?.({ data: { id, answer: run(one, request) } } as MessageEvent);
          } catch (e) {
            listener?.({ data: { id, error: (e as Error).message } } as MessageEvent);
          }
        });
      },
    };
  }

  it("answers each call with its own answer", async () => {
    const compiler = createCompiler(port());
    const [version, checked, broken] = await Promise.all([
      compiler.version(),
      compiler.check(tasks),
      compiler.check({ "main.one": "entity book {\n\tdueAt  date\n}\n" }),
    ]);
    expect(version).toBe(JSON.parse(readFileSync(`${repository}packages/compiler/package.json`, "utf8")).version);
    expect(checked.problems).toEqual([]);
    expect(broken.problems).toHaveLength(1);
  });

  it("turns a failure into a rejected promise", async () => {
    await expect(createCompiler(port()).check({ "/absolute.one": "" })).rejects.toThrow("named within it");
  });
});
