// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A surface of cards placed anywhere, drawn as SVG: each card a frame the surface
// draws, holding what a UI kit's Canvas draws of it. It pans and zooms, a card dragged
// moves and one dragged by its corner resizes, and the canvas is told where it went
// once it's let go, snapped to the grid. The wheel pans,
// with Ctrl or ⌘ it zooms at the pointer; on a phone one finger pans and two pinch.
// The arrows move the card chosen a grid step, with Shift resizing it; Enter opens it.

import { useEffect, useId, useRef, useState, type CSSProperties, type KeyboardEvent, type PointerEvent, type ReactNode, type WheelEvent } from "react";
import type { CanvasProps, CanvasCard } from "./contract.js";

const corner = 14; // the square at a card's lower right corner that resizes it
const smallest = 30;

type Box = { x: number; y: number; width: number; height: number };

export function Surface({
  cards,
  grid,
  onPlace,
  onOpen,
  card,
  height = 560,
  style,
  className,
}: Pick<CanvasProps, "cards" | "grid" | "onPlace" | "onOpen"> & {
  className?: string; // a kit's own look, like its focus ring
  card: (card: CanvasCard, chosen: boolean) => ReactNode; // how a kit draws a card, filling its box
  height?: number;
  style?: CSSProperties;
}) {
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [zoom, setZoom] = useState(1);
  const [chosen, setChosen] = useState<string>();
  // Where a card being moved is, until it's let go.
  const [live, setLive] = useState<Record<string, Box>>({});
  const held = useRef<{ card?: string; resizing?: boolean; from: Box; start: { x: number; y: number }; pan: { x: number; y: number } } | undefined>(undefined);
  const touches = useRef(new Map<number, { x: number; y: number }>());
  const pinch = useRef<{ distance: number; zoom: number; middle: { x: number; y: number }; pan: { x: number; y: number } } | undefined>(undefined);
  const surface = useRef<SVGSVGElement>(null);

  const snapped = (at: number, origin: number) => (grid && grid.step > 0 ? Math.round((at - origin) / grid.step) * grid.step + origin : at);
  const boxOf = (c: CanvasCard): Box => live[c.id] ?? { x: c.x, y: c.y, width: c.width, height: c.height };
  // Let go: the canvas shows it where it was put from now on, or back if it's refused.
  const place = (id: string, box: Box) => {
    setLive(({ [id]: _, ...rest }) => rest);
    onPlace?.(id, box.x, box.y, box.width, box.height);
  };
  const local = (event: { clientX: number; clientY: number }) => {
    const r = surface.current?.getBoundingClientRect();
    return { x: event.clientX - (r?.left ?? 0), y: event.clientY - (r?.top ?? 0) };
  };
  // Zoomed about a point of the surface, from a tenth to four times.
  const zoomAt = (to: number, at: { x: number; y: number }) => {
    const z = Math.min(4, Math.max(0.1, to));
    setPan((p) => ({ x: at.x - ((at.x - p.x) * z) / zoom, y: at.y - ((at.y - p.y) * z) / zoom }));
    setZoom(z);
  };

  const down = (event: PointerEvent<SVGElement>, c?: CanvasCard) => {
    if (event.button !== 0 && event.pointerType === "mouse") return;
    touches.current.set(event.pointerId, local(event));
    if (touches.current.size === 2) {
      // A second finger: a pinch, and what the first was moving goes back.
      const [a, b] = [...touches.current.values()];
      pinch.current = { distance: Math.hypot(a.x - b.x, a.y - b.y), zoom, middle: { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }, pan };
      const was = held.current?.card;
      if (was) setLive(({ [was]: _, ...rest }) => rest);
      held.current = undefined;
      return;
    }
    event.currentTarget.setPointerCapture(event.pointerId);
    const at = local(event);
    if (c) {
      event.stopPropagation();
      setChosen(c.id);
      const box = boxOf(c);
      const r = (event.currentTarget as SVGGraphicsElement).getBoundingClientRect();
      const resizing = event.clientX >= r.right - corner && event.clientY >= r.bottom - corner;
      held.current = onPlace ? { card: c.id, resizing, from: box, start: at, pan } : undefined;
      return;
    }
    setChosen(undefined);
    held.current = { from: { x: 0, y: 0, width: 0, height: 0 }, start: at, pan };
  };
  const moved = (event: PointerEvent<SVGElement>) => {
    if (!touches.current.has(event.pointerId)) return;
    touches.current.set(event.pointerId, local(event));
    if (pinch.current && touches.current.size === 2) {
      const [a, b] = [...touches.current.values()];
      const p = pinch.current;
      const z = Math.min(4, Math.max(0.1, (p.zoom * Math.hypot(a.x - b.x, a.y - b.y)) / p.distance));
      const middle = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
      setZoom(z);
      setPan({ x: middle.x - ((p.middle.x - p.pan.x) * z) / p.zoom, y: middle.y - ((p.middle.y - p.pan.y) * z) / p.zoom });
      return;
    }
    const h = held.current;
    if (!h) return;
    const at = local(event);
    const dx = at.x - h.start.x;
    const dy = at.y - h.start.y;
    if (!h.card) {
      setPan({ x: h.pan.x + dx, y: h.pan.y + dy });
      return;
    }
    const box = h.resizing
      ? { ...h.from, width: Math.max(smallest, h.from.width + dx / zoom), height: Math.max(smallest, h.from.height + dy / zoom) }
      : { ...h.from, x: h.from.x + dx / zoom, y: h.from.y + dy / zoom };
    setLive((now) => ({ ...now, [h.card!]: box }));
  };
  const up = (event: PointerEvent<SVGElement>) => {
    touches.current.delete(event.pointerId);
    if (touches.current.size < 2) pinch.current = undefined;
    const h = held.current;
    held.current = undefined;
    if (!h?.card) return;
    const box = live[h.card];
    if (!box || (box.x === h.from.x && box.y === h.from.y && box.width === h.from.width && box.height === h.from.height)) {
      setLive(({ [h.card!]: _, ...rest }) => rest);
      return;
    }
    // Snapped by where it was put; its size as it was made.
    place(h.card, h.resizing ? box : { ...box, x: snapped(box.x, grid?.x ?? 0), y: snapped(box.y, grid?.y ?? 0) });
  };
  const wheel = (event: WheelEvent<SVGSVGElement>) => {
    if (event.ctrlKey || event.metaKey) {
      zoomAt(zoom * Math.exp(-event.deltaY / 300), local(event));
    } else {
      setPan((p) => ({ x: p.x - event.deltaX, y: p.y - event.deltaY }));
    }
  };
  // Pages don't scroll under a surface being wheeled, nor zoom.
  useEffect(() => {
    const el = surface.current;
    if (!el) return;
    const stop = (e: globalThis.WheelEvent) => e.preventDefault();
    el.addEventListener("wheel", stop, { passive: false });
    return () => el.removeEventListener("wheel", stop);
  }, []);
  const key = (event: KeyboardEvent<SVGSVGElement>) => {
    if (event.key === "+" || event.key === "=") return zoomAt(zoom * 1.25, { x: (surface.current?.clientWidth ?? 0) / 2, y: height / 2 });
    if (event.key === "-") return zoomAt(zoom * 0.8, { x: (surface.current?.clientWidth ?? 0) / 2, y: height / 2 });
    if (event.key === "0") return zoomAt(1, { x: (surface.current?.clientWidth ?? 0) / 2, y: height / 2 });
    const c = cards.find((one) => one.id === chosen);
    if (!c) return;
    if (event.key === "Enter") return onOpen?.(c.id);
    if (event.key === "Escape") return setChosen(undefined);
    const step = grid?.step || 10;
    const by: Record<string, [number, number]> = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] };
    const d = by[event.key];
    if (!d || !onPlace) return;
    event.preventDefault();
    const box = boxOf(c);
    place(c.id, event.shiftKey ? { ...box, width: Math.max(smallest, box.width + d[0]), height: Math.max(smallest, box.height + d[1]) } : { ...box, x: box.x + d[0], y: box.y + d[1] });
  };

  const step = grid && grid.step > 0 ? grid.step * zoom : 0;
  const pattern = `canvas-grid-${useId().replace(/[^a-zA-Z0-9-]/g, "")}`;
  return (
    <svg
      ref={surface}
      tabIndex={0}
      role="application"
      aria-label="Canvas: drag a card to move it, or its corner to resize it; the arrows move the chosen card, with Shift resizing it"
      width="100%"
      height={height}
      className={className}
      onPointerDown={(e) => down(e)}
      onPointerMove={moved}
      onPointerUp={up}
      onPointerCancel={up}
      onWheel={wheel}
      onKeyDown={key}
      style={{ display: "block", touchAction: "none", cursor: held.current && !held.current.card ? "grabbing" : "grab", ...style }}
    >
      {/* The grid's points, while they're far enough apart to read. */}
      {step >= 24 && (
        <>
          <defs>
            <pattern id={pattern} width={step} height={step} patternUnits="userSpaceOnUse" x={pan.x + (grid?.x ?? 0) * zoom} y={pan.y + (grid?.y ?? 0) * zoom}>
              <circle cx={0} cy={0} r={1.2} fill="var(--color-muted, #888)" opacity={0.45} />
              <circle cx={step} cy={0} r={1.2} fill="var(--color-muted, #888)" opacity={0.45} />
              <circle cx={0} cy={step} r={1.2} fill="var(--color-muted, #888)" opacity={0.45} />
              <circle cx={step} cy={step} r={1.2} fill="var(--color-muted, #888)" opacity={0.45} />
            </pattern>
          </defs>
          <rect width="100%" height="100%" fill={`url(#${pattern})`} />
        </>
      )}
      <g transform={`translate(${pan.x} ${pan.y}) scale(${zoom})`}>
        {cards.map((c) => {
          const box = boxOf(c);
          const isChosen = chosen === c.id;
          return (
            <g
              key={c.id}
              onPointerDown={(e) => down(e, c)}
              onDoubleClick={() => onOpen?.(c.id)}
              style={{
                transform: `translate(${box.x}px, ${box.y}px)`,
                transition: held.current?.card === c.id ? undefined : "transform 0.2s",
                cursor: onPlace ? "move" : "default",
              }}
            >
              <rect
                width={box.width}
                height={box.height}
                rx={8}
                fill={c.kind === "text" ? "transparent" : "var(--color-surface, #fff)"}
                stroke={isChosen ? "var(--color-accent, #36c)" : c.kind === "text" ? "transparent" : "var(--color-line, #ccc)"}
                strokeWidth={isChosen ? 2 : 1}
              />
              <foreignObject width={box.width} height={box.height} style={{ overflow: "hidden" }}>
                {card(c, isChosen)}
              </foreignObject>
              {onPlace && isChosen && <rect aria-hidden x={box.width - corner / 2} y={box.height - corner / 2} width={corner / 2} height={corner / 2} rx={2} fill="var(--color-accent, #36c)" style={{ cursor: "nwse-resize" }} />}
            </g>
          );
        })}
      </g>
    </svg>
  );
}
