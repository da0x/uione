// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The kinds that are words alone, set in the page's face: a title, a subtitle, a
// heading ruled under across the line, text, and a quote with a bar beside it.

import type { BlockKind } from "./kinds.js";
import { inFace } from "./kinds.js";

export const title: BlockKind = {
  look: (at) => inFace(at, 32, true, { fontWeight: 700, lineHeight: 1.2 }),
  room: [0, 6],
  leads: true,
  outlined: 1,
};

export const subtitle: BlockKind = {
  look: (at) => inFace(at, 19, false, { lineHeight: 1.4, color: "var(--color-muted, #5b6577)" }),
  room: [0, 22],
  leads: true,
};

// A heading is ruled under, across the whole line, its words aligned over it.
export const heading: BlockKind = {
  look: (at) => inFace(at, 22, true, { fontWeight: 600, lineHeight: 1.3, paddingBottom: 8 * at.zoom, borderBottom: "1px solid var(--color-line, #dde2ea)" }),
  room: [30, 12],
  leads: true,
  outlined: 2,
};

// Text, which "# " makes a heading and "> " a quote as it's typed.
export const text: BlockKind = {
  look: (at) => inFace(at, 16, false, { lineHeight: 1.6 }),
  room: [0, 12],
  shortcut: (first) => (first === "#" ? "heading" : first === ">" ? "quote" : undefined),
};

// A quote, quieter and slanted, a bar down its side.
export const quote: BlockKind = {
  look: (at) =>
    inFace(at, 16, false, { lineHeight: 1.6, fontStyle: "italic", color: "var(--color-muted, #5b6577)", paddingLeft: 17 * at.zoom, borderLeft: "3px solid var(--color-line, #dde2ea)" }),
  room: [0, 12],
};
