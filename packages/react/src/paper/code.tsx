// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Code: kept as it's typed, in the code face, in a well, colored as one::syntax reads its
// language once the caret's left it. Enter starts a new line, and Enter on an empty last
// line leaves it for a block after it. Written apart, its language is chosen there.

import { useInsertionEffect } from "react";
import type { ReactNode } from "react";
import { findLanguage, highlight, syntaxColor } from "../syntax.js";
import type { SyntaxKind } from "../syntax.js";
import type { BlockKind } from "./kinds.js";
import { inCode, well } from "./kinds.js";

const kinds: SyntaxKind[] = ["keyword", "type", "constant", "string", "number", "comment", "directive", "function"];
const colors = (dark: boolean) => kinds.map((k) => `--syntax-${k}: ${syntaxColor(k, dark)};`).join(" ");
// Each kind's color, light or dark as the page is, as the theme's own colors are.
const syntaxCss = `:root { ${colors(false)} } @media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { ${colors(true)} } } :root[data-theme="dark"] { ${colors(true)} }`;

// Words colored as a language reads them; as they are when it's none one::syntax knows.
export function Colored({ words, language }: { words: string; language?: string }) {
  const l = language ? findLanguage(language) : undefined;
  // The colors, once, in the page's head.
  useInsertionEffect(() => {
    if (!l || document.getElementById("one-syntax")) return;
    const style = document.createElement("style");
    style.id = "one-syntax";
    style.textContent = syntaxCss;
    document.head.appendChild(style);
  }, [l]);
  const parts: ReactNode[] = [];
  let done = 0;
  for (const s of l ? highlight(words, l) : []) {
    if (s.from > done) parts.push(words.slice(done, s.from));
    parts.push(
      <span key={s.from} style={{ color: `var(--syntax-${s.kind})` }}>
        {words.slice(s.from, s.to)}
      </span>,
    );
    done = s.to;
  }
  parts.push(words.slice(done));
  return <>{parts}</>;
}

export const code: BlockKind = {
  look: inCode,
  room: [10, 18],
  enter: (words, caret, selecting) => (!selecting && caret === words.length && words.endsWith("\n") ? "leave" : "line"),
  apart: { name: "Code", lines: true, languages: true },
  showing: ({ parts, words, focused, readOnly, toWords }) => {
    const colored = words !== "" && !!parts.language && !!findLanguage(parts.language);
    if (!colored || (focused && !readOnly)) return { raw: true, words: well };
    return {
      raw: false,
      // Not shown while it's colored, but still there to take the caret.
      words: { ...well, opacity: 0 },
      over: (
        <div
          style={{ ...well, gridArea: "1 / 1", whiteSpace: "pre-wrap", overflowWrap: "break-word", tabSize: 4, textAlign: "left", cursor: readOnly ? "default" : "text" }}
          onMouseDown={(e) => {
            if (readOnly) return;
            e.preventDefault();
            toWords();
          }}
        >
          <Colored words={words} language={parts.language} />
        </div>
      ),
    };
  },
};
