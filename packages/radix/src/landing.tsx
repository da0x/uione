// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Front pages, in four designs: waveform, studio, aurora and grid. Each draws the
// same parts, a hero, what the site offers and what it asks, in its own way, in the
// theme's colors: its accent, and the accent turned either way around the color
// wheel for a gradient, so every app's front page is its own. Motion stops for
// anyone who asks for less.

import type { CSSProperties, ReactNode } from "react";
import type { LandingProps } from "@uione/react";

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

function Features({ features, card }: { features: LandingProps["features"]; card: (at: number) => CSSProperties }) {
  if (features.length === 0) return null;
  return (
    <section className="mx-auto grid max-w-6xl gap-5 px-6 sm:grid-cols-2 lg:grid-cols-3">
      {features.map((f, at) => (
        <div key={at} className="flex flex-col gap-3 rounded-box border border-line p-7" style={card(at)}>
          <h2 className="text-[1.4rem] font-bold tracking-[-0.01em]">{f.title}</h2>
          <div className="leading-7 text-muted">{f.text}</div>
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
        <h1 className="text-[3.2rem] leading-[1] font-extrabold tracking-[-0.03em] sm:text-[5.2rem]">{p.title}</h1>
        {p.text.map((words, at) => (
          <p key={at} className="max-w-2xl text-lg leading-relaxed text-muted sm:text-xl">
            {words}
          </p>
        ))}
        <Buttons links={p.links} shape="pill" />
      </section>
      <Wave bars={56} />
      <Features features={p.features} card={() => ({ background: "linear-gradient(180deg, color-mix(in oklab, var(--color-accent) 12%, transparent), var(--color-surface))" })} />
      <Asks asks={p.asks} framed={false} />
    </div>
  );
}

function Studio(p: LandingProps) {
  return (
    <div className="one-landing flex flex-col gap-24 pb-24">
      <section className="mx-auto grid max-w-6xl items-center gap-12 px-6 pt-12 lg:grid-cols-2">
        <div className="flex flex-col items-start gap-6 text-left [&>div]:justify-start">
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
      {p.features.length > 0 && (
        <ol className="mx-auto grid w-full max-w-6xl list-none gap-3 px-6 sm:grid-cols-2 lg:grid-cols-3">
          {p.features.map((f, at) => (
            <li key={at} className="flex flex-col gap-3 rounded-box border border-line bg-surface p-6">
              <span className="font-mono text-xs text-muted">{String(at + 1).padStart(2, "0")}</span>
              <h2 className="text-[1.3rem] font-bold">{f.title}</h2>
              <div className="leading-7 text-muted">{f.text}</div>
            </li>
          ))}
        </ol>
      )}
      {p.asks.map((ask, at) => (
        <section key={at} id={ask.id} className="mx-auto w-full max-w-6xl scroll-mt-24 px-6">
          <div className="flex flex-wrap items-start gap-8 rounded-[24px] p-10 text-white sm:p-14 [&_form]:w-full" style={{ background: "linear-gradient(120deg, var(--one-landing-1), var(--color-accent) 50%, var(--one-landing-3))" }}>
            <div className="flex flex-1 basis-[22rem] flex-col gap-3">
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
        <h1 className="text-[3.4rem] leading-[0.98] font-light tracking-[-0.04em] sm:text-[6.4rem]">{p.title}</h1>
        {p.text.map((words, at) => (
          <p key={at} className="max-w-2xl text-lg leading-relaxed text-ink/85 sm:text-xl">
            {words}
          </p>
        ))}
        <Buttons links={p.links} shape="pill" />
      </section>
      <Features features={p.features} card={() => ({ background: "color-mix(in oklab, var(--color-surface) 55%, transparent)", backdropFilter: "blur(18px)" })} />
      <Asks asks={p.asks} framed />
    </div>
  );
}

function Grid(p: LandingProps) {
  const marquee = p.features.length ? p.features.map((f) => f.title) : [p.title];
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
      {p.features.length > 0 && (
        <div className="mx-auto w-full max-w-6xl px-6">
          <section className="grid gap-px border border-line bg-line sm:grid-cols-2 lg:grid-cols-3">
            {p.features.map((f, at) => (
              <div key={at} className="flex flex-col gap-3 bg-page p-8">
                <span className="font-mono text-xs text-accent">[ {String(at + 1).padStart(2, "0")} ]</span>
                <h2 className="text-[1.4rem] font-bold">{f.title}</h2>
                <div className="leading-7 text-muted">{f.text}</div>
              </div>
            ))}
          </section>
        </div>
      )}
      {p.asks.map((ask, at) => (
        <section key={at} id={ask.id} className="mx-auto w-full max-w-6xl scroll-mt-24 px-6">
          <div className="flex flex-wrap items-start gap-8 rounded-[6px] border p-10 [&_form]:flex-1 [&_form]:basis-[24rem]" style={{ borderColor: "var(--one-landing-1)", background: "linear-gradient(180deg, color-mix(in oklab, var(--one-landing-1) 10%, transparent), transparent)" }}>
            <div className="flex flex-1 basis-[22rem] flex-col gap-3">
              <span className="font-mono text-xs text-success">&gt; {ask.title.toLowerCase().replaceAll(" ", "_")}</span>
              <h2 className="text-[2.4rem] font-extrabold">{ask.title}</h2>
            </div>
            <div className="flex flex-1 basis-[24rem] flex-col gap-4 [&>p]:text-muted">{ask.children}</div>
          </div>
        </section>
      ))}
    </div>
  );
}

const designs: Record<string, (p: LandingProps) => ReactNode> = { waveform: Waveform, studio: Studio, aurora: Aurora, grid: Grid };

export function Landing(p: LandingProps) {
  const Design = designs[p.design] ?? Waveform;
  return (
    <>
      {p.before}
      <Design {...p} />
    </>
  );
}
