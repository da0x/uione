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
import type { CSSProperties, KeyboardEvent } from "react";
import { useRunner } from "./data.js";
import type { ViewState } from "./data.js";
import { useSaying } from "./said.js";
import { hintOf, setPicked, useKeys, useOff, useTool } from "./workspace.js";
import { useUI } from "./ui.js";
import { kindOf } from "./paper/index.js";

type Row = Record<string, unknown>;
interface Block {
  id: string;
  kind: string;
  align: string;
  words: string;
  position: number;
}

const num = (v: unknown) => (typeof v === "number" && Number.isFinite(v) ? v : Number(v) || 0);
const str = (v: unknown) => (typeof v === "string" ? v : "");

// A letter page's width, at a screen's 96 to the inch, and an inch of margin; the gap
// between one page and the next.
const sheetWidth = 816;
const letterShape = 11 / 8.5;
const pageGap = 28;

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

// insert "Equation" icon equation { kind = kind::math }: a block made after the caret's.
// larger "Larger" and smaller "Smaller": the reader's page a step larger or smaller.
export function PaperButton({ paper, label, icon, keys, given = {}, sizes: by = 0 }: { paper: string; label: string; icon?: string; keys?: string[]; given?: Row; sizes?: number }) {
  const ui = useUI();
  const tool = useTool() ?? "small";
  const step = useSizeStep();
  const off = useOff() || (by > 0 && step >= sizes.length - 1) || (by < 0 && step <= 0);
  const press = () => (by ? resize(by) : askInsert(paper, given));
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
    .map((r) => ({ id: str(r["id"]), kind: str(r[field("kind")]) || "text", align: str(r[field("align")]), words: str(r[field("words")]), position: num(r[field("position")]) }))
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
    const rects = selection.getRangeAt(0).getClientRects();
    if (rects.length > 0) return rects[0]!;
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
  warm = false,
  spread = false,
  readOnly = false,
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
  warm?: boolean; // set on warm paper, as under a reading lamp: its page, sheet, ink and lines
  spread?: boolean; // its pages two at a time, side by side
  readOnly?: boolean; // read, not written on
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

  const goTo = (i: number, at: "start" | "end" | number) => {
    const b = shown[i];
    const el = b && elements.current.get(b.id);
    if (!el) return;
    placeCaret(el, at === "start" ? 0 : at === "end" ? wordsOf(el).length : at);
  };

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
    const made: Row = { position, words: tail, kind: middle ? b.kind : "text" };
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
    const words = wordsOf(el);
    const { start, end } = caretIn(el);
    const control = e.ctrlKey || e.metaKey;
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
  const zoom = sizes[useSizeStep()] ?? 1;
  const pagesAcross = spread ? 2 : 1;
  const fit = Math.max(0.05, Math.min(1, Math.max(0, roomWidth - 56) / (sheetWidth * pagesAcross + (pagesAcross - 1) * pageGap)));
  const size = zoom * fit;
  const pageWidth = sheetWidth * fit;
  const pageHeight = pageWidth * letterShape;
  const margin = 72 * fit;
  const placed = pagesOf(heights.map((height, i) => ({ kind: shown[i]?.kind ?? "text", height })), pageHeight, margin, size);
  // Where a page is drawn, from its place in one column: side by side, two to a row.
  const pageAt = (n: number) => (spread ? { left: (n % 2) * (pageWidth + pageGap), top: Math.floor(n / 2) * (pageHeight + pageGap) } : { left: 0, top: n * (pageHeight + pageGap) });
  const rows = spread ? Math.ceil(placed.pages / 2) : placed.pages;
  const sheet: CSSProperties = {
    position: "relative",
    width: spread ? 2 * pageWidth + pageGap : pageWidth,
    height: rows * pageHeight + (rows - 1) * pageGap,
    margin: "28px auto",
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
        height: pageHeight,
        background: "var(--color-surface, #fff)",
        border: "1px solid var(--color-line, #dde2ea)",
        borderRadius: 3,
        boxShadow: "0 2px 0 rgba(0, 0, 0, 0.06)",
        boxSizing: "border-box",
      }}
    >
      <span style={{ position: "absolute", right: margin, bottom: margin / 2, transform: "translateY(50%)", fontSize: 13 * fit, color: "var(--color-muted, #5b6577)" }}>{n + 1}</span>
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
    <nav aria-label="On this page" style={{ width: 232, flexShrink: 0, padding: "28px 16px 16px 0", boxSizing: "border-box", ...(fills ? { overflowY: "auto" } : { position: "sticky", top: 0, alignSelf: "flex-start" }) }}>
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
                    placeCaret(el, wordsOf(el).length);
                    el.scrollIntoView?.({ block: "start", behavior: "smooth" });
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
    <div ref={room} className="one-paper" style={{ ...(fills ? { height: "100%", overflowY: "auto" } : {}), flex: 1, minWidth: 0 }}>
      <div style={{ ...sheet, ...(warm ? warmPaper : {}) }}>
        {sheets}
        {shown.map((b, i) => (
          <PaperBlock
            key={b.id === "" || b.id === firstId.current ? "first" : b.id}
            block={b}
            place={(() => {
              const top = placed.tops[i] ?? 0, n = Math.floor(top / (pageHeight + pageGap)), at = pageAt(n);
              return { top: at.top + top - n * (pageHeight + pageGap), left: at.left + margin, width: pageWidth - 2 * margin };
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
  onFocus,
  onBlur,
  typing,
}: {
  block: Block;
  place: { top: number; left: number; width: number };
  zoom: number;
  readOnly: boolean;
  measured: (height: number) => void;
  face: string;
  hint: string | undefined;
  element: (el: HTMLElement | null) => void;
  onInput: (el: HTMLElement) => void;
  onKeyDown: (e: KeyboardEvent<HTMLElement>) => void;
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
  // Placed where its page puts it, and telling how tall it is whenever that changes.
  const box = useRef<HTMLDivElement | null>(null);
  useLayoutEffect(() => {
    if (box.current) measured(box.current.offsetHeight);
  });
  return (
    <div
      ref={box}
      style={{ ...look, position: "absolute", top: place.top, left: place.left, width: place.width, display: "grid", boxSizing: "border-box" }}
      data-kind={block.kind}
    >
      {hint !== undefined && words === "" && (
        <div aria-hidden style={{ position: "absolute", inset: 0, textAlign: align, opacity: 0.45, pointerEvents: "none", color: "var(--color-muted, #5b6577)" }}>
          {hint}
        </div>
      )}
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
