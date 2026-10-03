// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A place in a file, as an address carries it after #, the way GitHub's do: L12 is
// line 12, L12C5 is line 12 at its fifth character, and L12-L20 or L12C5-L14C2 is
// from one to the other. Lines and characters count from 1. Sending someone the
// address shows them exactly that.

import type { Text } from "@codemirror/state";

export interface Point {
  line: number;
  column?: number; // the character within the line; the whole line when it isn't given
}

export interface Spot {
  from: Point;
  to?: Point; // where a range ends; a spot without one is a single place
}

const point = /^L(\d+)(?:C(\d+))?$/;

// The spot an address names after its #, or undefined when it names none.
export function readSpot(hash: string): Spot | undefined {
  const [first, second, ...more] = hash.replace(/^#/, "").split("-");
  if (!first || more.length > 0) return undefined;
  const read = (written: string): Point | undefined => {
    const found = point.exec(written);
    if (!found) return undefined;
    const line = Number(found[1]);
    const column = found[2] === undefined ? undefined : Number(found[2]);
    if (line < 1 || (column !== undefined && column < 1)) return undefined;
    return column === undefined ? { line } : { line, column };
  };
  const from = read(first);
  if (!from) return undefined;
  if (second === undefined) return { from };
  const to = read(second);
  return to ? { from, to } : undefined;
}

// A spot as an address writes it after its #, without the #.
export function writeSpot(spot: Spot): string {
  const write = (p: Point) => `L${p.line}${p.column === undefined ? "" : `C${p.column}`}`;
  return spot.to ? `${write(spot.from)}-${write(spot.to)}` : write(spot.from);
}

// Where a spot is in a document, as offsets: a line on its own is the whole line,
// and a line or character past the end is the end of what's there.
export function place(doc: Text, spot: Spot): { anchor: number; head: number } {
  const at = (p: Point, end: boolean) => {
    const line = doc.line(Math.min(Math.max(p.line, 1), doc.lines));
    if (p.column === undefined) return end ? line.to : line.from;
    return Math.min(line.from + p.column - 1, line.to);
  };
  const anchor = at(spot.from, false);
  const head = spot.to ? at(spot.to, true) : anchor;
  return { anchor, head };
}

// The spot a selection is, from offsets: a cursor is a line and character, and a
// range is from one to the other.
export function spotOf(doc: Text, anchor: number, head: number): Spot {
  const p = (offset: number): Point => {
    const line = doc.lineAt(offset);
    return { line: line.number, column: offset - line.from + 1 };
  };
  const from = p(Math.min(anchor, head));
  if (anchor === head) return { from };
  return { from, to: p(Math.max(anchor, head)) };
}
