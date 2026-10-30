// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A block diagram drawn as diagram.ts lays it out, as one::diagram lays it out for a
// window: its blocks' outlines, its wires and their arrows, the dots where they branch,
// and what's written, set as equations are. In ems, as big as the words around it, and
// never wider than the page.

import { useMemo } from "react";
import { layOutDiagram } from "./diagram.js";
import type { DiagramShape } from "./diagram.js";
import { MathDrawn } from "./equation.js";
import { layOutMath, parseMath } from "./math.js";

const stroke = 0.06;

function Shape({ s }: { s: DiagramShape }) {
  const { x, y, width: w, height: h } = s;
  if (s.type === "sum") return <circle cx={x + w / 2} cy={y + h / 2} r={w / 2} />;
  if (s.type === "gain") {
    const points = s.flipped ? [[x + w, y], [x + w, y + h], [x, y + h / 2]] : [[x, y], [x, y + h], [x + w, y + h / 2]];
    return <polygon points={points.map((p) => p.join(",")).join(" ")} />;
  }
  return <rect x={x} y={y} width={w} height={h} rx={s.type === "port" ? 0.3 : 0} />;
}

// An arrow's head, its tip where a wire ends, pointing the way it last runs.
function head(points: [number, number][]) {
  const [tx, ty] = points[points.length - 1]!;
  const [px, py] = points[points.length - 2]!;
  const run = Math.hypot(tx - px, ty - py) || 1;
  const ux = (tx - px) / run,
    uy = (ty - py) / run;
  const bx = tx - ux * 0.38,
    by = ty - uy * 0.38;
  return `${tx},${ty} ${bx - uy * 0.16},${by + ux * 0.16} ${bx + uy * 0.16},${by - ux * 0.16}`;
}

function Face({ written, size, x, y }: { written: string; size: number; x: number; y: number }) {
  const read = parseMath(written);
  if (!("node" in read)) return null;
  return (
    <g transform={`translate(${x} ${y})`}>
      <MathDrawn laid={layOutMath(read.node, size, false)} />
    </g>
  );
}

export function DiagramView({ written }: { written: string }) {
  const laid = useMemo(() => layOutDiagram(written), [written]);
  // One that can't be read is shown as it's written, saying why when pointed at.
  if (!("diagram" in laid)) {
    return (
      <code data-problem="" title={laid.problem.says}>
        {written}
      </code>
    );
  }
  const d = laid.diagram;
  return (
    <svg role="img" aria-label={written} width={`${d.width}em`} viewBox={`0 0 ${d.width} ${d.height}`} style={{ fontSize: "1.25em", maxWidth: "100%", height: "auto" }} fill="currentColor">
      <g fill="none" stroke="currentColor" strokeWidth={stroke} strokeLinejoin="round">
        {d.shapes.map((s, i) => (
          <Shape key={i} s={s} />
        ))}
        {d.wires.map((w, i) => (
          <polyline key={i} points={w.points.map((p) => p.join(",")).join(" ")} />
        ))}
      </g>
      {d.wires.map((w, i) => w.arrow && <polygon key={i} points={head(w.points)} />)}
      {d.dots.map(([x, y], i) => (
        <circle key={i} cx={x} cy={y} r={0.12} />
      ))}
      {d.faces.map((f, i) => (
        <Face key={i} {...f} />
      ))}
    </svg>
  );
}
