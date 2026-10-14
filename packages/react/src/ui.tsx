// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { createContext, useContext } from "react";
import type { MouseEvent } from "react";
import { useNavigate, useParams } from "react-router";
import type { ComponentSet, LinkProps } from "./contract.js";

export const UIContext = createContext<ComponentSet | null>(null);

export function useUI(): ComponentSet {
  const ui = useContext(UIContext);
  if (!ui) throw new Error("uione: screens need an App with a component set");
  return ui;
}

// The href and click handler for a link. A route inside the app is followed without
// reloading the page; anything else, like #waitlist or another site, is left to the
// browser. A click with a modifier key still opens a new tab, as people expect.
// Whether an address is this app's own, written out in full.
function sameSite(to: string): boolean {
  try {
    return typeof window !== "undefined" && new URL(to).origin === window.location.origin;
  } catch {
    return false;
  }
}

// A web address as a table shows it: its host and path, without the scheme or what
// follows a ? or #, and shortened in the middle when it's long, at a slash, like
// example.com/…/on/to/the/end.html, or to its site, like console.cloud.google.com/…,
// when its last part is too long to show whole.
export function shortAddress(address: string, most = 42): string {
  // What's after a ? or # says how to show the page, not which page it is.
  const plain = address.replace(/^https?:\/\//i, "").replace(/[?#].*$/, "").replace(/\/$/, "");
  if (plain.length <= most) return plain;
  const host = plain.split("/")[0]!;
  let tail = plain.slice(-Math.max(8, most - host.length - 3));
  // From a whole path segment, so no part of a name is shown cut; with none that
  // fits, the site alone says where it goes.
  const slash = tail.indexOf("/");
  if (slash < 0) return `${host}/…`;
  tail = tail.slice(slash);
  return host.length + tail.length + 2 >= plain.length ? plain : `${host}/…${tail}`;
}

export function useLinks(): (to: string) => LinkProps {
  const navigate = useNavigate();
  return (to: string) => {
    if (/^https?:\/\//i.test(to)) return { href: to, external: !sameSite(to) };
    if (!to.startsWith("/")) return { href: to };
    return {
      href: to,
      onClick: (event: MouseEvent<HTMLAnchorElement>) => {
        if (event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
        event.preventDefault();
        navigate(to);
      },
    };
  };
}

// A readable label from a snake_case name: created_at becomes "Created at".
export function label(name: string): string {
  const words = name.split("_").filter(Boolean).join(" ");
  return words.charAt(0).toUpperCase() + words.slice(1);
}

// How a value from a view is shown: a date as a date, anything else as text.
// A date picked without a time is stored at midnight UTC, so it's shown as that day
// wherever the reader is, not the evening before. A moment today is shown as its
// time, as mail is, so a claim held until 7:25 PM says so.
export function dateOnly(value: Date): boolean {
  return value.getUTCHours() === 0 && value.getUTCMinutes() === 0 && value.getUTCSeconds() === 0 && value.getUTCMilliseconds() === 0;
}

// A running time: 35s, 4m, 1h 12m, 2d 3h.
export function runningTime(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  if (s < 60) return `${s}s`;
  if (s < 3600) return `${Math.floor(s / 60)}m`;
  if (s < 86400) return s % 3600 >= 60 ? `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m` : `${Math.floor(s / 3600)}h`;
  return s % 86400 >= 3600 ? `${Math.floor(s / 86400)}d ${Math.floor((s % 86400) / 3600)}h` : `${Math.floor(s / 86400)}d`;
}

// What a worker said of what it holds, its card's data, as a sentence: Failed at
// Publishing to npm, 2h 10m ago: step publish exited 1. Anything else it said follows,
// each by its name.
const workWords: Record<string, string> = { inactive: "Not started", waiting: "Waiting", working: "Working", success: "Succeeded", done: "Succeeded", failed: "Failed" };
function saidOfWork(data: Record<string, unknown>, now: Date): string | undefined {
  const words = typeof data.status === "string" ? workWords[data.status] : undefined;
  if (!words) return undefined;
  let said = words;
  if (typeof data.at === "string" && data.at) said += ` at ${data.at}`;
  const since = data.since instanceof Date ? data.since.getTime() : typeof data.since === "string" ? Date.parse(data.since) : NaN;
  if (!Number.isNaN(since)) said += `, ${runningTime((now.getTime() - since) / 1000)} ago`;
  const rest = Object.entries(data)
    .filter(([name]) => !["status", "at", "since", "note"].includes(name))
    .map(([name, item]) => [name, show(item, now)])
    .filter(([, shown]) => shown !== "")
    .map(([name, shown]) => `${name} ${shown}`);
  if (rest.length > 0) said += `, ${rest.join(", ")}`;
  if (typeof data.note === "string" && data.note) said += `: ${data.note}`;
  return said;
}

export function show(value: unknown, now = new Date()): string {
  if (value instanceof Date && !dateOnly(value) && value.toDateString() === now.toDateString()) {
    return value.toLocaleTimeString(undefined, { timeStyle: "short" });
  }
  if (value instanceof Date) return value.toLocaleDateString(undefined, { dateStyle: "medium", ...(dateOnly(value) ? { timeZone: "UTC" } : {}) });
  if (Array.isArray(value)) return value.map((item) => show(item, now)).filter((item) => item !== "").join(", ");
  // Named values, like a card's data, each by its name: status working, runner 2.
  if (value !== null && typeof value === "object") {
    const work = saidOfWork(value as Record<string, unknown>, now);
    if (work) return work;
    return Object.entries(value)
      .map(([name, item]) => [name, show(item, now)])
      .filter(([, shown]) => shown !== "")
      .map(([name, shown]) => `${name} ${shown}`)
      .join(", ");
  }
  return String(value ?? "");
}

// The action in a command's full name: waitlist::signup::create becomes "create".
export function action(command: string): string {
  return command.split("::").pop() ?? command;
}

// A parameter of the screen's route: on /books/:book, useParam("book") is the book's
// id, which is what a view with one document per book is read with.
// The name of the screen's last parameter when it takes the rest of the address,
// like file in /code/:file*.
export const Rest = createContext<string | undefined>(undefined);

export function useParam(name: string): string | undefined {
  const params = useParams();
  const rest = useContext(Rest);
  return params[name] ?? (name === rest ? params["*"] : undefined);
}
