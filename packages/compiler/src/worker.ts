// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The compiler in a Web Worker, so a page stays responsive while it runs. It loads
// once, and answers each message in turn.

import { run } from "./project.js";
import type { Module, Request } from "./project.js";

// @ts-expect-error: one.mjs is the Emscripten build, copied in beside this file.
import load from "./one.mjs";

const ready: Promise<Module> = load();

self.onmessage = async (event: MessageEvent<{ id: number; request: Request }>) => {
  const { id, request } = event.data;
  try {
    self.postMessage({ id, answer: run(await ready, request) });
  } catch (e) {
    self.postMessage({ id, error: e instanceof Error ? e.message : String(e) });
  }
};
