// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// notify planner.reminders at remind_at { title  task::finish "Done" }: whatever screen
// is open, a row whose time has come, said once, with what it shows and its buttons,
// each acting on the row (decision 0024). It's kept in a corner of the page, and said
// by the browser too where the reader has let the page notify them. What's been said is
// remembered in this browser, each row at its time, so a time moved later is said
// again; one that passed while the page was closed is said when it's next open. The
// same as the desktop's.

import { useEffect, useRef, useState } from "react";
import type { Notice } from "./contract.js";
import { useRunner, useSource } from "./data.js";
import type { ViewState } from "./data.js";
import { dateOnly, show, useUI } from "./ui.js";

export interface NoticeSpec {
  view: string;
  list: string;
  at: string; // the date saying when, like remind_at
  shown: string[]; // what it says, its first line first, like title
  buttons?: { command: string; label: string; given?: () => Record<string, unknown> }[];
}

type Row = Record<string, unknown>;

// A time as it's read here: a day alone, at midnight UTC, the start of that day.
function readHere(at: Date): number {
  return dateOnly(at) ? new Date(at.getUTCFullYear(), at.getUTCMonth(), at.getUTCDate()).getTime() : at.getTime();
}

function remembered(key: string): Set<string> {
  try {
    return new Set((globalThis.localStorage?.getItem(key) ?? "").split("\n").filter(Boolean));
  } catch {
    return new Set();
  }
}

export function Notices({ name, notices }: { name: string; notices: readonly NoticeSpec[] }) {
  const ui = useUI();
  const source = useSource();
  const runner = useRunner();
  const latest = useRef<ViewState[]>(notices.map(() => ({ status: "loading", data: undefined })));
  const [shown, setShown] = useState<(Notice & { spec: number; row: string })[]>([]);
  const kept = `uione-notices-said:${name}`;
  useEffect(() => {
    // What's come due and isn't said yet: said, and remembered.
    const look = () => {
      const said = remembered(kept);
      const still = new Set<string>();
      const now = Date.now();
      const due: (Notice & { spec: number; row: string })[] = [];
      notices.forEach((spec, i) => {
        const v = latest.current[i];
        if (v?.status !== "live") return;
        const rows = ((v.data?.[spec.list] as Row[] | undefined) ?? []).filter((r) => r !== null && typeof r === "object");
        for (const row of rows) {
          const held = row[spec.at];
          const at = held instanceof Date ? held : typeof held === "string" && held !== "" ? new Date(held) : undefined;
          if (!at || Number.isNaN(at.getTime()) || readHere(at) > now) continue;
          const mark = `${String(row["id"])}@${Math.floor(at.getTime() / 1000)}`;
          still.add(mark);
          if (said.has(mark)) continue;
          const [first, ...rest] = spec.shown;
          due.push({ key: mark, spec: i, row: String(row["id"]), title: first ? show(row[first]) : "", body: rest.map((f) => show(row[f])).filter(Boolean).join("\n"), buttons: [], onClose: () => {} });
        }
      });
      const everyRead = latest.current.every((v) => v.status === "live");
      if (due.length === 0 && (!everyRead || [...said].every((m) => still.has(m)))) return;
      try {
        globalThis.localStorage?.setItem(kept, [...still].join("\n"));
      } catch {
        // said again next time the page is open
      }
      if (due.length === 0) return;
      for (const n of due) {
        if (typeof Notification !== "undefined" && Notification.permission === "granted") {
          const told = new Notification(n.title, { body: n.body, tag: n.key });
          told.onclick = () => globalThis.focus?.();
        }
      }
      setShown((now) => [...now, ...due]);
    };
    const stops = notices.map((spec, i) =>
      source.subscribe(spec.view, undefined, (state) => {
        latest.current[i] = state;
        look();
      }),
    );
    const every = setInterval(look, 15000);
    return () => {
      clearInterval(every);
      stops.forEach((stop) => stop());
    };
  }, [source, notices, kept]);
  if (shown.length === 0) return null;
  const away = (key: string) => setShown((now) => now.filter((n) => n.key !== key));
  // Pressed, a button acts on its row, and the page may notify them from then on.
  const asking = () => {
    if (typeof Notification !== "undefined" && Notification.permission === "default") void Notification.requestPermission();
  };
  return (
    <ui.Notices
      notices={shown.map((n) => ({
        ...n,
        buttons: (notices[n.spec]?.buttons ?? []).map((b) => ({
          label: b.label,
          onClick: () => {
            asking();
            away(n.key);
            void runner.run(b.command, { ...(b.given?.() ?? {}), id: n.row });
          },
        })),
        onClose: () => {
          asking();
          away(n.key);
        },
      }))}
    />
  );
}
