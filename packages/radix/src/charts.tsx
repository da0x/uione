// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Charts and monitors. A chart is a run of numbers over time, newest at the right, as
// btop draws one: a 2px line over a soft fill in the accent, a quiet grid, its title
// and its latest value in ink, and, pointed at, the value under the pointer. A
// monitor is a tile for each record whose status changes, like an agent: its dot and
// state, what it's doing and how far, and its charts.

import { useId, useState } from "react";
import type { ChartProps, MonitorProps, MonitorTile } from "@uione/react";

const toneColors: Record<MonitorTile["tone"], string> = {
  inactive: "var(--color-muted)",
  waiting: "var(--color-accent)",
  working: "#d9730d",
  success: "var(--color-success)",
  failed: "var(--color-danger)",
};

// At least this many places along a chart, so a run just begun starts at the right
// and grows leftward, as btop's does, rather than stretching across.
const leastPlaces = 60;

export function Chart({ title, values, top, latest, topSaid, read, size }: ChartProps) {
  const large = size === "large";
  const width = 600;
  const height = large ? 140 : 44;
  const places = Math.max(values.length, leastPlaces);
  const fill = useId();
  const [pointed, setPointed] = useState<number | undefined>(undefined);
  const xOf = (i: number) => ((places - values.length + i) / (places - 1)) * width;
  const yOf = (v: number) => height - (Math.min(Math.max(v, 0), top) / top) * (height - 2) - 1;
  const line = values.map((v, i) => `${i === 0 ? "M" : "L"}${xOf(i).toFixed(1)},${yOf(v).toFixed(1)}`).join("");
  const area = values.length > 1 ? `${line}L${xOf(values.length - 1).toFixed(1)},${height}L${xOf(0).toFixed(1)},${height}Z` : "";
  const shown = pointed !== undefined && pointed < values.length ? pointed : undefined;
  const point = (event: React.PointerEvent<SVGSVGElement>) => {
    const box = event.currentTarget.getBoundingClientRect();
    if (box.width <= 0 || values.length === 0) return;
    const place = Math.round(((event.clientX - box.left) / box.width) * (places - 1));
    const i = place - (places - values.length);
    setPointed(i >= 0 ? i : undefined);
  };
  return (
    <figure className={`flex min-w-0 flex-col ${large ? "gap-2 rounded-box border border-line bg-surface p-4 shadow-panel" : "gap-1"}`}>
      <figcaption className="flex items-baseline justify-between gap-2">
        <span className={large ? "text-sm font-medium text-muted" : "text-xs text-muted"}>{title}</span>
        <span className={`tabular-nums text-ink ${large ? "text-2xl font-semibold tracking-tight" : "text-xs font-medium"}`}>
          {shown !== undefined ? read(values[shown]!) : latest}
        </span>
      </figcaption>
      <div className="relative">
        <svg
          viewBox={`0 0 ${width} ${height}`}
          preserveAspectRatio="none"
          className="block w-full overflow-visible"
          style={{ height }}
          role="img"
          aria-label={`${title}: ${latest}, of ${topSaid}`}
          onPointerMove={point}
          onPointerLeave={() => setPointed(undefined)}
        >
          <defs>
            <linearGradient id={fill} x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="var(--color-accent)" stopOpacity={0.28} />
              <stop offset="100%" stopColor="var(--color-accent)" stopOpacity={0.02} />
            </linearGradient>
          </defs>
          {large &&
            [0.25, 0.5, 0.75].map((at) => (
              <line key={at} x1={0} x2={width} y1={height * at} y2={height * at} stroke="var(--color-line)" strokeWidth={1} vectorEffect="non-scaling-stroke" strokeDasharray="2 4" />
            ))}
          <line x1={0} x2={width} y1={height - 0.5} y2={height - 0.5} stroke="var(--color-line)" strokeWidth={1} vectorEffect="non-scaling-stroke" />
          {area && <path d={area} fill={`url(#${fill})`} />}
          {line && (
            <path d={line} fill="none" stroke="var(--color-accent)" strokeWidth={large ? 2 : 1.5} strokeLinejoin="round" strokeLinecap="round" vectorEffect="non-scaling-stroke" />
          )}
          {shown !== undefined && (
            <line x1={xOf(shown)} x2={xOf(shown)} y1={0} y2={height} stroke="var(--color-muted)" strokeWidth={1} vectorEffect="non-scaling-stroke" />
          )}
        </svg>
        {large && <span className="pointer-events-none absolute top-0 right-0 text-[11px] text-muted tabular-nums">{topSaid}</span>}
      </div>
    </figure>
  );
}

// A small run of numbers in a row of a monitor: a line over a soft fill, no grid, and
// its latest value beside it.
function Spark({ chart }: { chart: ChartProps }) {
  const width = 120;
  const height = 22;
  const places = Math.max(chart.values.length, leastPlaces);
  const xOf = (i: number) => ((places - chart.values.length + i) / (places - 1)) * width;
  const yOf = (v: number) => height - (Math.min(Math.max(v, 0), chart.top) / chart.top) * (height - 2) - 1;
  const line = chart.values.map((v, i) => `${i === 0 ? "M" : "L"}${xOf(i).toFixed(1)},${yOf(v).toFixed(1)}`).join("");
  const area = chart.values.length > 1 ? `${line}L${xOf(chart.values.length - 1).toFixed(1)},${height}L${xOf(0).toFixed(1)},${height}Z` : "";
  return (
    <div className="flex min-w-0 items-center gap-2" role="img" aria-label={`${chart.title}: ${chart.latest}`}>
      <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" className="block h-[22px] min-w-0 flex-1" aria-hidden="true">
        <line x1={0} x2={width} y1={height - 0.5} y2={height - 0.5} stroke="var(--color-line)" strokeWidth={1} vectorEffect="non-scaling-stroke" />
        {area && <path d={area} fill="var(--color-accent)" fillOpacity={0.14} />}
        {line && <path d={line} fill="none" stroke="var(--color-accent)" strokeWidth={1.5} strokeLinejoin="round" vectorEffect="non-scaling-stroke" />}
      </svg>
      <span className="w-16 shrink-0 text-right text-xs font-medium text-ink tabular-nums">{chart.latest}</span>
    </div>
  );
}

// Records whose status changes, as a process list is: a row each, under a heading
// naming each column, its dot and name, its state and for how long, what it's doing,
// how far, and a small chart of each thing it reports.
export function Monitor({ status, tiles }: MonitorProps) {
  if (status === "live" && tiles.length === 0) return <p className="text-sm text-muted">Nothing here yet.</p>;
  const charts = tiles[0]?.charts.map((c) => c.title) ?? [];
  const columns = `minmax(10rem, 14rem) 7.5rem minmax(10rem, 1fr) 6.5rem${charts.map(() => " minmax(9rem, 12rem)").join("")}`;
  return (
    <div className="overflow-x-auto rounded-box border border-line bg-surface shadow-panel" aria-busy={status === "loading"}>
      <div role="table" className="min-w-max text-sm">
        <div role="row" className="grid items-center gap-x-4 border-b border-line bg-sunken px-4 py-2 text-xs font-medium text-muted" style={{ gridTemplateColumns: columns }}>
          <span role="columnheader">Name</span>
          <span role="columnheader">State</span>
          <span role="columnheader">Doing</span>
          <span role="columnheader">Progress</span>
          {charts.map((title) => (
            <span key={title} role="columnheader">
              {title}
            </span>
          ))}
        </div>
        {tiles.map((tile) => {
          const color = toneColors[tile.tone];
          const off = tile.tone === "inactive";
          return (
            <div
              key={tile.id}
              role="row"
              className={`grid items-center gap-x-4 border-b border-line px-4 py-1.5 last:border-b-0 hover:bg-sunken/60 ${off ? "text-muted" : ""}`}
              style={{ gridTemplateColumns: columns }}
            >
              <span role="cell" className="flex min-w-0 items-center gap-2">
                <span aria-hidden="true" className={`size-2 shrink-0 rounded-full ${tile.tone === "working" ? "animate-pulse" : ""}`} style={{ background: color }} />
                {tile.link ? (
                  <a {...tile.link} className={`truncate font-medium hover:text-accent hover:underline ${off ? "" : "text-ink"}`}>
                    {tile.title}
                  </a>
                ) : (
                  <span className="truncate font-medium">{tile.title}</span>
                )}
              </span>
              <span role="cell" className="flex items-baseline gap-1.5 whitespace-nowrap">
                <span className={off ? "" : "font-medium text-ink"}>{tile.state}</span>
                {tile.since && <span className="text-xs text-muted tabular-nums">{tile.since}</span>}
              </span>
              <span role="cell" className="truncate text-muted" title={tile.doing}>
                {tile.doing ?? ""}
              </span>
              <span role="cell">{tile.progress ? <Progress step={tile.progress.step} steps={tile.progress.steps} /> : null}</span>
              {charts.map((title, i) => (
                <span key={title} role="cell" className="min-w-0">
                  {tile.charts[i] && !off ? <Spark chart={tile.charts[i]!} /> : <span className="text-xs text-muted">–</span>}
                </span>
              ))}
            </div>
          );
        })}
      </div>
    </div>
  );
}

// Several charts, two side by side where there's room.
export function Charts({ charts }: { charts: ChartProps[] }) {
  return (
    <div className="grid gap-4 md:grid-cols-2">
      {charts.map((chart) => (
        <Chart key={chart.title} {...chart} />
      ))}
    </div>
  );
}

// How far a card is in its stage, as its holder says, like 2 of 5: a thin bar in the
// accent and its count.
export function Progress({ step, steps, className = "" }: { step: number; steps: number; className?: string }) {
  return (
    <div className={`flex items-center gap-1.5 ${className}`} role="img" aria-label={`step ${step} of ${steps}`}>
      <div className="h-1 min-w-8 flex-1 overflow-hidden rounded-full bg-sunken">
        <div className="h-full rounded-full bg-accent transition-[width] duration-500" style={{ width: `${(100 * step) / steps}%` }} />
      </div>
      <span className="text-[11px] text-muted tabular-nums">
        {step}/{steps}
      </span>
    </div>
  );
}
