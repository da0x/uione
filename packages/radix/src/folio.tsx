// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The folio design: a front page set like a printed portfolio. The site's name large
// and plain with its title under it, then everything it offers as numbered entries
// between hairlines, each group's title in the margin to their left. A group whose
// offerings all have icons is drawn as a launcher instead, each an icon with its name
// under it, and an offering standing alone is a title in the margin, as a group's is.
// No motion, no glow and no gradient: only type, rules and the theme's colors.

import type { ReactNode } from "react";
import type { LandingFeature, LandingProps } from "@uione/react";
import { Icon, grouped, heading } from "./parts.js";

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
          <div className="max-w-[16rem] text-sm leading-6 text-muted empty:hidden">{f.text}</div>
        </li>
      ))}
    </ul>
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
        <div className="max-w-md leading-7 text-muted empty:hidden">{text}</div>
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
            <div className="max-w-prose empty:hidden">{e.text}</div>
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

export function Folio(p: LandingProps) {
  return (
    <div className="one-landing">
      <div className="mx-auto flex w-full max-w-6xl flex-col gap-24 px-6 pt-10 pb-24 sm:pt-16">
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

        {grouped(p.features).map((g, at) =>
          !g.heading && g.features.length === 1 && !g.features[0].icon ? (
            <Part key={at} overline={g.features[0].overline} title={g.features[0].title} text={g.features[0].text}>
              {null}
            </Part>
          ) : (
            <Part key={at} overline={g.heading?.overline} title={g.heading?.title} text={g.heading?.text}>
              {g.features.every((f) => f.icon) ? <Launcher features={g.features} /> : <Entries entries={g.features} />}
            </Part>
          ),
        )}

        {p.statement !== undefined && (
          <section className="grid border-t border-line pt-10 lg:grid-cols-12 lg:gap-12">
            <p className="m-0 max-w-4xl text-[1.75rem] leading-snug font-normal tracking-[-0.01em] text-ink sm:text-[2.4rem] lg:col-span-8 lg:col-start-5">{p.statement}</p>
          </section>
        )}

        {p.steps.map((s, at) => (
          <Part key={at} id={s.id} overline={s.overline} title={s.title} text={s.text}>
            <Entries entries={s.steps} />
          </Part>
        ))}

        {p.asks.map((ask, at) => (
          <Part key={at} id={ask.id} overline={ask.overline} title={ask.title}>
            <div className="flex max-w-prose flex-col gap-4 leading-7 text-muted [&_form]:max-w-xl">{ask.children}</div>
          </Part>
        ))}
      </div>
    </div>
  );
}
