// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture alone, all of its screen, as a photo's own screen is: fitted in on black,
// zoomed and moved about as it's looked at. And whether its screen's bars are shown:
// for two seconds after the pointer moves or a key is pressed, then not until either
// is again, nor the pointer either.

import { useEffect, useLayoutEffect, useRef, useState, useSyncExternalStore } from "react";
import { useAsked } from "./collage-view.js";
import { useKeys } from "./workspace.js";

// How it's looked at: how many times larger than fitted, and the point of it in the
// middle of its place, in fractions of its width and height.
export interface Looked {
  times: number;
  x: number;
  y: number;
}
export const fitted: Looked = { times: 1, x: 0.5, y: 0.5 };

// Its place, and the picture's own size.
export interface Seen {
  w: number;
  h: number;
  wide: number;
  high: number;
}

// Where it's drawn in its place: fitted, then as many times larger as it's zoomed, its
// point in the middle, but never past its edges once it's larger than its place.
export function drawnAt(l: Looked, s: Seen): { x: number; y: number; w: number; h: number } {
  if (s.wide <= 0 || s.high <= 0) return { x: 0, y: 0, w: s.w, h: s.h };
  const scale = Math.min(s.w / s.wide, s.h / s.high) * l.times;
  const w = s.wide * scale, h = s.high * scale;
  const x = w <= s.w ? (s.w - w) / 2 : Math.min(0, Math.max(s.w - w, s.w / 2 - l.x * w));
  const y = h <= s.h ? (s.h - h) / 2 : Math.min(0, Math.max(s.h - h, s.h / 2 - l.y * h));
  return { x, y, w, h };
}

// Its point in the middle, as it's drawn, so a drag past an edge doesn't build up.
function settled(l: Looked, s: Seen): Looked {
  const r = drawnAt(l, s);
  if (r.w <= 0 || r.h <= 0) return l;
  return { times: l.times, x: (s.w / 2 - r.x) / r.w, y: (s.h / 2 - r.y) / r.h };
}

// How many times larger than fitted it's drawn at 100%, a pixel of it a pixel of the
// screen's, `ratio` of them to each of the page's.
export function actual(s: Seen, ratio: number): number {
  if (s.wide <= 0 || s.high <= 0 || s.w <= 0 || s.h <= 0) return 1;
  return 1 / (Math.min(s.w / s.wide, s.h / s.high) * ratio);
}

// Zoomed by `by` about a point in its place, which stays where it is: never smaller
// than fitted, nor larger than eight times 100%.
export function zoomed(l: Looked, by: number, at: { x: number; y: number }, s: Seen, ratio: number): Looked {
  const before = drawnAt(l, s);
  if (before.w <= 0 || before.h <= 0 || s.wide <= 0 || s.high <= 0) return l;
  const fx = (at.x - before.x) / before.w, fy = (at.y - before.y) / before.h;
  const times = Math.min(Math.max(l.times * by, 1), Math.max(4, 8 * actual(s, ratio)));
  if (times <= 1.0001) return fitted;
  const scale = Math.min(s.w / s.wide, s.h / s.high) * times;
  return settled({ times, x: fx + (s.w / 2 - at.x) / (s.wide * scale), y: fy + (s.h / 2 - at.y) / (s.high * scale) }, s);
}

// Moved by what's dragged or scrolled, in the page's pixels.
export function moved(l: Looked, dx: number, dy: number, s: Seen): Looked {
  const r = drawnAt(l, s);
  if (r.w <= 0 || r.h <= 0) return l;
  return settled({ times: l.times, x: l.x + dx / r.w, y: l.y + dy / r.h }, s);
}

// awake

let awakeNow = true;
let resting: ReturnType<typeof setTimeout> | undefined;
let awakeBefore = true; // as they were before the key being heard
let onlyStepped = false; // the key being heard only stepped to the next picture
const listening = new Set<() => void>();

function told() {
  listening.forEach((changed) => changed());
}

// The pointer moved, or a key was pressed: they're shown, for two seconds more.
export function wake() {
  clearTimeout(resting);
  resting = setTimeout(rest, 2000);
  if (!awakeNow) {
    awakeNow = true;
    told();
  }
}

// The two seconds over, as a test says without waiting: they're hidden.
export function rest() {
  clearTimeout(resting);
  if (awakeNow) {
    awakeNow = false;
    told();
  }
}

// A key pressed, before anything hears it, which wakes them unless it's one held for
// another, like Ctrl for the wheel.
export function keyDown(e: KeyboardEvent) {
  if (["Control", "Shift", "Alt", "Meta"].includes(e.key)) return;
  awakeBefore = awakeNow;
  onlyStepped = false;
  wake();
}

// A key that, as it turned out, only stepped to the next picture: it leaves them as
// they were, and the next picture, coming in, doesn't wake them.
export function stepped() {
  onlyStepped = true;
  if (!awakeBefore) rest();
}

// The screen's steps to the record before and after, for a swipe to take: each step
// says where it goes while it's shown.
const steps = new Map<boolean, () => void>();
export function useSwipeStep(next: boolean, go: (() => void) | undefined) {
  const latest = useRef(go);
  latest.current = go;
  useEffect(() => {
    const step = () => latest.current?.();
    steps.set(next, step);
    return () => {
      if (steps.get(next) === step) steps.delete(next);
    };
  }, [next]);
}

// How far a swipe goes before it steps: an eighth of the picture's width, or 60 pixels
// on a narrow one.
export function swipeSteps(dx: number, width: number): boolean {
  return Math.abs(dx) >= Math.max(60, width / 8);
}

export function useAwake(): boolean {
  return useSyncExternalStore(
    (changed) => {
      listening.add(changed);
      return () => listening.delete(changed);
    },
    () => awakeNow,
    () => true,
  );
}

// Whether a screen that's only its picture shows its bars now, listening while it's
// shown; coming to it from another screen wakes them, and stepping to it doesn't.
export function useAwakeOn(on: boolean): boolean {
  useEffect(() => {
    if (!on) return;
    if (!onlyStepped) wake();
    onlyStepped = false;
    window.addEventListener("pointermove", wake);
    window.addEventListener("keydown", keyDown, true);
    return () => {
      window.removeEventListener("pointermove", wake);
      window.removeEventListener("keydown", keyDown, true);
    };
  }, [on]);
  return useAwake();
}

// picture clipping_page.image fit alone: all of its place, on black, zoomed by Ctrl and
// =, - or 0, by the wheel with Ctrl or a touchpad's pinch, which a browser hands over
// as one, about the pointer, and by the screen's zoom buttons, asked by `zoom`; to 100%
// and back by a double click; dragged, or scrolled, to move about it once it's larger
// than fits; pinched with two fingers. At fit, a swipe across steps to the record after,
// to the left, or before, to the right, as a photo app's does: the picture follows the
// finger, and goes back to its place when the swipe is too short. The picture's turned as
// its camera says, as the browser does.
export function PictureAlone({ source, zoom }: { source?: string; zoom: string }) {
  const box = useRef<HTMLDivElement>(null);
  const [look, setLook] = useState<Looked>(fitted);
  const [size, setSize] = useState({ w: 0, h: 0 });
  const [own, setOwn] = useState({ wide: 0, high: 0 });
  const [held, setHeld] = useState<{ x: number; y: number } | undefined>();
  // A swipe at fit: where it began, and how far across it's gone.
  const [swipe, setSwipe] = useState<{ from: number; by: number } | undefined>();
  // The fingers down, by pointer, for a pinch of two.
  const fingers = useRef(new Map<number, { x: number; y: number }>());
  const apart = useRef(0);
  const awake = useAwake();
  const seen: Seen = { ...size, ...own };
  const ratio = typeof window === "undefined" ? 1 : window.devicePixelRatio || 1;
  // What's done reads where it is now, not where it was when it was last drawn.
  const now = useRef({ look, seen, ratio });
  now.current = { look, seen, ratio };
  const change = (to: (l: Looked, s: Seen, ratio: number) => Looked) => setLook((l) => to(l, now.current.seen, now.current.ratio));
  const middle = () => ({ x: now.current.seen.w / 2, y: now.current.seen.h / 2 });
  // Another picture is looked at fitted, as it's first shown.
  useEffect(() => setLook(fitted), [source]);
  useLayoutEffect(() => {
    const el = box.current;
    if (!el) return;
    const measured = () => setSize({ w: el.clientWidth, h: el.clientHeight });
    measured();
    if (typeof ResizeObserver === "undefined") {
      window.addEventListener("resize", measured);
      return () => window.removeEventListener("resize", measured);
    }
    const watching = new ResizeObserver(measured);
    watching.observe(el);
    return () => watching.disconnect();
  }, []);
  useKeys(["ctrl+equal", "ctrl+plus"], () => change((l, s, r) => zoomed(l, 1.25, middle(), s, r)));
  useKeys(["ctrl+minus"], () => change((l, s, r) => zoomed(l, 0.8, middle(), s, r)));
  useKeys(["ctrl+0"], () => setLook(fitted));
  useAsked(zoom, (does) => {
    if (does === "zoom in") change((l, s, r) => zoomed(l, 1.25, middle(), s, r));
    else if (does === "zoom out") change((l, s, r) => zoomed(l, 0.8, middle(), s, r));
    else if (does === "zoom to fit") setLook(fitted);
  });
  // The wheel's taken from the page, so it doesn't scroll or zoom it as well.
  useEffect(() => {
    const el = box.current;
    if (!el) return;
    const wheeled = (e: WheelEvent) => {
      const at = el.getBoundingClientRect();
      const by = e.deltaMode === 1 ? 16 : 1;
      if (e.ctrlKey) {
        e.preventDefault();
        change((l, s, r) => zoomed(l, Math.exp(-e.deltaY * by * 0.01), { x: e.clientX - at.left, y: e.clientY - at.top }, s, r));
        return;
      }
      if (now.current.look.times <= 1.0001) return;
      e.preventDefault();
      const [dx, dy] = e.shiftKey ? [e.deltaY, e.deltaX] : [e.deltaX, e.deltaY];
      change((l, s) => moved(l, dx * by, dy * by, s));
    };
    el.addEventListener("wheel", wheeled, { passive: false });
    return () => el.removeEventListener("wheel", wheeled);
  }, []);
  const r = drawnAt(look, seen);
  if (swipe && look.times <= 1.0001) r.x += swipe.by;
  const larger = look.times > 1.0001;
  const pointedAt = (e: { clientX: number; clientY: number }) => {
    const at = box.current?.getBoundingClientRect();
    return { x: e.clientX - (at?.left ?? 0), y: e.clientY - (at?.top ?? 0) };
  };
  return (
    <div
      ref={box}
      className="one-picture-alone"
      data-zoomed={larger ? look.times.toFixed(2) : undefined}
      style={{ position: "absolute", inset: 0, overflow: "hidden", background: "#000", touchAction: "none", cursor: !awake ? "none" : held ? "grabbing" : larger ? "grab" : "default" }}
      onDoubleClick={(e) => {
        const at = pointedAt(e);
        change((l, s, ratio) => {
          if (l.times > 1.0001) return fitted;
          const to = actual(s, ratio);
          return zoomed(l, to > 1.01 ? to : 2, at, s, ratio);
        });
      }}
      onPointerDown={(e) => {
        if (e.button > 0) return;
        e.currentTarget.setPointerCapture?.(e.pointerId);
        fingers.current.set(e.pointerId, pointedAt(e));
        // A second finger pinches, instead of moving or swiping.
        if (fingers.current.size === 2) {
          const [a, b] = [...fingers.current.values()] as [{ x: number; y: number }, { x: number; y: number }];
          apart.current = Math.hypot(a.x - b.x, a.y - b.y);
          setHeld(undefined);
          setSwipe(undefined);
          return;
        }
        if (fingers.current.size > 2) return;
        if (larger) setHeld(pointedAt(e));
        else setSwipe({ from: e.clientX, by: 0 });
      }}
      onPointerMove={(e) => {
        if (!fingers.current.has(e.pointerId)) return;
        const at = pointedAt(e);
        fingers.current.set(e.pointerId, at);
        if (fingers.current.size === 2) {
          const [a, b] = [...fingers.current.values()] as [{ x: number; y: number }, { x: number; y: number }];
          const now = Math.hypot(a.x - b.x, a.y - b.y);
          if (apart.current > 0 && now > 0) {
            const by = now / apart.current;
            change((l, s, ratio) => zoomed(l, by, { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }, s, ratio));
          }
          apart.current = now;
          return;
        }
        if (swipe) {
          setSwipe({ from: swipe.from, by: e.clientX - swipe.from });
          return;
        }
        if (!held) return;
        change((l, s) => moved(l, held.x - at.x, held.y - at.y, s));
        setHeld(at);
      }}
      onPointerUp={(e) => {
        fingers.current.delete(e.pointerId);
        if (swipe && fingers.current.size === 0 && swipeSteps(e.clientX - swipe.from, now.current.seen.w)) steps.get(e.clientX < swipe.from)?.();
        setSwipe(undefined);
        setHeld(undefined);
      }}
      onPointerCancel={(e) => {
        fingers.current.delete(e.pointerId);
        setSwipe(undefined);
        setHeld(undefined);
      }}
    >
      {source && (
        <img
          src={source}
          alt=""
          draggable={false}
          onLoad={(e) => setOwn({ wide: e.currentTarget.naturalWidth, high: e.currentTarget.naturalHeight })}
          style={{ position: "absolute", left: r.x, top: r.y, width: r.w, height: r.h, maxWidth: "none", imageOrientation: "from-image", userSelect: "none" }}
        />
      )}
    </div>
  );
}
