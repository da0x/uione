// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a screen does as it's opened (decision 0020): the first of its on open lines
// that applies, once a visit, decided once every view they read is live. A line opens
// the first row of a view's list its condition holds for, runs a command as its
// button would, picks one of a choice, or turns a toggle on or off, as its toggle would. The desktop's and the terminal's runtimes do the same, from the same
// lines. And how big the screen is, in its own pixels.

import { useEffect, useRef } from "react";
import { useNavigate } from "react-router";
import { useRunner } from "./data.js";
import type { CommandInput } from "./data.js";
import { setChosen, setToggled } from "./workspace.js";

export interface OpeningLine {
  open?: string; // a route like /posters/:poster, opened with the row's id
  rows?: Record<string, unknown>[];
  where?: (row: Record<string, unknown>) => boolean;
  run?: { name: string; id?: string; given?: CommandInput; thenOpen?: string };
  choose?: { name: string; choice: string };
  turn?: { name: string; on: boolean };
  when?: boolean;
}

// The route with what's opened in its last parameter.
const opening = (route: string, id: string) => route.replace(/:[a-z_]+\*?$/, encodeURIComponent(id));

export function OnOpen({ ready, lines }: { ready: boolean; lines: OpeningLine[] }) {
  const runner = useRunner();
  const navigate = useNavigate();
  const decided = useRef(false);
  useEffect(() => {
    if (decided.current || !ready) return;
    decided.current = true;
    for (const line of lines) {
      if (line.when === false) continue;
      if (line.choose) return setChosen(line.choose.name, line.choose.choice);
      if (line.turn) return setToggled(line.turn.name, line.turn.on);
      if (line.run) {
        const { name, id, given, thenOpen } = line.run;
        // What a screen does as it's opened isn't the person's to undo (decision 0020).
        void runner.reply(name, { ...(given ?? {}), ...(id === undefined ? {} : { id }), "one:history": "apart" }).then((made) => {
          if (made?.id && thenOpen) void navigate(opening(thenOpen, made.id), { replace: true });
        });
        return;
      }
      const row = (line.rows ?? []).find((r) => !line.where || line.where(r));
      if (row && line.open) return void navigate(opening(line.open, String(row["id"])), { replace: true });
    }
  });
  return ready ? null : <p className="one-opening">Opening…</p>;
}

// The screen the page is shown on, in its own pixels, as the desktop measures it.
export function screenSize(): { width: number; height: number } {
  if (typeof window === "undefined") return { width: 1920, height: 1080 };
  const ratio = window.devicePixelRatio || 1;
  return { width: Math.round(window.screen.width * ratio), height: Math.round(window.screen.height * ratio) };
}
