// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Front pages, in seven designs: waveform, studio, aurora, grid, relay,
// constellation and folio. Each draws the same parts, a hero, what the site offers, the
// sentence it stands for, its steps and what it asks, in its own way, in the theme's
// colors: its accent, and the accent turned either way around the color wheel for a
// gradient, so every app's front page is its own. Motion stops for anyone who asks
// for less.

import type { CSSProperties, ReactNode } from "react";
import type { LandingFeature, LandingProps } from "@uione/react";
import { Constellation } from "./constellation.js";
import { Folio } from "./folio.js";
import { Icon, Over, grouped, heading, titles } from "./parts.js";

const gradient = "linear-gradient(90deg, var(--one-landing-1), var(--color-accent) 55%, var(--one-landing-3))";
const upright = "linear-gradient(180deg, var(--one-landing-1), var(--color-accent) 55%, var(--one-landing-3))";

function Mark({ icon, size }: { icon?: string; size: number }) {
  return icon ? <img src={icon} alt="" width={size} height={size} /> : null;
}

function Buttons({ links, shape }: { links: LandingProps["links"]; shape: "pill" | "square" | "soft" }) {
  const round = shape === "pill" ? "rounded-full" : shape === "square" ? "rounded-[4px]" : "rounded-control";
  return (
    <div className="flex flex-wrap justify-center gap-3">
      {links.map((link, at) => (
        <a
          key={at}
          href={link.href}
          onClick={link.onClick}
          className={`${round} px-6 py-3.5 text-base font-semibold no-underline transition-transform hover:-translate-y-0.5 hover:no-underline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-accent ${
            at === 0 ? "text-white" : "border border-line text-ink hover:border-accent"
          }`}
          style={at === 0 ? { background: gradient } : undefined}
        >
          {link.label}
        </a>
      ))}
    </div>
  );
}





function Grouped({ features, cards }: { features: LandingFeature[]; cards: (list: LandingFeature[]) => ReactNode }) {
  return (
    <>
      {grouped(features).map((g, at) => (
        <div key={at} className="flex flex-col gap-10">
          {g.heading && (
            <div className="mx-auto flex w-full max-w-6xl flex-col gap-3 px-6">
              <Over>{g.heading.overline}</Over>
              <h2 className="m-0 text-[2.2rem] leading-tight font-extrabold tracking-[-0.02em]" style={heading}>
                {g.heading.title}
              </h2>
              <div className="max-w-2xl text-lg leading-relaxed text-muted empty:hidden">{g.heading.text}</div>
            </div>
          )}
          {cards(g.features)}
        </div>
      ))}
    </>
  );
}

// What an offering holds, under its words on its card.
function Held({ features }: { features: LandingFeature[] }) {
  if (features.length === 0) return null;
  return (
    <ul className="m-0 flex list-none flex-col gap-3 border-t border-line p-0 pt-4">
      {features.map((f, at) => (
        <li key={at} className="flex flex-col gap-1">
          <span className="font-semibold text-ink">{f.title}</span>
          <span className="text-sm leading-6 text-muted">{f.text}</span>
        </li>
      ))}
    </ul>
  );
}

// The sentence the site stands for, large, on its own.
function Said({ statement }: { statement?: ReactNode }) {
  if (statement === undefined) return null;
  return (
    <section className="mx-auto w-full max-w-5xl px-6">
      <p className="m-0 text-[2rem] leading-[1.15] font-bold tracking-[-0.03em] text-ink sm:text-[3rem]" style={heading}>
        {statement}
      </p>
    </section>
  );
}

// Each section as steps: its title, then its steps in order, each numbered.
function StepsShown({ steps }: { steps: LandingProps["steps"] }) {
  return (
    <>
      {steps.map((s, at) => (
        <section key={at} id={s.id} className="mx-auto flex w-full max-w-6xl scroll-mt-24 flex-col gap-10 px-6">
          <div className="flex flex-col gap-3">
            <Over>{s.overline}</Over>
            <h2 className="m-0 text-[2.2rem] leading-tight font-extrabold tracking-[-0.02em]" style={heading}>
              {s.title}
            </h2>
            <div className="max-w-2xl text-lg leading-relaxed text-muted empty:hidden">{s.text}</div>
          </div>
          <ol className="m-0 grid list-none gap-8 p-0 sm:grid-cols-2 lg:grid-cols-4">
            {s.steps.map((step, number) => (
              <li key={number} className="flex flex-col gap-2 border-t-2 pt-4" style={{ borderColor: "var(--color-accent)" }}>
                <span className="font-mono text-sm text-accent">{String(number + 1).padStart(2, "0")}</span>
                <span className="text-[1.2rem] font-bold text-ink">{step.title}</span>
                <span className="leading-7 text-muted">{step.text}</span>
              </li>
            ))}
          </ol>
        </section>
      ))}
    </>
  );
}

function Features({ features, card }: { features: LandingFeature[]; card: (at: number) => CSSProperties }) {
  if (features.length === 0) return null;
  return (
    <section className="mx-auto grid w-full max-w-6xl gap-5 px-6 sm:grid-cols-2 lg:grid-cols-3">
      {features.map((f, at) => (
        <div key={at} className="flex flex-col gap-3 rounded-box border border-line p-7" style={card(at)}>
          <Icon name={f.icon} className="size-7" />
          <Over>{f.overline}</Over>
          <h2 className="text-[1.4rem] font-bold tracking-[-0.01em]">{f.title}</h2>
          <div className="leading-7 text-muted">{f.text}</div>
          <Held features={f.features} />
        </div>
      ))}
    </section>
  );
}

function Asks({ asks, framed }: { asks: LandingProps["asks"]; framed: boolean }) {
  return (
    <>
      {asks.map((ask, at) => (
        <section key={at} id={ask.id} className="mx-auto flex w-full max-w-3xl scroll-mt-24 flex-col items-center gap-5 px-6 text-center">
          <Over>{ask.overline}</Over>
          <h2 className="text-[2.6rem] leading-tight font-extrabold tracking-[-0.02em]">{ask.title}</h2>
          <div
            className={`flex w-full flex-col items-center gap-5 [&_form]:w-full [&_form]:max-w-xl [&_form]:text-left [&>p]:text-lg [&>p]:text-muted ${
              framed ? "rounded-box border border-line bg-surface/70 p-8 backdrop-blur" : ""
            }`}
          >
            {ask.children}
          </div>
        </section>
      ))}
    </>
  );
}

// A wave of bars, tallest in the middle, each rising and falling in its turn.
function Wave({ bars }: { bars: number }) {
  const shown = [];
  for (let at = 0; at < bars; at++) {
    const x = (at - (bars - 1) / 2) / ((bars - 1) / 2);
    const envelope = Math.exp(-x * x * 2.2);
    const wobble = 0.55 + 0.45 * Math.abs(Math.sin(at * 1.7) * Math.cos(at * 0.37));
    shown.push(
      <span
        key={at}
        className="one-landing-bar block w-2 rounded-full"
        style={{ height: `${Math.round(16 + 210 * envelope * wobble)}px`, animationDelay: `${(at % 14) * 0.09}s`, opacity: 0.45 + 0.55 * envelope, background: upright }}
      />,
    );
  }
  return (
    <div aria-hidden="true" className="flex h-64 items-center justify-center gap-[6px] overflow-hidden">
      {shown}
    </div>
  );
}

function Waveform(p: LandingProps) {
  return (
    <div className="one-landing flex flex-col gap-24 pb-24" style={{ background: "radial-gradient(1100px 560px at 50% 12%, color-mix(in oklab, var(--color-accent) 22%, transparent), transparent 70%)" }}>
      <section className="mx-auto flex max-w-5xl flex-col items-center gap-7 px-6 pt-16 text-center">
        <span className="rounded-full border border-line px-3 py-1.5 font-mono text-xs tracking-[0.14em] text-success">{p.name}</span>
        <Over>{p.overline}</Over>
        <h1 className="text-[3.2rem] leading-[1] font-extrabold tracking-[-0.03em] sm:text-[5.2rem]">{p.title}</h1>
        {p.text.map((words, at) => (
          <p key={at} className="max-w-2xl text-lg leading-relaxed text-muted sm:text-xl">
            {words}
          </p>
        ))}
        <Buttons links={p.links} shape="pill" />
      </section>
      <Wave bars={56} />
      <Grouped features={p.features} cards={(list) => <Features features={list} card={() => ({ background: "linear-gradient(180deg, color-mix(in oklab, var(--color-accent) 12%, transparent), var(--color-surface))" })} />} />
      <Said statement={p.statement} />
      <StepsShown steps={p.steps} />
      <Asks asks={p.asks} framed={false} />
    </div>
  );
}

function Studio(p: LandingProps) {
  return (
    <div className="one-landing flex flex-col gap-24 pb-24">
      <section className="mx-auto grid max-w-6xl items-center gap-12 px-6 pt-12 lg:grid-cols-2">
        <div className="flex flex-col items-start gap-6 text-left [&>div]:justify-start">
          <Over>{p.overline}</Over>
          <h1 className="text-[3rem] leading-[1.03] font-extrabold tracking-[-0.025em] sm:text-[4rem]">{p.title}</h1>
          {p.text.map((words, at) => (
            <p key={at} className="text-lg leading-relaxed text-muted">
              {words}
            </p>
          ))}
          <Buttons links={p.links} shape="soft" />
        </div>
        {/* A player, playing: the mark on the frame, and a timeline whose head moves across its cues. */}
        <div aria-hidden="true" className="overflow-hidden rounded-box border border-line bg-surface" style={{ boxShadow: "0 30px 80px color-mix(in oklab, var(--color-accent) 25%, transparent)" }}>
          <div className="relative flex aspect-video items-center justify-center" style={{ background: "radial-gradient(500px 260px at 30% 30%, color-mix(in oklab, var(--color-accent) 30%, var(--color-sunken)), var(--color-sunken) 70%)" }}>
            <span className="one-landing-blink absolute top-4 left-4 flex items-center gap-2 font-mono text-xs text-ink">
              <span className="block size-2 rounded-full" style={{ background: "var(--one-landing-3)" }} />
              {p.name}
            </span>
            <Mark icon={p.icon} size={96} />
          </div>
          <div className="flex flex-col gap-2 border-t border-line p-4">
            <div className="relative h-14 overflow-hidden rounded-md bg-sunken">
              {[
                [4, 26, 1],
                [33, 34, 2],
                [70, 24, 1],
              ].map(([left, width, row], at) => (
                <span key={at} className="absolute top-2 h-4 rounded" style={{ left: `${left}%`, width: `${width}%`, background: row === 1 ? "var(--one-landing-1)" : "var(--color-accent)" }} />
              ))}
              {[
                [6, 24],
                [34, 31],
                [71, 22],
              ].map(([left, width], at) => (
                <span key={at} className="absolute top-8 h-4 rounded opacity-85" style={{ left: `${left}%`, width: `${width}%`, background: "var(--one-landing-3)" }} />
              ))}
              <span className="one-landing-head absolute top-0 bottom-0 w-0.5 bg-success" style={{ boxShadow: "0 0 12px var(--color-success)" }} />
            </div>
          </div>
        </div>
      </section>
      <Grouped
        features={p.features}
        cards={(list) => (
          <ol className="mx-auto grid w-full max-w-6xl list-none gap-3 px-6 sm:grid-cols-2 lg:grid-cols-3">
            {list.map((f, at) => (
              <li key={at} className="flex flex-col gap-3 rounded-box border border-line bg-surface p-6">
                <span className="flex items-center justify-between font-mono text-xs text-muted">
                  {String(at + 1).padStart(2, "0")}
                  <Icon name={f.icon} className="size-6" />
                </span>
                <h2 className="text-[1.3rem] font-bold">{f.title}</h2>
                <div className="leading-7 text-muted">{f.text}</div>
                <Held features={f.features} />
              </li>
            ))}
          </ol>
        )}
      />
      <Said statement={p.statement} />
      <StepsShown steps={p.steps} />
      {p.asks.map((ask, at) => (
        <section key={at} id={ask.id} className="mx-auto w-full max-w-6xl scroll-mt-24 px-6">
          <div className="flex flex-wrap items-start gap-8 rounded-[24px] p-10 text-white sm:p-14 [&_form]:w-full" style={{ background: "linear-gradient(120deg, var(--one-landing-1), var(--color-accent) 50%, var(--one-landing-3))" }}>
            <div className="flex flex-1 basis-[22rem] flex-col gap-3">
              <Over className="!text-white/85">{ask.overline}</Over>
              <h2 className="text-[2.5rem] font-extrabold">{ask.title}</h2>
            </div>
            <div className="flex flex-1 basis-[22rem] flex-col gap-4 rounded-box bg-surface p-6 text-ink [&>p]:text-ink">{ask.children}</div>
          </div>
        </section>
      ))}
    </div>
  );
}

function Aurora(p: LandingProps) {
  return (
    <div className="one-landing relative isolate flex flex-col gap-24 overflow-hidden pb-24">
      <div aria-hidden="true" className="pointer-events-none absolute inset-0 -z-10 overflow-hidden">
        <div className="one-landing-drift1 absolute -top-[8%] -left-[10%] size-[46rem] rounded-full opacity-55 blur-[140px]" style={{ background: "var(--one-landing-1)" }} />
        <div className="one-landing-drift2 absolute top-[4%] -right-[12%] size-[50rem] rounded-full opacity-45 blur-[160px]" style={{ background: "var(--one-landing-3)" }} />
        <div className="one-landing-drift3 absolute top-[30%] left-[28%] size-[42rem] rounded-full opacity-55 blur-[150px]" style={{ background: "var(--color-accent)" }} />
        <div className="absolute inset-x-0 bottom-0 h-3/5" style={{ background: "linear-gradient(180deg, transparent, var(--color-page) 70%)" }} />
      </div>
      <section className="mx-auto flex max-w-5xl flex-col items-center gap-8 px-6 pt-28 text-center">
        <Over>{p.overline}</Over>
        <h1 className="text-[3.4rem] leading-[0.98] font-light tracking-[-0.04em] sm:text-[6.4rem]">{p.title}</h1>
        {p.text.map((words, at) => (
          <p key={at} className="max-w-2xl text-lg leading-relaxed text-ink/85 sm:text-xl">
            {words}
          </p>
        ))}
        <Buttons links={p.links} shape="pill" />
      </section>
      <Grouped features={p.features} cards={(list) => <Features features={list} card={() => ({ background: "color-mix(in oklab, var(--color-surface) 55%, transparent)", backdropFilter: "blur(18px)" })} />} />
      <Said statement={p.statement} />
      <StepsShown steps={p.steps} />
      <Asks asks={p.asks} framed />
    </div>
  );
}

function Grid(p: LandingProps) {
  const marquee = p.features.length ? titles(p.features) : [p.title];
  return (
    <div className="one-landing flex flex-col gap-24 pb-24">
      <section className="relative flex min-h-[38rem] flex-col items-center justify-center gap-7 overflow-hidden px-6 text-center">
        <div aria-hidden="true" className="absolute inset-x-0 bottom-0 h-1/2 overflow-hidden [perspective:520px]">
          <div
            className="one-landing-floor absolute -inset-x-1/2 top-0 -bottom-[40%] origin-top [transform:rotateX(62deg)]"
            style={{
              backgroundImage:
                "linear-gradient(color-mix(in oklab, var(--color-accent) 75%, transparent) 1px, transparent 1px), linear-gradient(90deg, color-mix(in oklab, var(--one-landing-1) 60%, transparent) 1px, transparent 1px)",
              backgroundSize: "64px 64px",
            }}
          />
          <div className="absolute inset-0" style={{ background: "linear-gradient(180deg, var(--color-page) 0%, transparent 35%, transparent 70%, var(--color-page))" }} />
        </div>
        <div aria-hidden="true" className="pointer-events-none absolute top-[38%] left-1/2 h-64 w-[48rem] -translate-x-1/2 -translate-y-1/2" style={{ background: "radial-gradient(closest-side, color-mix(in oklab, var(--one-landing-3) 35%, transparent), transparent)" }} />
        <span className="relative font-mono text-[0.8rem] text-success">00:00:00:00 → {p.name}</span>
        <p className="one-landing-neon relative text-[3rem] leading-none font-extrabold tracking-[0.14em] uppercase sm:text-[6rem]" style={{ fontFamily: "var(--font-heading, var(--font-sans))" }}>
          {p.name}
        </p>
        <Over className="relative">{p.overline}</Over>
        <h1 className="relative max-w-4xl text-[1.6rem] font-semibold sm:text-[1.9rem]">{p.title}</h1>
        {p.text.map((words, at) => (
          <p key={at} className="relative max-w-2xl text-lg leading-relaxed text-muted">
            {words}
          </p>
        ))}
        <div className="relative">
          <Buttons links={p.links} shape="square" />
        </div>
      </section>
      <div aria-hidden="true" className="-mt-16 overflow-hidden border-y border-line bg-sunken py-4">
        <div className="one-landing-track flex w-max gap-11 font-mono text-[0.95rem] whitespace-nowrap text-muted">
          {[...marquee, ...marquee, ...marquee, ...marquee].map((line, at) => (
            <span key={at}>
              <span className="text-accent">[{String((at % marquee.length) + 1).padStart(2, "0")}]</span> {line}
            </span>
          ))}
        </div>
      </div>
      <Grouped
        features={p.features}
        cards={(list) => (
          <div className="mx-auto w-full max-w-6xl px-6">
            <section className="grid gap-px border border-line bg-line sm:grid-cols-2 lg:grid-cols-3">
              {list.map((f, at) => (
                <div key={at} className="flex flex-col gap-3 bg-page p-8">
                  <span className="flex items-center justify-between font-mono text-xs text-accent">
                    [ {String(at + 1).padStart(2, "0")} ]
                    <Icon name={f.icon} className="size-6" />
                  </span>
                  <h2 className="text-[1.4rem] font-bold">{f.title}</h2>
                  <div className="leading-7 text-muted">{f.text}</div>
                  <Held features={f.features} />
                </div>
              ))}
            </section>
          </div>
        )}
      />
      <Said statement={p.statement} />
      <StepsShown steps={p.steps} />
      {p.asks.map((ask, at) => (
        <section key={at} id={ask.id} className="mx-auto w-full max-w-6xl scroll-mt-24 px-6">
          <div className="flex flex-wrap items-start gap-8 rounded-[6px] border p-10 [&_form]:flex-1 [&_form]:basis-[24rem]" style={{ borderColor: "var(--one-landing-1)", background: "linear-gradient(180deg, color-mix(in oklab, var(--one-landing-1) 10%, transparent), transparent)" }}>
            <div className="flex flex-1 basis-[22rem] flex-col gap-3">
              <span className="font-mono text-xs text-success">&gt; {ask.title.toLowerCase().replaceAll(" ", "_")}</span>
              <Over>{ask.overline}</Over>
              <h2 className="text-[2.4rem] font-extrabold">{ask.title}</h2>
            </div>
            <div className="flex flex-1 basis-[24rem] flex-col gap-4 [&>p]:text-muted">{ask.children}</div>
          </div>
        </section>
      ))}
    </div>
  );
}

// A board of work, as the relay design draws it: its stages, each with the cards in
// it, a card being who's working it, a person by their initial or an agent, and how
// urgent it is, as Trac colored a ticket's priority. A card is one of what the site
// offers, by its title, or left blank when it offers fewer.
const stages: { title: string; cards: { at: number; worker?: string; tone?: number }[]; shown: string }[] = [
  { title: "To do", cards: [{ at: 4, tone: 5 }, { at: 5, worker: "D" }], shown: "hidden lg:grid" },
  { title: "In progress", cards: [{ at: 1, worker: "M", tone: 1 }], shown: "grid" },
  { title: "Review", cards: [{ at: 0, worker: "agent", tone: 2 }, { at: 2, worker: "R" }], shown: "grid" },
  { title: "Done", cards: [{ at: 3, worker: "agent", tone: 4 }, { at: 6, worker: "M" }], shown: "hidden sm:grid" },
];
const toneOf = (at: number) => stages.flatMap((stage) => stage.cards).find((card) => card.at === at)?.tone;
const edge = (tone?: number) => (tone ? `var(--color-tone-${tone}-edge)` : "var(--color-line)");

function Worker({ who }: { who?: string }) {
  if (!who) return null;
  if (who === "agent") {
    return (
      <span className="rounded-[4px] px-1.5 py-px text-[0.7rem] leading-5 font-semibold" style={{ color: "var(--one-landing-3)", background: "color-mix(in oklab, var(--one-landing-3) 13%, var(--color-surface))" }}>
        agent
      </span>
    );
  }
  return (
    <span className="grid size-5 place-items-center rounded-full text-[0.68rem] font-semibold text-accent" style={{ background: "color-mix(in oklab, var(--color-accent) 14%, var(--color-surface))" }}>
      {who}
    </span>
  );
}

function Ticket({ title, at, worker, tone, className = "" }: { title?: string; at: number; worker?: string; tone?: number; className?: string }) {
  return (
    <div className={`relative flex h-[5.75rem] flex-col justify-between overflow-hidden rounded-control border border-line bg-surface py-2.5 pr-2.5 pl-3.5 ${className}`}>
      <span className="absolute inset-y-0 left-0 w-[3px]" style={{ background: edge(tone) }} />
      {title ? (
        <span className="line-clamp-2 text-[0.85rem] leading-snug font-medium text-ink">{title}</span>
      ) : (
        <span className="flex flex-col gap-1.5 pt-1">
          <span className="h-2 w-4/5 rounded-full bg-sunken" />
          <span className="h-2 w-1/2 rounded-full bg-sunken" />
        </span>
      )}
      <span className="flex items-center justify-between text-xs text-muted tabular-nums">
        #{12 + at}
        <Worker who={worker} />
      </span>
    </div>
  );
}

// How many cards a stage has, and, for the two the card moves between, how many it
// had before, shown until the card lands.
function Count({ stage, cards }: { stage: string; cards: number }) {
  const had = stage === "In progress" ? cards + 1 : stage === "Review" ? cards - 1 : cards;
  return (
    <span className="relative font-normal text-muted tabular-nums">
      <span className={had === cards ? "" : "one-landing-counted"}>{cards}</span>
      {had !== cards && <span className="one-landing-counting absolute inset-0 opacity-0">{had}</span>}
    </span>
  );
}

// The hero's board. One moment moves: an agent's card is carried from In progress
// to Review, the card left behind settles up, and the move is written down below,
// as every change is. Without motion, it's where it ends.
function Board({ name, icon, titles }: { name: string; icon?: string; titles: string[] }) {
  return (
    <div aria-hidden="true" className="overflow-hidden rounded-box border border-line bg-surface select-none">
      <div className="flex items-center justify-between gap-4 border-b border-line px-4 py-2.5 text-sm">
        <span className="flex items-center gap-2 font-semibold text-ink">
          <Mark icon={icon} size={18} />
          {name}
        </span>
        <span className="flex items-center gap-1.5">
          <Worker who="M" />
          <Worker who="R" />
          <Worker who="D" />
          <Worker who="agent" />
        </span>
      </div>
      <div className="grid grid-cols-2 gap-3 p-3 sm:grid-cols-3 lg:grid-cols-4">
        {stages.map((stage) => (
          <div key={stage.title} className={`${stage.shown} grid-rows-[auto_5.75rem_5.75rem] gap-2 rounded-box bg-sunken p-2`}>
            <div className="flex items-baseline justify-between px-1.5 pt-0.5 text-[0.8rem] font-semibold text-ink">
              {stage.title}
              <Count stage={stage.title} cards={stage.cards.length} />
            </div>
            {stage.cards.map((card, at) =>
              stage.title === "Review" && at === 0 ? (
                <div key={card.at} className="relative">
                  <span className="one-landing-target absolute inset-0 rounded-control border-2 border-dashed border-accent opacity-0" />
                  <Ticket title={titles[card.at]} {...card} className="one-landing-carry z-10" />
                </div>
              ) : (
                <Ticket key={card.at} title={titles[card.at]} {...card} className={stage.title === "In progress" ? "one-landing-settle" : ""} />
              ),
            )}
          </div>
        ))}
      </div>
      <ul className="m-0 flex list-none flex-col gap-2 border-t border-line px-4 py-3 text-[0.85rem] text-muted">
        <li className="one-landing-logged flex items-center gap-2.5">
          <Worker who="agent" />
          <span className="min-w-0 flex-1 truncate text-ink">moved #12 to Review</span>
          <span className="shrink-0">just now</span>
        </li>
        <li className="flex items-center gap-2.5">
          <Worker who="M" />
          <span className="min-w-0 flex-1 truncate">commented on #13</span>
          <span className="shrink-0">4 min ago</span>
        </li>
      </ul>
    </div>
  );
}

function Relay(p: LandingProps) {
  const before = Array.isArray(p.before) ? p.before.length > 0 : p.before != null;
  const gutter = "px-4 sm:px-6 lg:px-8";
  return (
    <div className="one-landing flex flex-col gap-16 pb-24 sm:gap-20">
      <div className="flex flex-col gap-10 pt-8 sm:pt-12 lg:pt-16">
        <section className={`grid gap-7 lg:grid-cols-12 lg:items-end lg:gap-12 ${gutter}`}>
          <div className="flex flex-col gap-4 lg:col-span-7">
            <Over>{p.overline}</Over>
            <h1 className="m-0 max-w-[21ch] text-[2.5rem] leading-[1.03] font-semibold tracking-[-0.035em] text-ink sm:text-[3.5rem] xl:text-[4.25rem]" style={heading}>
              {p.title}
            </h1>
          </div>
          <div className="flex flex-col items-start gap-6 lg:col-span-5 lg:pb-1.5">
            {p.text.map((words, at) => (
              <p key={at} className="m-0 max-w-[46ch] text-[1.125rem] leading-relaxed text-muted">
                {words}
              </p>
            ))}
            {p.links.length > 0 && (
              <div className="flex flex-wrap gap-3">
                {p.links.map((link, at) => (
                  <a
                    key={at}
                    href={link.href}
                    onClick={link.onClick}
                    className={`rounded-control px-5 py-3 text-base font-semibold no-underline transition-colors hover:no-underline focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-accent ${
                      at === 0 ? "bg-accent text-accent-ink hover:bg-accent-hover" : "border border-line bg-surface text-ink hover:border-accent"
                    }`}
                  >
                    {link.label}
                  </a>
                ))}
              </div>
            )}
          </div>
        </section>
        <div className={gutter}>
          <Board name={p.name} icon={p.icon} titles={titles(p.features)} />
        </div>
      </div>
      {/* The screen's own items, like whoever's signed in's work, come right under the board. */}
      {before && <div className={`flex flex-col gap-10 empty:hidden ${gutter}`}>{p.before}</div>}
      <Grouped
        features={p.features}
        cards={(list) => (
          <div className={gutter}>
            <section className="grid gap-x-10 gap-y-12 border-t border-line pt-14 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4">
              {list.map((f, at) => (
                <div key={at} className="relative flex flex-col gap-2.5 pl-5">
                  <span className="absolute inset-y-1 left-0 w-[3px] rounded-full" style={{ background: edge(toneOf(at)) }} />
                  <Icon name={f.icon} className="size-6" />
                  <h2 className="m-0 text-[1.2rem] leading-snug font-semibold tracking-[-0.01em] text-ink" style={heading}>
                    {f.title}
                  </h2>
                  <div className="max-w-[40ch] leading-7 text-muted">{f.text}</div>
                  <Held features={f.features} />
                </div>
              ))}
            </section>
          </div>
        )}
      />
      <Said statement={p.statement} />
      <StepsShown steps={p.steps} />
      {p.asks.map((ask, at) => (
        <section key={at} id={ask.id} className={`flex scroll-mt-24 flex-col gap-5 ${gutter}`}>
          <Over>{ask.overline}</Over>
          <h2 className="m-0 text-[1.75rem] leading-tight font-semibold tracking-[-0.02em] text-ink" style={heading}>
            {ask.title}
          </h2>
          <div className="flex flex-col gap-4 [&_form]:max-w-2xl [&>p]:max-w-[60ch] [&>p]:text-muted">{ask.children}</div>
        </section>
      ))}
    </div>
  );
}

const designs: Record<string, (p: LandingProps) => ReactNode> = { waveform: Waveform, studio: Studio, aurora: Aurora, grid: Grid, relay: Relay, constellation: Constellation, folio: Folio };
// The designs that place the screen's other items, like its tables, themselves; the
// others have them first.
const placing = new Set(["relay"]);

export function Landing(p: LandingProps) {
  const Design = designs[p.design] ?? Waveform;
  return (
    <>
      {!placing.has(p.design) && p.before}
      <Design {...p} />
    </>
  );
}
