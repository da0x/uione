// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Copies tests/languages.tsv, but its comments, into syntax.hpp and @uione/react's
// syntax.ts, where each keeps it as it's written: run after a language is added or changed.
// Each one's test says when it's behind.

import { readFileSync, writeFileSync } from "node:fs";

const here = new URL(".", import.meta.url);
const table = readFileSync(new URL("tests/languages.tsv", here), "utf8")
  .split("\n")
  .filter((line) => line !== "" && !line.startsWith("#"))
  .map((line) => line + "\n")
  .join("");

const header = new URL("include/one/syntax.hpp", here);
const cpp = readFileSync(header, "utf8").replace(/R"tsv\([\s\S]*?\)tsv"/, () => `R"tsv(${table})tsv"`);
writeFileSync(header, cpp);

const module = new URL("../packages/react/src/syntax.ts", here);
const escaped = table.replaceAll("\\", "\\\\").replaceAll("`", "\\`").replaceAll("${", "\\${");
const ts = readFileSync(module, "utf8").replace(/export const syntaxTable = `[\s\S]*?(?<!\\)`;/, () => `export const syntaxTable = \`${escaped}\`;`);
writeFileSync(module, ts);
