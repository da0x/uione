// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a screen does as it's opened (decision 0020): the first of its on open lines
// that applies, once a visit, decided once every view they read is live. A line opens
// the first row of a view's list its condition holds for, or runs a command as its
// button would. The desktop's and the terminal's runtimes do the same, from the same
// lines. And how big the screen is, in its own pixels.

import { useEffect, useRef } from "react";
import { useNavigate, useParams } from "react-router";
import { useRunner } from "./data.js";
import type { CommandInput } from "./data.js";

export interface OpeningLine {
  open?: string; // a route like /posters/:poster, opened with the row's id
  rows?: Record<string, unknown>[];
  where?: (row: Record<string, unknown>) => boolean;
  run?: { name: string; id?: string; given?: CommandInput; thenOpen?: string };
  when?: boolean;
}

// The route with what's opened in its last parameter.
const opening = (route: string, id: string) => route.replace(/:[a-z_]+\*?$/, encodeURIComponent(id));

// Where `then open` goes once a command answers: its answer, what it made or acted
// on, in the parameter named like its entity, photo::delete's in :photo, and every
// other parameter as the page's address has it. A route with no parameter named like
// the entity doesn't take the answer.
export function madeOpens(route: string, command: string, made: string | undefined, params: Record<string, string | undefined>): string {
  const parts = command.split("::");
  const entity = parts.length > 1 ? parts[parts.length - 2] : "";
  return route.replace(/:([A-Za-z_]\w*)\*?/g, (written, name: string) => {
    const value = name === entity ? made : params[name];
    return value === undefined ? written : encodeURIComponent(value);
  });
}

export function OnOpen({ ready, lines }: { ready: boolean; lines: OpeningLine[] }) {
  const runner = useRunner();
  const navigate = useNavigate();
  const params = useParams();
  const decided = useRef(false);
  useEffect(() => {
    if (decided.current || !ready) return;
    decided.current = true;
    for (const line of lines) {
      if (line.when === false) continue;
      if (line.run) {
        const { name, id, given, thenOpen } = line.run;
        // What a screen does as it's opened isn't the person's to undo (decision 0020).
        void runner.reply(name, { ...(given ?? {}), ...(id === undefined ? {} : { id }), "one:history": "apart" }).then((made) => {
          if (made?.id && thenOpen) void navigate(madeOpens(thenOpen, name, made.id, params), { replace: true });
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
