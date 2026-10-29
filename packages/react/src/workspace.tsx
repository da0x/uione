// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace, laid out as a drawing app is: a toolbar of named groups whose buttons
// are tools, keys that run what's on a line, toggles a screen reads by name, undo and
// redo, full screen, and the messages saying what was just done. The desktop's
// runtime does the same, from the same lines.

import { Children, createContext, useCallback, useContext, useEffect, useRef, useState, useSyncExternalStore } from "react";
import type { ReactNode } from "react";
import { useRunner, useView } from "./data.js";
import type { ViewState } from "./data.js";
import { useSaying } from "./said.js";
import type { Said } from "./said.js";
import { useUI } from "./ui.js";

// A toolbar's group its buttons are in, which draws them as tools.
const InGroup = createContext<{ small: boolean } | undefined>(undefined);
// Buttons over a collage's picked layer, small tools too.
export const OverLayer = createContext(false);
// What the layer buttons are over is: a picture, words or a group, so only those that
// fit it are there, as crop over a picture.
export const OverKind = createContext<{ picture: boolean; words: boolean; group: boolean } | undefined>(undefined);

// How a button in a toolbar, or over a picked layer, is drawn, if it's in one.
export function useTool(): "large" | "small" | undefined {
  const group = useContext(InGroup);
  const over = useContext(OverLayer);
  return over ? "small" : group ? (group.small ? "small" : "large") : undefined;
}

// Whether what's in a toolbar doesn't apply now: drawn, so the toolbar keeps its shape,
// but it can't be pressed, and its keys do nothing.
const Off = createContext(false);

// A toolbar's group or tool that applies only while `when` holds, and while what it's
// in does.
export function Applies({ when, children }: { when: boolean; children?: ReactNode }) {
  const off = useContext(Off);
  return <Off.Provider value={off || !when}>{children}</Off.Provider>;
}

// Whether what's drawn is in a toolbar's group.
export function useInGroup(): boolean {
  return Boolean(useContext(InGroup));
}

// Whether a tool is off: in a toolbar's group, and not applying now.
export function useOff(): boolean {
  const group = useContext(InGroup);
  const off = useContext(Off);
  return Boolean(group) && off;
}

// Tools side by side with the same words, like Bold that sets it and Bold that takes it
// away: one, the first whose when holds, or the first, off.
export function Either({ children }: { children?: ReactNode }) {
  const ways = Children.toArray(children) as { props: { when?: boolean; allowed?: boolean } }[];
  const holds = ways.find((w) => w.props.when !== false && w.props.allowed !== false);
  return <>{holds ?? ways[0]}</>;
}

export function Group({ name, small = false, across, atEnd = false, unsaid = false, children }: { name: string; small?: boolean; across?: number; atEnd?: boolean; unsaid?: boolean; children?: ReactNode }) {
  const ui = useUI();
  return (
    <InGroup.Provider value={{ small }}>
      <ui.Group name={name} small={small} across={across} atEnd={atEnd} unsaid={unsaid}>
        {children}
      </ui.Group>
    </InGroup.Provider>
  );
}

// options "Page" icon page { ... }: a tool that opens its lines, each a choice drawn with
// what it's for, closing once one's chosen. The same as the desktop's.
// What runs a button over a collage's picked layer, when the collage runs it its own
// way, like deleting a group with all it holds: true when it has.
export const OverRun = createContext<((name: string, sent: Record<string, unknown>) => boolean) | undefined>(undefined);
export function useOverRun() {
  return useContext(OverRun);
}

const InOptions = createContext<(() => void) | undefined>(undefined);
export function useInOptions(): (() => void) | undefined {
  return useContext(InOptions);
}
export function Options({ label, icon, children }: { label: string; icon?: string; children?: ReactNode }) {
  const ui = useUI();
  const tool = useTool();
  const [open, setOpen] = useState(false);
  const close = useCallback(() => setOpen(false), []);
  return (
    <ui.Options label={label} icon={icon} tool={tool} open={open} onToggle={() => setOpen((was) => !was)} onClose={close}>
      <InGroup.Provider value={undefined}>
        <InOptions.Provider value={close}>{children}</InOptions.Provider>
      </InGroup.Provider>
    </ui.Options>
  );
}

// A workspace's tools, a column down its side: each a small tool, its words its hint.
export function Tools({ children }: { children?: ReactNode }) {
  return <InGroup.Provider value={{ small: true }}>{children}</InGroup.Provider>;
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
export function typing(target: EventTarget | null): boolean {
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
  const off = useOff();
  const flip = useCallback(() => setToggled(name, !stored(name)), [name]);
  useKeys(keys, flip, !off);
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} pressed={on && !off} disabled={off} hint={hintOf(label, keys)} onClick={flip}>
      {label}
    </ui.Button>
  );
}

// toggle tool::pen: one of an enum's choices, pressed while it's the one the page has;
// the screen's toggles over one enum are one choice, its first until another's chosen,
// for as long as the page is open, as a pick is.
const chosenNow = new Map<string, string>();
const choosing = new Set<() => void>();
export function useChosen(name: string, first: string): string {
  return useSyncExternalStore(
    (changed) => {
      choosing.add(changed);
      return () => choosing.delete(changed);
    },
    () => chosenNow.get(name) ?? first,
    () => first,
  );
}
export function setChosen(name: string, choice: string) {
  chosenNow.set(name, choice);
  choosing.forEach((changed) => changed());
}

// What a command's buttons last gave, kept for what's drawn next, as a drawing program
// draws in the colors last chosen: each value as the last one to give it gave it. The
// same as the desktop's.
const givenNow = new Map<string, Record<string, unknown>>();
const giving = new Set<() => void>();
const noneGiven: Record<string, unknown> = {};
export function lastGiven(command: string): Record<string, unknown> {
  return givenNow.get(command) ?? noneGiven;
}
export function useLastGiven(command: string): Record<string, unknown> {
  return useSyncExternalStore(
    (changed) => {
      giving.add(changed);
      return () => giving.delete(changed);
    },
    () => lastGiven(command),
    () => noneGiven,
  );
}
export function rememberGiven(command: string, given: Record<string, unknown>) {
  const kept = Object.fromEntries(Object.entries(given).filter(([name]) => name !== "id" && !name.startsWith("one:")));
  givenNow.set(command, { ...lastGiven(command), ...kept });
  giving.forEach((changed) => changed());
}

// What a collage's style applies to, by its pick's name: each of several chosen, or all
// a picked group holds; none while one alone is picked. Said by the collage. The same
// as the desktop's.
const styledNow = new Map<string, string[]>();
const styling = new Set<() => void>();
const noneStyled: string[] = [];
export function useStyled(pick: string): string[] {
  return useSyncExternalStore(
    (changed) => {
      styling.add(changed);
      return () => styling.delete(changed);
    },
    () => styledNow.get(pick) ?? noneStyled,
    () => noneStyled,
  );
}
export function styleThese(pick: string, ids: string[]) {
  const was = styledNow.get(pick) ?? noneStyled;
  if (was.length === ids.length && was.every((id, i) => id === ids[i])) return;
  styledNow.set(pick, ids);
  styling.forEach((changed) => changed());
}

export function Choose({ name, choice, first, label, icon, keys }: { name: string; choice: string; first: string; label: string; icon?: string; keys?: string[] }) {
  const ui = useUI();
  const tool = useTool() ?? "small";
  const on = useChosen(name, first) === choice;
  const off = useOff();
  const pick = useCallback(() => setChosen(name, choice), [name, choice]);
  useKeys(keys, pick, !off);
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} pressed={on && !off} disabled={off} hint={hintOf(label, keys)} onClick={pick}>
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
  // Off while there's nothing to take back, or again, as the source's history says; a
  // source that doesn't say leaves it on.
  const kept = useView("one::history");
  const none = kept.status === "live" && !(kept.data as Record<string, unknown> | undefined)?.[redo ? "redo" : "undo"];
  const off = useOff() || none;
  useKeys(keys, () => void take(), !off);
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} hint={hintOf(words, keys)} disabled={off || runner.busy(command)} onClick={() => void take()}>
      {words}
    </ui.Button>
  );
}

// full screen

function fullNow(): boolean {
  return typeof document !== "undefined" && Boolean(document.fullscreenElement);
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
  const off = useOff();
  useKeys(keys, flip, !off);
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} pressed={full && !off} disabled={off} hint={hintOf(label, keys)} onClick={flip}>
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

// add pictures "Photo": the device's own file chooser, opened by a press, showing only
// pictures; on a phone, its photos and its camera too. The input is in the page while
// it's open, as some phones only tell a chosen file to one that is.
export function choosePictures(chosen: (files: File[]) => void): void {
  const input = document.createElement("input");
  input.type = "file";
  input.accept = "image/*";
  input.multiple = true;
  input.hidden = true;
  input.addEventListener("change", () => {
    const files = [...(input.files ?? [])];
    input.remove();
    if (files.length > 0) chosen(files);
  });
  input.addEventListener("cancel", () => input.remove());
  document.body.appendChild(input);
  input.click();
}

// add folder "Add folder": a folder chosen in the device's own chooser, and every picture
// in it and in the folders in it, but hidden ones, which the browser lists whole as
// handles, read one at a time as they're taken. What isn't a picture, by what's in it, is
// passed over, not said to be wrong. The same as the desktop's.
const folderOf = new WeakMap<object, string>();
export function chooseFolder(chosen: (files: File[]) => void): void {
  const input = document.createElement("input");
  input.type = "file";
  input.setAttribute("webkitdirectory", "");
  input.hidden = true;
  input.addEventListener("change", () => {
    const files = [...(input.files ?? [])].filter((file) => !(file.webkitRelativePath || file.name).split("/").some((part) => part.startsWith(".")));
    input.remove();
    const name = (input.files?.[0]?.webkitRelativePath ?? "").split("/")[0] ?? "";
    for (const file of files) folderOf.set(file, name);
    chosen(files);
  });
  input.addEventListener("cancel", () => input.remove());
  document.body.appendChild(input);
  input.click();
}
// What a taker says of a file that isn't a picture: passed over, when it's from a folder,
// or why it couldn't be taken.
export const passedOver = Symbol("passed over");
export function notAPicture(file: File): string | typeof passedOver {
  return folderOf.has(file) ? passedOver : `${file.name} isn't a picture this can read.`;
}

// What a screen's paste and add pictures buttons hand what they go into, a paper or
// cards, by what names it: the pictures chosen, or a paste, which reads the clipboard
// as the browser lets it be read.
const handed = new Map<string, File[] | "paste">();
const handedListeners = new Set<() => void>();
export function handPictures(name: string, files: File[] | "paste"): void {
  handed.set(name, files);
  handedListeners.forEach((changed) => changed());
}

// Pictures taken one at a time, the page given back to the browser before each, so it
// draws and can be worked in while many come in; the workspace saying how far they've
// got while there are several, and, once they're in, what couldn't be taken, as
// `take` says why, and how many were already in, when `take` says one was. The same
// as the desktop's.
export const alreadyIn = Symbol("already in");
export async function takeInTurn<T>(given: T[], say: (said: Said | undefined) => void, take: (item: T, index: number) => Promise<string | typeof alreadyIn | typeof passedOver | void>): Promise<void> {
  let wrong: string | undefined;
  let already = 0;
  let failed = 0;
  let over = 0;
  // A folder's, said by its name.
  const folder = given.length > 0 && typeof given[0] === "object" && given[0] !== null ? folderOf.get(given[0]) : undefined;
  for (const [i, item] of given.entries()) {
    await givenBack();
    if (folder !== undefined) say({ text: `Adding pictures from ${folder}, ${i + 1 - over} so far…` });
    else if (given.length > 1) say({ text: `Adding ${i + 1} of ${given.length} pictures…` });
    const taken = await take(item, i);
    if (taken === alreadyIn) already += 1;
    else if (taken === passedOver) over += 1;
    else if (taken) (wrong = taken), (failed += 1);
  }
  const added = given.length - already - failed - over;
  const said =
    already > 0 ? alreadySaid(added, already) : folder === undefined ? "" : added === 0 ? `There are no pictures in ${folder}.` : `Added ${added} picture${added === 1 ? "" : "s"}.`;
  if (wrong) say({ text: said ? `${said} ${wrong}` : wrong, problem: true });
  else if (said) say({ text: said });
  else if (given.length > 1) say(undefined);
}

// What's said once they're in, when some were already: "Added 31 pictures, 9 were
// already in." The same as the desktop's.
export function alreadySaid(added: number, already: number): string {
  if (added <= 0) return already === 1 ? "That picture was already in." : `All ${already} pictures were already in.`;
  return `Added ${added} picture${added === 1 ? "" : "s"}, ${already} ${already === 1 ? "was" : "were"} already in.`;
}

// The page given back to the browser for a moment, to draw and take what's pressed.
export const givenBack = (): Promise<void> => new Promise((done) => setTimeout(done, 0));

// And what it's handed taken, by what names it.
export function usePicturesHanded(name: string | undefined, take: (files: File[]) => void | Promise<void>): void {
  const { say } = useSaying();
  const asked = useSyncExternalStore(
    (changed) => {
      handedListeners.add(changed);
      return () => handedListeners.delete(changed);
    },
    () => (name ? handed.get(name) : undefined),
    () => undefined,
  );
  useEffect(() => {
    if (!asked || !name) return;
    handed.delete(name);
    if (asked !== "paste") return void take(asked);
    void (async () => {
      try {
        const items = await navigator.clipboard.read();
        const pictures: File[] = [];
        for (const item of items) {
          const kind = item.types.find((t) => t.startsWith("image/"));
          if (kind) pictures.push(new File([await item.getType(kind)], `Pasted.${kind.slice(6)}`, { type: kind }));
        }
        if (pictures.length === 0) return say({ text: "There's no picture on the clipboard to paste." });
        await take(pictures);
      } catch {
        say({ text: "This browser didn't let the page read the clipboard. Press Ctrl+V instead.", problem: true });
      }
    })();
  }, [asked]);
}
