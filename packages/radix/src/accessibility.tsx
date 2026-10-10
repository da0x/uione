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
  if (display.font === "legible") loadLegible();
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

// The settings in three groups, by what they're for.
const groups: readonly (readonly [string, readonly (keyof Display)[]])[] = [
  ["Reading", ["text", "font", "spacing"]],
  ["Color and movement", ["contrast", "motion", "transparency"]],
  ["Finding your way", ["links", "focus"]],
];

// The legible font, loaded once, so its card can show it before it's chosen.
function loadLegible() {
  if (typeof document === "undefined" || document.getElementById("uione-legible")) return;
  const link = document.createElement("link");
  link.id = "uione-legible";
  link.rel = "stylesheet";
  link.href = "https://fonts.googleapis.com/css2?family=Atkinson+Hyperlegible:ital,wght@0,400;0,700;1,400&display=swap";
  document.head.append(link);
}

// What each choice shows beside its words: a picture of what it does, where one says
// it better than the words alone. Each is hidden from assistive technology, which
// reads the words.
function preview(name: keyof Display, value: string) {
  switch (name) {
    case "text": {
      const size = { "100": "0.875rem", "125": "1.0625rem", "150": "1.3125rem", "200": "1.625rem" }[value];
      return <span aria-hidden="true" className="flex h-8 items-end font-semibold leading-none" style={{ fontSize: size }}>A</span>;
    }
    case "font":
      return (
        <span aria-hidden="true" className="text-2xl leading-none" style={value === "legible" ? { fontFamily: '"Atkinson Hyperlegible", ui-sans-serif, system-ui, sans-serif' } : undefined}>
          Aa 0O l1
        </span>
      );
    case "spacing": {
      const gap = value === "comfortable" ? 7 : 4;
      return (
        <svg aria-hidden="true" viewBox="0 0 48 24" className="h-6 w-12" fill="currentColor">
          {[0, 1, 2].map((i) => (
            <rect key={i} x="0" y={2 + i * gap} width={i === 2 ? 30 : 48} height="2.5" rx="1.25" opacity={0.55} />
          ))}
        </svg>
      );
    }
    case "links":
      return (
        <span aria-hidden="true" className="text-sm font-medium text-accent" style={{ textDecorationLine: value === "underline" ? "underline" : "none", textUnderlineOffset: "0.2em" }}>
          Read more
        </span>
      );
    case "focus":
      return (
        <span
          aria-hidden="true"
          className="h-5 w-10 rounded-control bg-surface"
          style={{ outline: `${value === "strong" ? 4 : 2}px solid var(--color-accent)`, outlineOffset: value === "strong" ? 3 : 1 }}
        />
      );
    default:
      return null;
  }
}

// One setting: its choices side by side, as a segmented control where they're words,
// and as cards where each shows what it does. They're radio buttons underneath, so
// arrows move between them and assistive technology reads them as one.
function Setting({ id, name, value, onChange }: { id: string; name: keyof Display; value: string; onChange: (value: string) => void }) {
  const options = choices[name];
  const pictured = preview(name, options[0][0]) !== null;
  const columns = { 2: "grid-cols-2", 3: "grid-cols-3", 4: "grid-cols-4" }[options.length];
  return (
    <fieldset className="flex flex-col gap-2">
      <legend className="mb-2 text-sm font-medium text-ink">{legends[name]}</legend>
      <div className={`grid ${columns} ${pictured ? "gap-2" : "gap-1 rounded-control bg-sunken p-1"}`}>
        {options.map(([option, label]) => (
          <label
            key={option}
            className={
              pictured
                ? "relative flex min-h-11 cursor-pointer flex-col items-center justify-end gap-2 rounded-control border border-line bg-surface px-2 pt-3 pb-2 text-center text-xs text-muted transition-colors hover:border-control-line hover:text-ink has-[:checked]:border-accent has-[:checked]:bg-accent-soft has-[:checked]:text-ink has-[:checked]:shadow-[inset_0_0_0_1px_var(--color-accent)] has-[:focus-visible]:outline-2 has-[:focus-visible]:outline-offset-2 has-[:focus-visible]:outline-accent"
                : "relative flex min-h-9 cursor-pointer items-center justify-center rounded-[calc(var(--radius-control)-2px)] px-2 py-1.5 text-center text-xs leading-tight text-muted transition-colors hover:text-ink has-[:checked]:bg-surface has-[:checked]:font-medium has-[:checked]:text-ink has-[:checked]:shadow-panel has-[:checked]:ring-1 has-[:checked]:ring-line has-[:focus-visible]:outline-2 has-[:focus-visible]:outline-offset-2 has-[:focus-visible]:outline-accent"
            }
          >
            <input
              type="radio"
              className="sr-only"
              name={`${id}-${name}`}
              value={option}
              checked={value === option}
              onChange={() => onChange(option)}
            />
            {preview(name, option)}
            {label}
          </label>
        ))}
      </div>
    </fieldset>
  );
}

export function DisplaySettings() {
  const [open, setOpen] = useState(false);
  const [display, setDisplay] = useState<Display>(stored);
  const id = useId();
  const change = (next: Display) => {
    setDisplay(next);
    apply(next);
    save(next);
  };
  const changed = (Object.keys(defaults) as (keyof Display)[]).filter((name) => display[name] !== defaults[name]).length;
  return (
    <>
      <button
        type="button"
        aria-label="Display settings"
        title="Display settings"
        aria-haspopup="dialog"
        aria-expanded={open}
        className="flex h-8 w-8 items-center justify-center rounded-box text-muted hover:bg-surface hover:text-ink"
        onClick={() => {
          loadLegible();
          setOpen(true);
        }}
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
        <p className="-mt-1 text-sm text-muted">Changes show at once, and stay in this browser.</p>
        {groups.map(([title, names]) => (
          <section key={title} aria-label={title} className="flex flex-col gap-4 border-t border-line pt-4">
            <h3 className="text-xs font-semibold text-muted">{title}</h3>
            {names.map((name) => (
              <Setting key={name} id={id} name={name} value={display[name]} onChange={(value) => change({ ...display, [name]: value })} />
            ))}
          </section>
        ))}
        <div className="flex items-center justify-between gap-3 border-t border-line pt-4">
          <span className="text-xs text-muted" aria-live="polite">
            {changed === 0 ? "Following your system" : `${changed} changed from your system`}
          </span>
          <button
            type="button"
            disabled={changed === 0}
            className="rounded-control border border-line px-3 py-1.5 text-sm text-ink hover:bg-sunken disabled:cursor-default disabled:text-muted disabled:hover:bg-transparent"
            onClick={() => change({ ...defaults })}
          >
            Back to my system's
          </button>
        </div>
      </Sheet>
    </>
  );
}
