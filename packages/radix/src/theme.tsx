// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { useEffect, useState } from "react";

// Light or dark. The page follows the system until the reader picks one with the
// toggle, and then keeps their pick, in this browser only.

export type Theme = "light" | "dark";

const key = "uione-theme";

function stored(): Theme | undefined {
  try {
    const value = globalThis.localStorage?.getItem(key);
    return value === "light" || value === "dark" ? value : undefined;
  } catch {
    return undefined; // storage can be off, as in a private window
  }
}

function system(): Theme {
  return globalThis.matchMedia?.("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

function apply(theme: Theme | undefined) {
  if (typeof document === "undefined") return;
  if (theme) document.documentElement.dataset.theme = theme;
  else delete document.documentElement.dataset.theme;
}

// A pick made before is in place before anything is drawn, so the page never
// shows the other theme first.
apply(stored());

export function ThemeToggle() {
  const [theme, setTheme] = useState<Theme>(() => stored() ?? system());
  // Without a pick, the page keeps following the system as it changes.
  useEffect(() => {
    const media = globalThis.matchMedia?.("(prefers-color-scheme: dark)");
    if (!media) return;
    const follow = () => {
      if (!stored()) setTheme(system());
    };
    media.addEventListener("change", follow);
    return () => media.removeEventListener("change", follow);
  }, []);
  const next: Theme = theme === "dark" ? "light" : "dark";
  return (
    <button
      type="button"
      aria-label={`Switch to ${next} mode`}
      title={`Switch to ${next} mode`}
      className="flex h-8 w-8 items-center justify-center rounded-box text-muted hover:bg-surface hover:text-ink"
      onClick={() => {
        try {
          globalThis.localStorage?.setItem(key, next);
        } catch {
          // the pick lasts until the page is left
        }
        apply(next);
        setTheme(next);
      }}
    >
      {theme === "dark" ? (
        <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" aria-hidden="true">
          <circle cx="12" cy="12" r="4" />
          <path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4" />
        </svg>
      ) : (
        <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
          <path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z" />
        </svg>
      )}
    </button>
  );
}
