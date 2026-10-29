// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A paper: a list's rows as one upright sheet, written on in place (decision 0022).
// Each row is a block, in the order its position says, set as its kind says and aligned
// as its align says; each is typed into where it is. What's typed is sent once typing
// pauses or the caret leaves the block; Enter splits a block, the words after the caret
// made a block of their own, and Backspace at a block's start joins it to the one before,
// each one step undo takes back. The block the caret is in is the screen's pick, which
// the toolbar acts on. The desktop's paper keeps the same rules.

import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import type { CSSProperties, KeyboardEvent, MouseEvent } from "react";
import { useFiles, useRunner } from "./data.js";
import type { ViewState } from "./data.js";
import { useSaying } from "./said.js";
import { pictureSize } from "./vectors.js";
import { choosePictures, hintOf, setPicked, setToggled, useKeys, useOff, useTool } from "./workspace.js";
import { useUI } from "./ui.js";
import { kindOf } from "./paper/index.js";
import type { Parts } from "./paper/index.js";

type Row = Record<string, unknown>;
interface Block {
  id: string;
  kind: string;
  align: string;
  words: string;
  position: number;
  parts?: Parts;
}

const num = (v: unknown) => (typeof v === "number" && Number.isFinite(v) ? v : Number(v) || 0);
const str = (v: unknown) => (typeof v === "string" ? v : "");

// A letter page's width, at a screen's 96 to the inch, and an inch of margin; the gap
// between one page and the next.
const sheetWidth = 816;
const letterShape = 11 / 8.5;
const pageGap = 28;
// On a window narrower than a small screen's, as a phone's, a letter page shown whole
// would set its words too small to read, so it's one page the room's width instead,
// with a narrow margin.
const narrowMargin = 18;
const narrowQuery = "(max-width: 639.98px)";
function useNarrow(): boolean {
  return useSyncExternalStore(
    (changed) => {
      const query = window.matchMedia?.(narrowQuery);
      query?.addEventListener("change", changed);
      return () => query?.removeEventListener("change", changed);
    },
    () => window.matchMedia?.(narrowQuery).matches ?? false,
    () => false,
  );
}

// Room above and below each kind, as a book sets them; the same as the desktop's, and
// added, rather than the larger taken, as a page's margins would be.
const roomOf = (kind: string): [number, number] => kindOf(kind).room;
const leads = (kind: string) => kindOf(kind).leads === true;

// Where each block goes, page by page, from how tall each is: a block that doesn't fit
// what's left of a page starts the next, and a title, subtitle or heading goes with
// what it heads when that wouldn't fit after it. Each block's top, and how many pages.
export function pagesOf(blocks: { kind: string; height: number }[], pageHeight: number, margin: number, zoom = 1): { tops: number[]; pages: number } {
  const room = (kind: string) => roomOf(kind).map((r) => r * zoom) as [number, number];
  let top = margin, bottom = pageHeight - margin, y = top, pages = 1;
  const tops: number[] = [];
  blocks.forEach((b, i) => {
    const first = y === top;
    let above = first ? 0 : room(b.kind)[0];
    let needs = above + b.height;
    const next = blocks[i + 1];
    if (leads(b.kind) && next && next.height <= bottom - top) needs += room(b.kind)[1] + room(next.kind)[0] + next.height;
    if (!first && y + needs > bottom) {
      top += pageHeight + pageGap;
      bottom += pageHeight + pageGap;
      y = top;
      above = 0;
      pages++;
    }
    tops.push(y + above);
    y = y + above + b.height + room(b.kind)[1];
  });
  return { tops, pages };
}

// The theme on warm paper, as under a reading lamp; the same as the desktop's.
const warmPaper: CSSProperties = {
  ["--color-page" as string]: "#f3ece2",
  ["--color-surface" as string]: "#f8f3ed",
  ["--color-sunken" as string]: "#eae2d7",
  ["--color-ink" as string]: "#423024",
  ["--color-muted" as string]: "#6f5e52",
  ["--color-line" as string]: "#d6cec2",
};
const pause = 600;

// The sizes a reader's page goes between, a step at a time, as a book is zoomed; the
// step it's at kept for the reader in this browser, for every page alike, as the
// desktop's is.
const sizes = [0.8, 0.9, 1, 1.15, 1.3, 1.5];
const sizeKey = "uione-paper-size";
let sizeStep = (() => {
  try {
    const kept = Number(globalThis.localStorage?.getItem(sizeKey));
    return Number.isInteger(kept) && kept >= 0 && kept < sizes.length && globalThis.localStorage?.getItem(sizeKey) !== null ? kept : 2;
  } catch {
    return 2;
  }
})();
const sizeListeners = new Set<() => void>();
function resize(by: number) {
  sizeStep = Math.max(0, Math.min(sizes.length - 1, sizeStep + by));
  try {
    globalThis.localStorage?.setItem(sizeKey, String(sizeStep));
  } catch {
    // the size lasts until the page is left
  }
  sizeListeners.forEach((changed) => changed());
}
function useSizeStep(): number {
  return useSyncExternalStore(
    (changed) => {
      sizeListeners.add(changed);
      return () => sizeListeners.delete(changed);
    },
    () => sizeStep,
    () => 2,
  );
}

// What a paper's insert buttons ask of it, by what names it: the values a block made
// after the caret's is given.
const inserts = new Map<string, Row>();
const insertListeners = new Set<() => void>();
function askInsert(name: string, given: Row) {
  inserts.set(name, given);
  insertListeners.forEach((changed) => changed());
}

// What a paper's paste buttons ask of it: the clipboard's pictures after the caret.
const pastes = new Set<string>();
function askPaste(name: string) {
  pastes.add(name);
  insertListeners.forEach((changed) => changed());
}

// What its add pictures buttons hand it: the pictures chosen, after the caret.
const chosen = new Map<string, File[]>();
function handChosen(name: string, files: File[]) {
  chosen.set(name, files);
  insertListeners.forEach((changed) => changed());
}

// insert "Equation" icon equation { kind = kind::math }: a block made after the caret's.
// larger "Larger" and smaller "Smaller": the reader's page a step larger or smaller.
// paste "Paste": what's on the clipboard, after the caret.
// add pictures "Photo": pictures chosen from the device's files, after the caret.
export function PaperButton({
  paper,
  label,
  icon,
  keys,
  given = {},
  sizes: by = 0,
  pastes: pasting = false,
  chooses = false,
}: {
  paper: string;
  label: string;
  icon?: string;
  keys?: string[];
  given?: Row;
  sizes?: number;
  pastes?: boolean;
  chooses?: boolean;
}) {
  const ui = useUI();
  const tool = useTool() ?? "small";
  const step = useSizeStep();
  const off = useOff() || (by > 0 && step >= sizes.length - 1) || (by < 0 && step <= 0);
  const press = () => (chooses ? choosePictures((files) => handChosen(paper, files)) : pasting ? askPaste(paper) : by ? resize(by) : askInsert(paper, given));
  useKeys(keys, press, !off);
  return (
    <ui.Button kind="secondary" icon={icon} tool={tool} disabled={off} hint={hintOf(label, keys)} onClick={press}>
      {label}
    </ui.Button>
  );
}

// A paper's view as its parts, with the fields its parts are kept in read as those parts.
function blocksOf(view: ViewState, list: string, fields: Record<string, string>): Block[] {
  if (view.status !== "live") return [];
  const rows = (((view.data ?? {}) as Row)[list] as Row[] | undefined) ?? [];
  const field = (part: string) => fields[part] ?? part;
  return rows
    .map((r) => ({
      id: str(r["id"]),
      kind: str(r[field("kind")]) || "text",
      align: str(r[field("align")]),
      words: str(r[field("words")]),
      position: num(r[field("position")]),
      parts: {
        picture: str(r[field("picture")]) || undefined,
        pictureWidth: num(r[field("picture_width")]),
        pictureHeight: num(r[field("picture_height")]),
        width: r[field("width")] === undefined || r[field("width")] === null ? 1 : num(r[field("width")]),
        corners: num(r[field("corners")]),
      },
    }))
    .sort((a, b) => a.position - b.position);
}

// And what it sends, its parts named as the fields they're kept in.
function fieldsOf(sent: Row, fields: Record<string, string>): Row {
  const out: Row = {};
  for (const [part, value] of Object.entries(sent)) out[fields[part] ?? part] = value;
  return out;
}

// A place between two blocks' positions, or after the last.
const between = (before: number, after: number | undefined) => (after === undefined ? before + 1 : (before + after) / 2);

// Where the caret is in an element's words, in characters from its start.
function caretIn(el: HTMLElement): { start: number; end: number } {
  const selection = el.ownerDocument.getSelection();
  if (!selection || selection.rangeCount === 0 || !el.contains(selection.anchorNode)) return { start: 0, end: 0 };
  const range = selection.getRangeAt(0);
  const before = el.ownerDocument.createRange();
  before.selectNodeContents(el);
  before.setEnd(range.startContainer, range.startOffset);
  const start = before.toString().length;
  before.setEnd(range.endContainer, range.endOffset);
  return { start, end: before.toString().length };
}

// The caret put at a place in an element's words.
function placeCaret(el: HTMLElement, at: number): void {
  el.focus({ preventScroll: false });
  const selection = el.ownerDocument.getSelection();
  if (!selection) return;
  const walker = el.ownerDocument.createTreeWalker(el, NodeFilter.SHOW_TEXT);
  let left = at;
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    const length = node.textContent?.length ?? 0;
    if (left <= length) {
      selection.collapse(node, left);
      return;
    }
    left -= length;
  }
  selection.selectAllChildren(el);
  selection.collapseToEnd();
}

// The text node and the place in it that a place in an element's words is.
function pointIn(el: HTMLElement, at: number): [Node, number] {
  const walker = el.ownerDocument.createTreeWalker(el, NodeFilter.SHOW_TEXT);
  let left = at, last: Node | null = null;
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    const length = node.textContent?.length ?? 0;
    if (left <= length) return [node, left];
    left -= length;
    last = node;
  }
  return last ? [last, last.textContent?.length ?? 0] : [el, 0];
}

// A selection in an element's words, from one place to another, the caret at the second.
function selectIn(el: HTMLElement, from: number, to: number): void {
  el.focus({ preventScroll: true });
  const selection = el.ownerDocument.getSelection();
  if (!selection) return;
  const [a, ai] = pointIn(el, from), [b, bi] = pointIn(el, to);
  selection.setBaseAndExtent(a, ai, b, bi);
}

// Where a selection's ends are in an element's words: where it began, and the caret.
function endsIn(el: HTMLElement): { anchor: number; focus: number } {
  const selection = el.ownerDocument.getSelection();
  if (!selection || !selection.anchorNode || !el.contains(selection.anchorNode)) return { anchor: 0, focus: 0 };
  const count = (node: Node, offset: number) => {
    const before = el.ownerDocument.createRange();
    before.selectNodeContents(el);
    before.setEnd(node, offset);
    return before.toString().length;
  };
  return { anchor: count(selection.anchorNode, selection.anchorOffset), focus: selection.focusNode ? count(selection.focusNode, selection.focusOffset) : 0 };
}

// The caret's own box, where a selection's moving end is.
function focusBox(el: HTMLElement): DOMRect {
  const selection = el.ownerDocument.getSelection();
  if (selection?.focusNode && el.contains(selection.focusNode)) {
    const range = el.ownerDocument.createRange();
    range.setStart(selection.focusNode, selection.focusOffset);
    const rects = range.getClientRects?.();
    if (rects && rects.length > 0) return rects[0]!;
  }
  return caretBox(el);
}

// The word around a place in some words, as a double press picks it.
function wordAround(words: string, at: number): [number, number] {
  const letter = /[\p{L}\p{N}_'’]/u;
  let from = at, to = at;
  while (from > 0 && letter.test(words[from - 1]!)) from--;
  while (to < words.length && letter.test(words[to]!)) to++;
  return from === to ? [at, Math.min(words.length, at + 1)] : [from, to];
}

// How a selection across blocks is drawn in the blocks the caret isn't in: the words
// highlighted, as the browser draws one, and a block shown as more than its words, like
// an equation or a picture, tinted whole.
const acrossLook = `
::highlight(one-paper-across) { background-color: color-mix(in srgb, var(--color-accent, #2457d6) 24%, transparent); }
.one-paper [data-whole][data-chosen]::after { content: ""; position: absolute; inset: 0; background: color-mix(in srgb, var(--color-accent, #2457d6) 22%, transparent); pointer-events: none; }
`;

// The place in an element's words nearest a point, as the browser finds it.
function offsetAt(el: HTMLElement, x: number, y: number): number | undefined {
  const doc = el.ownerDocument as Document & {
    caretPositionFromPoint?: (x: number, y: number) => { offsetNode: Node; offset: number } | null;
    caretRangeFromPoint?: (x: number, y: number) => Range | null;
  };
  let node: Node | undefined, offset = 0;
  const position = doc.caretPositionFromPoint?.(x, y);
  if (position) (node = position.offsetNode), (offset = position.offset);
  else {
    const range = doc.caretRangeFromPoint?.(x, y);
    if (range) (node = range.startContainer), (offset = range.startOffset);
  }
  if (!node || !el.contains(node)) return undefined;
  const before = doc.createRange();
  before.selectNodeContents(el);
  before.setEnd(node, offset);
  return before.toString().length;
}

// The caret's box, or the element's start when it has no words.
function caretBox(el: HTMLElement): DOMRect {
  const selection = el.ownerDocument.getSelection();
  if (selection && selection.rangeCount > 0) {
    const rects = selection.getRangeAt(0).getClientRects?.();
    if (rects && rects.length > 0) return rects[0]!;
  }
  return el.getBoundingClientRect();
}

// A block's words as they're typed: plain text keeps its line breaks as text.
const wordsOf = (el: HTMLElement) => el.textContent ?? "";

export function Paper({
  view,
  list,
  fields,
  write,
  writesKind = false,
  add,
  remove,
  parent,
  subject,
  pick,
  empty,
  name,
  outline = false,
  outlineDrawer = false,
  outlineToggle,
  warm = false,
  spread = false,
  readOnly = false,
  paste = false,
  progress = false,
  fills = false,
}: {
  view: ViewState;
  list: string;
  fields: Record<string, string>; // the field each of its parts is kept in, like words: "body"
  write?: string; // the command a block's words written are sent to
  writesKind?: boolean; // whether it changes the kind too, so "# " can make a heading
  add?: string; // the command that makes a block
  remove?: string; // the one that deletes a block joined into the one before
  parent?: string; // the field a block names what the view is per by, like document
  subject?: string; // the id of what the view is per
  pick?: string; // the screen's pick of its blocks, the one the caret is in
  empty?: string; // where an empty page's first words go
  name?: string; // what its insert buttons name it by
  outline?: boolean; // its title and headings listed beside it, the caret's section marked, each going there
  outlineDrawer?: boolean; // and as a drawer at its side, there while outline is, as a toggle says
  outlineToggle?: string; // that toggle, turned off when a heading's chosen where the drawer fills the room
  warm?: boolean; // set on warm paper, as under a reading lamp: its page, sheet, ink and lines
  spread?: boolean; // its pages two at a time, side by side
  readOnly?: boolean; // read, not written on
  paste?: boolean; // pictures pasted or dropped, made picture blocks after the caret
  progress?: boolean; // a bar across its top, as far along as it's been read
  fills?: boolean; // in a workspace, as big as its region, scrolling
}) {
  const runner = useRunner();
  const { say } = useSaying();
  const kept = Object.entries(fields).flat().join(" ");
  const read = useMemo(() => blocksOf(view, list, fields), [view, list, kept]);
  // Words typed, by block, until the view has them; and what of them was sent.
  const written = useRef(new Map<string, string>());
  const sent = useRef(new Map<string, string>());
  const elements = useRef(new Map<string, HTMLElement>());
  // Where the caret goes once the view has the block it's going to: one by its id, or
  // one being made at a position.
  // A block being made is found as the one at its position the page didn't have before,
  // so two blocks at one position, as an interrupted split can leave, can't mislead it.
  const going = useRef<{ id?: string; position?: number; at: number; known?: Set<string> } | null>(null);
  const opened = useRef(false);
  const timers = useRef(new Map<string, ReturnType<typeof setTimeout>>());
  const caretBlock = useRef<string | undefined>(undefined);
  // The selection's other end when it runs into other blocks: the block, by its id, and
  // the place in its words. The caret's block is then chosen from its edge nearest that
  // end to the caret, so the browser draws and moves that part as it would alone.
  const across = useRef<{ id: string; at: number } | null>(null);
  const dragging = useRef(false);
  const pointer = useRef("mouse");  // what last pressed it: a mouse, a finger or a pen
  const firstMaking = useRef(false);  // an empty page's first block asked for, and not yet adopted
  const firstSent = useRef("");       // the words it was asked for with
  const firstId = useRef<string | undefined>(undefined);
  function adoptFirst(id: string) {
    if (firstId.current === id) return;
    firstId.current = id;
    firstMaking.current = false;
    written.current.set(id, written.current.get("") ?? firstSent.current);
    sent.current.set(id, firstSent.current);
    written.current.delete("");
  }
  // How wide its place is, and how tall each block is, as the browser lays them out.
  const room = useRef<HTMLDivElement | null>(null);
  const [roomWidth, setRoomWidth] = useState(sheetWidth + 56);
  const [heights, setHeights] = useState<number[]>([]);
  // How far along it's been read: the share of it scrolled past, in its own scroll or
  // the page's.
  const [past, setPast] = useState(0);
  const readTo = (top: number, span: number) => setPast(span > 0 ? Math.min(1, Math.max(0, top / span)) : 0);
  // In the page's scroll, when it doesn't scroll itself.
  useEffect(() => {
    if (!progress || fills) return;
    const measure = () => readTo(window.scrollY, document.documentElement.scrollHeight - window.innerHeight);
    measure();
    window.addEventListener("scroll", measure, { passive: true });
    return () => window.removeEventListener("scroll", measure);
  }, [progress, fills]);
  useLayoutEffect(() => {
    const el = room.current;
    if (!el) return;
    const measure = () => el.clientWidth > 0 && setRoomWidth(el.clientWidth);
    measure();
    if (typeof ResizeObserver === "undefined") return;
    const watching = new ResizeObserver(measure);
    watching.observe(el);
    return () => watching.disconnect();
  }, []);
  const [writingIn, setWritingIn] = useState<string | undefined>(undefined);
  const [, rebuilt] = useState(0);

  // What the view has caught up with is no longer kept apart; what it hasn't yet is
  // shown as it was written.
  const face = view.status === "live" ? str(((view.data ?? {}) as Row)[fields["face"] ?? "face"]) : "";
  // The first block of an empty page, while it's being made: what's typed on the page
  // is its words, whichever of the view and the answer says its id first, and it's
  // drawn by the same element throughout, so nothing typed meanwhile is lost.
  if (firstMaking.current && !firstId.current && read.length > 0) adoptFirst(read[0]!.id);
  const blocks = read.map((b) => {
    const w = written.current.get(b.id);
    if (w === undefined) return b;
    if (w === b.words && sent.current.get(b.id) === w) {
      written.current.delete(b.id);
      sent.current.delete(b.id);
      return b;
    }
    return { ...b, words: w };
  });
  const first = blocks.length === 0;
  // An empty page shows one empty block where its first words go; once that block is
  // made, by its id, with what's been typed, until the view has it.
  const made = firstId.current;
  const shown: Block[] = first ? [{ id: made ?? "", kind: "title", align: "", words: written.current.get(made ?? "") ?? "", position: 1 }] : blocks;

  const run = useCallback((command: string | undefined, input: Row) => (command ? runner.reply(command, input) : Promise.resolve(undefined)), [runner]);

  const send = useCallback(
    (id: string) => {
      const words = written.current.get(id);
      if (!id || words === undefined || sent.current.get(id) === words) return;
      sent.current.set(id, words);
      void run(write, fieldsOf({ id, words }, fields));
    },
    [run, write, kept],
  );
  const sendAll = useCallback(() => {
    for (const id of written.current.keys()) send(id);
  }, [send]);
  useEffect(() => () => sendAll(), [sendAll]);

  // The caret, once the block it's going to is there; on opening, at the end of the last.
  useLayoutEffect(() => {
    if (view.status !== "live") return;
    const want = going.current;
    if (want) {
      const target = shown.find((b) => (want.id !== undefined ? b.id === want.id : b.position === want.position && b.id !== "" && !want.known?.has(b.id)));
      const el = target && elements.current.get(target.id);
      if (el) {
        going.current = null;
        placeCaret(el, Math.min(want.at, wordsOf(el).length));
        if (pick && target.id) setPicked(pick, target.id);
      }
      return;
    }
    if (!opened.current) {
      opened.current = true;
      const last = shown[shown.length - 1];
      const el = last && elements.current.get(last.id);
      if (el) placeCaret(el, wordsOf(el).length);
      if (pick && last?.id) setPicked(pick, last.id);
    }
  });

  // What's typed, kept and sent once it pauses; on an empty page, the first block made
  // with it; "# " and "> " at a text block's start, a heading and a quote.
  const typed = (b: Block, el: HTMLElement) => {
    let words = wordsOf(el);
    if ((first && !firstId.current) || (b.id === "" && firstMaking.current)) {
      written.current.set("", words);
      // Asked for once; what's typed until it's made is kept, and written once it is.
      if (!add || words === "" || firstMaking.current) return;
      firstMaking.current = true;
      firstSent.current = words;
      const made: Row = { position: 1, words, kind: "title" };
      if (parent && subject) made[parent] = subject;
      void run(add, fieldsOf(made, fields)).then((reply) => {
        if (reply?.id) adoptFirst(reply.id);
        if (firstId.current) send(firstId.current);
        rebuilt((n) => n + 1);
      });
      return;
    }
    const becomes = writesKind && /^. /.test(words) ? kindOf(b.kind).shortcut?.(words[0]!) : undefined;
    if (becomes) {
      const kind = becomes;
      words = words.slice(2);
      el.textContent = words;
      placeCaret(el, 0);
      written.current.set(b.id, words);
      sent.current.set(b.id, words);
      void run(write, fieldsOf({ id: b.id, words, kind }, fields));
      return;
    }
    written.current.set(b.id, words);
    clearTimeout(timers.current.get(b.id));
    timers.current.set(
      b.id,
      setTimeout(() => send(b.id), pause),
    );
  };

  // Inserted from a toolbar: an empty block after the caret's, given what the button
  // gives, the caret then in it.
  const asked = useSyncExternalStore(
    (changed) => {
      insertListeners.add(changed);
      return () => insertListeners.delete(changed);
    },
    () => (name ? inserts.get(name) : undefined),
    () => undefined,
  );
  useEffect(() => {
    if (!asked || !name || !add) return;
    inserts.delete(name);
    sendAll();
    const i = shown.findIndex((b) => b.id === caretBlock.current);
    const here = i >= 0 ? shown[i]! : shown[shown.length - 1];
    const at = here ? shown.indexOf(here) : -1;
    const position = first || !here ? 1 : between(here.position, shown[at + 1]?.position);
    const made: Row = { ...asked, words: "", position };
    if (parent && subject) made[parent] = subject;
    going.current = { position, at: 0, known: new Set(shown.map((b) => b.id)) };
    void run(add, fieldsOf(made, fields));
  }, [asked]);

  // Pictures, each a block after the caret's, in turn, the caret then in the first's
  // caption; all one step to undo. The same as the desktop's.
  const files = useFiles();
  const addPictures = async (given: File[]) => {
    if (!add || !paste || !files) return;
    sendAll();
    const i = shown.findIndex((b) => b.id === caretBlock.current);
    const here = i >= 0 ? shown[i] : shown[shown.length - 1];
    const at = here ? shown.indexOf(here) : -1;
    const from = first || !here ? 0 : here.position;
    const to = first ? undefined : shown[at + 1]?.position;
    const known = new Set(shown.map((b) => b.id));
    let n = 0;
    for (const file of given) {
      const size = await pictureSize(file);
      if (!size) {
        say({ text: `${file.name} isn't a picture this can read.`, problem: true });
        continue;
      }
      const position = to !== undefined ? from + ((to - from) * (n + 1)) / (given.length + 1) : from + n + 1;
      const path = await files.upload(file, () => {});
      const made: Row = { picture: path, picture_width: size.w, picture_height: size.h, position, words: "", kind: "picture", align: "center" };
      if (parent && subject) made[parent] = subject;
      if (n > 0) made["one:history"] = "joined";
      else going.current = { position, at: 0, known };
      n++;
      await run(add, fieldsOf(made, fields));
    }
  };
  // A paste button: the clipboard's pictures, read as the browser lets them be.
  const pasteAsked = useSyncExternalStore(
    (changed) => {
      insertListeners.add(changed);
      return () => insertListeners.delete(changed);
    },
    () => (name ? pastes.has(name) : false),
    () => false,
  );
  useEffect(() => {
    if (!pasteAsked || !name) return;
    pastes.delete(name);
    void (async () => {
      try {
        const items = await navigator.clipboard.read();
        const pictures: File[] = [];
        for (const item of items) {
          const kind = item.types.find((t) => t.startsWith("image/"));
          if (kind) pictures.push(new File([await item.getType(kind)], `Pasted.${kind.slice(6)}`, { type: kind }));
        }
        if (pictures.length === 0) return say({ text: "There's no picture on the clipboard to paste." });
        await addPictures(pictures);
      } catch {
        say({ text: "This browser didn't let the page read the clipboard. Press Ctrl+V, or choose the picture from your files.", problem: true });
      }
    })();
  }, [pasteAsked]);
  // An add pictures button: the pictures chosen.
  const picked = useSyncExternalStore(
    (changed) => {
      insertListeners.add(changed);
      return () => insertListeners.delete(changed);
    },
    () => (name ? chosen.get(name) : undefined),
    () => undefined,
  );
  useEffect(() => {
    if (!picked || !name) return;
    chosen.delete(name);
    void addPictures(picked);
  }, [picked]);

  const goTo = (i: number, at: "start" | "end" | number) => {
    const b = shown[i];
    const el = b && elements.current.get(b.id);
    if (!el) return;
    clearAcross();
    placeCaret(el, at === "start" ? 0 : at === "end" ? wordsOf(el).length : at);
  };

  // A selection running across blocks, its first end and its last, each a block's index
  // and a place in its words; the same as the desktop's.
  const caretIndex = () => shown.findIndex((b) => b.id === caretBlock.current);
  const acrossNow = () => {
    const other = across.current;
    const i = caretIndex();
    const a = other ? shown.findIndex((b) => b.id === other.id) : -1;
    const el = i >= 0 ? elements.current.get(shown[i]!.id) : undefined;
    if (!other || a < 0 || !el || a === i) return null;
    const there = { block: a, at: other.at }, here = { block: i, at: endsIn(el).focus };
    return a < i ? { first: there, last: here } : { first: here, last: there };
  };
  const chosenIn = (k: number, r: NonNullable<ReturnType<typeof acrossNow>>): [number, number] => {
    const el = elements.current.get(shown[k]!.id);
    const size = el ? wordsOf(el).length : 0;
    return [k === r.first.block ? Math.min(r.first.at, size) : 0, k === r.last.block ? Math.min(r.last.at, size) : size];
  };
  const chosenWords = (r: NonNullable<ReturnType<typeof acrossNow>>) => {
    const out: string[] = [];
    for (let k = r.first.block; k <= r.last.block; k++) {
      const el = elements.current.get(shown[k]!.id);
      const [from, to] = chosenIn(k, r);
      out.push(el ? wordsOf(el).slice(from, to) : "");
    }
    return out.join("\n");
  };
  // Drawn in the blocks the caret isn't in, where the browser has the highlights to draw it.
  const paintAcross = () => {
    const r = acrossNow();
    for (const el of elements.current.values()) delete el.parentElement?.dataset["chosen"];
    const highlights = (globalThis as { CSS?: { highlights?: Map<string, unknown> } }).CSS?.highlights;
    const Highlight = (globalThis as { Highlight?: new (...ranges: Range[]) => unknown }).Highlight;
    if (!r) return void highlights?.delete("one-paper-across");
    const ranges: Range[] = [];
    for (let k = r.first.block; k <= r.last.block; k++) {
      const el = elements.current.get(shown[k]!.id);
      if (!el) continue;
      if (el.parentElement) el.parentElement.dataset["chosen"] = "";
      if (k === caretIndex()) continue;
      const [from, to] = chosenIn(k, r);
      const range = el.ownerDocument.createRange();
      const [a, ai] = pointIn(el, from), [b, bi] = pointIn(el, to);
      range.setStart(a, ai);
      range.setEnd(b, bi);
      ranges.push(range);
    }
    if (highlights && Highlight) highlights.set("one-paper-across", new Highlight(...ranges));
  };
  function clearAcross() {
    if (!across.current) return;
    across.current = null;
    paintAcross();
  }
  // The selection's moving end put in block k, at a place in its words: back in the block
  // it began in, a selection there alone; in another, one across blocks.
  const extendTo = (k: number, at: number) => {
    const target = shown[k];
    const el = target && elements.current.get(target.id);
    const i = caretIndex(), now = i >= 0 ? elements.current.get(shown[i]!.id) : undefined;
    if (!el || !now || !target.id) return;
    const anchor = across.current ?? { id: shown[i]!.id, at: endsIn(now).anchor };
    const a = shown.findIndex((b) => b.id === anchor.id);
    if (a < 0) return;
    const size = wordsOf(el).length;
    across.current = null;
    if (k === a) selectIn(el, Math.min(anchor.at, size), Math.min(at, size));
    else selectIn(el, k > a ? 0 : size, Math.min(at, size));
    caretBlock.current = target.id;
    if (k !== a) across.current = anchor;
    paintAcross();
  };
  // The selection across blocks taken away, as one step, and what's typed or pasted put
  // in its place: the first block's words before it, those, and the last's after it
  // made one, and the blocks between and the last deleted.
  const eraseAcross = (put = "") => {
    const r = acrossNow();
    if (!r || !remove) return false;
    const first = shown[r.first.block]!, firstEl = elements.current.get(first.id);
    const lastEl = elements.current.get(shown[r.last.block]!.id);
    if (!firstEl || !lastEl) return false;
    const head = wordsOf(firstEl).slice(0, r.first.at), tail = wordsOf(lastEl).slice(r.last.at);
    const words = head + put + tail;
    across.current = null;
    firstEl.textContent = words;
    written.current.set(first.id, words);
    sent.current.set(first.id, words);
    going.current = { id: first.id, at: head.length + put.length };
    void run(write, fieldsOf({ id: first.id, words }, fields));
    for (let k = r.first.block + 1; k <= r.last.block; k++) {
      written.current.delete(shown[k]!.id);
      void run(remove, { id: shown[k]!.id, "one:history": "joined" });
    }
    paintAcross();
    placeCaret(firstEl, head.length + put.length);
    return true;
  };
  useLayoutEffect(() => {
    if (across.current) paintAcross();
  });
  // Pressed with a mouse: the caret put where it's pressed, a word chosen by a double
  // press and the block's words by a triple, and dragged, a selection that runs on into
  // other blocks; with Shift, from where it was to there. A finger or a pen leaves it to
  // the browser.
  const pressed = (i: number) => (e: MouseEvent<HTMLElement>) => {
    if (pointer.current !== "mouse" || e.button !== 0 || readOnly) return;
    const el = e.currentTarget;
    const at = offsetAt(el, e.clientX, e.clientY);
    if (at === undefined) return;
    e.preventDefault();
    if (e.shiftKey && caretIndex() >= 0) return extendTo(i, at);
    clearAcross();
    const words = wordsOf(el);
    if (e.detail === 2) selectIn(el, ...wordAround(words, at));
    else if (e.detail >= 3) selectIn(el, 0, words.length);
    else placeCaret(el, at);
    dragging.current = e.detail === 1;
  };
  useEffect(() => {
    const moved = (e: globalThis.MouseEvent) => {
      if (!dragging.current) return;
      if (!(e.buttons & 1)) return void (dragging.current = false);
      // The block nearest the pointer, down the page, and the place in it nearest it.
      let k = -1, nearest = Infinity;
      shown.forEach((b, n) => {
        const box = elements.current.get(b.id)?.getBoundingClientRect();
        if (!box) return;
        const away = e.clientY < box.top ? box.top - e.clientY : e.clientY > box.bottom ? e.clientY - box.bottom : 0;
        if (away < nearest) (nearest = away), (k = n);
      });
      const el = k >= 0 ? elements.current.get(shown[k]!.id) : undefined;
      if (!el) return;
      const box = el.getBoundingClientRect();
      const at = offsetAt(el, Math.min(Math.max(e.clientX, box.left + 1), box.right - 1), Math.min(Math.max(e.clientY, box.top + 1), box.bottom - 1));
      extendTo(k, at ?? (e.clientY < box.top ? 0 : wordsOf(el).length));
    };
    const let_go = () => void (dragging.current = false);
    document.addEventListener("mousemove", moved);
    document.addEventListener("mouseup", let_go);
    return () => {
      document.removeEventListener("mousemove", moved);
      document.removeEventListener("mouseup", let_go);
    };
  });

  // Enter: the words after the caret made a block of their own after this one, of the
  // same kind when they're the middle of it, or text after its end.
  const split = (i: number, el: HTMLElement) => {
    const b = shown[i]!;
    if (!add || !b.id) return;
    const words = wordsOf(el);
    const { start, end } = caretIn(el);
    const head = words.slice(0, start), tail = words.slice(end);
    const middle = tail !== "";
    el.textContent = head;
    written.current.set(b.id, head);
    if (sent.current.get(b.id) !== head || b.words !== head) {
      sent.current.set(b.id, head);
      void run(write, fieldsOf({ id: b.id, words: head }, fields));
    }
    const position = between(b.position, shown[i + 1]?.position);
    const made: Row = { position, words: tail, kind: middle ? (kindOf(b.kind).continuesAs ?? b.kind) : "text" };
    if (b.align && b.align !== "left" && (middle || b.kind === "text")) made["align"] = b.align;
    if (parent && subject) made[parent] = subject;
    made["one:history"] = "joined";
    going.current = { position, at: 0, known: new Set(shown.map((b) => b.id)) };
    void run(add, fieldsOf(made, fields));
  };

  // Backspace at a block's start: its words onto the end of the one before, and it
  // deleted. Before an equation, the caret only goes there.
  const joinBack = (i: number, el: HTMLElement) => {
    const before = shown[i - 1], b = shown[i];
    if (!before || !b) return;
    const words = wordsOf(el);
    if (kindOf(before.kind).joins === false || !remove) {
      if (words === "" && remove) void run(remove, { id: b.id });
      return goTo(i - 1, "end");
    }
    const prior = elements.current.get(before.id);
    const joined = (prior ? wordsOf(prior) : before.words) + words;
    const at = joined.length - words.length;
    written.current.delete(b.id);
    written.current.set(before.id, joined);
    sent.current.set(before.id, joined);
    if (prior) prior.textContent = joined;
    going.current = { id: before.id, at };
    void run(write, fieldsOf({ id: before.id, words: joined }, fields));
    void run(remove, { id: b.id, "one:history": "joined" });
    if (prior) placeCaret(prior, at);
  };

  const keyDown = (i: number) => (e: KeyboardEvent<HTMLElement>) => {
    const el = e.currentTarget;
    const control = e.ctrlKey || e.metaKey;
    // Across blocks: erased all at once, what's typed then in its place; moved without
    // Shift, the caret goes to the end it moved toward. Copied and cut as the page is.
    const r = acrossNow();
    if (r) {
      const printable = e.key.length === 1 && !control && !e.altKey;
      if (e.key === "Backspace" || e.key === "Delete" || printable) {
        e.preventDefault();
        eraseAcross(printable ? e.key : "");
        return;
      }
      if (e.key === "Enter" && !control) {
        e.preventDefault();
        eraseAcross();
        const at = shown[r.first.block] && elements.current.get(shown[r.first.block]!.id);
        if (at && !e.shiftKey) split(r.first.block, at);
        return;
      }
      if (!e.shiftKey && !control && e.key.startsWith("Arrow")) {
        e.preventDefault();
        const to = e.key === "ArrowLeft" || e.key === "ArrowUp" ? r.first : r.last;
        goTo(to.block, to.at);
        return;
      }
    }
    const words = wordsOf(el);
    const { start, end } = caretIn(el);
    const { focus } = endsIn(el);
    // With Control, to the page's start or end; with Shift too, chosen to there.
    if (control && !e.altKey && (e.key === "Home" || e.key === "End") && shown.length > 0) {
      e.preventDefault();
      const last = shown.length - 1, toEnd = e.key === "End";
      const lastEl = elements.current.get(shown[last]!.id);
      if (e.shiftKey) extendTo(toEnd ? last : 0, toEnd ? (lastEl ? wordsOf(lastEl).length : 0) : 0);
      else goTo(toEnd ? last : 0, toEnd ? "end" : "start");
      return;
    }
    // Once to choose the block's words, twice the whole page's.
    if (control && !e.altKey && !e.shiftKey && (e.key === "a" || e.key === "A") && shown.length > 1 && (r || (start === 0 && end === words.length))) {
      e.preventDefault();
      const last = shown.length - 1, lastEl = elements.current.get(shown[last]!.id);
      if (!lastEl || !shown[0]!.id) return;
      across.current = null;
      selectIn(lastEl, 0, wordsOf(lastEl).length);
      caretBlock.current = shown[last]!.id;
      if (last > 0) across.current = { id: shown[0]!.id, at: 0 };
      paintAcross();
      return;
    }
    // With Shift past a block's edge, the selection runs on into the next block.
    if (e.shiftKey && !control && !e.altKey) {
      if (e.key === "ArrowLeft" && focus === 0 && i > 0) {
        e.preventDefault();
        const before = elements.current.get(shown[i - 1]!.id);
        return extendTo(i - 1, before ? wordsOf(before).length : 0);
      }
      if (e.key === "ArrowRight" && focus === words.length && i + 1 < shown.length) {
        e.preventDefault();
        return extendTo(i + 1, 0);
      }
      if (e.key === "ArrowUp" || e.key === "ArrowDown") {
        const up = e.key === "ArrowUp";
        const to = up ? i - 1 : i + 1;
        const next = shown[to] && elements.current.get(shown[to]!.id);
        if (!next) return;
        const caret = focusBox(el), box = el.getBoundingClientRect();
        const line = parseFloat(getComputedStyle(el).lineHeight) || 24;
        const leaves = up ? caret.top - box.top < line * 0.8 : box.bottom - caret.bottom < line * 0.8;
        if (!leaves) return;
        e.preventDefault();
        const into = next.getBoundingClientRect();
        const at = offsetAt(next, caret.left, up ? into.bottom - line / 2 : into.top + line / 2);
        return extendTo(to, at ?? (up ? wordsOf(next).length : 0));
      }
    }
    if (!e.shiftKey && !control && (e.key.startsWith("Arrow") || e.key === "Home" || e.key === "End")) clearAcross();
    if (control && !e.altKey && (e.key === "z" || e.key === "Z" || e.key === "y")) {
      // Undone by the app, with what's typed sent first so it's undone too.
      e.preventDefault();
      sendAll();
      const redo = e.key === "y" || e.shiftKey;
      void runner.reply(redo ? "one::redo" : "one::undo").then((step) => {
        if (step) say({ text: `${redo ? "Redid" : "Undid"} ${step.id?.split("::").pop()?.replaceAll("_", " ") || "the last change"}.` });
      });
      return;
    }
    if (e.key === "Enter" && !e.shiftKey && !control) {
      // As its kind says: split, a new line, or out of it, its empty last line taken away.
      const does = kindOf(shown[i]!.kind).enter?.(words, start, start !== end) ?? "split";
      if (does === "line") return;
      if (does === "leave") {
        el.textContent = words.slice(0, -1);
        placeCaret(el, words.length - 1);
      }
      e.preventDefault();
      split(i, el);
      return;
    }
    if (e.key === "Backspace" && start === 0 && end === 0 && i > 0) {
      e.preventDefault();
      joinBack(i, el);
      return;
    }
    if (e.key === "Delete" && start === words.length && end === start && i + 1 < shown.length && kindOf(shown[i]!.kind).joins !== false) {
      e.preventDefault();
      const next = elements.current.get(shown[i + 1]!.id);
      if (next) joinBack(i + 1, next);
      return;
    }
    if (e.shiftKey || control || e.altKey) return;
    if (e.key === "ArrowLeft" && start === 0 && end === 0 && i > 0) {
      e.preventDefault();
      goTo(i - 1, "end");
    } else if (e.key === "ArrowRight" && start === words.length && end === start && i + 1 < shown.length) {
      e.preventDefault();
      goTo(i + 1, "start");
    } else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
      // Past a block's first or last line, into the one before or after, near the
      // caret's x.
      const up = e.key === "ArrowUp";
      const to = up ? i - 1 : i + 1;
      const target = shown[to];
      const next = target && elements.current.get(target.id);
      if (!next) return;
      const caret = caretBox(el), box = el.getBoundingClientRect();
      const line = parseFloat(getComputedStyle(el).lineHeight) || 24;
      const leaves = up ? caret.top - box.top < line * 0.8 : box.bottom - caret.bottom < line * 0.8;
      if (!leaves) return;
      e.preventDefault();
      const into = next.getBoundingClientRect();
      const at = offsetAt(next, caret.left, up ? into.bottom - line / 2 : into.top + line / 2);
      goTo(to, at ?? (up ? "end" : "start"));
    }
  };

  // The pages, as wide as a letter page or the room there is, each as tall as its
  // width says, the blocks placed on them as pagesOf says.
  // Its pages are a letter page's size, made smaller, every part alike, when they're
  // wider than the room there is, as a book's pages are shown whole on a small screen:
  // showing two at a time, or a narrower window, moves nothing from one page to another.
  // Its words are the reader's size on them, larger or smaller; the same as the desktop's.
  // On a phone it's one page as wide as the room, as long as its words, set at the
  // reader's size, edge to edge.
  const zoom = sizes[useSizeStep()] ?? 1;
  const narrow = useNarrow();
  const pagesAcross = spread && !narrow ? 2 : 1;
  const fit = narrow ? 1 : Math.max(0.05, Math.min(1, Math.max(0, roomWidth - 56) / (sheetWidth * pagesAcross + (pagesAcross - 1) * pageGap)));
  const size = zoom * fit;
  const pageWidth = narrow ? roomWidth : sheetWidth * fit;
  const margin = narrow ? narrowMargin : 72 * fit;
  const pageHeight = narrow ? Number.MAX_SAFE_INTEGER : pageWidth * letterShape;
  const placed = pagesOf(heights.map((height, i) => ({ kind: shown[i]?.kind ?? "text", height })), pageHeight, margin, size);
  const writtenTo = shown.length === 0 ? margin : (placed.tops[shown.length - 1] ?? margin) + (heights[shown.length - 1] ?? 0);
  // Where a page is drawn, from its place in one column: side by side, two to a row.
  const pageAt = (n: number) => (pagesAcross === 2 ? { left: (n % 2) * (pageWidth + pageGap), top: Math.floor(n / 2) * (pageHeight + pageGap) } : { left: 0, top: n * (pageHeight + pageGap) });
  const rows = pagesAcross === 2 ? Math.ceil(placed.pages / 2) : placed.pages;
  const tall = narrow ? Math.max(writtenTo + 3 * margin, 240) : rows * pageHeight + (rows - 1) * pageGap;
  const sheet: CSSProperties = {
    position: "relative",
    width: pagesAcross === 2 ? 2 * pageWidth + pageGap : pageWidth,
    height: tall,
    ...(narrow && fills ? { minHeight: "100%" } : {}),
    margin: narrow ? 0 : "28px auto",
    color: "var(--color-ink, #0e1726)",
  };
  const sheets = Array.from({ length: placed.pages }, (_, n) => (
    <div
      key={n}
      aria-hidden
      style={{
        position: "absolute",
        left: pageAt(n).left,
        width: pageWidth,
        top: pageAt(n).top,
        height: narrow ? "100%" : pageHeight,
        background: "var(--color-surface, #fff)",
        border: narrow ? "none" : "1px solid var(--color-line, #dde2ea)",
        borderRadius: narrow ? 0 : 3,
        boxShadow: narrow ? "none" : "0 2px 0 rgba(0, 0, 0, 0.06)",
        boxSizing: "border-box",
      }}
    >
      {!narrow && <span style={{ position: "absolute", right: margin, bottom: margin / 2, transform: "translateY(50%)", fontSize: 13 * fit, color: "var(--color-muted, #5b6577)" }}>{n + 1}</span>}
    </div>
  ));

  // The outline: the title and headings, the section the caret is in marked, each
  // putting the caret at its end and bringing it into view.
  const listed = shown.filter((b) => kindOf(b.kind).outlined && b.words !== "" && b.id !== "");
  let section: string | undefined;
  for (const b of shown) {
    if (kindOf(b.kind).outlined) section = b.id;
    if (b.id === writingIn) break;
  }
  const rail = outline ? (
    <nav
      aria-label="On this page"
      style={
        // As a drawer, it's the workspace's side, as wide and on the same surface.
        outlineDrawer && narrow && fills
          ? { position: "absolute", inset: 0, zIndex: 3, padding: 16, boxSizing: "border-box", overflowY: "auto", background: "var(--color-surface, #ffffff)" }
          : outlineDrawer
          ? { width: 288, flexShrink: 0, padding: 12, boxSizing: "border-box", overflowY: "auto", background: "var(--color-surface, #ffffff)", borderLeft: "1px solid var(--color-line, #dde2ea)", ...(fills ? { margin: "-12px -12px -12px 0" } : { position: "sticky", top: 0, alignSelf: "flex-start" }) }
          : { width: 232, flexShrink: 0, padding: "28px 16px 16px 0", boxSizing: "border-box", ...(fills ? { overflowY: "auto" } : { position: "sticky", top: 0, alignSelf: "flex-start" }) }
      }
    >
      <p style={{ margin: "0 0 8px 12px", fontSize: 11, fontWeight: 600, color: "var(--color-muted, #5b6577)" }}>On this page</p>
      {listed.length === 0 ? (
        <p style={{ margin: "0 0 0 12px", fontSize: 13, color: "var(--color-muted, #5b6577)" }}>Headings show here as you write them.</p>
      ) : (
        <ul style={{ listStyle: "none", margin: 0, padding: 0 }}>
          {listed.map((b) => {
            const here = b.id === section;
            return (
              <li key={b.id}>
                <a
                  href={`#${b.id}`}
                  aria-current={here ? "location" : undefined}
                  onClick={(e) => {
                    e.preventDefault();
                    const el = elements.current.get(b.id);
                    if (!el) return;
                    clearAcross();
                    placeCaret(el, 0);
                    el.scrollIntoView?.({ block: "start", behavior: "smooth" });
                    // Where the contents fill the room, choosing one is done with them.
                    if (narrow && fills && outlineToggle) setToggled(outlineToggle, false);
                  }}
                  style={{
                    display: "block",
                    padding: "3px 0 3px 10px",
                    borderLeft: `2px solid ${here ? "var(--color-accent, #2457d6)" : "var(--color-line, #dde2ea)"}`,
                    fontSize: 14,
                    fontWeight: kindOf(b.kind).outlined === 1 ? 600 : 400,
                    color: here ? "var(--color-ink, #0e1726)" : "var(--color-muted, #5b6577)",
                    textDecoration: "none",
                  }}
                >
                  {b.words}
                </a>
              </li>
            );
          })}
        </ul>
      )}
    </nav>
  ) : null;

  if (view.status === "loading") return <p className="one-quiet">Loading…</p>;
  const page = (
    <div
      ref={room}
      className="one-paper"
      style={{ ...(fills ? { height: "100%", overflowY: "auto" } : {}), ...(fills && narrow ? { margin: -12, height: "calc(100% + 24px)" } : {}), flex: 1, minWidth: 0 }}
      onScroll={(e) => {
        if (progress && fills) readTo(e.currentTarget.scrollTop, e.currentTarget.scrollHeight - e.currentTarget.clientHeight);
      }}
      onPointerDownCapture={(e) => {
        pointer.current = e.pointerType;
      }}
      onCopyCapture={(e) => {
        const r = acrossNow();
        if (!r) return;
        e.preventDefault();
        e.clipboardData.setData("text/plain", chosenWords(r));
      }}
      onCutCapture={(e) => {
        const r = acrossNow();
        if (!r) return;
        e.preventDefault();
        e.clipboardData.setData("text/plain", chosenWords(r));
        eraseAcross();
      }}
      onPasteCapture={(e) => {
        // Over a selection across blocks, what's pasted takes its place.
        if (acrossNow() && e.clipboardData.files.length === 0) {
          e.preventDefault();
          eraseAcross(e.clipboardData.getData("text/plain"));
          return;
        }
        // A picture pasted is a picture block after the caret's; words are typed.
        const pictures = [...e.clipboardData.files].filter((f) => f.type.startsWith("image/"));
        if (!paste || pictures.length === 0) return;
        e.preventDefault();
        void addPictures(pictures);
      }}
      onDragOver={(e) => {
        if (paste && e.dataTransfer.types.includes("Files")) e.preventDefault();
      }}
      onDrop={(e) => {
        if (!paste) return;
        const pictures = [...e.dataTransfer.files].filter((f) => f.type.startsWith("image/"));
        if (pictures.length === 0) {
          if (e.dataTransfer.getData("text/uri-list")) say({ text: "That picture is on the web. Save it first, then drop it here." });
          return;
        }
        e.preventDefault();
        void addPictures(pictures);
      }}
    >
      {progress && (
        <div
          role="progressbar"
          aria-label="Read"
          aria-valuemin={0}
          aria-valuemax={100}
          aria-valuenow={Math.round(past * 100)}
          style={{ position: "sticky", top: 0, height: 3, width: `${past * 100}%`, background: "var(--color-accent, #2457d6)", zIndex: 2 }}
        />
      )}
      <style>{acrossLook}</style>
      <div style={{ ...sheet, ...(warm ? warmPaper : {}) }}>
        {sheets}
        {shown.map((b, i) => (
          <PaperBlock
            key={b.id === "" || b.id === firstId.current ? "first" : b.id}
            block={b}
            place={(() => {
              const top = placed.tops[i] ?? 0, n = Math.floor(top / (pageHeight + pageGap)), at = pageAt(n);
              return { top: at.top + top - n * (pageHeight + pageGap), left: at.left + margin, width: pageWidth - 2 * margin, tall: narrow ? pageWidth : pageHeight - 2 * margin };
            })()}
            zoom={size}
            readOnly={readOnly}
            measured={(height) => setHeights((now) => (now[i] === height ? now : Object.assign([...now], { [i]: height })))}
            face={face}
            hint={first ? empty : undefined}
            element={(el) => {
              if (el) elements.current.set(b.id, el);
              else elements.current.delete(b.id);
            }}
            onInput={(el) => typed(b, el)}
            onKeyDown={keyDown(i)}
            onMouseDown={pressed(i)}
            onFocus={() => {
              caretBlock.current = b.id;
              setWritingIn(b.id);
              if (pick && b.id) setPicked(pick, b.id);
            }}
            onBlur={() => send(b.id)}
            typing={() => written.current.has(b.id)}
          />
        ))}
      </div>
    </div>
  );
  if (!rail) return page;
  // A drawer takes its room from the page's, as the workspace's side does.
  if (outlineDrawer) {
    return (
      <div style={{ display: "flex", ...(fills ? { height: "100%" } : {}) }}>
        {page}
        {rail}
      </div>
    );
  }
  // Beside the page, and as much room on its other side, so the page stays in the
  // middle, as it is without one.
  return (
    <div style={{ display: "flex", ...(fills ? { height: "100%" } : {}) }}>
      <div aria-hidden style={{ width: 232, flexShrink: 0 }} />
      {page}
      {rail}
    </div>
  );
}

// One block: its words typed into where they are, set as its kind says. A math block is
// its equation, set, until the caret's in it, when its words show over the equation.
function PaperBlock({
  block,
  place,
  zoom,
  readOnly,
  measured,
  face,
  hint,
  element,
  onInput,
  onKeyDown,
  onMouseDown,
  onFocus,
  onBlur,
  typing,
}: {
  block: Block;
  place: { top: number; left: number; width: number; tall: number };
  zoom: number;
  readOnly: boolean;
  measured: (height: number) => void;
  face: string;
  hint: string | undefined;
  element: (el: HTMLElement | null) => void;
  onInput: (el: HTMLElement) => void;
  onKeyDown: (e: KeyboardEvent<HTMLElement>) => void;
  onMouseDown: (e: MouseEvent<HTMLElement>) => void;
  onFocus: () => void;
  onBlur: () => void;
  typing: () => boolean;
}) {
  const ref = useRef<HTMLDivElement | null>(null);
  const [focused, setFocused] = useState(false);
  const [words, setWords] = useState(block.words);
  // Its words as they're kept, unless they're being typed: set into it, the caret kept
  // where it was.
  // Its words go in when it first appears, whatever's being typed.
  const shownYet = useRef(false);
  useLayoutEffect(() => {
    const el = ref.current;
    const fresh = !shownYet.current;
    shownYet.current = true;
    if (!el || (!fresh && typing()) || wordsOf(el) === block.words) return;
    const here = el.ownerDocument.activeElement === el;
    const at = here ? caretIn(el).start : 0;
    el.textContent = block.words;
    setWords(block.words);
    if (here) placeCaret(el, Math.min(at, block.words.length));
  }, [block.words]);
  // As its kind says: how it looks, and what it shows besides its words.
  const kind = kindOf(block.kind);
  const look = kind.look({ face, zoom });
  const align = (block.align || "left") as CSSProperties["textAlign"];
  const showing = kind.showing?.({
    parts: block.parts ?? {},
    across: place.width,
    tall: place.tall,
    words,
    focused,
    readOnly,
    zoom,
    align,
    toWords: () => {
      const el = ref.current;
      if (el) placeCaret(el, wordsOf(el).length);
    },
  }) ?? { raw: true };
  const editable: CSSProperties = {
    outline: "none",
    whiteSpace: "pre-wrap",
    overflowWrap: "break-word",
    textAlign: align,
    minHeight: "1lh",
    gridArea: "1 / 1",
    alignSelf: "center",
    ...showing.words,
  };
  // Placed where its page puts it, and telling how tall it is whenever that changes:
  // as it's drawn, and as it's laid out again, as when its font arrives.
  const box = useRef<HTMLDivElement | null>(null);
  const tell = useRef(measured);
  tell.current = measured;
  useLayoutEffect(() => {
    if (box.current) measured(box.current.offsetHeight);
  });
  useEffect(() => {
    const el = box.current;
    if (!el || typeof ResizeObserver === "undefined") return;
    const watching = new ResizeObserver(() => tell.current(el.offsetHeight));
    watching.observe(el);
    return () => watching.disconnect();
  }, []);
  return (
    <div
      ref={box}
      style={{ ...look, position: "absolute", top: place.top, left: place.left, width: place.width, display: "grid", boxSizing: "border-box" }}
      data-kind={block.kind}
      data-whole={showing.raw === false || showing.before ? "" : undefined}
    >
      {hint !== undefined && words === "" && (
        <div aria-hidden style={{ position: "absolute", inset: 0, textAlign: align, opacity: 0.45, pointerEvents: "none", color: "var(--color-muted, #5b6577)" }}>
          {hint}
        </div>
      )}
      {showing.before}
      <div
        ref={(el) => {
          ref.current = el;
          element(el);
        }}
        contentEditable={readOnly ? "false" : "plaintext-only"}
        suppressContentEditableWarning
        role="textbox"
        aria-multiline
        aria-label={hint ?? block.kind}
        spellCheck
        style={editable}
        onInput={(e) => {
          setWords(wordsOf(e.currentTarget));
          onInput(e.currentTarget);
        }}
        onKeyDown={onKeyDown}
        onMouseDown={onMouseDown}
        onFocus={() => {
          setFocused(true);
          onFocus();
        }}
        onBlur={() => {
          setFocused(false);
          onBlur();
        }}
      />
      {showing.over}
    </div>
  );
}
