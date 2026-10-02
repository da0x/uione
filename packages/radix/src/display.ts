// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// How code is shown: how wide a tab is, and how names look. Both are the reader's
// choice (decisions 0007 and 0011), the same for every piece of code on the page,
// and remembered by the browser.

import { useSyncExternalStore } from "react";

// Names as written in the file, which is snake_case, or in another style.
export type NameStyle = "default" | "camelCase" | "PascalCase" | "kebab-case";

export const nameStyles: NameStyle[] = ["default", "camelCase", "PascalCase", "kebab-case"];
export const tabWidths = [2, 4, 6, 8];

export interface CodeDisplay {
  tabWidth: number;
  names: NameStyle;
}

const standard: CodeDisplay = { tabWidth: 4, names: "default" };
const key = "uione.code-display";

function stored(): CodeDisplay {
  try {
    const saved = JSON.parse(globalThis.localStorage?.getItem(key) ?? "null") as Partial<CodeDisplay> | null;
    return {
      tabWidth: tabWidths.includes(saved?.tabWidth ?? 0) ? saved!.tabWidth! : standard.tabWidth,
      names: nameStyles.includes(saved?.names as NameStyle) ? (saved!.names as NameStyle) : standard.names,
    };
  } catch {
    return standard;
  }
}

let current: CodeDisplay | undefined;
const listeners = new Set<() => void>();

function read(): CodeDisplay {
  current ??= stored();
  return current;
}

export function setCodeDisplay(change: Partial<CodeDisplay>) {
  current = { ...read(), ...change };
  try {
    globalThis.localStorage?.setItem(key, JSON.stringify(current));
  } catch {
    // A browser that won't store it still shows the choice until the page is left.
  }
  for (const listener of listeners) listener();
}

// The reader's choices, kept in step across every code box on the page.
export function useCodeDisplay(): CodeDisplay {
  return useSyncExternalStore(
    (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    read,
    () => standard,
  );
}

// A snake_case name in another style: due_at as dueAt, DueAt or due-at.
export function restyle(name: string, style: NameStyle): string {
  if (style === "default") return name;
  return name.replace(/\b[a-z][a-z0-9]*(?:_[a-z0-9]+)*\b/g, (word) => {
    const parts = word.split("_");
    if (style === "kebab-case") return parts.join("-");
    const capital = (p: string) => p.charAt(0).toUpperCase() + p.slice(1);
    const joined = parts.map((p, i) => (i === 0 && style === "camelCase" ? p : capital(p))).join("");
    return joined;
  });
}
