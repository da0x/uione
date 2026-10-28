// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// @uione/radix: a styled component set for uione apps. It only draws. Everything a
// screen does, from routing to commands to hiding values that aren't live, happens
// in @uione/react before these components are called.
//
// The dialog is built on Radix, which gets focus, Escape and screen readers right.
// Everything else is plain elements with Tailwind classes, compiled into
// dist/styles.css so an app only has to import that one file.

// IBM Plex: an engineering face, served with the app rather than from a font CDN.
import "@fontsource-variable/ibm-plex-sans";
import "@fontsource/ibm-plex-mono/400.css";
import "@fontsource/ibm-plex-mono/500.css";
// The faces a paper is set in beyond the app's own: book's Literata and lyric's Fraunces.
import "@fontsource-variable/literata/wght.css";
import "@fontsource-variable/literata/wght-italic.css";
import "@fontsource-variable/fraunces/wght.css";
import "@fontsource-variable/fraunces/wght-italic.css";
import { Children, Fragment, useEffect, useId, useMemo, useRef, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import type { ButtonProps, ComponentSet } from "@uione/react";
import { Surface } from "@uione/react";
import { highlight, highlightCodeBlocks } from "./highlight.js";
import { MarkdownField, MarkdownText } from "./markdown.js";
import { nameStyles, setCodeDisplay, tabWidths, useCodeDisplay } from "./display.js";
import { DisplaySettings } from "./accessibility.js";
import { Sheet } from "./sheet.js";
import { ThemeToggle } from "./theme.js";
import { Landing } from "./landing.js";
import { buttonIcons } from "./icons.js";
import { Chart, Charts, Monitor, Progress } from "./charts.js";
import type { CodeDisplay, NameStyle } from "./display.js";

// A card's dot: gray not started, blue waiting, orange working, green done, red failed.
const statusColors: Record<string, string> = {
  inactive: "var(--color-muted)",
  waiting: "var(--color-accent)",
  working: "#d9730d",
  success: "var(--color-success)",
  done: "var(--color-success)",
  failed: "var(--color-danger)",
};

// A tally's parts, by what they mean: done, solid; under way, a tint; failed, red.
const tallyRank: Record<string, number> = { done: 0, partial: 1, failed: 2, none: 3 };
const tallyLook: Record<string, CSSProperties> = {
  done: { background: "var(--color-accent)" },
  partial: { background: "color-mix(in oklab, var(--color-accent) 35%, var(--color-surface))" },
  failed: { background: "var(--color-danger)" },
};

// The box code sits in, on its own or inside a page of docs.
// Whether the page is a phone's width, narrower than Tailwind's sm, as it changes.
// Where it can't be told, as in tests, it isn't.
function useNarrow(): boolean {
  const query = "(max-width: 39.99rem)";
  const [narrow, setNarrow] = useState(() => typeof window !== "undefined" && typeof window.matchMedia === "function" && window.matchMedia(query).matches);
  useEffect(() => {
    if (typeof window.matchMedia !== "function") return;
    const media = window.matchMedia(query);
    const change = () => setNarrow(media.matches);
    change();
    media.addEventListener("change", change);
    return () => media.removeEventListener("change", change);
  }, []);
  return narrow;
}

// How close a table's rows sit: the reader's pick, from the button on a table's
// toolbar, kept in this browser and followed by every table on the site.
type Density = "comfortable" | "compact";
const densityKey = "uione-density";
const densityChange = "uione-density";

function storedDensity(): Density {
  try {
    return globalThis.localStorage?.getItem(densityKey) === "compact" ? "compact" : "comfortable";
  } catch {
    return "comfortable"; // storage can be off, as in a private window
  }
}

function useDensity(): [Density, (next: Density) => void] {
  const [density, setDensity] = useState<Density>(storedDensity);
  useEffect(() => {
    const follow = () => setDensity(storedDensity());
    window.addEventListener(densityChange, follow);
    return () => window.removeEventListener(densityChange, follow);
  }, []);
  const choose = (next: Density) => {
    try {
      globalThis.localStorage?.setItem(densityKey, next);
    } catch {
      // the pick lasts until the page is left
    }
    setDensity(next);
    window.dispatchEvent(new Event(densityChange));
  };
  return [density, choose];
}

function DensityToggle({ density, onChange }: { density: Density; onChange: (next: Density) => void }) {
  const compact = density === "compact";
  return (
    <div className="inline-flex rounded-control border border-line bg-surface p-0.5 shadow-panel">
      <button
        type="button"
        aria-pressed={compact}
        aria-label="Compact rows"
        title="Compact rows"
        onClick={() => onChange(compact ? "comfortable" : "compact")}
        className={`inline-flex items-center rounded-control px-2 py-1 focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden ${
          compact ? "bg-accent text-accent-ink" : "text-muted hover:text-ink"
        }`}
      >
        <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
          <path d="M2.5 3.5h11M2.5 6.5h11M2.5 9.5h11M2.5 12.5h11" />
        </svg>
      </button>
    </div>
  );
}

// The column a row is called by, on a phone: the one headed Title or Name, or else
// the first that isn't only numbers, like a #.
function leadColumn(columns: string[], rows: { cells: ReactNode[] }[]): number {
  const named = columns.findIndex((column) => /^(title|name)$/i.test(column.trim()));
  if (named >= 0) return named;
  const plain = columns.findIndex((_, i) => !rows.every((row) => typeof row.cells[i] === "number" || (typeof row.cells[i] === "string" && /^\d*$/.test(row.cells[i] as string))));
  return plain >= 0 ? plain : 0;
}

// Whether the first column is a number said before the lead, like #12.
function numbered(columns: string[], lead: number): boolean {
  return lead > 0 && /^(#|number|no\.?)$/i.test((columns[0] ?? "").trim());
}

// A cell with nothing in it, left off a phone's line rather than shown as a gap.
function blank(cell: ReactNode): boolean {
  return cell === null || cell === undefined || cell === false || cell === "" || (Array.isArray(cell) && cell.length === 0);
}

const box = "one-code overflow-x-auto rounded-box border border-line bg-surface p-4 text-[0.84rem] leading-6 shadow-panel [&_pre]:!bg-transparent";

// The reader's controls for how code looks: how wide a tab is, and how names are
// written. One change applies to every piece of code on the page.
// Octicons' link-external (MIT): the link leaves the app, for another site.
function ExternalMark() {
  return (
    <svg viewBox="0 0 16 16" width="12" height="12" fill="currentColor" aria-hidden="true" className="shrink-0 opacity-70">
      <path d="M3.75 2h3.5a.75.75 0 0 1 0 1.5h-3.5a.25.25 0 0 0-.25.25v8.5c0 .138.112.25.25.25h8.5a.25.25 0 0 0 .25-.25v-3.5a.75.75 0 0 1 1.5 0v3.5A1.75 1.75 0 0 1 12.25 14h-8.5A1.75 1.75 0 0 1 2 12.25v-8.5C2 2.784 2.784 2 3.75 2Zm6.854-1h4.146a.25.25 0 0 1 .25.25v4.146a.25.25 0 0 1-.427.177L13.03 4.03 9.28 7.78a.751.751 0 0 1-1.042-.018.751.751 0 0 1-.018-1.042l3.75-3.75-1.543-1.543A.25.25 0 0 1 10.604 1Z" />
    </svg>
  );
}

function CodeToolbar({ display }: { display: CodeDisplay }) {
  const names = useId();
  return (
    <div className="flex flex-wrap items-center justify-end gap-x-5 gap-y-2 px-3 py-2 text-xs text-muted">
      <div role="group" aria-label="Tab width" className="flex items-center gap-1">
        <span className="mr-1">Tab width</span>
        {tabWidths.map((width) => (
          <button
            key={width}
            type="button"
            aria-pressed={display.tabWidth === width}
            onClick={() => setCodeDisplay({ tabWidth: width })}
            className={`rounded-md px-1.5 py-0.5 tabular-nums ${display.tabWidth === width ? "bg-accent-soft font-medium text-accent" : "hover:text-ink"}`}
          >
            {width}
          </button>
        ))}
      </div>
      <label htmlFor={names} className="flex items-center gap-1.5">
        Names
        <select
          id={names}
          value={display.names}
          onChange={(event) => setCodeDisplay({ names: event.target.value as NameStyle })}
          className="rounded-md border border-line bg-surface px-1.5 py-0.5 text-ink"
        >
          {nameStyles.map((style) => (
            <option key={style} value={style}>
              {style === "default" ? "Default" : style}
            </option>
          ))}
        </select>
      </label>
    </div>
  );
}

// Buttons have real presence: a solid accent for what the screen is for, an
// outlined panel for the rest.
// Icons for choices whose names say what they are, drawn in the text's own color.
const choiceIcons: Record<string, ReactNode> = {
  private: (
    <svg viewBox="0 0 24 24" width="26" height="26" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="5" y="11" width="14" height="10" rx="2" />
      <path d="M8 11V7.5a4 4 0 0 1 8 0V11" />
    </svg>
  ),
  public: (
    <svg viewBox="0 0 24 24" width="26" height="26" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="12" cy="12" r="9" />
      <path d="M3 12h18M12 3c2.5 2.6 3.8 5.6 3.8 9s-1.3 6.4-3.8 9c-2.5-2.6-3.8-5.6-3.8-9S9.5 5.6 12 3z" />
    </svg>
  ),
};

// A choice of a few is a row of cards to pick from; more than that is a list.
const fewChoices = 3;

// What a list is filtered by, like Opened by me, with a cross that clears it.
function FilterChip({ label, onClear }: { label: string; onClear: () => void }) {
  return (
    <span className="inline-flex items-center gap-1 rounded-full bg-accent/10 py-0.5 pr-1 pl-2.5 text-xs font-medium text-accent">
      {label}
      <button type="button" onClick={onClear} aria-label={`Clear ${label}`} className="rounded-full px-1 hover:bg-accent/20">
        ×
      </button>
    </span>
  );
}

// The pictures a switch draws for what it switches between: rows for a table, and
// columns of cards for a board.
const switchIcons: Record<string, ReactNode> = {
  // A row of stages along a line, the classic deploy screen's.
  pipeline: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden="true">
      <circle cx="3" cy="5" r="1.5" />
      <circle cx="8" cy="5" r="1.5" />
      <circle cx="13" cy="5" r="1.5" />
      <path d="M4.5 5h2M9.5 5h2M2 11h12" />
    </svg>
  ),
  // Notes placed anywhere, between a column's lines.
  whiteboard: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden="true">
      <rect x="2" y="2.5" width="12" height="11" rx="1.5" />
      <path d="M8 2.5v11" strokeDasharray="1.5 1.5" />
      <rect x="3.5" y="4.5" width="3" height="3" rx="0.5" />
      <rect x="9.5" y="8" width="3" height="3" rx="0.5" />
    </svg>
  ),
  table: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden="true">
      <rect x="2" y="2.5" width="12" height="11" rx="1.5" />
      <path d="M2 6h12M2 9.5h12" />
    </svg>
  ),
  board: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden="true">
      <rect x="2" y="2.5" width="3.5" height="11" rx="1" />
      <rect x="6.25" y="2.5" width="3.5" height="7" rx="1" />
      <rect x="10.5" y="2.5" width="3.5" height="9" rx="1" />
    </svg>
  ),
};

const button: Record<NonNullable<ButtonProps["kind"]>, string> = {
  primary: "bg-accent text-accent-ink shadow-panel hover:bg-accent-hover",
  secondary: "border border-line bg-surface text-ink shadow-panel hover:bg-sunken",
  danger: "bg-danger text-white shadow-panel hover:opacity-90",
};
// How urgent a row or a box of details is, as Trac colored a ticket's priority: an
// edge down its left side, and nothing else, so it's seen without being in the way.
// 3 is plain, as Trac's normal was. A card draws its edge itself, inside its border.
const tones: Record<number, string> = {
  1: "shadow-[inset_3px_0_0_var(--color-tone-1-edge)]",
  2: "shadow-[inset_3px_0_0_var(--color-tone-2-edge)]",
  4: "shadow-[inset_3px_0_0_var(--color-tone-4-edge)]",
  5: "shadow-[inset_3px_0_0_var(--color-tone-5-edge)]",
};
const toned = (tone: number | undefined) => (tone ? (tones[tone] ?? "") : "");
const edged = (tone: number | undefined) => (tone && tones[tone] ? `var(--color-tone-${tone}-edge)` : undefined);

// Who's working something, each a small round face: their picture, or their first
// letter, named for whoever can't see it, close together as a group.
function Faces({ people }: { people: { name: string; picture?: string }[] }) {
  return (
    <span data-people="" className="flex shrink-0 items-center -space-x-1">
      {people.map((person, i) => (
        <span key={i} title={person.name} className="inline-flex">
          {person.picture ? (
            <img src={person.picture} alt="" loading="lazy" referrerPolicy="no-referrer" className="one-person size-5 rounded-full border border-surface object-cover" />
          ) : (
            <span
              aria-hidden="true"
              className="grid size-5 place-items-center rounded-full border border-surface text-[0.68rem] font-semibold text-accent"
              style={{ background: "color-mix(in oklab, var(--color-accent) 14%, var(--color-surface))" }}
            >
              {person.name.trim().charAt(0).toUpperCase()}
            </span>
          )}
          <span className="sr-only">{person.name}</span>
        </span>
      ))}
    </span>
  );
}

const pressable = "inline-flex items-center justify-center gap-2 rounded-control px-4 py-2 text-sm font-medium transition-colors disabled:opacity-50";

// A label's hue: the one people expect for the usual ones, like red for a bug, and
// for any other one picked from its words, so it's the same each time.
const knownHues: Record<string, number> = {
  bug: 2,
  defect: 2,
  regression: 2,
  security: 340,
  feature: 152,
  enhancement: 152,
  documentation: 212,
  docs: 212,
  question: 285,
  "good first issue": 262,
  "help wanted": 168,
  duplicate: 220,
  wontfix: 220,
};

export function labelHue(label: string): number {
  const known = knownHues[label.trim().toLowerCase()];
  if (known !== undefined) return known;
  let hash = 0;
  for (const c of label.trim().toLowerCase()) hash = (hash * 31 + c.codePointAt(0)!) >>> 0;
  return hash % 360;
}

export const radix: ComponentSet = {
  Page: ({ name, icon, home, nav, title, untitled, account, heading, crumbs, subtitle, badges, footer, display, children }) => (
    // Clipped across, so a hero's grid, drawn past the page's sides, never widens it.
    <div className="min-h-screen overflow-x-clip bg-page text-ink">
      {/* The first thing a keyboard reaches: past the header, straight to the page's own. */}
      <a href="#content" className="sr-only z-30 rounded-control bg-accent px-3 py-2 text-accent-ink focus:not-sr-only focus:fixed focus:left-4 focus:top-3">
        Skip to content
      </a>
      {/* The header stays in view, over a blur of the page as it scrolls. The page
          you're on is underlined in the accent, along the header's edge. */}
      <header className="sticky top-0 z-20 border-b border-line bg-page/80 backdrop-blur-md">
        {/* On a phone, the navigation takes a row of its own under the name. */}
        <div className="flex flex-wrap items-center gap-x-7 gap-y-1 px-4 pt-3 sm:px-6 sm:pt-0 lg:px-8">
          <a {...home} className="flex shrink-0 items-center gap-2.5 py-0 text-[1.05rem] font-semibold tracking-[-0.01em] sm:py-3.5">
            {icon && <img src={icon} alt="" className="h-7 w-7" />}
            {name}
            {badges
              ?.filter((badge) => badge.count > 0)
              .map((badge) => (
                <span key={badge.label} className="rounded-full bg-accent px-1.5 text-xs font-medium text-accent-ink tabular-nums">
                  {badge.count}
                  <span className="sr-only"> {badge.label}</span>
                </span>
              ))}
          </a>
          <nav className="order-last -mb-px flex w-full flex-wrap gap-x-5 text-sm sm:order-none sm:w-auto">
            {nav.map((item) => (
              <a
                key={item.href}
                href={item.href}
                onClick={item.onClick}
                aria-current={item.current ? "page" : undefined}
                className={`border-b-2 py-3 transition-colors sm:py-4 ${item.current ? "border-accent font-medium text-ink" : "border-transparent text-muted hover:text-ink"}`}
              >
                {item.label}
              </a>
            ))}
          </nav>
          <div className="ml-auto flex items-center gap-2 text-sm">
            {display && <DisplaySettings />}
            <ThemeToggle />
            {account}
          </div>
        </div>
      </header>
      {/* The page is as wide as the window, for tables, code and editors; text keeps
          its own reading width. */}
      {/* A page with a heading of its own has room above it; one whose first thing
          is its own header, like a project's, starts close under the navigation. */}
      <main id="content" tabIndex={-1} className={`px-4 pb-8 outline-none sm:px-6 sm:pb-10 lg:px-8 ${!untitled ? "pt-5 sm:pt-6" : "pt-4 sm:pt-5"}`}>
        {crumbs}
        {/* The title, and a screen's own buttons at the end of its row. */}
        <div className={`one-heading flex flex-wrap items-start gap-3 ${!untitled ? "mb-5" : ""}`}>
          {/* The title, and under it what the screen says about itself, like a project's summary. */}
          <div className="mr-auto flex min-w-0 max-w-3xl flex-col gap-1">
            {!untitled && <h1 className="text-[1.75rem] leading-tight font-semibold tracking-[-0.025em]">{title || "\u00a0"}</h1>}
            {subtitle}
          </div>
          <div className="ml-auto flex items-center gap-2 pt-1 empty:hidden">{heading}</div>
        </div>
        <div className="flex flex-col gap-7">{children}</div>
      </main>
      {/* Quietly, at the foot of the page: in one line, its parts apart, or in
          columns, each a section with a smaller heading. Its links are quiet too,
          and leave the site without the mark that says so. */}
      {footer && (
        <footer
          className={`border-t border-line px-4 py-4 text-xs text-muted sm:px-6 lg:px-8 [&_a]:font-normal [&_a]:text-muted [&_a]:underline-offset-4 [&_a:hover]:text-ink [&_a:hover]:underline [&_a_svg]:hidden ${
            footer.layout === "columns"
              ? "grid gap-6 sm:grid-cols-[repeat(auto-fit,minmax(10rem,1fr))] [&_h2]:text-xs [&_h2]:font-semibold [&_h2]:tracking-normal [&_h2]:text-ink [&_section]:gap-1.5 [&_p]:text-xs [&_p]:leading-5 [&_p]:text-muted"
              : "flex flex-wrap items-baseline gap-x-2 gap-y-1 [&>*]:inline [&>*]:text-xs [&>*]:leading-5 [&>*]:text-muted [&>*+*]:before:mr-2 [&>*+*]:before:content-['·']"
          }`}
        >
          {footer.children}
        </footer>
      )}
    </div>
  ),

  Account: ({ name, ready, error, onSignIn, onSignOut }) =>
    !ready ? null : (
      <span className="flex items-center gap-3">
        {error && (
          <span role="alert" className="text-danger">
            {error}
          </span>
        )}
        {name === undefined ? (
          <button type="button" onClick={onSignIn} className={`${pressable} px-3.5 py-1.5 ${button.primary}`}>
            Sign in
          </button>
        ) : (
          <>
            <span className="text-muted">{name}</span>
            <button type="button" onClick={onSignOut} className="text-muted hover:text-ink">
              Sign out
            </button>
          </>
        )}
      </span>
    ),

  // A button for each way, its mark before its name, as wide as the dialog.
  SignIn: ({ methods, busy, error, onChoose }) => (
    <div className="flex flex-col gap-2.5">
      {methods.map(({ id, name, Mark }) => (
        <button
          key={id}
          type="button"
          disabled={busy !== undefined}
          onClick={() => onChoose(id)}
          className={`${pressable} w-full justify-start gap-3 py-2.5 ${button.secondary}`}
        >
          {Mark && <Mark />}
          {busy === id ? `Signing in with ${name}…` : `Continue with ${name}`}
        </button>
      ))}
      {error && (
        <p role="alert" className="text-sm text-danger">
          {error}
        </p>
      )}
    </div>
  ),

  Hero: ({ title, children }) => (
    // The hero's links become buttons in a row: the first is what the page is for.
    <section className="one-blueprint flex flex-col gap-7 pt-10 pb-6 sm:pt-16 sm:pb-10">
      <h1 className="max-w-4xl text-[2.6rem] leading-[1.04] font-semibold tracking-[-0.035em] sm:text-[3.75rem]">{title}</h1>
      <div
        className={[
          "flex max-w-2xl flex-wrap items-center gap-x-3 gap-y-4 text-[1.15rem] leading-relaxed text-muted [&>p]:basis-full",
          "[&>a]:inline-flex [&>a]:items-center [&>a]:rounded-control [&>a]:border [&>a]:border-line [&>a]:bg-surface [&>a]:px-4 [&>a]:py-2.5",
          "[&>a]:text-[0.95rem] [&>a]:text-ink [&>a]:shadow-panel [&>a:hover]:bg-sunken [&>a:hover]:no-underline",
          "[&>a:first-of-type]:border-accent [&>a:first-of-type]:bg-accent [&>a:first-of-type]:text-accent-ink [&>a:first-of-type:hover]:bg-accent-hover",
        ].join(" ")}
      >
        {children}
      </div>
    </section>
  ),

  // A section with a hue has its heading in it, and the accent's place inside, so
  // its links and tables follow.
  Section: ({ title, id, hue, children }) => (
    <section id={id} data-hue={hue} className="flex scroll-mt-24 flex-col gap-4">
      <h2 className={`text-[1.5rem] font-semibold tracking-[-0.02em]${hue ? " text-accent" : ""}`}>{title}</h2>
      {children}
    </section>
  ),

  Text: ({ children }) => <p className="max-w-3xl leading-7 text-ink/90">{children}</p>,

  Link: ({ href, onClick, external, icon, children }) =>
    // Drawn as an icon, like a button's, its words still naming it.
    icon && buttonIcons[icon] ? (
      <a
        href={href}
        onClick={onClick}
        aria-label={typeof children === "string" ? children : undefined}
        title={typeof children === "string" ? children : undefined}
        className={`self-start ${pressable} ${button.secondary} !px-2`}
      >
        {buttonIcons[icon]}
      </a>
    ) : external ? (
      <a
        href={href}
        target="_blank"
        rel="noreferrer"
        title="Opens in a new tab"
        className="inline-flex items-center gap-1 font-medium text-accent decoration-accent/40 underline-offset-4 hover:underline"
      >
        {children}
        <ExternalMark />
      </a>
    ) : (
      <a href={href} onClick={onClick} className="font-medium text-accent decoration-accent/40 underline-offset-4 hover:underline">
        {children}
      </a>
    ),

  Code: function RadixCode({ lang, source }) {
    const display = useCodeDisplay();
    const html = useMemo(() => highlight(source, lang, display.names), [source, lang, display.names]);
    if (html === undefined) {
      return (
        <pre className={box}>
          <code>{source}</code>
        </pre>
      );
    }
    return (
      <div className="one-code overflow-hidden rounded-box border border-line bg-surface shadow-panel">
        <div className="border-b border-line bg-sunken">
          <CodeToolbar display={display} />
        </div>
        <div
          className="overflow-x-auto p-4 text-[0.84rem] leading-6 [&_pre]:!bg-transparent"
          style={{ tabSize: display.tabWidth }}
          dangerouslySetInnerHTML={{ __html: html }}
        />
      </div>
    );
  },

  Pages: function RadixPages({ pages, title, html, document }) {
    const display = useCodeDisplay();
    const body = useMemo(() => (html === undefined ? undefined : highlightCodeBlocks(html, box, display.names)), [html, display.names]);
    const hasCode = body?.includes("one-code") ?? false;
    return (
      <div className={pages.length > 1 ? "grid gap-8 md:grid-cols-[12rem_minmax(0,1fr)] md:gap-10" : undefined}>
        {/* On a phone, the pages are a row of tabs above the page. One page is just the page. */}
        {pages.length > 1 && <nav aria-label="Pages" className="flex flex-wrap gap-1 text-sm md:flex-col">
          {pages.map((page) => (
            <a
              key={page.href}
              href={page.href}
              onClick={page.onClick}
              aria-current={page.current ? "page" : undefined}
              className={`rounded-control px-3 py-1.5 transition-colors ${page.current ? "bg-accent-soft font-medium text-accent" : "text-muted hover:bg-sunken hover:text-ink"}`}
            >
              {page.title}
            </a>
          ))}
        </nav>}
        {body === undefined ? (
          <p className="text-muted">There's no page here.</p>
        ) : (
          <div className={document ? "mx-auto w-full min-w-0 max-w-[46rem] rounded-box border border-line bg-surface px-5 py-10 shadow-panel sm:px-14 sm:py-14" : "min-w-0 max-w-3xl"}>
            {hasCode && (
              <div className="mb-6 rounded-box border border-line bg-surface shadow-panel">
                <CodeToolbar display={display} />
              </div>
            )}
            <article
              aria-label={title}
              className={document ? "one-prose one-document" : "one-prose"}
              style={{ tabSize: display.tabWidth }}
              dangerouslySetInnerHTML={{ __html: body }}
            />
          </div>
        )}
      </div>
    );
  },

  // A menu down the side on a wide screen, like a project's settings; on a phone, a
  // row of tabs above the page.
  Menu: ({ links, children }) => (
    <div className="grid gap-8 md:grid-cols-[13rem_minmax(0,1fr)] md:gap-10">
      <nav aria-label="Menu" className="flex flex-wrap gap-1 text-sm md:flex-col">
        {links.map((item) => (
          <a
            key={item.href}
            href={item.href}
            onClick={item.onClick}
            aria-current={item.current ? "page" : undefined}
            className={`rounded-control px-3 py-1.5 transition-colors ${item.current ? "bg-accent-soft font-medium text-accent" : "text-muted hover:bg-sunken hover:text-ink"}`}
          >
            {item.title}
          </a>
        ))}
      </nav>
      <div className="flex min-w-0 flex-col gap-10">{children}</div>
    </div>
  ),

  Table: function RadixTable({ status, columns, rows, error, tabs, search, pages, reorder, tools, filtered, hue, empty }) {
    // The row being dragged, and the place it would go.
    const [dragged, setDragged] = useState<number>();
    const [over, setOver] = useState<number>();
    const narrow = useNarrow();
    const [density, setDensity] = useDensity();
    // A cell's room above and below, and a phone's entry's, by how close rows sit.
    const room = density === "compact" ? "py-0.5" : "py-1";
    const entry = density === "compact" ? "py-1.5" : "py-2.5";
    const pictured = rows.some((row) => row.picture);
    // A row's small picture, before its cells.
    const thumb = (row: (typeof rows)[number]) => (
      <span className="block size-10 shrink-0 overflow-hidden rounded-control bg-sunken">
        {row.picture?.source && <img src={row.picture.source} alt="" className={`h-full w-full ${row.picture.fit ? "object-contain" : "object-cover"}`} />}
      </span>
    );
    return (
    <div data-hue={hue} className="overflow-x-auto rounded-box border border-line bg-surface shadow-panel">
      {/* Its rows by a choice, like Open and Closed, each with how many there are,
          and the search box at the end of the same row. */}
      {(tabs || search || tools || filtered) && (
        <div className="flex flex-wrap items-end gap-x-3 border-b border-line px-2">
          {tabs && (
            <div role="tablist" className="flex gap-1 pt-1.5">
              {tabs.map((tab) => (
                <button
                  key={tab.label}
                  type="button"
                  role="tab"
                  aria-selected={tab.selected}
                  onClick={tab.onSelect}
                  className={`-mb-px inline-flex items-center gap-1.5 border-b-2 px-2.5 pt-1 pb-1.5 text-sm ${tab.selected ? "border-accent font-medium text-ink" : "border-transparent text-muted hover:text-ink"}`}
                >
                  {tab.label}
                  <span className="rounded-full bg-sunken px-1.5 text-xs tabular-nums text-muted">{tab.count}</span>
                </button>
              ))}
            </div>
          )}
          {(search || tools || filtered) && (
            <div className="ml-auto flex w-full flex-wrap items-center justify-end gap-2 py-1.5 sm:w-auto">
              {filtered && <FilterChip {...filtered} />}
              {search && (
                <input
                  type="search"
                  value={search.value}
                  onChange={(event) => search.onChange(event.target.value)}
                  placeholder={`${search.label}…`}
                  aria-label={search.label}
                  className="w-full rounded-control border border-control-line bg-page px-2.5 py-1 text-sm focus:outline-hidden focus-visible:ring-2 focus-visible:ring-accent sm:w-72"
                />
              )}
              {tools}
              <DensityToggle density={density} onChange={setDensity} />
            </div>
          )}
        </div>
      )}
      {/* On a phone, each row is one entry: what it's called across the whole width,
          and the rest on a line under it, rather than columns squeezed side by side. */}
      {narrow && (
        <ul className="divide-y divide-line text-sm" aria-busy={status === "loading"}>
          {rows.map((row, at) => {
            const lead = leadColumn(columns, rows);
            const rest = row.cells.map((cell, i) => ({ cell, i })).filter(({ cell, i }) => i !== lead && !(i === 0 && numbered(columns, lead)) && !blank(cell));
            return (
              <li
                key={row.id}
                aria-selected={row.onPick ? row.picked === true : undefined}
                onClick={
                  row.onPick || row.link
                    ? (event) => {
                        if ((event.target as HTMLElement).closest("a, button, input, select, textarea")) return;
                        if (row.onPick) row.onPick();
                        else event.currentTarget.querySelector("a")?.click();
                      }
                    : undefined
                }
                className={`flex items-start gap-2 px-3 ${entry} ${toned(row.tone)} ${row.link || row.onPick ? "cursor-pointer active:bg-sunken/60" : ""} ${row.picked ? "bg-accent-soft" : ""}`}
              >
                {reorder && (
                  <button
                    type="button"
                    aria-label={`${reorder.label} ${typeof row.cells[lead] === "string" ? row.cells[lead] : ""}`.trim()}
                    onKeyDown={(event) => {
                      if (!event.altKey || (event.key !== "ArrowUp" && event.key !== "ArrowDown")) return;
                      event.preventDefault();
                      reorder.onMove(at, event.key === "ArrowUp" ? at - 1 : at + 1);
                    }}
                    className="rounded-control px-1 text-muted"
                  >
                    ⠿
                  </button>
                )}
                {pictured && thumb(row)}
                <div className="flex min-w-0 flex-1 flex-col gap-0.5">
                  <div className="leading-snug">
                    {numbered(columns, lead) && !blank(row.cells[0]) && <span className="mr-1.5 text-muted tabular-nums">#{row.cells[0]}</span>}
                    {row.link ? (
                      <a {...row.link} className="font-medium text-accent hover:underline">
                        {row.cells[lead]}
                      </a>
                    ) : (
                      <span className="font-medium">{row.cells[lead]}</span>
                    )}
                  </div>
                  {rest.length > 0 && (
                    <div className="flex flex-wrap items-center gap-x-3 gap-y-0.5 text-[0.8rem] text-muted">
                      {rest.map(({ cell, i }) => (
                        // A cell that draws nothing, or only an empty box, like labels when there are
                          // none, takes no room.
                        <span key={i} className="inline-flex items-center has-[>[data-cell]:empty]:hidden has-[>[data-cell]>:only-child:empty]:hidden">
                          <span className="sr-only">{columns[i]}: </span>
                          <span data-cell className="inline-flex items-center gap-1">
                            {cell}
                          </span>
                        </span>
                      ))}
                    </div>
                  )}
                </div>
                {row.actions.length > 0 && (
                  <div className="flex shrink-0 gap-3">
                    {row.actions.map((a) => (
                      <button
                        key={a.label}
                        type="button"
                        disabled={a.disabled}
                        onClick={a.onClick}
                        aria-label={typeof row.cells[lead] === "string" && row.cells[lead] ? `${a.label} ${row.cells[lead]}` : undefined}
                        title={a.icon && buttonIcons[a.icon] ? a.label : undefined}
                        className="font-medium text-accent hover:underline disabled:opacity-50"
                      >
                        {a.icon && buttonIcons[a.icon] ? <span aria-label={a.label}>{buttonIcons[a.icon]}</span> : a.label}
                      </button>
                    ))}
                  </div>
                )}
              </li>
            );
          })}
        </ul>
      )}
      {!narrow && (
      <table className="w-full text-left text-sm" aria-busy={status === "loading"}>
        <thead className="border-b border-line bg-sunken text-[0.8rem] text-muted">
          <tr>
            {reorder && (
              <th className="w-0 py-1.5 pl-2">
                <span className="sr-only">Order</span>
              </th>
            )}
            {pictured && (
              <th className="w-0 py-1.5 pl-2">
                <span className="sr-only">Picture</span>
              </th>
            )}
            {columns.map((column) => (
              <th key={column} className="px-3 py-1.5 font-medium">
                {column}
              </th>
            ))}
            {rows.some((row) => row.actions.length > 0) && (
              <th className="px-3 py-1.5">
                <span className="sr-only">Actions</span>
              </th>
            )}
          </tr>
        </thead>
        <tbody>
          {rows.map((row, at) => (
            // A row that opens something opens it wherever it's clicked, as its first
            // cell's link does; a link or button of its own still does its own thing.
            <tr
              key={row.id}
              draggable={reorder ? true : undefined}
              onDragStart={reorder ? () => setDragged(at) : undefined}
              onDragOver={
                reorder
                  ? (event) => {
                      event.preventDefault();
                      setOver(at);
                    }
                  : undefined
              }
              onDragEnd={
                reorder
                  ? () => {
                      setDragged(undefined);
                      setOver(undefined);
                    }
                  : undefined
              }
              onDrop={
                reorder
                  ? (event) => {
                      event.preventDefault();
                      if (dragged !== undefined) reorder.onMove(dragged, at);
                      setDragged(undefined);
                      setOver(undefined);
                    }
                  : undefined
              }
              aria-selected={row.onPick ? row.picked === true : undefined}
              onClick={
                row.onPick || row.link
                  ? (event) => {
                      if ((event.target as HTMLElement).closest("a, button, input, select, textarea")) return;
                      if (row.onPick) row.onPick();
                      else event.currentTarget.querySelector("a")?.click();
                    }
                  : undefined
              }
              className={`border-t border-line transition-colors first:border-t-0 hover:bg-sunken/60 ${toned(row.tone)} ${row.link || row.onPick ? "cursor-pointer" : ""} ${row.picked ? "!bg-accent-soft" : ""} ${dragged === at ? "opacity-40" : ""} ${over === at && dragged !== undefined && dragged !== at ? (dragged < at ? "shadow-[inset_0_-2px_0_var(--color-accent)]" : "shadow-[inset_0_2px_0_var(--color-accent)]") : ""}`}
            >
              {reorder && (
                <td className={`w-0 pl-2 ${room}`}>
                  <button
                    type="button"
                    aria-label={`${reorder.label} ${typeof row.cells[0] === "string" ? row.cells[0] : ""}`.trim()}
                    title="Drag, or Alt+↑ and Alt+↓"
                    onKeyDown={(event) => {
                      if (!event.altKey || (event.key !== "ArrowUp" && event.key !== "ArrowDown")) return;
                      event.preventDefault();
                      reorder.onMove(at, event.key === "ArrowUp" ? at - 1 : at + 1);
                    }}
                    className="cursor-grab rounded-control px-1 text-muted hover:text-ink focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden active:cursor-grabbing"
                  >
                    ⠿
                  </button>
                </td>
              )}
              {pictured && <td className={`w-0 pl-2 ${room}`}>{thumb(row)}</td>}
              {row.cells.map((cell, i) => (
                <td key={i} className={`px-3 ${room}`}>
                  {i === 0 && row.link ? (
                    <a {...row.link} className="font-medium text-accent hover:underline">
                      {cell}
                    </a>
                  ) : (
                    cell
                  )}
                </td>
              ))}
              {rows.some((other) => other.actions.length > 0) && (
                <td className={`px-3 text-right whitespace-nowrap ${room}`}>
                  {row.actions.map((a) => (
                    <button
                      key={a.label}
                      type="button"
                      disabled={a.disabled}
                      onClick={a.onClick}
                      // Named for its row too, like Rename Open, among the same button on every row.
                      aria-label={typeof row.cells[0] === "string" && row.cells[0] ? `${a.label} ${row.cells[0]}` : undefined}
                      title={a.icon && buttonIcons[a.icon] ? a.label : undefined}
                      className="ml-3 font-medium text-accent hover:underline disabled:opacity-50"
                    >
                      {a.icon && buttonIcons[a.icon] ? <span aria-label={a.label}>{buttonIcons[a.icon]}</span> : a.label}
                    </button>
                  ))}
                </td>
              )}
            </tr>
          ))}
        </tbody>
      </table>
      )}
      {rows.length === 0 && (
        <p className="px-4 py-6 text-center text-sm text-muted">
          {status === "loading" ? "Loading…" : status === "denied" ? "You can't see this." : search?.value.trim() ? "Nothing matches." : (empty ?? "Nothing here yet.")}
        </p>
      )}
      {pages && (
        <nav aria-label="Pages" className="flex items-center justify-between gap-3 border-t border-line px-4 py-2 text-sm text-muted">
          <button type="button" disabled={pages.page <= 1} onClick={() => pages.onPage(pages.page - 1)} className="hover:text-ink disabled:opacity-40">
            ← Previous
          </button>
          <span className="tabular-nums">
            Page {pages.page} of {pages.count}
          </span>
          <button type="button" disabled={pages.page >= pages.count} onClick={() => pages.onPage(pages.page + 1)} className="hover:text-ink disabled:opacity-40">
            Next →
          </button>
        </nav>
      )}
      {error && (
        <p role="alert" className="border-t border-line px-4 py-2 text-sm text-danger">
          {error}
        </p>
      )}
    </div>
    );
  },

  // A list's records as large cards in a row, like a project's milestones as a
  // timeline: an arrow from each to the next, each its title, what else it shows,
  // like its date, and how many rows it holds; the one picked stands out, pressed.
  // On a narrow page the row scrolls sideways rather than wrapping, to stay a line.
  // A track, compact: each card its dot, its title and how many it holds on one line,
  // what else it shows under them.
  Track: ({ label, steps }) => (
    <div role="group" aria-label={label} className="-mx-1 flex min-w-0 items-stretch gap-1 overflow-x-auto px-1 py-1">
      {steps.map((step, i) => (
        <Fragment key={step.id}>
          {i > 0 && (
            <svg aria-hidden="true" viewBox="0 0 24 24" className="h-6 w-6 shrink-0 self-center text-muted" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M5 12h14M13 6l6 6-6 6" />
            </svg>
          )}
          <button
            type="button"
            aria-pressed={step.selected}
            onClick={step.onSelect}
            className={
              "flex min-w-36 shrink-0 flex-col items-start gap-0.5 rounded-box border px-3 py-1.5 text-left shadow-panel transition-colors focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden " +
              (step.selected ? "border-accent bg-accent-soft" : "border-line bg-surface hover:border-accent")
            }
          >
            <span className="inline-flex w-full items-baseline gap-2">
              <span className={"inline-flex items-center gap-2 font-semibold " + (step.selected ? "text-accent" : "text-ink")}>
                {step.status && <span role="img" aria-label={step.status.words} title={step.status.words} className="size-2 shrink-0 rounded-full" style={{ background: statusColors[step.status.is] }} />}
                {step.title}
              </span>
              <span className="ml-auto pl-2 text-xs text-muted">
                <span className="font-semibold text-ink tabular-nums">{step.count}</span> {step.count === 1 ? step.noun.replace(/s$/, "") : step.noun}
              </span>
            </span>
            {step.details.length > 0 && (
              <span className="flex flex-wrap gap-x-3 text-xs text-muted">
                {step.details.map((detail, at) => (
                  <span key={at}>{detail}</span>
                ))}
              </span>
            )}
          </button>
        </Fragment>
      ))}
    </div>
  ),

  // Large cards, three across a wide page and one on a narrow one: a title that opens
  // what it's for, how many it holds and how they split, as a slim bar in the accent
  // with its parts named under it, and a few links that open it filtered.
  Chart,
  Charts,
  Monitor,
  FilePicture: ({ source, fit }) => (
    <div className="h-80 w-full overflow-hidden rounded-box bg-sunken">
      {source && <img src={source} alt="" className={`h-full w-full ${fit ? "object-contain" : "object-cover"}`} />}
    </div>
  ),
  Cards: ({ status, cards }) => (
    <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3" aria-busy={status === "loading"}>
      {cards.map((card) => {
        const total = card.tally?.reduce((n, part) => n + part.count, 0) ?? 0;
        return (
          <article key={card.id} className="flex flex-col gap-3 rounded-box border border-line bg-surface p-4 shadow-panel">
            {card.picture && (
              <div className="-mx-4 -mt-4 aspect-video overflow-hidden rounded-t-box bg-sunken">
                {card.picture.source && <img src={card.picture.source} alt="" className={`h-full w-full ${card.picture.fit ? "object-contain" : "object-cover"}`} />}
              </div>
            )}
            <div className="flex items-start justify-between gap-3">
              <h2 className="text-lg leading-snug font-semibold">
                {card.link ? (
                  <a {...card.link} className="text-ink hover:text-accent hover:underline">
                    {card.title}
                  </a>
                ) : (
                  card.title
                )}
              </h2>
              {card.tags && card.tags.length > 0 && (
                <div className="flex shrink-0 flex-wrap justify-end gap-1 pt-1">
                  {card.tags.map((tag) => (
                    <span key={tag} className="rounded-full border border-line bg-sunken px-2 py-0.5 text-xs font-medium text-muted">
                      {tag}
                    </span>
                  ))}
                </div>
              )}
            </div>
            {card.details.length > 0 && <div className="flex flex-wrap gap-x-3 gap-y-1 text-sm text-muted">{card.details.map((detail, i) => <span key={i}>{detail}</span>)}</div>}
            {card.tally && (
              <div className="flex flex-col gap-1.5">
                <p className="text-sm text-muted">
                  <span className="font-semibold text-ink tabular-nums">{total}</span> {card.noun ? (total === 1 ? card.noun.replace(/s$/, "") : card.noun) : "in all"}
                </p>
                {total > 0 && (
                  // How far along it is: what's done solid from the left, then what's
                  // under way in a tint, what failed in red, and what hasn't started
                  // left as the empty track.
                  <div className="flex h-2 gap-0.5 overflow-hidden rounded-full bg-sunken" aria-hidden="true">
                    {[...card.tally]
                      .filter((part) => part.state !== "none")
                      .sort((a, b) => tallyRank[a.state ?? "partial"] - tallyRank[b.state ?? "partial"])
                      .map((part) => (
                        <div key={part.label} style={{ width: `${(100 * part.count) / total}%`, ...tallyLook[part.state ?? "partial"] }} />
                      ))}
                  </div>
                )}
                <p className="flex flex-wrap gap-x-3 gap-y-0.5 text-xs text-muted">
                  {card.tally.map((part) => (
                    <span key={part.label} className="inline-flex items-center gap-1">
                      <span aria-hidden="true" className="size-2 rounded-[2px] border border-line" style={part.state === "none" ? undefined : tallyLook[part.state ?? "partial"]} />
                      {part.label} <span className="tabular-nums text-ink">{part.count}</span>
                    </span>
                  ))}
                </p>
              </div>
            )}
            {card.filters.length > 0 && (
              <nav aria-label="Filters" className="mt-auto flex flex-wrap gap-2 border-t border-line pt-3">
                {card.filters.map((f) => (
                  <a
                    key={f.label}
                    {...f.link}
                    data-hue={f.hue}
                    className={
                      f.hue
                        ? "rounded-full border border-accent/35 bg-accent-soft px-2.5 py-0.5 text-xs font-medium text-accent hover:border-accent hover:text-accent-hover"
                        : "rounded-full border border-line px-2.5 py-0.5 text-xs font-medium text-muted hover:border-accent hover:text-accent"
                    }
                  >
                    {f.label}
                  </a>
                ))}
              </nav>
            )}
          </article>
        );
      })}
      {cards.length === 0 && <p className="text-sm text-muted">{status === "loading" ? "Loading…" : "Nothing here yet."}</p>}
    </div>
  ),

  // Columns side by side, scrolled across when there are many, each with its cards and
  // how many; a card dragged dims the columns it can't go to and marks the one it's
  // over. Alt+← and Alt+→ move a focused card to the nearest column it can go to.
  // A list's toolbar on its own line, above a table or board switched between: what
  // it's filtered by, its search box and its buttons, in the same place for both.
  Toolbar: ({ search, tools, filtered }) => (
    <>
      {filtered && <FilterChip {...filtered} />}
      {tools}
      {search && (
        <input
          type="search"
          value={search.value}
          onChange={(event) => search.onChange(event.target.value)}
          placeholder={`${search.label}…`}
          aria-label={search.label}
          className="w-full rounded-control border border-control-line bg-page px-2.5 py-1 text-sm focus:outline-hidden focus-visible:ring-2 focus-visible:ring-accent sm:w-72"
        />
      )}
    </>
  ),

  // A pipeline, as the classic deploy screen drew one: a heading for each lane, and in
  // it a row for each card, its title first, then its stages in order, each a chip:
  // done with a tick, the one it's at in its status's color, the one it failed in red,
  // those to come faint. What its holder last said is at the row's end. Dense, a card
  // a line, so a whole release fits on one screen.
  Pipeline: ({ status, lanes }) => (
    <div className="flex flex-col gap-3" aria-busy={status === "loading"}>
      {lanes.length === 0 && status === "live" && <p className="text-sm text-muted">Nothing here yet.</p>}
      {lanes.map((lane) => (
        <section key={lane.title} aria-label={lane.title} className="flex flex-col gap-1">
          <h3 className="text-xs font-semibold text-ink">
            {lane.title} <span className="font-normal text-muted tabular-nums">{lane.rows.length}</span>
          </h3>
          <div className="divide-y divide-line overflow-hidden rounded-box border border-line bg-surface">
            {lane.rows.map((row) => (
              <div key={row.id} className="flex flex-col gap-1 px-3 py-1.5 text-sm lg:flex-row lg:items-center lg:gap-3">
                <div className="flex min-w-0 items-baseline gap-2 lg:w-72 lg:shrink-0">
                  {row.link ? (
                    <a {...row.link} className="truncate font-medium text-ink hover:text-accent hover:underline">
                      {row.title}
                    </a>
                  ) : (
                    <span className="truncate font-medium text-ink">{row.title}</span>
                  )}
                  {row.details.length > 0 && <span className="shrink-0 text-xs text-muted">{row.details.map((d, i) => <Fragment key={i}>{i > 0 && " · "}{d}</Fragment>)}</span>}
                </div>
                <ol className="flex flex-wrap items-center gap-0.5 text-[11px] leading-4" aria-label={`Stages of ${typeof row.title === "string" ? row.title : "this card"}`}>
                  {row.steps.map((step, i) => (
                    <li key={step.id} className="flex items-center gap-1">
                      {i > 0 && <span aria-hidden className={`h-px w-2 ${step.at === "later" ? "bg-line" : "bg-muted/50"}`} />}
                      <span
                        aria-current={step.at === "now" ? "step" : undefined}
                        className={`inline-flex items-center gap-1 rounded-full border px-1.5 ${
                          step.at === "done"
                            ? "border-success/30 bg-success/10 text-success"
                            : step.at === "failed"
                              ? "border-danger/40 bg-danger/10 font-medium text-danger"
                              : step.at === "now"
                                ? "border-current font-medium"
                                : "border-line text-muted/70"
                        }`}
                        style={step.at === "now" && row.status ? { color: statusColors[row.status.is] } : undefined}
                        title={step.at === "now" && row.status ? row.status.words : undefined}
                      >
                        {step.at === "done" && <span aria-hidden>✓</span>}
                        {step.at === "failed" && <span aria-hidden>✗</span>}
                        {step.at === "now" && <span aria-hidden className="size-1.5 rounded-full bg-current" />}
                        {step.title}
                      </span>
                    </li>
                  ))}
                </ol>
                {row.progress && <Progress step={row.progress.step} steps={row.progress.steps} className="w-24 shrink-0" />}
                {row.note && <p className="truncate text-xs text-muted lg:ml-auto lg:max-w-sm" title={row.note}>{row.note}</p>}
              </div>
            ))}
          </div>
        </section>
      ))}
    </div>
  ),
  // A canvas: cards placed anywhere, on the shared SVG surface, each drawn as a
  // board's card is: its dot, its title, what else it shows; text large and bare.
  Canvas: ({ status, cards, grid, error, onPlace, onOpen, regions, onResize, onFiles }) => (
    <div className="flex flex-col gap-2" aria-busy={status === "loading"}>
      {error && <p className="text-sm text-danger">{error}</p>}
      <Surface
        cards={cards}
        grid={grid}
        onPlace={onPlace}
        onOpen={onOpen}
        regions={regions}
        onResize={onResize}
        onFiles={onFiles}
        className="outline-none focus-visible:ring-2 focus-visible:ring-accent/40"
        style={{ borderRadius: "var(--radius-box, 10px)", background: "var(--color-sunken)", border: "1px solid var(--color-line)" }}
        card={(card) =>
          card.kind === "text" ? (
            <div className="h-full p-2 text-lg leading-snug font-medium text-ink">{card.title}</div>
          ) : (
            <div className={`flex h-full flex-col gap-1 overflow-hidden p-2.5 text-sm ${tones[card.tone ?? 3] ?? ""}`}>
              {card.picture && <img src={card.picture} alt="" draggable={false} className="min-h-0 w-full flex-1 rounded-control object-cover" />}
              <div className="flex items-start gap-1.5">
                {card.status && <span role="img" aria-label={card.status.words} title={card.status.words} className="mt-1.5 size-2 shrink-0 rounded-full" style={{ background: statusColors[card.status.is] }} />}
                {card.link ? (
                  <a {...card.link} draggable={false} onPointerDown={(e) => e.stopPropagation()} className="leading-snug font-medium text-ink hover:text-accent hover:underline">
                    {card.title}
                  </a>
                ) : (
                  <span className="leading-snug font-medium text-ink">{card.title}</span>
                )}
              </div>
              {card.details.length > 0 && <div className="flex flex-wrap gap-x-2 gap-y-0.5 text-xs text-muted">{card.details.map((d, i) => <span key={i}>{d}</span>)}</div>}
              {card.progress && <Progress step={card.progress.step} steps={card.progress.steps} />}
            </div>
          )
        }
      />
      <p className="text-xs text-muted">
        {regions?.length
          ? "Drag a card to move it, into another column to move it there, or by its corner to resize it. Drag a column's edge to widen it. Drag the space, or scroll, to look around; Ctrl and scroll, or pinch, to zoom."
          : "Drag a card to move it, or its corner to resize it. Drag the space, or scroll, to look around; Ctrl and scroll, or pinch, to zoom."}
        {onFiles && " Paste or drop an image to put it on the board."}
      </p>
    </div>
  ),
  // What's said as rows' times come, in the page's corner, the newest last: each its
  // title, what's under it, its buttons, and a way to put it away.
  Notices: ({ notices }) => (
    <aside aria-label="Notices" className="fixed right-4 bottom-4 z-50 flex w-80 flex-col gap-2">
      {notices.map((n) => (
        <section key={n.key} role="status" aria-label={n.title} className="flex flex-col gap-2 rounded-box border border-line bg-surface p-3 text-sm shadow-raised">
          <div className="flex items-start gap-2">
            <span aria-hidden="true" className="mt-0.5 text-accent">
              {buttonIcons["bell"]}
            </span>
            <strong className="mr-auto leading-snug">{n.title}</strong>
            <button type="button" aria-label="Put away" onClick={n.onClose} className="-mt-1 -mr-1 flex size-6 items-center justify-center rounded-control text-muted hover:text-ink">
              ×
            </button>
          </div>
          {n.body && <p className="pl-6 text-muted whitespace-pre-line">{n.body}</p>}
          {n.buttons.length > 0 && (
            <div className="flex flex-wrap gap-2 pl-6">
              {n.buttons.map((b, i) => (
                <button key={b.label} type="button" onClick={b.onClick} className={`${pressable} ${i === 0 ? button.primary : button.secondary} !px-3 !py-1.5`}>
                  {b.label}
                </button>
              ))}
            </div>
          )}
        </section>
      ))}
    </aside>
  ),

  // A calendar: what it's on, beside the ways back, to today and on; then its days, a
  // week side by side or a month's grid, today's in the accent, each with its rows, a
  // day alone first, each picked with a click or Enter and dragged to another day,
  // where it's outlined while a row is carried over it.
  Calendar: function RadixCalendar({ status, title, month, days, error, back, next, onToday, onMove }) {
    const [over, setOver] = useState<string>();
    const tool = "inline-flex size-8 items-center justify-center rounded-control text-ink hover:bg-sunken focus-visible:outline-2 focus-visible:outline-accent";
    return (
      <div aria-busy={status === "loading"} className="flex min-h-0 flex-1 flex-col gap-3">
        <div className="flex items-center gap-2">
          <h2 className="mr-auto text-lg font-semibold" style={{ fontFamily: "var(--font-heading, var(--font-sans))" }}>
            {title}
          </h2>
          <button type="button" aria-label={back.label} title={back.label} onClick={back.onClick} className={tool}>
            {buttonIcons["previous"]}
          </button>
          <button type="button" onClick={onToday} className={`${pressable} ${button.secondary}`}>
            Today
          </button>
          <button type="button" aria-label={next.label} title={next.label} onClick={next.onClick} className={tool}>
            {buttonIcons["next"]}
          </button>
        </div>
        {error && (
          <p role="alert" className="text-sm text-danger">
            {error}
          </p>
        )}
        <div className={`grid min-h-0 flex-1 grid-cols-7 gap-2 ${month ? "auto-rows-fr" : ""}`}>
          {days.map((day) => (
            <section
              key={day.date}
              aria-label={day.date}
              onDragOver={(event) => {
                if (!onMove) return;
                event.preventDefault();
                if (over !== day.date) setOver(day.date);
              }}
              onDragLeave={() => over === day.date && setOver(undefined)}
              onDrop={(event) => {
                if (!onMove) return;
                event.preventDefault();
                setOver(undefined);
                onMove(event.dataTransfer.getData("text/plain"), day.date);
              }}
              className={`flex min-h-24 min-w-0 flex-col gap-1.5 rounded-box border p-2 ${day.inMonth ? "bg-surface" : "bg-sunken"} ${
                over === day.date ? "border-accent ring-2 ring-accent/30" : "border-line"
              }`}
            >
              <h3 className={`text-xs ${day.today ? "font-semibold text-accent" : day.inMonth ? "text-muted" : "text-muted/60"}`}>{day.label}</h3>
              <ul className="flex flex-col gap-1.5">
                {day.cards.map((card) => (
                  <li
                    key={card.id}
                    tabIndex={card.onPick ? 0 : undefined}
                    aria-selected={card.onPick ? card.picked === true : undefined}
                    draggable={onMove ? true : undefined}
                    onDragStart={(event) => {
                      event.dataTransfer.setData("text/plain", card.id);
                      event.dataTransfer.effectAllowed = "move";
                    }}
                    onClick={card.onPick}
                    onKeyDown={(event) => {
                      if (card.onPick && (event.key === "Enter" || event.key === " ")) {
                        event.preventDefault();
                        card.onPick();
                      }
                    }}
                    className={`relative overflow-hidden rounded-control border bg-surface py-1.5 pr-2 pl-2.5 text-sm shadow-panel focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden ${
                      card.picked ? "border-accent ring-2 ring-accent/30" : "border-line"
                    } ${onMove ? "cursor-grab active:cursor-grabbing" : card.onPick ? "cursor-pointer" : ""}`}
                  >
                    {edged(card.tone) && <span aria-hidden="true" className="absolute inset-y-0 left-0 w-[3px]" style={{ background: edged(card.tone) }} />}
                    {card.time && <span className="block text-xs text-muted tabular-nums">{card.time}</span>}
                    <span className="block leading-snug">{card.title}</span>
                    {card.details.length > 0 && (
                      <span className="mt-0.5 flex flex-wrap gap-x-2 text-xs text-muted">
                        {card.details.map((detail, i) => (
                          <span key={i}>{detail}</span>
                        ))}
                      </span>
                    )}
                  </li>
                ))}
              </ul>
              {day.more > 0 && <p className="text-xs text-muted">{day.more} more</p>}
            </section>
          ))}
        </div>
      </div>
    );
  },

  Board: function RadixBoard({ status, columns, error, onMove, onArrange, search, tools, filtered, column, onColumn }) {
    const [dragged, setDragged] = useState<{ card: string; from: string; reaches: string[] }>();
    const [over, setOver] = useState<string>();
    // Where a card dragged within its own column would land: before this card, or
    // last when it's none.
    const [landing, setLanding] = useState<{ column: string; next: string | undefined }>();
    const reachable = (column: string) => !dragged || column === dragged.from || dragged.reaches.includes(column);
    const drop = () => {
      setDragged(undefined);
      setOver(undefined);
      setLanding(undefined);
    };
    // On a phone, a stage a page: swiped between, or picked from the tabs above, which
    // follow the swiping.
    const strip = useRef<HTMLDivElement>(null);
    // Which, as the address says when it says, so a link opens on the same stage.
    const [own, setOwn] = useState(0);
    const asked = column === undefined ? -1 : columns.findIndex((c) => c.id === column);
    const shown = asked >= 0 ? asked : own;
    const setShown = (at: number) => {
      setOwn(at);
      const id = columns[at]?.id;
      if (onColumn && id !== undefined) onColumn(at === 0 ? "" : id);
    };
    const turnTo = (at: number) => {
      const box = strip.current;
      const page = box?.children[at] as HTMLElement | undefined;
      if (box && page) box.scrollTo?.({ left: page.offsetLeft - box.offsetLeft, behavior: "smooth" });
      setShown(at);
    };
    // Opened on a stage the address names, it starts there.
    const opened = useRef(false);
    useEffect(() => {
      if (opened.current || asked <= 0 || !strip.current) return;
      const page = strip.current.children[asked] as HTMLElement | undefined;
      if (page) strip.current.scrollTo?.({ left: page.offsetLeft - strip.current.offsetLeft });
      opened.current = true;
    }, [asked]);
    return (
      <div className="flex flex-col gap-2">
        {(search || tools || filtered) && (
          <div className="flex flex-wrap items-center justify-end gap-2">
            {filtered && <FilterChip {...filtered} />}
            {search && (
              <input
                type="search"
                value={search.value}
                onChange={(event) => search.onChange(event.target.value)}
                placeholder={`${search.label}…`}
                aria-label={search.label}
                className="w-full rounded-control border border-control-line bg-page px-2.5 py-1 text-sm focus:outline-hidden focus-visible:ring-2 focus-visible:ring-accent sm:w-72"
              />
            )}
            {tools}
          </div>
        )}
        {columns.length > 1 && (
          <div role="tablist" aria-label="Stages" className="-mx-1 flex gap-1 overflow-x-auto px-1 sm:hidden">
            {columns.map((column, at) => (
              <button
                key={column.id}
                type="button"
                role="tab"
                aria-selected={shown === at}
                onClick={() => turnTo(at)}
                className={`inline-flex shrink-0 items-center gap-1.5 border-b-2 px-2.5 pt-1 pb-1.5 text-sm ${shown === at ? "border-accent font-medium text-ink" : "border-transparent text-muted"}`}
              >
                {column.title}
                <span className="rounded-full bg-sunken px-1.5 text-xs tabular-nums text-muted">{column.cards.length}</span>
              </button>
            ))}
          </div>
        )}
        <div
          ref={strip}
          onScroll={(event) => {
            const box = event.currentTarget;
            if (box.clientWidth === 0 || window.matchMedia("(min-width: 40rem)").matches) return;
            const at = Math.round(box.scrollLeft / box.clientWidth);
            if (at !== shown && at >= 0 && at < columns.length) setShown(at);
          }}
          className="flex snap-x snap-mandatory items-start gap-3 overflow-x-auto pb-2 sm:snap-none"
          aria-busy={status === "loading"}
        >
          {columns.map((column, at) => (
            <section
              key={column.id}
              aria-label={column.title}
              onDragOver={(event) => {
                // Below its column's cards, a card dragged within it goes last.
                if (onArrange && dragged?.from === column.id) {
                  event.preventDefault();
                  setLanding({ column: column.id, next: undefined });
                  return;
                }
                if (!dragged?.reaches.includes(column.id)) return;
                event.preventDefault();
                setOver(column.id);
              }}
              onDrop={(event) => {
                event.preventDefault();
                if (dragged && onArrange && dragged.from === column.id && landing?.column === column.id) onArrange(dragged.card, landing.next);
                else if (dragged && onMove && dragged.reaches.includes(column.id)) onMove(dragged.card, column.id);
                drop();
              }}
              className={`flex w-full shrink-0 snap-start flex-col rounded-box border bg-sunken/70 transition-opacity sm:w-72 ${reachable(column.id) ? "" : "opacity-40"} ${
                over === column.id ? "border-accent ring-2 ring-accent/30" : "border-line"
              }`}
            >
              <h3 className="flex items-center justify-between gap-2 px-3 pt-2.5 pb-1.5 text-[0.8rem] font-medium text-muted">
                <span className="inline-flex items-center gap-1.5">
                  {column.title}
                  {column.notify && (
                    <svg viewBox="0 0 16 16" width="13" height="13" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" role="img" aria-label="Chimes when a card arrives">
                      <title>Chimes when a card arrives</title>
                      <path d="M4 11V7a4 4 0 0 1 8 0v4l1.25 1.5H2.75L4 11zM6.5 14a1.5 1.5 0 0 0 3 0" />
                    </svg>
                  )}
                </span>
                <span className="rounded-full bg-surface px-1.5 text-xs tabular-nums">{column.cards.length}</span>
              </h3>
              <ul className="flex min-h-16 flex-col gap-2 px-2 pb-2">
                {column.cards.map((card, place) => {
                  const movable = Boolean(onMove) && card.reaches.length > 0;
                  const draggable = movable || Boolean(onArrange);
                  return (
                    <li
                      key={card.id}
                      draggable={draggable ? true : undefined}
                      tabIndex={(draggable || card.onPick) && !card.link ? 0 : undefined}
                      aria-selected={card.onPick ? card.picked === true : undefined}
                      onClick={card.onPick}
                      onDragStart={() => setDragged({ card: card.id, from: column.id, reaches: card.reaches })}
                      onDragEnd={drop}
                      onDragOver={(event) => {
                        // Within its own column, a card lands before the one it's over
                        // when it's over that card's top half, and after it otherwise.
                        if (!onArrange || dragged?.from !== column.id) return;
                        event.preventDefault();
                        event.stopPropagation();
                        const box = event.currentTarget.getBoundingClientRect();
                        const next = event.clientY < box.top + box.height / 2 ? card.id : column.cards[place + 1]?.id;
                        if (landing?.column !== column.id || landing.next !== next) setLanding({ column: column.id, next });
                      }}
                      onDrop={(event) => {
                        if (!onArrange || !dragged || dragged.from !== column.id || landing?.column !== column.id) return;
                        event.preventDefault();
                        event.stopPropagation();
                        onArrange(dragged.card, landing.next);
                        drop();
                      }}
                      style={landing?.column === column.id && landing.next === card.id && dragged?.card !== card.id ? { boxShadow: "0 -0.375rem 0 -0.125rem var(--color-accent)" } : undefined}
                      onKeyDown={(event) => {
                        // Alt and up or down: a place up or down its column.
                        if (onArrange && event.altKey && (event.key === "ArrowUp" || event.key === "ArrowDown")) {
                          const up = event.key === "ArrowUp";
                          if (up ? place === 0 : place === column.cards.length - 1) return;
                          event.preventDefault();
                          onArrange(card.id, up ? column.cards[place - 1].id : column.cards[place + 2]?.id);
                          return;
                        }
                        // Enter or space picks it, as a click does.
                        if (card.onPick && event.target === event.currentTarget && (event.key === "Enter" || event.key === " ")) {
                          event.preventDefault();
                          card.onPick();
                          return;
                        }
                        if (!onMove || !event.altKey || (event.key !== "ArrowLeft" && event.key !== "ArrowRight")) return;
                        const step = event.key === "ArrowLeft" ? -1 : 1;
                        for (let next = at + step; next >= 0 && next < columns.length; next += step) {
                          if (!card.reaches.includes(columns[next].id)) continue;
                          event.preventDefault();
                          onMove(card.id, columns[next].id);
                          return;
                        }
                      }}
                      className={`relative overflow-hidden rounded-control border bg-surface py-2.5 pr-2.5 pl-3.5 text-sm shadow-panel focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden ${
                        card.picked ? "border-accent ring-2 ring-accent/30" : "border-line"
                      } ${draggable ? "cursor-grab active:cursor-grabbing" : card.onPick ? "cursor-pointer" : ""} ${dragged?.card === card.id ? "opacity-40" : ""}`}
                    >
                      {/* How urgent it is, an edge down its left side. */}
                      {edged(card.tone) && <span data-edge="" aria-hidden="true" className="absolute inset-y-0 left-0 w-[3px]" style={{ background: edged(card.tone) }} />}
                      {/* How it's going, a dot before its title, said in words too. */}
                      {card.status && (
                        <span
                          role="img"
                          aria-label={card.status.words}
                          title={card.status.words}
                          className="mr-1.5 inline-block size-2 shrink-0 translate-y-[-1px] rounded-full align-middle"
                          style={{ background: statusColors[card.status.is] }}
                        />
                      )}
                      {card.link ? (
                        <a {...card.link} draggable={false} className="leading-snug font-medium text-ink hover:text-accent hover:underline">
                          {card.title}
                        </a>
                      ) : (
                        <span className="leading-snug font-medium">{card.title}</span>
                      )}
                      {/* Under it, what else it shows, and who's working it. */}
                      {(card.details.length > 0 || (card.people?.length ?? 0) > 0) && (
                        <div className="mt-2 flex items-end justify-between gap-2">
                          <div data-details="" className="flex min-w-0 flex-wrap items-center gap-x-2.5 gap-y-1 text-xs text-muted tabular-nums">
                            {card.details.map((detail, i) => (
                              <span key={i} className="inline-flex items-center">
                                {detail}
                              </span>
                            ))}
                          </div>
                          {card.people && card.people.length > 0 && <Faces people={card.people} />}
                        </div>
                      )}
                      {card.progress && <Progress step={card.progress.step} steps={card.progress.steps} className="mt-2" />}
                      {/* A finger can't drag one, so on a phone a card says where it can go. */}
                      {movable && onMove && (
                        <select
                          value=""
                          onChange={(event) => event.target.value && onMove(card.id, event.target.value)}
                          aria-label={`Move ${card.title}`}
                          className="mt-2 w-full rounded-control border border-control-line bg-page px-2 py-1 text-xs text-muted sm:hidden"
                        >
                          <option value="">Move to…</option>
                          {columns
                            .filter((other) => card.reaches.includes(other.id))
                            .map((other) => (
                              <option key={other.id} value={other.id}>
                                {other.title}
                              </option>
                            ))}
                        </select>
                      )}
                    </li>
                  );
                })}
                {/* Dragged below its column's cards, it goes last. */}
                {landing?.column === column.id && landing.next === undefined && <li aria-hidden="true" className="h-0.5 rounded-full bg-accent" />}
              </ul>
            </section>
          ))}
        </div>
        {columns.length === 0 && <p className="px-4 py-6 text-center text-sm text-muted">{status === "loading" ? "Loading…" : "Nothing here yet."}</p>}
        {error && (
          <p role="alert" className="text-sm text-danger">
            {error}
          </p>
        )}
      </div>
    );
  },

  // Each thing a box in a row, in order, and each arrow between two an arc: forward
  // above, back below, higher the farther it goes, its label at its top. An arrow
  // is pressed, or focused and Enter pressed, to change it; dragging from a box to
  // another draws a new one.
  Diagram: function RadixDiagram({ status, nodes, edges, onConnect, error }) {
    const [drawing, setDrawing] = useState<{ from: string; x: number; y: number }>();
    const width = 132;
    const height = 40;
    const gap = 64;
    const at = (id: string) => nodes.findIndex((n) => n.id === id);
    const span = (from: string, to: string) => Math.abs(at(to) - at(from));
    const rise = (n: number) => 22 + 26 * n;
    const above = Math.max(0, ...edges.filter((e) => at(e.to) > at(e.from)).map((e) => rise(span(e.from, e.to))));
    const below = Math.max(0, ...edges.filter((e) => at(e.to) < at(e.from)).map((e) => rise(span(e.from, e.to))));
    const top = above + 24;
    const total = { w: Math.max(1, nodes.length * (width + gap) - gap) + 24, h: top + height + below + 24 };
    const left = (i: number) => 12 + i * (width + gap);
    // An arc from one box to another, leaving and arriving a little apart from its
    // reverse, so a step and its way back don't sit on each other.
    const arc = (from: string, to: string) => {
      const [i, j] = [at(from), at(to)];
      const forward = j > i;
      const y = forward ? top : top + height;
      const x1 = left(i) + width / 2 + (forward ? 10 : -10);
      const x2 = left(j) + width / 2 + (forward ? -10 : 10);
      const peak = forward ? y - rise(Math.abs(j - i)) : y + rise(Math.abs(j - i));
      return { d: `M ${x1} ${y} C ${x1} ${peak}, ${x2} ${peak}, ${x2} ${y}`, lx: (x1 + x2) / 2, ly: forward ? peak + 18 : peak - 8 };
    };
    const point = (event: React.PointerEvent<SVGSVGElement>) => {
      const box = event.currentTarget.getBoundingClientRect();
      return { x: event.clientX - box.left, y: event.clientY - box.top };
    };
    const over = (x: number, y: number) => nodes.find((_, i) => x >= left(i) && x <= left(i) + width && y >= top && y <= top + height);
    return (
      <div className="flex flex-col gap-2">
        <div className="mx-auto w-fit max-w-full overflow-x-auto rounded-box border border-line bg-surface p-2 shadow-panel">
          <svg
            width={total.w}
            height={total.h}
            role="group"
            aria-busy={status === "loading"}
            className="touch-none select-none text-muted"
            onPointerMove={(event) => drawing && setDrawing({ ...drawing, ...point(event) })}
            onPointerUp={(event) => {
              if (!drawing) return;
              const { x, y } = point(event);
              const target = over(x, y);
              setDrawing(undefined);
              if (target && target.id !== drawing.from) onConnect?.(drawing.from, target.id);
            }}
            onPointerLeave={() => setDrawing(undefined)}
          >
            <defs>
              <marker id="one-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
                <path d="M 0 0 L 10 5 L 0 10 z" fill="currentColor" />
              </marker>
            </defs>
            {edges.map((edge) => {
              const { d, lx, ly } = arc(edge.from, edge.to);
              return (
                <g
                  key={`${edge.from}-${edge.to}`}
                  role={edge.onClick ? "button" : undefined}
                  aria-label={edge.title}
                  tabIndex={edge.onClick ? 0 : undefined}
                  onClick={edge.onClick}
                  onKeyDown={(event) => {
                    if (edge.onClick && (event.key === "Enter" || event.key === " ")) {
                      event.preventDefault();
                      edge.onClick();
                    }
                  }}
                  className={`${edge.onClick ? "cursor-pointer hover:text-accent focus-visible:text-accent focus-visible:outline-none" : ""}`}
                >
                  <title>{edge.title}</title>
                  <path d={d} fill="none" stroke="transparent" strokeWidth="14" />
                  <path d={d} fill="none" stroke="currentColor" strokeWidth="1.5" markerEnd="url(#one-arrow)" />
                  <text x={lx} y={ly} textAnchor="middle" fontSize="11" fill="currentColor" stroke="var(--color-surface)" strokeWidth="4" paintOrder="stroke">
                    {edge.label.length > 28 ? `${edge.label.slice(0, 27)}…` : edge.label}
                  </text>
                </g>
              );
            })}
            {nodes.map((node, i) => (
              <g
                key={node.id}
                onPointerDown={
                  onConnect
                    ? (event) => {
                        const svg = (event.currentTarget as SVGGElement).ownerSVGElement;
                        if (!svg) return;
                        const box = svg.getBoundingClientRect();
                        setDrawing({ from: node.id, x: event.clientX - box.left, y: event.clientY - box.top });
                      }
                    : undefined
                }
                className={onConnect ? "cursor-crosshair" : undefined}
              >
                <rect
                  x={left(i)}
                  y={top}
                  width={width}
                  height={height}
                  rx="6"
                  className={`fill-[var(--color-sunken)] ${drawing?.from === node.id ? "stroke-[var(--color-accent)]" : "stroke-[var(--color-line)]"}`}
                  strokeWidth="1.5"
                />
                <text x={left(i) + width / 2} y={top + height / 2 + 4} textAnchor="middle" fontSize="13" fontWeight="500" className="fill-[var(--color-ink)]">
                  {node.label.length > 18 ? `${node.label.slice(0, 17)}…` : node.label}
                </text>
              </g>
            ))}
            {drawing && (
              <line
                x1={left(at(drawing.from)) + width / 2}
                y1={top + height / 2}
                x2={drawing.x}
                y2={drawing.y}
                stroke="var(--color-accent)"
                strokeWidth="1.5"
                strokeDasharray="4 3"
                markerEnd="url(#one-arrow)"
              />
            )}
          </svg>
        </div>
        {onConnect && <p className="text-center text-sm text-muted">Drag from one box to another to add an arrow between them; press an arrow to change it.</p>}
        {error && (
          <p role="alert" className="text-sm text-danger">
            {error}
          </p>
        )}
      </div>
    );
  },

  // The pages above this one, small, above its title, each a link and the last this
  // page itself.
  Crumbs: ({ items, current }) => (
    <nav aria-label="Breadcrumb" className="mb-1.5 text-sm text-muted">
      <ol className="flex flex-wrap items-center gap-x-1.5 gap-y-0.5">
        {items.map((item, at) => (
          <li key={at} className="flex items-center gap-1.5">
            <a {...item.link} className="hover:text-ink hover:underline">
              {item.label}
            </a>
            <span aria-hidden="true">›</span>
          </li>
        ))}
        <li aria-current="page" className="max-w-[40ch] truncate text-ink">
          {current}
        </li>
      </ol>
    </nav>
  ),

  // A few ways to show the same thing, side by side, the one shown filled in.
  Switch: ({ label, options }) => (
    <div role="group" aria-label={label} className="inline-flex self-start rounded-control border border-line bg-surface p-0.5 shadow-panel">
      {options.map((option) => (
        <button
          key={option.label}
          type="button"
          aria-pressed={option.selected}
          onClick={option.onSelect}
          aria-label={option.icon ? option.label : undefined}
          title={option.icon ? option.label : undefined}
          className={`inline-flex items-center rounded-control px-2 py-1 text-sm font-medium focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden ${
            option.selected ? "bg-accent text-accent-ink" : "text-muted hover:text-ink"
          }`}
        >
          {(option.icon && switchIcons[option.icon]) || option.label}
        </button>
      ))}
    </div>
  ),

  // Rows down the side and the same things across the top, each cell what goes from
  // its row to its column; a cell to press for what may change, and a dash where a
  // thing meets itself.
  Grid: ({ status, corner, columns, rows, error }) => (
    <div className="w-fit max-w-full overflow-x-auto rounded-box border border-line bg-surface shadow-panel">
      <table className="text-sm" aria-busy={status === "loading"}>
        <thead className="border-b border-line bg-sunken text-[0.8rem] text-muted">
          <tr>
            <th className="px-3 py-1.5 text-left font-medium">{corner}</th>
            {columns.map((column, i) => (
              <th key={i} scope="col" className="px-3 py-1.5 text-left font-medium whitespace-nowrap">
                {column}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((row, i) => (
            <tr key={i} className="border-t border-line first:border-t-0">
              <th scope="row" className="bg-sunken/50 px-3 py-1.5 text-left font-medium whitespace-nowrap">
                {row.label}
              </th>
              {row.cells.map((cell, j) => (
                <td key={j} className="border-l border-line p-0">
                  {cell.self ? (
                    <span className="block px-3 py-1.5 text-center text-muted" aria-label={cell.label}>
                      —
                    </span>
                  ) : cell.onClick ? (
                    <button
                      type="button"
                      aria-label={cell.label}
                      onClick={cell.onClick}
                      className={`block w-full min-w-24 px-3 py-1.5 hover:bg-sunken/60 focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden focus-visible:ring-inset ${cell.text ? "text-left font-medium text-ink" : "text-center text-muted"}`}
                    >
                      {cell.text || "·"}
                    </button>
                  ) : (
                    <span className={`block px-3 py-1.5 ${cell.text ? "" : "text-center text-muted"}`}>{cell.text || "·"}</span>
                  )}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
      {rows.length === 0 && <p className="px-4 py-6 text-center text-sm text-muted">{status === "loading" ? "Loading…" : "Nothing here yet."}</p>}
      {error && (
        <p role="alert" className="border-t border-line px-4 py-2 text-sm text-danger">
          {error}
        </p>
      )}
    </div>
  ),

  Form: function RadixForm({ fields, submit, busy, error, onSubmit }) {
    const id = useId();
    return (
      // Writing, like a comment, takes the column's width; a few short fields don't.
      <form
        className={`flex flex-col gap-5 ${fields.some((field) => field.type === "markdown") ? "max-w-3xl" : "max-w-md"}`}
        onSubmit={(event) => {
          event.preventDefault();
          onSubmit();
        }}
      >
        {fields.map((field) => (
          <div key={field.name} className="flex flex-col gap-1.5">
            <label id={`${id}-${field.name}-label`} htmlFor={`${id}-${field.name}`} className="text-sm font-medium">
              {field.label}
            </label>
            {field.type === "choices" && field.choices ? (
              // Several choices, like what a role allows: a box to tick beside each.
              <div role="group" aria-labelledby={`${id}-${field.name}-label`} className="grid gap-x-6 gap-y-1.5 sm:grid-cols-2">
                {field.choices.map(([value, shown]) => {
                  const ticked = field.value.split(",").map((item) => item.trim()).filter((item) => item !== "");
                  return (
                    <label key={value} className="flex w-fit cursor-pointer items-center gap-2.5 text-sm text-ink/90">
                      <input
                        type="checkbox"
                        checked={ticked.includes(value)}
                        onChange={(event) => field.onChange((event.target.checked ? [...ticked, value] : ticked.filter((item) => item !== value)).join(", "))}
                        className="size-4 rounded border-control-line accent-accent"
                      />
                      {shown}
                    </label>
                  );
                })}
              </div>
            ) : field.type === "boolean" ? (
              // A yes or no: a box to tick, beside what it asks.
              <label className="flex w-fit cursor-pointer items-center gap-2.5 text-sm text-ink/90">
                <input
                  id={`${id}-${field.name}`}
                  name={field.name}
                  type="checkbox"
                  checked={field.value === "true"}
                  onChange={(event) => field.onChange(event.target.checked ? "true" : "false")}
                  aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                  className="size-4 rounded border-control-line accent-accent"
                />
                Yes
              </label>
            ) : field.type === "file" || field.type === "files" ? (
              // Each file there, with how far it's come while it uploads, then a way to
              // choose another, or more.
              <div className="flex flex-col gap-2">
                {field.files?.map((file, i) => (
                  <div key={i} className="flex items-center gap-3 rounded-control border border-line bg-surface px-3 py-2 text-sm shadow-panel">
                    <span className="min-w-0 flex-1 truncate">{file.name}</span>
                    {file.error ? (
                      <span role="alert" className="text-xs text-danger">
                        {file.error}
                      </span>
                    ) : (
                      file.progress !== undefined && <progress value={file.progress} max={1} aria-label={`${file.name}, uploading`} className="h-1.5 w-24 accent-accent" />
                    )}
                    <button type="button" onClick={() => field.onRemove?.(i)} className="text-xs text-muted hover:text-ink">
                      Remove<span className="sr-only"> {file.name}</span>
                    </button>
                  </div>
                ))}
                <label className="flex w-fit cursor-pointer items-center gap-2 rounded-control border border-dashed border-control-line px-3 py-2 text-sm text-muted transition-colors hover:border-accent hover:text-ink has-[:focus-visible]:ring-4 has-[:focus-visible]:ring-accent-soft">
                  <input
                    id={`${id}-${field.name}`}
                    name={field.name}
                    type="file"
                    multiple={field.type === "files"}
                    aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                    onChange={(event) => {
                      const chosen = Array.from(event.target.files ?? []);
                      event.target.value = "";
                      if (chosen.length) field.onFiles?.(chosen);
                    }}
                    className="sr-only"
                  />
                  {field.type === "files" ? "Add files" : field.files?.length ? "Choose another" : "Choose a file"}
                </label>
              </div>
            ) : field.type === "markdown" ? (
              <MarkdownField
                id={`${id}-${field.name}`}
                name={field.name}
                value={field.value}
                labelledBy={`${id}-${field.name}-label`}
                describedBy={field.hint ? `${id}-${field.name}-hint` : undefined}
                onChange={field.onChange}
              />
            ) : field.choices && field.choices.length <= fewChoices && field.type !== "pick" ? (
              <div
                role="radiogroup"
                aria-labelledby={`${id}-${field.name}-label`}
                aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                className="grid grid-cols-2 gap-3 sm:grid-cols-3"
              >
                {field.choices.map(([value, shown]) => {
                  const picked = field.value === value;
                  return (
                    <label
                      key={value}
                      className={`flex aspect-[4/3] cursor-pointer flex-col items-center justify-center gap-2.5 rounded-box border bg-surface p-3 text-sm font-medium shadow-panel transition-colors has-[:focus-visible]:ring-4 has-[:focus-visible]:ring-accent-soft ${
                        picked ? "border-accent text-ink ring-2 ring-accent/30" : "border-line text-muted hover:border-control-line hover:text-ink"
                      }`}
                    >
                      <input
                        type="radio"
                        name={`${id}-${field.name}`}
                        value={value}
                        checked={picked}
                        onChange={() => field.onChange(value)}
                        className="sr-only"
                      />
                      {choiceIcons[value]}
                      {shown}
                    </label>
                  );
                })}
              </div>
            ) : field.type === "slider" && field.range ? (
              // A number from one to another, slid along, with where it is after it.
              <div className="flex max-w-sm items-center gap-3">
                <input
                  id={`${id}-${field.name}`}
                  name={field.name}
                  type="range"
                  min={field.range[0]}
                  max={field.range[1]}
                  step={field.range[2]}
                  value={field.value === "" ? field.range[0] : field.value}
                  onChange={(event) => field.onChange(event.target.value)}
                  aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                  className="h-10 min-w-0 flex-1 cursor-pointer rounded-control accent-accent focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden"
                />
                <output htmlFor={`${id}-${field.name}`} className="min-w-[3ch] text-right text-sm font-medium tabular-nums text-ink">
                  {field.value === "" ? field.range[0] : field.value}
                </output>
              </div>
            ) : field.choices ? (
              <select
                id={`${id}-${field.name}`}
                name={field.name}
                value={field.value}
                onChange={(event) => field.onChange(event.target.value)}
                aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                className="h-10 rounded-control border border-control-line/60 bg-surface px-3 text-base shadow-panel transition-colors hover:border-control-line focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none sm:text-sm"
              >
                {!field.required && <option value="">Choose one</option>}
                {field.choices.map(([value, shown]) => (
                  <option key={value} value={value}>
                    {shown}
                  </option>
                ))}
              </select>
            ) : field.type === "date" ? (
              // A day, and a time of day if it has one, as 2026-10-28 or 2026-10-28T09:00.
              <div className="flex gap-2">
                <input
                  id={`${id}-${field.name}`}
                  name={field.name}
                  type="date"
                  value={field.value.slice(0, 10)}
                  onChange={(event) => field.onChange(event.target.value === "" ? "" : event.target.value + (field.value.includes("T") ? field.value.slice(10) : ""))}
                  aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                  className="h-10 min-w-0 flex-1 rounded-control border border-control-line/60 bg-surface px-3 text-base shadow-panel transition-colors hover:border-control-line focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none sm:text-sm"
                />
                <input
                  type="time"
                  aria-label={`${field.label}, at`}
                  value={field.value.includes("T") ? field.value.slice(11, 16) : ""}
                  disabled={field.value === ""}
                  onChange={(event) => field.onChange(field.value.slice(0, 10) + (event.target.value === "" ? "" : "T" + event.target.value))}
                  className="h-10 w-32 rounded-control border border-control-line/60 bg-surface px-3 text-base shadow-panel transition-colors hover:border-control-line focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none disabled:opacity-50 sm:text-sm"
                />
              </div>
            ) : (
              <input
                id={`${id}-${field.name}`}
                name={field.name}
                type={field.type === "list" ? "text" : field.type}
                placeholder={field.type === "list" ? "separated by commas" : undefined}
                value={field.value}
                onChange={(event) => field.onChange(event.target.value)}
                aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                className="h-10 rounded-control border border-control-line/60 bg-surface px-3 text-base shadow-panel transition-colors hover:border-control-line focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none sm:text-sm"
              />
            )}
            {field.hint && (
              <span id={`${id}-${field.name}-hint`} className="text-xs text-muted">
                {field.hint}
              </span>
            )}
          </div>
        ))}
        {error && (
          <p role="alert" className="text-sm text-danger">
            {error}
          </p>
        )}
        <button
          type="submit"
          disabled={busy}
          className={`self-start ${pressable} ${button.primary}`}
        >
          {submit}
        </button>
      </form>
    );
  },

  Button: ({ kind = "secondary", disabled, error, onClick, children, icon, tool, pressed: on, hint, swatch }) => {
    // Drawn as an icon, its words still name it, and show when it's pointed at.
    const words = typeof children === "string" ? children : undefined;
    // A color it gives, drawn as a circle of it, outlined so white shows.
    const drawn = swatch ? (
      <span aria-hidden="true" className="size-4 rounded-full border border-line" style={{ backgroundColor: swatch }} />
    ) : icon ? (
      buttonIcons[icon]
    ) : undefined;
    if (tool) {
      // A tool in a workspace's toolbar: flat, tinted while it's on; large, its icon
      // over its words, as tall as two small ones stacked, as a group of them is, so
      // the toolbar stays as tall whichever groups it shows; small, its icon alone, or
      // its words when it has none, as wide as they are.
      const shape = tool === "large" ? "h-[4.125rem] min-w-[3.75rem] flex-col gap-1 px-2 py-2 text-xs [&>svg]:size-8" : drawn ? "size-8 text-xs" : "h-8 min-w-8 px-1.5 text-xs";
      // Pressed, it acts on what's being worked on and leaves the keyboard, and any
      // caret, with it; Tab still reaches it.
      return (
        <button
          type="button"
          disabled={disabled}
          onMouseDown={(e) => e.preventDefault()}
          onClick={onClick}
          aria-pressed={on}
          aria-label={tool === "small" && drawn ? words : undefined}
          title={hint ?? words}
          className={`inline-flex items-center justify-center rounded-control border border-transparent transition-colors hover:bg-sunken focus-visible:outline-2 focus-visible:outline-accent disabled:opacity-40 ${shape} ${on ? "border-accent/50 bg-accent-soft font-semibold text-accent" : "text-ink"}`}
        >
          {drawn}
          {(tool === "large" || !drawn) && <span className="whitespace-nowrap leading-none">{children}</span>}
        </button>
      );
    }
    const pressed = (
      <button
        type="button"
        disabled={disabled}
        onClick={onClick}
        aria-label={icon ? words : undefined}
        aria-pressed={on}
        title={hint ?? (icon ? words : undefined)}
        className={`self-start ${pressable} ${button[kind]} ${icon && buttonIcons[icon] ? "!px-2" : ""}`}
      >
        {(icon && buttonIcons[icon]) || children}
      </button>
    );
    if (!error) return pressed;
    return (
      <div className="flex flex-col items-start gap-1">
        {pressed}
        <p role="alert" className="text-sm text-danger">
          {error}
        </p>
      </div>
    );
  },

  // Over the page: a dialog on a wide screen, the whole screen on a phone.
  Dialog: ({ open, title, onClose, children }) => (
    <Sheet open={open} title={title} onClose={onClose}>
      {children}
    </Sheet>
  ),

  // Nothing written, like an issue without a description, takes no room.
  Markdown: ({ status, source, plain, framed = true }) =>
    status === "live" && source?.trim() && plain ? (
      <div className="max-w-3xl text-base text-muted [&_p]:my-0">
        <MarkdownText source={source} />
      </div>
    ) : status === "live" && source?.trim() && !framed ? (
      <MarkdownText source={source} />
    ) : status === "live" && source?.trim() ? (
      <div className="rounded-box border border-line bg-surface px-5 py-4 shadow-panel">
        <MarkdownText source={source} />
      </div>
    ) : status === "live" ? null : (
      <div aria-busy={status === "loading"} className="h-6" />
    ),

  // Asking whether visits may be counted with cookies: a small card at the foot of
  // the page, out of the way of what's on it, until it's answered.
  Consent: ({ onAnswer }) => (
    <aside
      aria-label="Counting visits"
      className="fixed inset-x-4 bottom-4 z-40 mx-auto flex max-w-xl flex-wrap items-center gap-3 rounded-box border border-line bg-surface p-4 text-sm shadow-panel sm:flex-nowrap"
    >
      <p className="m-0 flex-1 leading-6 text-ink/90">This site counts its visits with Google Analytics, to see how it's used. May it use cookies to do that?</p>
      <div className="flex shrink-0 gap-2">
        <button type="button" onClick={() => onAnswer(false)} className={`${pressable} ${button.secondary}`}>
          No thanks
        </button>
        <button type="button" onClick={() => onAnswer(true)} className={`${pressable} ${button.primary}`}>
          Allow
        </button>
      </div>
    </aside>
  ),

  // What a thing is, label beside value, in a quiet panel.
  Details: ({ items, tone }) => (
    <dl className={`grid max-w-xl grid-cols-[max-content_1fr] gap-x-6 gap-y-2 rounded-box border border-line bg-surface px-4 py-3 text-sm ${toned(tone)}`}>
      {items.map((item) => (
        <div key={item.label} className="contents">
          <dt className="text-muted">{item.label}</dt>
          <dd className="m-0 font-medium">{item.value}</dd>
        </div>
      ))}
    </dl>
  ),

  // Each word a small chip, like an issue's labels.
  // Each label in a color of its own, the same wherever it's shown.
  Labels: ({ items }) => (
    <span className="inline-flex flex-wrap gap-1">
      {items.map((item) => (
        <span key={item} className="one-label rounded-full border px-2 py-px text-xs font-medium" style={{ "--label-hue": labelHue(item) } as CSSProperties}>
          {item}
        </span>
      ))}
    </span>
  ),

  Landing,

  // Files, each its name as a link, beside a small picture of it when it's an image.
  Files: ({ items }) => (
    <span className="flex flex-col gap-1.5">
      {items.map((item, i) => (
        <button
          key={i}
          type="button"
          onClick={item.onOpen}
          className="flex w-fit items-center gap-2.5 rounded-control text-left text-sm text-accent underline-offset-2 hover:underline focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none"
        >
          {item.preview && <img src={item.preview} alt="" className="size-10 rounded-control border border-line object-cover" />}
          <span className="break-all">{item.name}</span>
        </button>
      ))}
    </span>
  ),

  // Two columns: what the page is about, wide, and what's said of it, narrow, beside
  // it on a wide screen and after it on a phone. Any other layout is one column.
  // A workspace, filling the window: its bar along the top, its toolbar under it,
  // its tools down the left, the work beside what's at its side, and a status line
  // along the bottom.
  Layout: ({ name, regions, site }) =>
    name === "workspace" ? (
      <div className="fixed inset-0 flex min-h-0 flex-col bg-sunken text-ink">
        {regions.bar && (
          // The app's name in the middle, in the theme's heading font, as a window's
          // title is; the bar's lines at the start and light and dark at the end, each
          // side as wide as the other so the name stays centered.
          <div className="grid h-11 shrink-0 grid-cols-[1fr_auto_1fr] items-center gap-2 border-b border-line bg-surface px-3">
            <div className="flex min-w-0 items-center gap-2">{regions.bar}</div>
            <span className="text-lg leading-none font-semibold" style={{ fontFamily: "var(--font-heading, var(--font-sans))" }}>
              {site}
            </span>
            <span className="flex justify-end">
              <ThemeToggle />
            </span>
          </div>
        )}
        {regions.toolbar && (
          <div role="toolbar" className="flex shrink-0 items-stretch gap-1 overflow-x-auto border-b border-line bg-surface px-2 py-1 [&>[data-at-end]]:ml-auto [&>[data-at-end]~[data-at-end]]:ml-0">
            {regions.toolbar}
          </div>
        )}
        <div className="flex min-h-0 flex-1">
          {regions.tools && (
            // Its tools in a column down its left side, as a drawing app's are.
            <div role="toolbar" aria-orientation="vertical" className="flex w-12 shrink-0 flex-col items-center gap-0.5 overflow-y-auto border-r border-line bg-surface py-2">
              {regions.tools}
            </div>
          )}
          <main className="relative flex min-w-0 flex-1 flex-col gap-4 overflow-auto p-3">{regions.main}</main>
          {regions.side && <aside className="w-72 shrink-0 overflow-y-auto border-l border-line bg-surface p-3">{regions.side}</aside>}
        </div>
        {regions.status && <div className="flex shrink-0 items-center gap-4 overflow-hidden whitespace-nowrap border-t border-line bg-surface px-3 py-1 text-xs text-muted">{regions.status}</div>}
      </div>
    ) : name === "two_columns" ? (
      <div className="grid gap-10 lg:grid-cols-[minmax(0,1fr)_18rem] lg:items-start">
        <div className="flex min-w-0 flex-col gap-10">{regions.main}</div>
        {/* Beside the page, a section is a glance at something, so its heading is a
            label's size and its parts sit close. */}
        <aside className="flex min-w-0 flex-col gap-8 [&_section]:gap-2.5 [&_section>h2]:text-base [&_section>h2]:tracking-normal">{regions.side}</aside>
      </div>
    ) : (
      <div className="flex flex-col gap-10">
        {Object.entries(regions).map(([region, content]) => (
          <div key={region} className="flex flex-col gap-10">
            {content}
          </div>
        ))}
      </div>
    ),

  // A toolbar's group: its tools, then its name under them, and a line after it; a
  // small one's tools in rows as long as it says, or two rows, read a row at a time,
  // the first the longer.
  Group: ({ name, small, across, atEnd, children }) => (
    <div role="group" aria-label={name} data-at-end={atEnd ? "" : undefined} className="flex flex-col items-center justify-between gap-0.5 border-r border-line px-2 last:border-r-0 [&:has(>div:empty)]:hidden">
      <div
        className={small ? "grid gap-0.5" : "flex items-start gap-0.5"}
        style={small ? { gridTemplateColumns: `repeat(${across ?? Math.max(1, Math.ceil(Children.toArray(children).length / 2))}, auto)` } : undefined}
      >
        {children}
      </div>
      <span className="text-[0.6875rem] leading-tight text-muted">{name}</span>
    </div>
  ),

  // What was just done, or why something was turned down, in the status line.
  Messages: ({ text, problem }) => (
    <span role="status" aria-live="polite" className={problem ? "text-danger" : ""}>
      {text}
    </span>
  ),

  // Buttons in a row; while none of them applies, the row takes no room.
  Actions: ({ children }) => <div className="flex flex-wrap items-center gap-2 empty:hidden">{children}</div>,

  // A conversation: each entry beside its author's picture, headed by their name
  // and when they wrote it.
  // A conversation not yet begun takes no room; what's below it, like the box to
  // write in, says so well enough.
  Thread: ({ status, entries }) =>
    status === "live" && entries.length === 0 ? null : status !== "live" ? (
      <p className="text-sm text-muted">{status === "loading" ? "Loading…" : "You can't see this."}</p>
    ) : (
      <ol className="flex flex-col gap-4">
        {entries.map((entry) => (
          <li key={entry.id} className="flex gap-3">
            {entry.picture?.startsWith("https://") ? (
              <img src={entry.picture} alt="" loading="lazy" referrerPolicy="no-referrer" className="one-person mt-1 size-8 flex-none rounded-full border border-line object-cover" />
            ) : (
              <span aria-hidden="true" className="one-person mt-1 size-8 flex-none rounded-full border border-line bg-sunken" />
            )}
            <article className="min-w-0 flex-1 overflow-hidden rounded-box border border-line bg-surface">
              <header className="flex flex-wrap items-baseline gap-x-2 border-b border-line bg-sunken/60 px-4 py-2 text-sm">
                <span className="font-medium">{entry.author}</span>
                <span className="text-muted">{entry.when}</span>
              </header>
              <div className="px-4 py-3">{entry.body}</div>
            </article>
          </li>
        ))}
      </ol>
    ),

  // What happened, oldest first, down a line, each change one sentence.
  // A box that finds as it's typed in; what it finds sits under it, list by list,
  // each result a link.
  Find: ({ label, query, onChange, groups }) => (
    <div className="flex flex-col gap-2">
      <input
        type="search"
        value={query}
        onChange={(event) => onChange(event.target.value)}
        placeholder={`${label}…`}
        aria-label={label}
        className="w-full rounded-control border border-control-line bg-page px-3 py-2 text-sm focus:outline-hidden focus-visible:ring-2 focus-visible:ring-accent"
      />
      {query.trim() && (
        <div className="flex flex-col gap-3 rounded-box border border-line bg-surface p-3 text-sm shadow-panel" role="region" aria-label={`${label}: what's found`}>
          {groups.length === 0 && <p className="text-muted">Nothing matches.</p>}
          {groups.map((group) => (
            <div key={group.label} className="flex flex-col gap-1">
              <h3 className="text-[0.8rem] font-medium text-muted">{group.label}</h3>
              <ul className="flex flex-col gap-1" aria-label={group.label}>
                {group.results.map((result) => (
                  <li key={result.id}>
                    {result.link ? (
                      <a {...result.link} className="font-medium text-accent hover:underline">
                        {result.text}
                      </a>
                    ) : (
                      result.text
                    )}
                  </li>
                ))}
                {group.more > 0 && <li className="text-muted">and {group.more} more</li>}
              </ul>
            </div>
          ))}
        </div>
      )}
    </div>
  ),

  Timeline: ({ status, entries, title, fresh }) => {
    if (status !== "live" || entries.length === 0) return null;
    const changes = (
      <ol className="relative ml-1.5 flex flex-col gap-3 border-l border-line pl-5 text-sm">
        {entries.map((entry) => (
          <li key={entry.id} className="relative">
            <span
              aria-hidden="true"
              className={`absolute top-1.5 -left-[1.6rem] size-2.5 rounded-full border-2 border-page ${entry.fresh ? "bg-accent" : "bg-muted"}`}
            />
            {entry.fresh && <span className="sr-only">New: </span>}
            <span className="font-medium">{entry.who}</span> <span className="text-muted">{entry.what}</span>
            {entry.subject && (
              <>
                {" "}
                {entry.link ? (
                  <a {...entry.link} className="font-medium text-accent hover:underline">
                    {entry.subject}
                  </a>
                ) : (
                  <span className="font-medium">{entry.subject}</span>
                )}
              </>
            )}
            {entry.after && <span className="text-muted"> {entry.after}</span>}
            <span className="text-muted"> · {entry.when}</span>
          </li>
        ))}
      </ol>
    );
    return title ? (
      <section className="flex flex-col gap-3">
        <h2 className="flex items-center gap-2 text-lg font-semibold">
          {title}
          {fresh ? <span className="rounded-full bg-accent px-2 py-0.5 text-xs font-medium text-accent-ink tabular-nums">{fresh} new</span> : null}
        </h2>
        {changes}
      </section>
    ) : (
      changes
    );
  },

  Picture: ({ source, service }) =>
    service ? (
      // A service, like an agent: a robot in its circle, so it isn't taken for a person.
      <span role="img" aria-label="A service" title="A service" className="one-person inline-flex size-6 items-center justify-center rounded-full border border-line bg-sunken text-muted">
        <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
          <rect x="3" y="5" width="10" height="8" rx="2" />
          <path d="M8 5V3M6 9h.01M10 9h.01M6.5 11h3M1.5 8.5v2M14.5 8.5v2" />
          <circle cx="8" cy="2.5" r="0.75" />
        </svg>
      </span>
    ) : (
      <img
        src={source}
        alt=""
        loading="lazy"
        referrerPolicy="no-referrer"
        className="one-person inline-block size-6 rounded-full border border-line object-cover"
      />
    ),

  // The scheme as a row of choices, a new theme beside it, then each role's colors,
  // light over dark, and the theme as a project says it, to copy.
  Themes: ({ scheme, schemes, onScheme, seed, colors, written, copied, onNew, onReset, onCopy }) => (
    <section className="flex flex-col gap-5" aria-label="Themes">
      <div className="flex flex-wrap items-center gap-3">
        <div role="radiogroup" aria-label="Scheme" className="inline-flex flex-wrap rounded-control border border-line bg-sunken p-0.5">
          {schemes.map(([value, shown]) => (
            <label
              key={value}
              className={`cursor-pointer rounded-control px-3 py-1.5 text-sm font-medium transition-colors has-[:focus-visible]:ring-2 has-[:focus-visible]:ring-accent ${
                value === scheme ? "bg-surface text-ink shadow-panel" : "text-muted hover:text-ink"
              }`}
            >
              <input type="radio" name="uione-theme-scheme" value={value} checked={value === scheme} onChange={() => onScheme(value)} className="sr-only" />
              {shown}
            </label>
          ))}
        </div>
        <button type="button" onClick={onNew} className={`${pressable} ${button.primary}`}>
          New theme
        </button>
        {seed !== undefined && (
          <button type="button" onClick={onReset} className={`${pressable} ${button.secondary}`}>
            Back to the app's theme
          </button>
        )}
        {seed !== undefined && <span className="font-mono text-sm text-muted tabular-nums">Seed {seed}</span>}
      </div>
      {seed !== undefined && (
        <>
          <ul className="grid grid-cols-5 gap-2 sm:grid-cols-10">
            {colors.map(([role, light, dark]) => (
              <li key={role} className="flex flex-col gap-1.5">
                <span className="flex h-14 flex-col overflow-hidden rounded-control border border-line" title={`${role}: ${light}, dark ${dark}`}>
                  <span className="flex-1" style={{ background: light }} />
                  <span className="flex-1" style={{ background: dark }} />
                </span>
                <span className="text-xs text-muted">{role}</span>
              </li>
            ))}
          </ul>
          <div className="relative rounded-box border border-line bg-sunken">
            <pre className="overflow-x-auto p-4 pr-24 font-mono text-sm leading-relaxed text-ink" tabIndex={0}>
              {written}
            </pre>
            <button type="button" onClick={onCopy} className={`absolute top-3 right-3 ${pressable} ${button.secondary}`}>
              {copied ? "Copied" : "Copy"}
            </button>
          </div>
        </>
      )}
    </section>
  ),

  Live: ({ status, value }) =>
    status === "live" ? (
      <span className="font-medium tabular-nums">{value}</span>
    ) : (
      <span aria-busy="true" className="inline-block h-[1em] w-8 animate-pulse rounded bg-surface align-middle">
        <span className="sr-only">not available right now</span>
      </span>
    ),
};
