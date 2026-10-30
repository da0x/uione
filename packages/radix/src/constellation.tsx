// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The constellation design: a network of points drifting behind the site's name at
// night, drawn toward the pointer, and a blueprint grid in daylight. Under it, a band
// of everything it offers, then its groups of offerings, each rising into view; its
// statement, lit word by word as it's scrolled past; its steps, along a line that
// fills as they're read; and what it asks, in a panel with a turning edge. All of it
// in the theme's colors, and still for anyone who asks for less motion.

import { Children, isValidElement, useEffect, useRef } from "react";
import type { CSSProperties, PointerEvent, ReactNode, RefObject } from "react";
import { Link, Text } from "@uione/react";
import type { LandingFeature, LandingProps } from "@uione/react";
import { Icon, Opens, Over, grouped, heading, titles } from "./parts.js";

function still() {
  const asked = document.documentElement.dataset.motion;
  if (asked === "reduce") return true;
  if (asked === "standard") return false;
  return window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ?? false;
}

// Points drifting on the hero, joined when they come near each other, and drawn
// toward the pointer: systems finding each other. Drawn only while it's shown, which
// is at night, and on the page.
function useNetwork(canvas: RefObject<HTMLCanvasElement | null>) {
  useEffect(() => {
    const element = canvas.current;
    const context = element?.getContext("2d");
    if (!element || !context) return;
    let width = 0;
    let height = 0;
    let frame = 0;
    let visible = false;
    let line = "";
    let glow = "";
    const pointer = { x: -9999, y: -9999 };
    type Point = { x: number; y: number; dx: number; dy: number; size: number };
    let points: Point[] = [];

    // Its colors, the theme's, read whenever it comes into view, as the theme may have changed.
    const colors = () => {
      const style = getComputedStyle(element);
      line = style.color;
      glow = style.borderTopColor;
    };

    const resize = () => {
      const ratio = Math.min(window.devicePixelRatio || 1, 2);
      width = element.clientWidth;
      height = element.clientHeight;
      element.width = width * ratio;
      element.height = height * ratio;
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
      const count = Math.min(120, Math.round((width * height) / 11000));
      points = Array.from({ length: count }, () => ({
        x: Math.random() * width,
        y: Math.random() * height,
        dx: (Math.random() - 0.5) * 0.35,
        dy: (Math.random() - 0.5) * 0.35,
        size: Math.random() * 1.6 + 0.6,
      }));
    };

    const draw = () => {
      context.clearRect(0, 0, width, height);
      const reach = Math.min(150, width / 6);
      for (const p of points) {
        const px = pointer.x - p.x;
        const py = pointer.y - p.y;
        if (Math.hypot(px, py) < 180) {
          p.x += px * 0.004;
          p.y += py * 0.004;
        }
        p.x += p.dx;
        p.y += p.dy;
        if (p.x < 0 || p.x > width) p.dx *= -1;
        if (p.y < 0 || p.y > height) p.dy *= -1;
      }
      context.strokeStyle = line;
      context.lineWidth = 1;
      for (let i = 0; i < points.length; i++) {
        for (let j = i + 1; j < points.length; j++) {
          const a = points[i]!;
          const b = points[j]!;
          const apart = Math.hypot(a.x - b.x, a.y - b.y);
          if (apart > reach) continue;
          context.globalAlpha = (1 - apart / reach) * 0.35;
          context.beginPath();
          context.moveTo(a.x, a.y);
          context.lineTo(b.x, b.y);
          context.stroke();
        }
      }
      for (const p of points) {
        const near = Math.hypot(pointer.x - p.x, pointer.y - p.y) < 180;
        context.globalAlpha = near ? 0.95 : 0.7;
        context.fillStyle = near ? glow : line;
        context.beginPath();
        context.arc(p.x, p.y, near ? p.size + 1 : p.size, 0, Math.PI * 2);
        context.fill();
      }
      context.globalAlpha = 1;
    };

    const tick = () => {
      if (visible && !document.hidden) draw();
      frame = requestAnimationFrame(tick);
    };
    const moved = (event: globalThis.PointerEvent) => {
      const box = element.getBoundingClientRect();
      pointer.x = event.clientX - box.left;
      pointer.y = event.clientY - box.top;
    };
    const left = () => {
      pointer.x = -9999;
      pointer.y = -9999;
    };

    resize();
    // Where nothing says what's in view, it's drawn all the time.
    const shown = (now: boolean) => {
      visible = now;
      if (!visible) return;
      colors();
      if (element.clientWidth !== width || element.clientHeight !== height) resize();
      if (still()) draw();
    };
    const watcher = typeof IntersectionObserver === "undefined" ? undefined : new IntersectionObserver(([entry]) => shown(entry?.isIntersecting ?? false));
    if (watcher) watcher.observe(element);
    else shown(true);
    window.addEventListener("resize", resize);
    if (!still()) {
      window.addEventListener("pointermove", moved);
      document.addEventListener("pointerleave", left);
      frame = requestAnimationFrame(tick);
    }
    return () => {
      cancelAnimationFrame(frame);
      watcher?.disconnect();
      window.removeEventListener("resize", resize);
      window.removeEventListener("pointermove", moved);
      document.removeEventListener("pointerleave", left);
    };
  }, [canvas]);
}

// How far the page has scrolled past each part marked data-progress, as --progress
// from 0 to 1, and how far down the hero is, as --scrolled.
function useScroll(root: RefObject<HTMLDivElement | null>) {
  useEffect(() => {
    const element = root.current;
    if (!element) return;
    const tracked = Array.from(element.querySelectorAll<HTMLElement>("[data-progress]"));
    let frame = 0;
    const update = () => {
      frame = 0;
      const tall = window.innerHeight;
      element.style.setProperty("--scrolled", String(Math.min(window.scrollY / tall, 1)));
      for (const part of tracked) {
        const box = part.getBoundingClientRect();
        const progress = (tall * 0.85 - box.top) / (box.height + tall * 0.35);
        part.style.setProperty("--progress", String(Math.max(0, Math.min(1, progress))));
      }
    };
    const scrolled = () => {
      if (!frame) frame = requestAnimationFrame(update);
    };
    update();
    window.addEventListener("scroll", scrolled, { passive: true });
    window.addEventListener("resize", scrolled);
    return () => {
      cancelAnimationFrame(frame);
      window.removeEventListener("scroll", scrolled);
      window.removeEventListener("resize", scrolled);
    };
  }, [root]);
}

// Each part marked data-reveal rises into view the first time it's scrolled to.
function useReveal(root: RefObject<HTMLDivElement | null>) {
  useEffect(() => {
    const element = root.current;
    if (!element) return;
    const parts = element.querySelectorAll("[data-reveal]");
    // Where nothing says what's in view, everything's shown at once.
    if (typeof IntersectionObserver === "undefined") {
      parts.forEach((part) => part.setAttribute("data-shown", ""));
      return;
    }
    const watcher = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (!entry.isIntersecting) continue;
          entry.target.setAttribute("data-shown", "");
          watcher.unobserve(entry.target);
        }
      },
      { threshold: 0.15 },
    );
    parts.forEach((part) => watcher.observe(part));
    return () => watcher.disconnect();
  }, [root]);
}

// The glow on a card follows the pointer across it.
function follow(event: PointerEvent<HTMLElement>) {
  const box = event.currentTarget.getBoundingClientRect();
  event.currentTarget.style.setProperty("--x", `${event.clientX - box.left}px`);
  event.currentTarget.style.setProperty("--y", `${event.clientY - box.top}px`);
}

// The site's name, its first word in the page's ink with a shine passing over it, and
// the rest in the accent; a name of one word is all accent.
function Name({ name }: { name: string }) {
  const space = name.indexOf(" ");
  const first = space > 0 ? name.slice(0, space) : "";
  const rest = space > 0 ? name.slice(space + 1) : name;
  return (
    <h1 className="one-constellation-name" aria-label={name}>
      {first && <span className="one-constellation-first">{first}</span>}
      <span className="one-constellation-rest">{rest}</span>
    </h1>
  );
}

// An offering on its own card; one holding offerings of its own spreads them beside it.
function Card({ feature, at }: { feature: LandingFeature; at: number }) {
  const holds = feature.features.length > 0;
  return (
    <article className={`one-constellation-card ${holds ? "one-constellation-wide" : ""}`} data-reveal style={{ "--delay": `${holds ? 0 : at * 110}ms` } as CSSProperties} onPointerMove={follow}>
      <div className="one-constellation-card-head">
        {feature.icon && <Icon name={feature.icon} className="one-constellation-icon" />}
        {holds && <span className="one-constellation-number">{String(at + 1).padStart(2, "0")}</span>}
        <Over className="one-constellation-card-over">{feature.overline}</Over>
        <h3 style={heading}>
          <Opens link={feature.link}>{feature.title}</Opens>
        </h3>
        <div className="one-constellation-muted empty:hidden">{feature.text}</div>
      </div>
      {holds && (
        <div className="one-constellation-held">
          {feature.features.map((inner, place) => (
            <div key={place} className="one-constellation-held-one" style={{ "--delay": `${150 + place * 120}ms`, ...(inner.link ? { position: "relative" } : {}) } as CSSProperties}>
              <h4 style={heading}>
                <Opens link={inner.link}>{inner.title}</Opens>
              </h4>
              <div className="one-constellation-muted">{inner.text}</div>
            </div>
          ))}
        </div>
      )}
    </article>
  );
}

function Heading({ overline, title, text }: { overline?: ReactNode; title: string; text?: ReactNode }) {
  return (
    <header className="one-constellation-heading" data-reveal>
      <Over>{overline}</Over>
      <h2 style={heading}>{title}</h2>
      {text !== undefined && <div className="one-constellation-intro empty:hidden">{text}</div>}
    </header>
  );
}

// Its statement, a word at a time, each lit as the page passes it.
function Statement({ statement }: { statement: ReactNode }) {
  const words = typeof statement === "string" ? statement.split(" ") : undefined;
  return (
    <section className="one-constellation-statement" data-progress>
      <p style={heading}>
        {words
          ? words.map((word, at) => (
              <span key={at} style={{ "--at": at / words.length } as CSSProperties}>
                {word}{" "}
              </span>
            ))
          : statement}
      </p>
    </section>
  );
}

// What it asks: words and links alone in a panel with a turning edge, its links as
// buttons; anything more, like a form or a table, on the page as it is.
function Ask({ ask }: { ask: LandingProps["asks"][number] }) {
  const simple = Children.toArray(ask.children).every((part) => isValidElement(part) && (part.type === Text || part.type === Link));
  return (
    <section id={ask.id} className="one-constellation-section">
      {simple ? (
        <div className="one-constellation-ask" data-reveal>
          <Over>{ask.overline}</Over>
          <h2 style={heading}>{ask.title}</h2>
          <div className="one-constellation-ask-inside">{ask.children}</div>
        </div>
      ) : (
        <>
          <Heading overline={ask.overline} title={ask.title} />
          <div className="flex flex-col gap-5">{ask.children}</div>
        </>
      )}
    </section>
  );
}

export function Constellation(p: LandingProps) {
  const root = useRef<HTMLDivElement>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  useNetwork(canvas);
  useScroll(root);
  useReveal(root);
  const band = titles(p.features);
  return (
    <div ref={root} className="one-landing one-constellation">
      <section className="one-constellation-hero">
        <canvas ref={canvas} className="one-constellation-network" aria-hidden="true" />
        <div className="one-constellation-grid" aria-hidden="true" />
        <div className="one-constellation-orb one-constellation-orb-one" aria-hidden="true" />
        <div className="one-constellation-orb one-constellation-orb-two" aria-hidden="true" />
        <div className="one-constellation-hero-inside">
          <Over>{p.overline}</Over>
          <Name name={p.name} />
          <p className="one-constellation-lead">{p.title}</p>
          {p.text.map((words, at) => (
            <p key={at} className="one-constellation-text">
              {words}
            </p>
          ))}
          {p.links.length > 0 && (
            <div className="one-constellation-links">
              {p.links.map((link, at) => (
                <a key={at} href={link.href} onClick={link.onClick} className={`one-constellation-button ${at === 0 ? "one-constellation-main" : ""}`}>
                  {link.label}
                </a>
              ))}
            </div>
          )}
        </div>
        <div className="one-constellation-hint" aria-hidden="true">
          <span />
        </div>
      </section>

      {band.length > 0 && (
        <div className="one-constellation-band" aria-hidden="true">
          <div className="one-constellation-track">
            {[0, 1].map((copy) =>
              band.map((title, at) => (
                <span key={`${copy}-${at}`} className="one-constellation-band-item" style={heading}>
                  {title}
                  <i />
                </span>
              )),
            )}
          </div>
        </div>
      )}

      {grouped(p.features).map((group, at) => (
        <section key={at} className="one-constellation-section">
          {group.heading && <Heading overline={group.heading.overline} title={group.heading.title} text={group.heading.text} />}
          <div className={group.features.some((f) => f.features.length > 0) ? "one-constellation-wides" : "one-constellation-cards"}>
            {group.features.map((feature, place) => (
              <Card key={place} feature={feature} at={place} />
            ))}
          </div>
        </section>
      ))}

      {p.statement !== undefined && <Statement statement={p.statement} />}

      {p.steps.map((s, at) => (
        <section key={at} id={s.id} className="one-constellation-section">
          <Heading overline={s.overline} title={s.title} text={s.text} />
          <div className="one-constellation-steps" data-progress>
            <div className="one-constellation-steps-line" aria-hidden="true" />
            <ol>
              {s.steps.map((step, number) => (
                <li key={number} className="one-constellation-step" data-reveal>
                  <span className="one-constellation-step-number" style={heading}>
                    {String(number + 1).padStart(2, "0")}
                  </span>
                  <div>
                    <h3 style={heading}>{step.title}</h3>
                    <div className="one-constellation-muted">{step.text}</div>
                  </div>
                </li>
              ))}
            </ol>
          </div>
        </section>
      ))}

      {p.asks.map((ask, at) => (
        <Ask key={at} ask={ask} />
      ))}
    </div>
  );
}
