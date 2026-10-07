// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The uione compiler for the browser. createCompiler starts it in a Web Worker;
// each call sends a project's files and resolves with what the compiler found.

import type { Built, Checked, Definition, Files, Request, Shown } from "./project.js";

export type { Built, Checked, Definition, Files, GeneratedFile, Outline, OutlinedEnvironment, OutlinedItem, OutlinedScreen, OutlinedSetting, OutlinedTable, Problem, Shown, ShownFile, Source } from "./project.js";

export interface Compiler {
  version(): Promise<string>;
  check(files: Files): Promise<Checked>;
  // Builds for the environment named, or the first.
  build(files: Files, environment?: string): Promise<Built>;
  // What the name at a line and column of a file means, both counted from 1.
  define(files: Files, path: string, line: number, column: number): Promise<Definition>;
  // The generated lines that came from a line, or lines from to to, of one file.
  show(files: Files, path: string, from: number, to?: number): Promise<Shown>;
  stop(): void;
}

// Anything that can stand in for a Worker: a real one, or a fake in a test.
export interface Port {
  postMessage(message: unknown): void;
  addEventListener(type: "message", listener: (event: MessageEvent) => void): void;
  terminate?(): void;
}

export function createCompiler(port: Port = new Worker(new URL("./worker.js", import.meta.url), { type: "module" })): Compiler {
  let next = 0;
  const waiting = new Map<number, { resolve: (answer: never) => void; reject: (error: Error) => void }>();
  port.addEventListener("message", (event: MessageEvent<{ id: number; answer?: unknown; error?: string }>) => {
    const { id, answer, error } = event.data;
    const pending = waiting.get(id);
    if (!pending) return;
    waiting.delete(id);
    if (error !== undefined) pending.reject(new Error(error));
    else pending.resolve(answer as never);
  });
  const ask = <T>(request: Request) =>
    new Promise<T>((resolve, reject) => {
      const id = next++;
      waiting.set(id, { resolve: resolve as (answer: never) => void, reject });
      port.postMessage({ id, request });
    });
  return {
    version: () => ask<string>({ kind: "version" }),
    check: (files) => ask<Checked>({ kind: "check", files }),
    build: (files, environment) => ask<Built>({ kind: "build", files, ...(environment === undefined ? {} : { environment }) }),
    define: (files, path, line, column) => ask<Definition>({ kind: "define", files, path, line, column }),
    show: (files, path, from, to) => ask<Shown>({ kind: "show", files, path, from, ...(to === undefined ? {} : { to }) }),
    stop: () => {
      port.terminate?.();
      for (const pending of waiting.values()) pending.reject(new Error("the compiler was stopped"));
      waiting.clear();
    },
  };
}
