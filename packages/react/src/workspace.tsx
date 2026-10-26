// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace, laid out as a drawing app is: a toolbar of named groups whose buttons
// are tools, keys that run what's on a line, toggles a screen reads by name, undo and
// redo, full screen, and the messages saying what was just done. The desktop's
// runtime does the same, from the same lines.

import { createContext, useCallback, useContext, useEffect, useRef, useState, useSyncExternalStore } from "react";
import type { ReactNode } from "react";
import { useRunner } from "./data.js";
import type { ViewState } from "./data.js";
import { useSaying } from "./said.js";
import { useUI } from "./ui.js";

// A toolbar's group its buttons are in, which draws them as tools.
const InGroup = createContext<{ small: boolean } | undefined>(undefined);
// Buttons over a collage's picked layer, small tools too.
export const OverLayer = createContext(false);

// How a button in a toolbar, or over a picked layer, is drawn, if it's in one.
export function useTool(): "large" | "small" | undefined {
  const group = useContext(InGroup);
  const over = useContext(OverLayer);
  return over ? "small" : group ? (group.small ? "small" : "large") : undefined;
}

export function Group({ name, small = false, atEnd = false, children }: { name: string; small?: boolean; atEnd?: boolean; children?: ReactNode }) {
  const ui = useUI();
  return (
    <InGroup.Provider value={{ small }}>
      <ui.Group name={name} small={small} atEnd={atEnd}>
        {children}
      </ui.Group>
    </InGroup.Provider>
  );
}

// keys

// ctrl+shift+z as people read it, Ctrl+Shift+Z.
export function keyShown(chord: string): string {
  const names: Record<string, string> = { page_up: "Page Up", page_down: "Page Down", minus: "-", equal: "=", plus: "+", comma: ",", period: ".", slash: "/" };
  return chord
    .split("+")
    .map((part) => names[part] ?? (part.length === 1 ? part.toUpperCase() : part[0]!.toUpperCase() + part.slice(1)))
    .join("+");
}

// What a line says when it's pointed at: its words, and its first key.
export function hintOf(words: string, keys: readonly string[] | undefined): string {
  return keys && keys.length > 0 ? `${words} (${keyShown(keys[0]!)})` : words;
}

// Whether a key pressed is a chord, like ctrl+z: the same modifiers, Ctrl or the
// Mac's Command, and the key, by its name.
export function pressed(e: KeyboardEvent, chord: string): boolean {
  const parts = chord.split("+");
  const key = parts.pop()!;
  const ctrl = parts.includes("ctrl"), shift = parts.includes("shift"), alt = parts.includes("alt");
  if ((e.ctrlKey || e.metaKey) !== ctrl || e.altKey !== alt) return false;
  const names: Record<string, string> = {
    escape: "Escape", enter: "Enter", tab: "Tab", space: " ", backspace: "Backspace", delete: "Delete", insert: "Insert", home: "Home", end: "End",
    page_up: "PageUp", page_down: "PageDown", left: "ArrowLeft", right: "ArrowRight", up: "ArrowUp", down: "ArrowDown",
    minus: "-", equal: "=", plus: "+", comma: ",", period: ".", slash: "/",
  };
  const wanted = names[key] ?? (/^f\d+$/.test(key) ? key.toUpperCase() : key);
  // A shifted key that's a character, like + or Z, says it's shifted by what it is.
  const typed = e.key.length === 1 ? e.key.toLowerCase() : e.key;
  if (typed !== wanted.toLowerCase() && e.key !== wanted) return false;
  return e.key.length === 1 && !/[a-z0-9 ]/i.test(e.key) ? true : e.shiftKey === shift;
}

// Whether what has the keyboard is somewhere to type, which keeps its keys.
function typing(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  if (!el || typeof el.tagName !== "string") return false;
  return el.isContentEditable || el.tagName === "INPUT" || el.tagName === "TEXTAREA" || el.tagName === "SELECT";
}

// Runs `run` when one of `keys` is pressed anywhere on the page, but where the
// person is typing; the first line shown with a key takes it.
export function useKeys(keys: readonly string[] | undefined, run: () => void, enabled = true): void {
  const latest = useRef(run);
  latest.current = run;
  const joined = keys?.join(" ") ?? "";
  useEffect(() => {
    if (!joined || !enabled) return;
    const chords = joined.split(" ");
    const down = (e: KeyboardEvent) => {
      if (e.defaultPrevented || typing(e.target)) return;
      if (!chords.some((chord) => pressed(e, chord))) return;
      e.preventDefault();
      latest.current();
    };
    window.addEventListener("keydown", down);
    return () => window.removeEventListener("keydown", down);
  }, [joined, enabled]);
}

// toggles

const toggled = new Map<string, boolean>();
const watching = new Set<() => void>();

function stored(name: string): boolean {
  if (!toggled.has(name)) {
    let on = false;
    try {
      on = window.localStorage.getItem(`uione toggle ${name}`) === "on";
    } catch {
      // Kept for as long as the page is open, then.
    }
    toggled.set(name, on);
  }
  return toggled.get(name)!;
}

export function setToggled(name: string, on: boolean): void {
  toggled.set(name, on);
  try {
    window.localStorage.setItem(`uione toggle ${name}`, on ? "on" : "off");
  } catch {
    // Kept for as long as the page is open, then.
  }
  for (const changed of [...watching]) changed();
}

// Whether a toggle, named by its screen's address and its name, is on, as the person
// left it on this device (decision 0017).
export function useToggled(name: string): boolean {
  return useSyncExternalStore(
    (changed) => {
      watching.add(changed);
      return () => watching.delete(changed);
    },
    () => stored(name),
    () => false,
  );
}

export function Toggle({ name, label, icon, keys }: { name: string; label: string; icon?: string; keys?: string[] }) {
  const ui = useUI();
  const tool = useTool() ?? "small";
  const on = useToggled(name);
  const flip = useCallback(() => setToggled(name, !stored(name)), [name]);
  useKeys(keys, flip);
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} pressed={on} hint={hintOf(label, keys)} onClick={flip}>
      {label}
    </ui.Button>
  );
}

// undo and redo

// The command a step was, as words: studio::layer::place is "place".
function stepWords(command: string | undefined): string {
  const last = command?.split("::").pop() ?? "";
  return last ? last.replaceAll("_", " ") : "the last change";
}

export function History({ redo = false, label, icon, keys }: { redo?: boolean; label?: string; icon?: string; keys?: string[] }) {
  const ui = useUI();
  const tool = useTool() ?? "small";
  const runner = useRunner();
  const { say } = useSaying();
  const words = label ?? (redo ? "Redo" : "Undo");
  const command = redo ? "one::redo" : "one::undo";
  const take = useCallback(async () => {
    const step = await runner.reply(command);
    if (step) say({ text: `${redo ? "Redid" : "Undid"} ${stepWords(step.id)}.` });
  }, [runner, command, redo, say]);
  useKeys(keys, () => void take());
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} hint={hintOf(words, keys)} disabled={runner.busy(command)} onClick={() => void take()}>
      {words}
    </ui.Button>
  );
}

// full screen

function fullNow(): boolean {
  return typeof document !== "undefined" && document.fullscreenElement !== null;
}

export function FullScreen({ label = "Full screen", icon, keys }: { label?: string; icon?: string; keys?: string[] }) {
  const ui = useUI();
  const tool = useTool() ?? "small";
  const { say } = useSaying();
  const full = useSyncExternalStore(
    (changed) => {
      document.addEventListener("fullscreenchange", changed);
      return () => document.removeEventListener("fullscreenchange", changed);
    },
    fullNow,
    () => false,
  );
  const flip = useCallback(() => {
    if (fullNow()) void document.exitFullscreen();
    else document.documentElement.requestFullscreen?.().catch(() => say({ text: "This browser didn't let the page fill the screen.", problem: true }));
  }, [say]);
  useKeys(keys, flip);
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} pressed={full} hint={hintOf(label, keys)} onClick={flip}>
      {label}
    </ui.Button>
  );
}

// picks

const picks = new Map<string, string>();
const watchingPicks = new Set<() => void>();

// Picks a row of a list by its id, named by its screen's address and the pick's
// name (decision 0017); nothing, to pick none.
export function setPicked(name: string, id: string | undefined): void {
  if (id === undefined) picks.delete(name);
  else picks.set(name, id);
  for (const changed of [...watchingPicks]) changed();
}

// The row picked of a view's list, as the view has it now: none until one's picked,
// or when what was picked is gone.
export function usePicked(name: string, view: ViewState, list: string): Record<string, unknown> | null {
  const id = useSyncExternalStore(
    (changed) => {
      watchingPicks.add(changed);
      return () => watchingPicks.delete(changed);
    },
    () => picks.get(name),
    () => undefined,
  );
  if (id === undefined || view.status !== "live") return null;
  const rows = (view.data?.[list] as Record<string, unknown>[] | undefined) ?? [];
  return rows.find((row) => row["id"] === id) ?? null;
}

export function usePickedId(name: string): string | undefined {
  return useSyncExternalStore(
    (changed) => {
      watchingPicks.add(changed);
      return () => watchingPicks.delete(changed);
    },
    () => picks.get(name),
    () => undefined,
  );
}

// messages

export function Messages() {
  const ui = useUI();
  const { said } = useSaying();
  return <ui.Messages text={said?.text ?? ""} problem={said?.problem} />;
}

// What a page says, provided by the app around each screen; a new screen starts
// with nothing said.
export function useSayingState() {
  const [said, say] = useState<{ text: string; problem?: boolean } | undefined>();
  return { said, say };
}
