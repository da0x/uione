// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture: as much of the page's width as it says, a third, a half or all of it, its
// shape its own, aligned as the block says, its corners square or rounded; and under
// it, its caption, which is the block's words, typed where they are. Never taller than
// a page has room for with a line of caption, narrower instead. While the caret's in
// it, it's ringed in the accent, so it's plain which picture the toolbar acts on. The
// same as the desktop's.

import { useEffect, useState } from "react";
import { useFiles } from "../data.js";
import type { BlockKind, Parts } from "./kinds.js";
import { inFace } from "./kinds.js";

// Where a kept file is read from, once it's been asked for.
function useAddress(path: string | undefined): string | undefined {
  const files = useFiles();
  const [url, setUrl] = useState<string>();
  useEffect(() => {
    if (!path || !files) return;
    let current = true;
    void files.address(path).then((made) => current && setUrl(made), () => {});
    return () => {
      current = false;
    };
  }, [path, files]);
  return url;
}

function Shown({ parts, across, tall, align, zoom, here, pick }: { parts: Parts; across: number; tall: number; align: string; zoom: number; here: boolean; pick?: () => void }) {
  const url = useAddress(parts.picture);
  const share = parts.width && parts.width > 0 ? Math.min(1, parts.width) : 1;
  const shape = parts.pictureWidth && parts.pictureHeight ? parts.pictureHeight / parts.pictureWidth : 2 / 3;
  const highest = tall > 0 ? Math.max(0, tall - (8 + captionLine) * zoom) : Infinity;
  const h = Math.min(across * share * shape, highest);
  const w = h / shape;
  const radius = (parts.corners ?? 0) * Math.min(w, h);
  const side = align === "left" ? { marginRight: "auto" } : align === "right" ? { marginLeft: "auto" } : { marginLeft: "auto", marginRight: "auto" };
  return (
    <div style={{ gridArea: "1 / 1", width: w, height: h, borderRadius: radius, overflow: "hidden", background: "var(--color-sunken, #eef1f5)", marginBottom: 8 * zoom, outline: here ? `${2 * zoom}px solid var(--color-accent, #2457d6)` : undefined, outlineOffset: 3 * zoom, ...side }}
      onMouseDown={(e) => {
        // Pressed, it's picked: the caret in its caption.
        if (!pick) return;
        e.preventDefault();
        pick();
      }}
    >
      {url && <img src={url} alt="" draggable={false} style={{ display: "block", width: "100%", height: "100%", objectFit: "cover" }} />}
    </div>
  );
}

// A caption's one line, as tall as its look sets it.
const captionLine = 14 * 1.5;

export const picture: BlockKind = {
  look: (at) => inFace(at, 14, false, { lineHeight: 1.5, fontStyle: "italic", color: "var(--color-muted, #5b6577)" }),
  room: [12, 18],
  joins: false,
  continuesAs: "text",
  showing: ({ parts, across, tall, align, zoom, focused, readOnly, toWords }) => ({
    raw: true,
    words: { gridArea: "2 / 1" },
    before: <Shown parts={parts} across={across} tall={tall} align={String(align ?? "center")} zoom={zoom} here={focused && !readOnly} pick={readOnly ? undefined : toWords} />,
  }),
};
