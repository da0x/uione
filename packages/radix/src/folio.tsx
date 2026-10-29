// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The folio design: a front page set like a printed portfolio, its parts in the order
// the screen writes them. The site's name large and plain with its title under it;
// what it offers as numbered entries between hairlines, each group's title in the
// margin to their left; a group whose offerings all have icons as a launcher, each
// an icon with its name under it, alone at the center of the first screen when the
// page opens on it; and an offering standing alone as a letter, a narrow column of
// words. No motion, no glow and no gradient: only type, rules and the theme's colors.

import { useLayoutEffect, useRef, useState } from "react";
import type { ReactNode } from "react";
import type { LandingFeature, LandingProps } from "@uione/react";
import { Icon, grouped, heading } from "./parts.js";
import type { Group } from "./parts.js";

// Offerings as a launcher: each an icon on a tile, its name centered under it.
function Launcher({ features }: { features: LandingFeature[] }) {
  return (
    <ul className="m-0 grid list-none grid-cols-2 gap-x-6 gap-y-10 p-0 sm:grid-cols-4">
      {features.map((f, at) => (
        <li key={at} className="flex flex-col items-center gap-4 text-center">
          <span className="grid size-24 place-items-center rounded-[22%] border border-line bg-surface">
            <Icon name={f.icon} className="size-10" />
          </span>
          <h3 className="m-0 text-[1.1rem] font-semibold text-ink" style={heading}>
            {f.title}
          </h3>
          <Words text={f.text} className="max-w-[16rem] text-sm leading-6 text-muted" />
        </li>
      ))}
    </ul>
  );
}

// Words given as several texts, each its own paragraph.
function Words({ text, className = "" }: { text?: ReactNode; className?: string }) {
  const paragraphs = (Array.isArray(text) ? text : [text]).filter((words) => words !== undefined && words !== null);
  if (paragraphs.length === 0) return null;
  return (
    <div className={`flex flex-col gap-4 ${className}`}>
      {paragraphs.map((words, at) => (
        <p key={at} className="m-0">
          {words}
        </p>
      ))}
    </div>
  );
}

// The short line over a title, quiet: small, spaced and muted.
function Quiet({ children }: { children?: ReactNode }) {
  if (children === undefined || children === null) return null;
  return <p className="m-0 font-mono text-xs tracking-[0.18em] text-muted uppercase">{children}</p>;
}

function number(at: number) {
  return String(at + 1).padStart(2, "0");
}

// A part of the page: its title in the margin, what it holds to the right.
function Part({ id, overline, title, text, children }: { id?: string; overline?: ReactNode; title?: string; text?: ReactNode; children: ReactNode }) {
  return (
    <section id={id} className="grid scroll-mt-24 gap-8 border-t border-line pt-10 lg:grid-cols-12 lg:gap-12">
      <div className="flex flex-col gap-3 lg:col-span-4">
        <Quiet>{overline}</Quiet>
        {title && (
          <h2 className="m-0 text-[1.6rem] leading-tight font-semibold tracking-[-0.01em] text-ink" style={heading}>
            {title}
          </h2>
        )}
        <Words text={text} className="max-w-md leading-7 text-muted" />
      </div>
      <div className="lg:col-span-8">{children}</div>
    </section>
  );
}

// Offerings, or steps, as numbered entries one under another.
function Entries({ entries }: { entries: { title: string; overline?: ReactNode; text: ReactNode; features?: LandingFeature[] }[] }) {
  return (
    <ol className="m-0 flex list-none flex-col p-0">
      {entries.map((e, at) => (
        <li key={at} className="grid grid-cols-[3rem_1fr] gap-x-4 border-b border-line py-7 first:pt-0 last:border-b-0 sm:grid-cols-[4rem_12rem_1fr] sm:gap-x-8">
          <span className="pt-1 font-mono text-sm text-muted">{number(at)}</span>
          <div className="flex flex-col gap-1">
            <Quiet>{e.overline}</Quiet>
            <h3 className="m-0 text-[1.25rem] leading-snug font-semibold text-ink" style={heading}>
              {e.title}
            </h3>
          </div>
          <div className="col-start-2 flex flex-col gap-4 pt-2 leading-7 text-muted sm:col-start-3 sm:pt-1">
            <Words text={e.text} className="max-w-prose" />
            {e.features && e.features.length > 0 && (
              <ul className="m-0 flex list-none flex-col gap-2 p-0">
                {e.features.map((f, held) => (
                  <li key={held}>
                    <span className="font-medium text-ink">{f.title}</span> <span>{f.text}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </li>
      ))}
    </ol>
  );
}

// The parts of the page in the order the screen writes them, each run of offerings
// in its groups; without an order, the hero first and the rest after it.
type Piece = { kind: "hero" } | { kind: "group"; group: Group } | { kind: "statement" } | { kind: "steps"; at: number } | { kind: "ask"; at: number };
function pieces(p: LandingProps): Piece[] {
  const order = p.order ?? ["hero", ...p.features.map(() => "feature" as const), ...(p.statement !== undefined ? ["statement" as const] : []), ...p.steps.map(() => "steps" as const), ...p.asks.map(() => "ask" as const)];
  const shown: Piece[] = [];
  let run: LandingFeature[] = [];
  let feature = 0;
  let step = 0;
  let ask = 0;
  const flush = () => {
    for (const group of grouped(run)) shown.push({ kind: "group", group });
    run = [];
  };
  for (const kind of order) {
    if (kind === "feature") {
      run.push(p.features[feature++]);
      continue;
    }
    flush();
    if (kind === "hero" || kind === "statement") shown.push({ kind });
    else if (kind === "steps") shown.push({ kind, at: step++ });
    else shown.push({ kind, at: ask++ });
  }
  flush();
  return shown;
}

const launched = (g: Group) => g.features.every((f) => f.icon);
const alone = (g: Group) => !g.heading && g.features.length === 1 && !g.features[0].icon;

function Hero(p: LandingProps) {
  return (
    <section className="flex flex-col gap-8">
      <div className="flex flex-col gap-4">
        <Quiet>{p.overline}</Quiet>
        <h1 className="m-0 text-[4.5rem] leading-[0.9] font-semibold tracking-[-0.04em] text-ink sm:text-[8rem]" style={heading} aria-label={p.name}>
          {p.name}
        </h1>
        <p className="m-0 text-[1.25rem] leading-snug text-muted sm:text-[1.6rem]">{p.title}</p>
      </div>
      {p.text.map((words, at) => (
        <p key={at} className="m-0 max-w-prose text-lg leading-relaxed text-muted">
          {words}
        </p>
      ))}
      {p.links.length > 0 && (
        <div className="flex flex-wrap items-center gap-x-8 gap-y-4">
          {p.links.map((link, at) => (
            <a
              key={at}
              href={link.href}
              onClick={link.onClick}
              className={`text-base font-medium no-underline hover:no-underline focus-visible:outline-2 focus-visible:outline-offset-4 focus-visible:outline-accent ${
                at === 0 ? "rounded-control px-5 py-3" : "border-b border-line pb-0.5 text-ink hover:border-ink"
              }`}
              style={at === 0 ? { background: "var(--color-ink)", color: "var(--color-page)" } : undefined}
            >
              {link.label}
            </a>
          ))}
        </div>
      )}
    </section>
  );
}

// A launcher the page opens on: alone on the first screen, at the center of the window
// rather than of what's left under the bar, so it's raised by however far down it starts.
function Opening({ group }: { group: Group }) {
  const place = useRef<HTMLElement>(null);
  const [raised, setRaised] = useState(0);
  useLayoutEffect(() => {
    const at = place.current;
    if (at) setRaised(at.getBoundingClientRect().top + window.scrollY);
  }, []);
  return (
    <section ref={place} className="flex min-h-svh flex-col items-center justify-center gap-12 text-center" style={{ marginTop: -raised }}>
      {(group.heading?.overline !== undefined || group.heading?.title) && (
        <div className="flex flex-col items-center gap-3">
          <Quiet>{group.heading?.overline}</Quiet>
          {group.heading?.title && (
            <h2 className="m-0 text-[1.25rem] font-semibold text-ink" style={heading}>
              {group.heading.title}
            </h2>
          )}
        </div>
      )}
      <div className="w-full max-w-4xl">
        <Launcher features={group.features} />
      </div>
    </section>
  );
}

// An offering standing alone, set as a letter: a narrow column of words to be read.
function Letter({ feature }: { feature: LandingFeature }) {
  return (
    <section className="mx-auto flex w-full max-w-[40rem] flex-col gap-8 border-t border-line pt-16">
      <div className="flex flex-col gap-3">
        <Quiet>{feature.overline}</Quiet>
        <h2 className="m-0 text-[1.6rem] leading-tight font-semibold tracking-[-0.01em] text-ink" style={heading}>
          {feature.title}
        </h2>
      </div>
      <Words text={feature.text} className="text-lg leading-8 text-ink" />
    </section>
  );
}

export function Folio(p: LandingProps) {
  return (
    <div className="one-landing">
      <div className="mx-auto flex w-full max-w-6xl flex-col gap-24 px-6 pt-10 pb-24 sm:pt-16">
        {pieces(p).map((piece, at) => {
          if (piece.kind === "hero") return <Hero key={at} {...p} />;
          if (piece.kind === "statement")
            return (
              <section key={at} className="grid border-t border-line pt-10 lg:grid-cols-12 lg:gap-12">
                <p className="m-0 max-w-4xl text-[1.75rem] leading-snug font-normal tracking-[-0.01em] text-ink sm:text-[2.4rem] lg:col-span-8 lg:col-start-5">{p.statement}</p>
              </section>
            );
          if (piece.kind === "steps") {
            const s = p.steps[piece.at];
            return (
              <Part key={at} id={s.id} overline={s.overline} title={s.title} text={s.text}>
                <Entries entries={s.steps} />
              </Part>
            );
          }
          if (piece.kind === "ask") {
            const ask = p.asks[piece.at];
            return (
              <Part key={at} id={ask.id} overline={ask.overline} title={ask.title}>
                <div className="flex max-w-prose flex-col gap-4 leading-7 text-muted [&_form]:max-w-xl">{ask.children}</div>
              </Part>
            );
          }
          const g = piece.group;
          if (at === 0 && launched(g)) return <Opening key={at} group={g} />;
          if (alone(g)) return <Letter key={at} feature={g.features[0]} />;
          return (
            <Part key={at} overline={g.heading?.overline} title={g.heading?.title} text={g.heading?.text}>
              {launched(g) ? <Launcher features={g.features} /> : <Entries entries={g.features} />}
            </Part>
          );
        })}
      </div>
    </div>
  );
}
