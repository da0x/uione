// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The kinds of block a paper is made of, each its own: how its words are set, the room
// around it, what it shows besides its words, and what Enter does in it. The paper asks
// a block's kind and knows none of them, so a kind is added as a file of its own and a
// line below; the desktop's kinds are the same, one for one.

import type { CSSProperties, ReactNode } from "react";

// What a kind is told as it's set: the document's face, and the size it's drawn at.
export interface Setting {
  face: string;
  zoom: number;
}

// What a block shows, given its words as they are now: whether its words show, how
// they look over the rest, and anything else it shows in their place, like an equation.
export interface Showing {
  raw: boolean;
  words?: CSSProperties;
  over?: ReactNode;
}

export interface ShowingAsked {
  words: string;
  focused: boolean;
  readOnly: boolean;
  zoom: number;
  align: CSSProperties["textAlign"];
  toWords: () => void; // the caret put in its words, as pressing what it shows does
}

export interface BlockKind {
  // How its block looks, its words and what's drawn around them, at a size.
  look(at: Setting): CSSProperties;
  // The room above and below it, before the size.
  room: [number, number];
  // Whether it goes to the next page with what follows it, as a heading does.
  leads?: boolean;
  // Its level in the outline, 1 a title's and 2 a heading's; not listed when it has none.
  outlined?: number;
  // Whether words can be joined onto its end, as Backspace at the next one's start does.
  joins?: boolean;
  // What Enter does, the caret at a place in its words: splits it, types a new line, or
  // leaves it, its empty last line taken away, for a block after it.
  enter?(words: string, caret: number, selecting: boolean): "split" | "line" | "leave";
  // What it becomes when it starts with a character and a space as it's typed.
  shortcut?(first: string): string | undefined;
  // What it shows besides its words; its words alone when it doesn't say.
  showing?(asked: ShowingAsked): Showing;
}

// The family a page's face is: a book's serif, the app's own text face, a lyric's
// warmer serif, or its code face, a typewriter's.
export const families: Record<string, string> = {
  book: "'Literata Variable', Literata, serif",
  modern: "var(--font-sans, sans-serif)",
  lyric: "'Fraunces Variable', Fraunces, serif",
  typewriter: "var(--font-mono, monospace)",
};

// Words in the page's face at a size, a typewriter's a size smaller but for headings,
// as its letters are wide.
export function inFace(at: Setting, size: number, heading: boolean, rest: CSSProperties = {}): CSSProperties {
  return { ...rest, fontSize: (size - (at.face === "typewriter" && !heading ? 1 : 0)) * at.zoom, fontFamily: families[at.face] ?? families["modern"] };
}

// Words in the code face, as an equation's words and code are, whatever the page's is.
export const inCode = (at: Setting): CSSProperties => ({ fontSize: 14 * at.zoom, lineHeight: 1.5, fontFamily: "var(--font-mono, monospace)" });

// A well, as code and an equation's words are typed in.
export const well: CSSProperties = {
  background: "var(--color-sunken, #eef1f5)",
  borderRadius: 4,
  boxShadow: "0 0 0 4px var(--color-sunken, #eef1f5), 8px 0 0 4px var(--color-sunken, #eef1f5), -8px 0 0 4px var(--color-sunken, #eef1f5)",
};
