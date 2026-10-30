// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// syntax.ts held to syntax/tests/languages.tsv, the table it keeps being that file but its
// comments, and to syntax/tests/cases.tsv, everything written there colored as it says, as
// one::syntax's syntax.hpp is.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { findLanguage, highlight, syntaxTable } from "../src/syntax.js";

const here = dirname(fileURLToPath(import.meta.url));
const read = (name: string) => readFileSync(join(here, "../../../syntax/tests", name), "utf8").split("\n");
// \n, \t and \\ as C writes them; any other \ is itself.
const unescaped = (s: string) => s.replace(/\\([nt\\])/g, (_, c: string) => (c === "n" ? "\n" : c === "t" ? "\t" : "\\"));

describe("code colored as one::syntax colors it", () => {
  it("keeps the table of languages as it's written", () => {
    const table = read("languages.tsv").filter((line) => line && !line.startsWith("#")).map((line) => line + "\n").join("");
    expect(syntaxTable).toBe(table);
  });

  for (const line of read("cases.tsv").filter((line) => line && !line.startsWith("#"))) {
    const [language, written, ...wanted] = line.split("\t") as [string, string, ...string[]];
    if (language === "find") {
      it(`finds ${written} as ${wanted[0]}`, () => expect(findLanguage(written)?.name ?? "-").toBe(wanted[0]));
      continue;
    }
    it(`colors ${language}: ${written}`, () => {
      const l = findLanguage(language);
      expect(l).toBeDefined();
      const source = unescaped(written);
      const got = highlight(source, l!).map((s) => `${s.kind}:${source.slice(s.from, s.to)}`);
      expect(got).toEqual(wanted.map(unescaped));
    });
  }
});
