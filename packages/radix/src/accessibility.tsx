// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { useId, useState } from "react";
import { Sheet } from "./sheet.js";

// Display settings beside light and dark: what a reader can ask of the page, each
// set on it as data-* and kept in their browser. What they leave as the system's
// follows their system, which theme.css and the component set's styles read for
// themselves: more contrast, less motion, less transparency. Nothing here reads or
// changes the page's content, or stands in for assistive technology; it changes only
// how the page is drawn.

export interface Display {
  contrast: "system" | "standard" | "more";
  text: "100" | "125" | "150" | "200";
  spacing: "standard" | "comfortable";
  font: "theme" | "legible";
  motion: "system" | "reduce" | "standard";
  transparency: "system" | "reduce";
  links: "standard" | "underline";
  focus: "standard" | "strong";
}

export const defaults: Display = {
  contrast: "system",
  text: "100",
  spacing: "standard",
  font: "theme",
  motion: "system",
  transparency: "system",
  links: "standard",
  focus: "standard",
};

const key = "uione-display";

export function stored(): Display {
  try {
    const value = JSON.parse(globalThis.localStorage?.getItem(key) ?? "{}") as Partial<Display>;
    const out = { ...defaults };
    for (const name of Object.keys(defaults) as (keyof Display)[]) {
      const given = value[name];
      if (typeof given === "string" && choices[name].some(([v]) => v === given)) (out as Record<string, string>)[name] = given;
    }
    return out;
  } catch {
    return { ...defaults }; // storage can be off, as in a private window
  }
}

// On the page: each setting that isn't the system's, or the page's as it is, as
// data-contrast and the rest; a legible font, loaded the first time it's asked for.
export function apply(display: Display) {
  if (typeof document === "undefined") return;
  const root = document.documentElement;
  for (const name of Object.keys(defaults) as (keyof Display)[]) {
    const value = display[name];
    if (value === defaults[name]) delete root.dataset[name];
    else root.dataset[name] = value;
  }
  if (display.font === "legible" && !document.getElementById("uione-legible")) {
    const link = document.createElement("link");
    link.id = "uione-legible";
    link.rel = "stylesheet";
    link.href = "https://fonts.googleapis.com/css2?family=Atkinson+Hyperlegible:ital,wght@0,400;0,700;1,400&display=swap";
    document.head.append(link);
  }
}

function save(display: Display) {
  try {
    globalThis.localStorage?.setItem(key, JSON.stringify(display));
  } catch {
    // the settings last until the page is left
  }
}

const choices: { [K in keyof Display]: readonly (readonly [Display[K], string])[] } = {
  contrast: [["system", "As my system"], ["standard", "Standard"], ["more", "More"]],
  text: [["100", "100%"], ["125", "125%"], ["150", "150%"], ["200", "200%"]],
  spacing: [["standard", "Standard"], ["comfortable", "Comfortable"]],
  font: [["theme", "The site's"], ["legible", "Atkinson Hyperlegible"]],
  motion: [["system", "As my system"], ["reduce", "Less"], ["standard", "As designed"]],
  transparency: [["system", "As my system"], ["reduce", "Solid"]],
  links: [["standard", "As designed"], ["underline", "Underline every link"]],
  focus: [["standard", "Standard"], ["strong", "Thick and clear"]],
};

// Settings chosen before are in place before anything is drawn.
apply(stored());

const legends: Record<keyof Display, string> = {
  contrast: "Contrast",
  text: "Text size",
  spacing: "Spacing",
  font: "Font",
  motion: "Motion",
  transparency: "Transparency",
  links: "Links",
  focus: "Keyboard focus",
};

export function DisplaySettings() {
  const [open, setOpen] = useState(false);
  const [display, setDisplay] = useState<Display>(stored);
  const id = useId();
  const change = (next: Display) => {
    setDisplay(next);
    apply(next);
    save(next);
  };
  const changed = (Object.keys(defaults) as (keyof Display)[]).some((name) => display[name] !== defaults[name]);
  return (
    <>
      <button
        type="button"
        aria-label="Display settings"
        title="Display settings"
        aria-haspopup="dialog"
        aria-expanded={open}
        className="flex h-8 w-8 items-center justify-center rounded-box text-muted hover:bg-surface hover:text-ink"
        onClick={() => setOpen(true)}
      >
        {/* A person with open arms, in a circle: the sign for accessibility. */}
        <svg viewBox="0 0 24 24" width="18" height="18" style={{ width: "1.125rem", height: "1.125rem" }} fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
          <circle cx="12" cy="12" r="10" />
          <circle cx="12" cy="7.5" r="1.2" fill="currentColor" stroke="none" />
          <path d="M7 10.5l5 1 5-1M12 11.5v3.5M9.5 19l2.5-4 2.5 4" />
        </svg>
      </button>
      {/* Opened as everything over the page is: a dialog, the whole screen on a phone. */}
      <Sheet open={open} title="Display settings" onClose={() => setOpen(false)}>
        <div className="flex flex-col gap-4 text-sm">
          {(Object.keys(choices) as (keyof Display)[]).map((name) => (
            <fieldset key={name} className="flex flex-col gap-1.5">
              <legend className="mb-1 font-medium">{legends[name]}</legend>
              <div className="flex flex-wrap gap-x-4 gap-y-1.5">
                {choices[name].map(([value, label]) => (
                  <label key={value} className="flex min-h-8 items-center gap-1.5">
                    <input
                      type="radio"
                      name={`${id}-${name}`}
                      value={value}
                      checked={display[name] === value}
                      onChange={() => change({ ...display, [name]: value })}
                    />
                    {label}
                  </label>
                ))}
              </div>
            </fieldset>
          ))}
          <button
            type="button"
            disabled={!changed}
            className="self-start rounded-control border border-line px-3 py-1.5 text-ink hover:bg-sunken disabled:text-muted"
            onClick={() => change({ ...defaults })}
          >
            Back to my system's
          </button>
        </div>
      </Sheet>
    </>
  );
}
