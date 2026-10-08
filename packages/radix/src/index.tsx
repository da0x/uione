// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

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
import * as Dialog from "@radix-ui/react-dialog";
import { useId, useMemo, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import type { ButtonProps, ComponentSet } from "@uione/react";
import { highlight, highlightCodeBlocks } from "./highlight.js";
import { MarkdownField, MarkdownText } from "./markdown.js";
import { nameStyles, setCodeDisplay, tabWidths, useCodeDisplay } from "./display.js";
import { ThemeToggle } from "./theme.js";
import type { CodeDisplay, NameStyle } from "./display.js";

// The box code sits in, on its own or inside a page of docs.
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

// The pictures a switch draws for what it switches between: rows for a table, and
// columns of cards for a board.
const switchIcons: Record<string, ReactNode> = {
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
  Page: ({ name, icon, home, nav, title, account, heading, children }) => (
    <div className="min-h-screen bg-page text-ink">
      {/* The header stays in view, over a blur of the page as it scrolls. The page
          you're on is underlined in the accent, along the header's edge. */}
      <header className="sticky top-0 z-20 border-b border-line bg-page/80 backdrop-blur-md">
        {/* On a phone, the navigation takes a row of its own under the name. */}
        <div className="flex flex-wrap items-center gap-x-7 gap-y-1 px-4 pt-3 sm:px-6 sm:pt-0 lg:px-8">
          <a {...home} className="flex shrink-0 items-center gap-2.5 py-0 text-[1.05rem] font-semibold tracking-[-0.01em] sm:py-3.5">
            {icon && <img src={icon} alt="" className="h-7 w-7" />}
            {name}
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
            <ThemeToggle />
            {account}
          </div>
        </div>
      </header>
      {/* The page is as wide as the window, for tables, code and editors; text keeps
          its own reading width. */}
      {/* A page with a heading of its own has room above it; one whose first thing
          is its own header, like a project's, starts close under the navigation. */}
      <main className={`px-4 pb-8 sm:px-6 sm:pb-10 lg:px-8 ${title !== name ? "pt-5 sm:pt-6" : "pt-4 sm:pt-5"}`}>
        {/* The title, and a screen's own buttons at the end of its row. */}
        <div className={`flex flex-wrap items-center gap-3 ${title !== name ? "mb-5" : ""}`}>
          {title !== name && <h1 className="mr-auto text-[1.75rem] leading-tight font-semibold tracking-[-0.025em]">{title || "\u00a0"}</h1>}
          <div className="ml-auto flex items-center gap-2 empty:hidden">{heading}</div>
        </div>
        <div className="flex flex-col gap-7">{children}</div>
      </main>
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

  Section: ({ title, id, children }) => (
    <section id={id} className="flex scroll-mt-24 flex-col gap-4">
      <h2 className="text-[1.5rem] font-semibold tracking-[-0.02em]">{title}</h2>
      {children}
    </section>
  ),

  Text: ({ children }) => <p className="max-w-3xl leading-7 text-ink/90">{children}</p>,

  Link: ({ href, onClick, external, children }) =>
    external ? (
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

  Pages: function RadixPages({ pages, title, html }) {
    const display = useCodeDisplay();
    const body = useMemo(() => (html === undefined ? undefined : highlightCodeBlocks(html, box, display.names)), [html, display.names]);
    const hasCode = body?.includes("one-code") ?? false;
    return (
      <div className="grid gap-8 md:grid-cols-[12rem_minmax(0,1fr)] md:gap-10">
        {/* On a phone, the pages are a row of tabs above the page. */}
        <nav aria-label="Pages" className="flex flex-wrap gap-1 text-sm md:flex-col">
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
        </nav>
        {body === undefined ? (
          <p className="text-muted">There's no page here.</p>
        ) : (
          <div className="min-w-0 max-w-3xl">
            {hasCode && (
              <div className="mb-6 rounded-box border border-line bg-surface shadow-panel">
                <CodeToolbar display={display} />
              </div>
            )}
            <article
              aria-label={title}
              className="one-prose"
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

  Table: function RadixTable({ status, columns, rows, error, tabs, search, pages, reorder, tools }) {
    // The row being dragged, and the place it would go.
    const [dragged, setDragged] = useState<number>();
    const [over, setOver] = useState<number>();
    return (
    <div className="overflow-x-auto rounded-box border border-line bg-surface shadow-panel">
      {/* Its rows by a choice, like Open and Closed, each with how many there are,
          and the search box at the end of the same row. */}
      {(tabs || search || tools) && (
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
          {(search || tools) && (
            <div className="ml-auto flex w-full flex-wrap items-center justify-end gap-2 py-1.5 sm:w-auto">
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
        </div>
      )}
      <table className="w-full text-left text-sm" aria-busy={status === "loading"}>
        <thead className="border-b border-line bg-sunken text-[0.8rem] text-muted">
          <tr>
            {reorder && (
              <th className="w-0 py-1.5 pl-2">
                <span className="sr-only">Order</span>
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
              onClick={
                row.link
                  ? (event) => {
                      if ((event.target as HTMLElement).closest("a, button, input, select, textarea")) return;
                      event.currentTarget.querySelector("a")?.click();
                    }
                  : undefined
              }
              className={`border-t border-line transition-colors first:border-t-0 hover:bg-sunken/60 ${row.link ? "cursor-pointer" : ""} ${dragged === at ? "opacity-40" : ""} ${over === at && dragged !== undefined && dragged !== at ? (dragged < at ? "shadow-[inset_0_-2px_0_var(--color-accent)]" : "shadow-[inset_0_2px_0_var(--color-accent)]") : ""}`}
            >
              {reorder && (
                <td className="w-0 py-1.5 pl-2">
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
              {row.cells.map((cell, i) => (
                <td key={i} className="px-3 py-1.5">
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
                <td className="px-3 py-1.5 text-right whitespace-nowrap">
                  {row.actions.map((a) => (
                    <button
                      key={a.label}
                      type="button"
                      disabled={a.disabled}
                      onClick={a.onClick}
                      // Named for its row too, like Rename Open, among the same button on every row.
                      aria-label={typeof row.cells[0] === "string" && row.cells[0] ? `${a.label} ${row.cells[0]}` : undefined}
                      className="ml-3 font-medium text-accent hover:underline disabled:opacity-50"
                    >
                      {a.label}
                    </button>
                  ))}
                </td>
              )}
            </tr>
          ))}
        </tbody>
      </table>
      {rows.length === 0 && (
        <p className="px-4 py-6 text-center text-sm text-muted">
          {status === "loading" ? "Loading…" : status === "denied" ? "You can't see this." : search?.value.trim() ? "Nothing matches." : "Nothing here yet."}
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

  // Columns side by side, scrolled across when there are many, each with its cards and
  // how many; a card dragged dims the columns it can't go to and marks the one it's
  // over. Alt+← and Alt+→ move a focused card to the nearest column it can go to.
  Board: function RadixBoard({ status, columns, error, onMove, search, tools }) {
    const [dragged, setDragged] = useState<{ card: string; from: string; reaches: string[] }>();
    const [over, setOver] = useState<string>();
    const reachable = (column: string) => !dragged || column === dragged.from || dragged.reaches.includes(column);
    const drop = () => {
      setDragged(undefined);
      setOver(undefined);
    };
    return (
      <div className="flex flex-col gap-2">
        {(search || tools) && (
          <div className="flex flex-wrap items-center justify-end gap-2">
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
        <div className="flex items-start gap-3 overflow-x-auto pb-2" aria-busy={status === "loading"}>
          {columns.map((column, at) => (
            <section
              key={column.id}
              aria-label={column.title}
              onDragOver={(event) => {
                if (!dragged?.reaches.includes(column.id)) return;
                event.preventDefault();
                setOver(column.id);
              }}
              onDrop={(event) => {
                event.preventDefault();
                if (dragged && onMove && dragged.reaches.includes(column.id)) onMove(dragged.card, column.id);
                drop();
              }}
              className={`flex w-72 shrink-0 flex-col rounded-box border bg-sunken/70 transition-opacity ${reachable(column.id) ? "" : "opacity-40"} ${
                over === column.id ? "border-accent ring-2 ring-accent/30" : "border-line"
              }`}
            >
              <h3 className="flex items-center justify-between gap-2 px-3 pt-2.5 pb-1.5 text-[0.8rem] font-medium text-muted">
                {column.title}
                <span className="rounded-full bg-surface px-1.5 text-xs tabular-nums">{column.cards.length}</span>
              </h3>
              <ul className="flex min-h-16 flex-col gap-2 px-2 pb-2">
                {column.cards.map((card) => {
                  const movable = Boolean(onMove) && card.reaches.length > 0;
                  return (
                    <li
                      key={card.id}
                      draggable={movable ? true : undefined}
                      tabIndex={movable && !card.link ? 0 : undefined}
                      onDragStart={() => setDragged({ card: card.id, from: column.id, reaches: card.reaches })}
                      onDragEnd={drop}
                      onKeyDown={(event) => {
                        if (!onMove || !event.altKey || (event.key !== "ArrowLeft" && event.key !== "ArrowRight")) return;
                        const step = event.key === "ArrowLeft" ? -1 : 1;
                        for (let next = at + step; next >= 0 && next < columns.length; next += step) {
                          if (!card.reaches.includes(columns[next].id)) continue;
                          event.preventDefault();
                          onMove(card.id, columns[next].id);
                          return;
                        }
                      }}
                      className={`rounded-box border border-line bg-surface px-3 py-2 text-sm shadow-panel focus-visible:ring-2 focus-visible:ring-accent focus-visible:outline-hidden ${
                        movable ? "cursor-grab active:cursor-grabbing" : ""
                      } ${dragged?.card === card.id ? "opacity-40" : ""}`}
                    >
                      {card.link ? (
                        <a {...card.link} draggable={false} className="font-medium text-ink hover:text-accent hover:underline">
                          {card.title}
                        </a>
                      ) : (
                        <span className="font-medium">{card.title}</span>
                      )}
                      {card.details.length > 0 && (
                        <div className="mt-1.5 flex flex-wrap items-center gap-x-2.5 gap-y-1 text-xs text-muted">
                          {card.details.map((detail, i) => (
                            <span key={i} className="inline-flex items-center">
                              {detail}
                            </span>
                          ))}
                        </div>
                      )}
                    </li>
                  );
                })}
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
            ) : field.choices ? (
              <select
                id={`${id}-${field.name}`}
                name={field.name}
                value={field.value}
                onChange={(event) => field.onChange(event.target.value)}
                aria-describedby={field.hint ? `${id}-${field.name}-hint` : undefined}
                className="h-10 rounded-control border border-control-line/60 bg-surface px-3 text-base shadow-panel transition-colors hover:border-control-line focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none sm:text-sm"
              >
                <option value="">Choose one</option>
                {field.choices.map(([value, shown]) => (
                  <option key={value} value={value}>
                    {shown}
                  </option>
                ))}
              </select>
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

  Button: ({ kind = "secondary", disabled, error, onClick, children }) => {
    const pressed = (
      <button
        type="button"
        disabled={disabled}
        onClick={onClick}
        className={`self-start ${pressable} ${button[kind]}`}
      >
        {children}
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

  Dialog: ({ open, title, onClose, children }) => (
    <Dialog.Root open={open} onOpenChange={(next) => !next && onClose()}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 bg-ink/30 backdrop-blur-[2px]" />
        <Dialog.Content
          aria-describedby={undefined}
          className="fixed top-1/2 left-1/2 flex max-h-[calc(100dvh-2rem)] w-[min(28rem,calc(100vw-2rem))] -translate-x-1/2 -translate-y-1/2 flex-col gap-4 overflow-y-auto rounded-box border border-line bg-surface p-6 text-ink shadow-raised"
        >
          <div className="flex items-start justify-between gap-4">
            <Dialog.Title className="text-lg font-semibold">{title}</Dialog.Title>
            <Dialog.Close aria-label="Close" className="-mt-1 -mr-2 rounded-box px-2 text-xl leading-8 text-muted hover:text-ink">
              <span aria-hidden="true">×</span>
            </Dialog.Close>
          </div>
          <div className="flex flex-col gap-4 [&>button]:self-auto">{children}</div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  ),

  // Nothing written, like an issue without a description, takes no room.
  Markdown: ({ status, source }) =>
    status === "live" && source?.trim() ? (
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
  Details: ({ items }) => (
    <dl className="grid max-w-xl grid-cols-[max-content_1fr] gap-x-6 gap-y-2 rounded-box border border-line bg-surface px-4 py-3 text-sm">
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

  // Two columns: what the page is about, wide, and what's said of it, narrow, beside
  // it on a wide screen and after it on a phone. Any other layout is one column.
  Layout: ({ name, regions }) =>
    name === "two_columns" ? (
      <div className="grid gap-10 lg:grid-cols-[minmax(0,1fr)_18rem] lg:items-start">
        <div className="flex min-w-0 flex-col gap-10">{regions.main}</div>
        <aside className="flex min-w-0 flex-col gap-6">{regions.side}</aside>
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
  Timeline: ({ status, entries }) =>
    status !== "live" || entries.length === 0 ? null : (
      <ol className="relative ml-1.5 flex flex-col gap-3 border-l border-line pl-5 text-sm">
        {entries.map((entry) => (
          <li key={entry.id} className="relative">
            <span aria-hidden="true" className="absolute top-1.5 -left-[1.6rem] size-2.5 rounded-full border-2 border-page bg-muted" />
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
    ),

  Picture: ({ source }) => (
    <img
      src={source}
      alt=""
      loading="lazy"
      referrerPolicy="no-referrer"
      className="one-person inline-block size-6 rounded-full border border-line object-cover"
    />
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
