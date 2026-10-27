// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture: as much of the page's width as it says, a third, a half or all of it, its
// shape its own, aligned as the block says, its corners square or rounded; and under
// it, its caption, which is the block's words, typed where they are. The same as the
// desktop's.

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

function Shown({ parts, across, align, zoom }: { parts: Parts; across: number; align: string; zoom: number }) {
  const url = useAddress(parts.picture);
  const share = parts.width && parts.width > 0 ? Math.min(1, parts.width) : 1;
  const w = across * share;
  const shape = parts.pictureWidth && parts.pictureHeight ? parts.pictureHeight / parts.pictureWidth : 2 / 3;
  const h = w * shape;
  const radius = (parts.corners ?? 0) * Math.min(w, h);
  const side = align === "left" ? { marginRight: "auto" } : align === "right" ? { marginLeft: "auto" } : { marginLeft: "auto", marginRight: "auto" };
  return (
    <div style={{ gridArea: "1 / 1", width: w, height: h, borderRadius: radius, overflow: "hidden", background: "var(--color-sunken, #eef1f5)", marginBottom: 8 * zoom, ...side }}>
      {url && <img src={url} alt="" draggable={false} style={{ display: "block", width: "100%", height: "100%", objectFit: "cover" }} />}
    </div>
  );
}

export const picture: BlockKind = {
  look: (at) => inFace(at, 14, false, { lineHeight: 1.5, fontStyle: "italic", color: "var(--color-muted, #5b6577)" }),
  room: [12, 18],
  joins: false,
  continuesAs: "text",
  showing: ({ parts, across, align, zoom }) => ({
    raw: true,
    words: { gridArea: "2 / 1" },
    before: <Shown parts={parts} across={across} align={String(align ?? "center")} zoom={zoom} />,
  }),
};
