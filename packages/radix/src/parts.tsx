// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What every front page's design draws with: the short line over a title, a
// section's icon, and what the page offers, in groups.

import type { ReactNode } from "react";
import type { LandingFeature } from "@uione/react";
import { buttonIcons } from "./icons.js";

export const heading = { fontFamily: "var(--font-heading, var(--font-sans))" };

// What an offering opens: its title made the link, stretched over the whole of it, so
// it's opened pressed anywhere, as a card is. Whatever holds it is `relative`, and
// marked as it's pointed at.
export const opens = "relative transition-colors hover:border-accent/60 focus-within:ring-2 focus-within:ring-accent";
export function Opens({ link, children }: { link?: LandingFeature["link"]; children: ReactNode }) {
  if (!link) return <>{children}</>;
  return (
    <a
      href={link.href}
      onClick={link.onClick}
      {...(link.external ? { target: "_blank", rel: "noreferrer" } : {})}
      title={typeof link.label === "string" ? link.label : undefined}
      className="text-inherit no-underline outline-none after:absolute after:inset-0 after:content-['']"
    >
      {children}
    </a>
  );
}

// The short line over a title, small, spaced and in the accent.
export function Over({ children, className = "" }: { children?: ReactNode; className?: string }) {
  if (children === undefined || children === null) return null;
  return <p className={`m-0 text-xs font-semibold tracking-[0.2em] text-accent uppercase ${className}`}>{children}</p>;
}

// A section's icon, at the size a design draws it.
export function Icon({ name, className = "" }: { name?: string; className?: string }) {
  if (!name || !buttonIcons[name]) return null;
  return (
    <span aria-hidden="true" className={`inline-grid shrink-0 place-items-center text-accent [&>svg]:size-full ${className}`}>
      {buttonIcons[name]}
    </span>
  );
}

// What a front page offers, in groups: each section of sections is one, its title
// over what it holds; the offerings written alone, one after another, are one with
// no title. A design draws each group's offerings in its own way.
export type Group = { heading?: LandingFeature; features: LandingFeature[] };
export function grouped(features: LandingFeature[]): Group[] {
  const groups: Group[] = [];
  for (const f of features) {
    const last = groups.at(-1);
    if (f.features.length > 0) groups.push({ heading: f, features: f.features });
    else if (last && !last.heading) last.features.push(f);
    else groups.push({ features: [f] });
  }
  return groups;
}

// Every offering's title, the innermost of each, as a design lists them all: a group
// of services each with its parts lists the parts.
export function titles(features: LandingFeature[]): string[] {
  return features.flatMap((f) => (f.features.length > 0 ? titles(f.features) : [f.title]));
}
