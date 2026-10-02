// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Highlighting for .one code, using the same TextMate grammar as the editors, so code
// on a page looks the way it does in VS Code. The highlighter runs on JavaScript's own
// regular expressions, so it works on the first render with nothing to download.

import githubDark from "@shikijs/themes/github-dark";
import githubLight from "@shikijs/themes/github-light";
import { createHighlighterCoreSync } from "shiki/core";
import type { LanguageRegistration } from "shiki/core";
import { createJavaScriptRegexEngine } from "shiki/engine/javascript";
import grammar from "./grammar/uione.json" with { type: "json" };
import { restyle } from "./display.js";
import type { NameStyle } from "./display.js";

const highlighter = createHighlighterCoreSync({
  themes: [githubLight, githubDark],
  langs: [grammar as unknown as LanguageRegistration],
  engine: createJavaScriptRegexEngine(),
});

// Whether a part of a line is a name: something the file declared or refers to,
// rather than a keyword, a type, a value, a string or a comment.
function isName(scopes: string[]): boolean {
  return scopes.every((s) => s === "source.uione" || s.startsWith("entity.name.") || s.startsWith("variable.parameter."));
}

// The code as highlighted HTML, or nothing for a language this doesn't know, in
// which case the code is shown plain. Names are shown in the style asked for; the
// file itself is unchanged.
export function highlight(source: string, lang: string, names: NameStyle = "default"): string | undefined {
  if (lang !== "uione") return undefined;
  return highlighter.codeToHtml(source, {
    lang: "uione",
    themes: { light: "github-light", dark: "github-dark" },
    defaultColor: "light",
    includeExplanation: names !== "default",
    transformers: [
      {
        tokens(lines) {
          if (names === "default") return;
          for (const line of lines) {
            for (const token of line) {
              if (!token.explanation) continue;
              // Each part is found in the token's text and restyled where it is, so
              // whatever lies between the parts, like spaces, stays as it was.
              let out = "";
              let at = 0;
              for (const part of token.explanation) {
                const found = token.content.indexOf(part.content, at);
                if (found < 0) continue;
                const name = isName(part.scopes.map((s) => s.scopeName));
                out += token.content.slice(at, found) + (name ? restyle(part.content, names) : part.content);
                at = found + part.content.length;
              }
              token.content = out + token.content.slice(at);
            }
          }
        },
      },
    ],
  });
}

// Highlights the code blocks in a page made from markdown, which arrive as
// <pre><code class="language-one">. Each becomes the same box the Code component
// draws, given as `box`. Blocks in a language this doesn't know stay as they are.
export function highlightCodeBlocks(html: string, box: string, names: NameStyle = "default"): string {
  return html.replace(/<pre><code class="language-([\w-]+)">([\s\S]*?)<\/code><\/pre>/g, (block, lang: string, escaped: string) => {
    const highlighted = highlight(unescape(escaped).replace(/\n$/, ""), lang === "one" ? "uione" : lang, names);
    return highlighted === undefined ? block : `<div class="${box}">${highlighted}</div>`;
  });
}

function unescape(text: string): string {
  return text
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&amp;/g, "&");
}

