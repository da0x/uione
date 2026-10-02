// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import type { GeneratedFile } from "@uione/compiler";

// The generated lines that came from a line of a .one file, by generated file, each
// numbered from 1. Files with none are left out.
export function fromLine(files: GeneratedFile[], path: string, line: number): Map<string, number[]> {
  const found = new Map<string, number[]>();
  for (const file of files) {
    const lines: number[] = [];
    file.sources.forEach((source, i) => {
      if (source && source !== "same" && source.path === path && source.line === line) lines.push(i + 1);
    });
    if (lines.length > 0) found.set(file.path, lines);
  }
  return found;
}
