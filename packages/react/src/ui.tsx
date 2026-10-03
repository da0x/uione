// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

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
export function useLinks(): (to: string) => LinkProps {
  const navigate = useNavigate();
  return (to: string) => {
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
export function show(value: unknown): string {
  if (value instanceof Date) return value.toLocaleDateString(undefined, { dateStyle: "medium" });
  if (Array.isArray(value)) return value.map(show).filter((item) => item !== "").join(", ");
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
