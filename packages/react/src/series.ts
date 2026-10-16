// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What charts and monitors read: a run of numbers from a record, newest last; each
// value as it reads, in its unit; the top of a chart's scale; and what a status word
// says, as a monitor's dot.

export type ChartUnit = "" | "percent" | "bytes" | "rate";

// A field of a record by its path: a joined field is kept whole, like
// "assignee.name", and anything else is a name within a field of data, like
// "stats.cpu".
export function valueAt(record: unknown, path: string): unknown {
  if (record === null || typeof record !== "object") return undefined;
  const whole = (record as Record<string, unknown>)[path];
  if (whole !== undefined) return whole;
  let at: unknown = record;
  for (const part of path.split(".")) {
    if (at === null || typeof at !== "object") return undefined;
    at = (at as Record<string, unknown>)[part];
  }
  return at;
}

// Its numbers, newest last; what isn't one is left out.
export function seriesOf(value: unknown): number[] {
  if (!Array.isArray(value)) return typeof value === "number" && Number.isFinite(value) ? [value] : [];
  return value.filter((n): n is number => typeof n === "number" && Number.isFinite(n));
}

const binary = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"];

// A value as it reads: 42%, 1.5 GiB, 3.2 MiB/s, or 12.5.
export function readValue(value: number, unit: ChartUnit): string {
  if (unit === "percent") return `${Math.round(value)}%`;
  if (unit === "bytes" || unit === "rate") {
    let at = 0;
    let n = Math.max(0, value);
    while (n >= 1024 && at < binary.length - 1) {
      n /= 1024;
      at += 1;
    }
    const shown = at === 0 || n >= 100 ? Math.round(n).toString() : n.toFixed(1).replace(/\.0$/, "");
    return `${shown} ${binary[at]}${unit === "rate" ? "/s" : ""}`;
  }
  return Number.isInteger(value) ? value.toString() : value.toFixed(1);
}

// The top of a chart's scale: 100 for a percent, and otherwise the least round number
// at or above its highest value, 1, 2 or 5 of a power of ten, or of 1024 for bytes.
export function scaleTop(values: number[], unit: ChartUnit): number {
  if (unit === "percent") return 100;
  const highest = Math.max(0, ...values);
  if (highest <= 0) return 1;
  const base = unit === "bytes" || unit === "rate" ? 1024 : 10;
  let power = 1;
  while (power * base <= highest) power *= base;
  for (const step of base === 1024 ? [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024] : [1, 2, 5, 10]) {
    if (power * step >= highest) return power * step;
  }
  return power * base;
}

export type Tone = "inactive" | "waiting" | "working" | "success" | "failed";

// What a status word says, as a dot: working, waiting (on standby), fine and ready
// (idle), not there (offline), or stopped by something wrong.
export function toneOfState(word: string): Tone {
  switch (word.toLowerCase()) {
    case "working":
    case "busy":
    case "running":
      return "working";
    case "standby":
    case "waiting":
    case "paused":
      return "waiting";
    case "idle":
    case "ready":
    case "success":
    case "done":
    case "live":
    case "online":
      return "success";
    case "stopped":
    case "failed":
    case "error":
    case "crashed":
      return "failed";
    default:
      return "inactive";
  }
}
