// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An equation drawn as math.ts sets it, as one::math sets it for a window: its glyphs in
// the math font, which the kit brings, its rules and lines, each where the layout puts
// them. Laid out in ems, so it's as big as the words around it, its baseline theirs.

import { useMemo } from "react";
import { layOutMath, mathFont, parseMath } from "./math.js";
import type { MathLaidOut } from "./math.js";

export function Equation({ written, displayed = false }: { written: string; displayed?: boolean }) {
  const read = useMemo(() => parseMath(written), [written]);
  const laid = useMemo(() => ("node" in read ? layOutMath(read.node, 1, displayed) : undefined), [read, displayed]);
  // One that can't be read is shown as it's written, saying why when pointed at.
  if (!laid || "problem" in read) {
    return (
      <code data-problem="" title={"problem" in read ? read.problem.says : undefined}>
        {written}
      </code>
    );
  }
  const height = laid.ascent + laid.descent;
  return (
    <svg
      role="img"
      aria-label={written}
      width={`${laid.width}em`}
      height={`${height}em`}
      viewBox={`0 ${-laid.ascent} ${laid.width} ${height}`}
      style={{ fontSize: displayed ? "1.25em" : "1.1em", verticalAlign: `${-laid.descent}em`, overflow: "visible" }}
      fill="currentColor"
    >
      <MathDrawn laid={laid} />
    </svg>
  );
}

// What an equation laid out draws, its baseline at 0, for an svg that places it.
export function MathDrawn({ laid }: { laid: MathLaidOut }) {
  return (
    <>
      {laid.drawn.map((d, i) =>
        d.type === "glyph" ? (
          <text key={i} x={d.x} y={d.y} fontFamily={mathFont} fontSize={d.size}>
            {String.fromCodePoint(d.code)}
          </text>
        ) : d.type === "rule" ? (
          <rect key={i} x={d.x} y={d.y} width={d.width} height={d.height} />
        ) : (
          <polyline key={i} points={d.points.map(([x, y]) => `${x},${y}`).join(" ")} fill="none" stroke="currentColor" strokeWidth={d.size} strokeLinecap="round" strokeLinejoin="round" />
        ),
      )}
    </>
  );
}
