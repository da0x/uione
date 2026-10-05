// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a toolbar control opens, like the theme gallery: beneath its button, moved
// in so it's always inside the window, and above the button when there's more room
// there; on a phone, the whole screen, with its name and a × to close it, the page
// beneath held still. It's drawn at the end of the page, so nothing it's put in can
// clip it. Escape, or a click outside it and its button, closes it.

import { useEffect, useLayoutEffect, useRef, useState } from "react";
import type { CSSProperties, ReactNode, RefObject } from "react";
import { createPortal } from "react-dom";

const margin = 8; // kept from the window's edges
const gap = 6; // between the button and what it opens
const narrow = "(max-width: 640px)";

// Whether the window is as narrow as a phone's, followed as it changes.
export function useNarrow(): boolean {
  const query = () => typeof matchMedia !== "undefined" && matchMedia(narrow).matches;
  const [is, setIs] = useState(query);
  useEffect(() => {
    if (typeof matchMedia === "undefined") return;
    const list = matchMedia(narrow);
    const update = () => setIs(list.matches);
    list.addEventListener("change", update);
    return () => list.removeEventListener("change", update);
  }, []);
  return is;
}

export function Popover({
  anchor,
  label,
  className,
  onClose,
  children,
}: {
  anchor: RefObject<HTMLElement | null>;
  label: string;
  className?: string;
  onClose: () => void;
  children: ReactNode;
}) {
  const box = useRef<HTMLDivElement>(null);
  const phone = useNarrow();
  const [place, setPlace] = useState<CSSProperties>({ visibility: "hidden", top: 0, left: 0 });

  useLayoutEffect(() => {
    if (phone) return;
    const fit = () => {
      const button = anchor.current?.getBoundingClientRect();
      const self = box.current;
      if (!button || !self) return;
      const width = Math.min(self.offsetWidth, innerWidth - 2 * margin);
      const left = Math.min(Math.max(button.left, margin), innerWidth - width - margin);
      const below = innerHeight - button.bottom - gap - margin;
      const above = button.top - gap - margin;
      setPlace(
        below >= Math.min(self.scrollHeight, 240) || below >= above
          ? { left, top: button.bottom + gap, maxHeight: below }
          : { left, bottom: innerHeight - button.top + gap, maxHeight: above },
      );
    };
    fit();
    addEventListener("resize", fit);
    addEventListener("scroll", fit, true);
    return () => {
      removeEventListener("resize", fit);
      removeEventListener("scroll", fit, true);
    };
  }, [phone, anchor]);

  useEffect(() => {
    const close = (e: MouseEvent | KeyboardEvent) => {
      if (e instanceof KeyboardEvent) {
        if (e.key === "Escape") onClose();
        return;
      }
      const target = e.target as Node;
      if (!box.current?.contains(target) && !anchor.current?.contains(target)) onClose();
    };
    document.addEventListener("mousedown", close);
    document.addEventListener("keydown", close);
    return () => {
      document.removeEventListener("mousedown", close);
      document.removeEventListener("keydown", close);
    };
  }, [onClose, anchor]);

  // On a phone the page beneath doesn't scroll while it's open.
  useEffect(() => {
    if (!phone) return;
    const root = document.documentElement;
    const was = root.style.overflow;
    root.style.overflow = "hidden";
    return () => {
      root.style.overflow = was;
    };
  }, [phone]);

  if (typeof document === "undefined") return null;
  return createPortal(
    <div
      ref={box}
      role="dialog"
      aria-label={label}
      className={`uione-popover${phone ? " uione-popover-sheet" : ""}${className ? ` ${className}` : ""}`}
      style={phone ? undefined : place}
    >
      {phone && (
        <div className="uione-popover-head">
          <span>{label}</span>
          <button type="button" className="uione-popover-close" aria-label="Close" onClick={onClose}>
            ×
          </button>
        </div>
      )}
      <div className="uione-popover-body">{children}</div>
    </div>,
    document.body,
  );
}
