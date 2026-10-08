// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// A component set that draws everything as plain, semantic HTML with no styling. The
// tests use it, and it shows that nothing in @uione/react depends on any one look.

import { useEffect, useId, useRef, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import type { ComponentSet, DialogProps } from "./contract.js";

// Read by screen readers, not shown.
const hidden: CSSProperties = {
  position: "absolute",
  width: 1,
  height: 1,
  margin: -1,
  padding: 0,
  border: 0,
  overflow: "hidden",
  clip: "rect(0 0 0 0)",
  whiteSpace: "nowrap",
};

// What a row is called, for its buttons: the first cell that's text.
function rowName(cells: ReactNode[]): string | undefined {
  const cell = cells.find((c) => (typeof c === "string" && c !== "") || typeof c === "number");
  return cell === undefined ? undefined : String(cell);
}

function PlainDialog({ open, title, onClose, children }: DialogProps) {
  const ref = useRef<HTMLDialogElement>(null);
  const heading = useId();
  useEffect(() => {
    const dialog = ref.current;
    if (!open || !dialog) return;
    const before = document.activeElement;
    // showModal keeps focus inside and closes on Escape; jsdom doesn't have it.
    if (typeof dialog.showModal === "function") dialog.showModal();
    else dialog.setAttribute("open", "");
    return () => {
      if (before instanceof HTMLElement && before.isConnected) before.focus();
    };
  }, [open]);
  if (!open) return null;
  return (
    <dialog
      ref={ref}
      aria-labelledby={heading}
      onCancel={(event) => {
        // Closing is the screen's decision, so Escape asks for it rather than closing.
        event.preventDefault();
        onClose();
      }}
      onClose={onClose}
    >
      <h2 id={heading}>{title}</h2>
      {children}
    </dialog>
  );
}

export const plain: ComponentSet = {
  Page: ({ name, icon, home, nav, title, account, heading, crumbs, subtitle, children }) => (
    <>
      <header>
        <a {...home}>
          {icon && <img src={icon} alt="" width={24} height={24} />}
          {name}
        </a>
        <nav>
          {nav.map((item) => (
            <a key={item.href} href={item.href} onClick={item.onClick} aria-current={item.current ? "page" : undefined}>
              {item.label}
            </a>
          ))}
        </nav>
        {account}
      </header>
      <main>
        {crumbs}
        {title !== name && <h1>{title}</h1>}
        {subtitle}
        {heading}
        {children}
      </main>
    </>
  ),

  Account: ({ name, ready, error, onSignIn, onSignOut }) =>
    !ready ? null : (
      <span>
        {name === undefined ? (
          <button type="button" onClick={onSignIn}>
            Sign in
          </button>
        ) : (
          <>
            {name}{" "}
            <button type="button" onClick={onSignOut}>
              Sign out
            </button>
          </>
        )}
        {error && <span role="alert"> {error}</span>}
      </span>
    ),

  SignIn: ({ methods, busy, error, onChoose }) => (
    <div>
      {methods.map(({ id, name, Mark }) => (
        <p key={id}>
          <button type="button" disabled={busy !== undefined} onClick={() => onChoose(id)}>
            {Mark && <Mark />} {busy === id ? `Signing in with ${name}…` : `Continue with ${name}`}
          </button>
        </p>
      ))}
      {error && <p role="alert">{error}</p>}
    </div>
  ),

  Actions: ({ children }) => <div>{children}</div>,

  Layout: ({ regions }) => (
    <>
      {Object.entries(regions).map(([name, content]) => (
        <div key={name} data-region={name}>
          {content}
        </div>
      ))}
    </>
  ),

  Labels: ({ items }) => <span>{items.join(", ")}</span>,

  Details: ({ items }) => (
    <dl>
      {items.map((item) => (
        <div key={item.label}>
          <dt>{item.label}</dt>
          <dd>{item.value}</dd>
        </div>
      ))}
    </dl>
  ),

  Thread: ({ status, entries }) =>
    status !== "live" || entries.length === 0 ? (
      <p>{status === "loading" ? "Loading…" : "Nothing here yet."}</p>
    ) : (
      <ol>
        {entries.map((entry) => (
          <li key={entry.id}>
            <p>
              <strong>{entry.author}</strong> {entry.when}
            </p>
            {entry.body}
          </li>
        ))}
      </ol>
    ),

  Timeline: ({ status, entries }) =>
    status !== "live" || entries.length === 0 ? null : (
      <ul>
        {entries.map((entry) => (
          <li key={entry.id}>
            {entry.who} {entry.what} {entry.subject && (entry.link ? <a {...entry.link}>{entry.subject}</a> : entry.subject)} {entry.after}, {entry.when}
          </li>
        ))}
      </ul>
    ),

  Hero: ({ title, children }) => (
    <section>
      <h1>{title}</h1>
      {children}
    </section>
  ),

  Section: ({ title, id, children }) => (
    <section id={id}>
      <h2>{title}</h2>
      {children}
    </section>
  ),

  Text: ({ children }) => <p>{children}</p>,

  Consent: ({ onAnswer }) => (
    <aside aria-label="Counting visits">
      <p>This site counts its visits with Google Analytics, to see how it's used. May it use cookies to do that?</p>
      <button type="button" onClick={() => onAnswer(true)}>
        Allow
      </button>
      <button type="button" onClick={() => onAnswer(false)}>
        No thanks
      </button>
    </aside>
  ),

  Link: ({ href, onClick, external, children }) =>
    external ? (
      <a href={href} target="_blank" rel="noreferrer" title="Opens in a new tab">
        {children} <span aria-hidden="true">↗</span>
      </a>
    ) : (
      <a href={href} onClick={onClick}>
        {children}
      </a>
    ),

  Code: ({ lang, source }) => (
    <pre>
      <code data-lang={lang}>{source}</code>
    </pre>
  ),

  Pages: ({ pages, title, html }) => (
    <>
      <nav aria-label="Pages">
        {pages.map((page) => (
          <a key={page.href} href={page.href} onClick={page.onClick} aria-current={page.current ? "page" : undefined}>
            {page.title}
          </a>
        ))}
      </nav>
      {html === undefined ? (
        <p>There's no page here.</p>
      ) : (
        <article aria-label={title} dangerouslySetInnerHTML={{ __html: html }} />
      )}
    </>
  ),

  Menu: ({ links, children }) => (
    <>
      <nav aria-label="Menu">
        {links.map((item) => (
          <a key={item.href} href={item.href} onClick={item.onClick} aria-current={item.current ? "page" : undefined}>
            {item.title}
          </a>
        ))}
      </nav>
      <div>{children}</div>
    </>
  ),

  Table: ({ status, columns, rows, error, tabs, search, pages, reorder, tools, filtered }) => {
    const actions = Math.max(0, ...rows.map((row) => row.actions.length));
    return (
      <>
        {filtered && (
          <p>
            {filtered.label}{" "}
            <button type="button" onClick={filtered.onClear}>
              Clear
            </button>
          </p>
        )}
        {search && <input type="search" value={search.value} aria-label={search.label} placeholder={search.label} onChange={(event) => search.onChange(event.target.value)} />}
        {tools}
        {pages && (
          <p>
            <button type="button" disabled={pages.page <= 1} onClick={() => pages.onPage(pages.page - 1)}>
              Previous
            </button>{" "}
            Page {pages.page} of {pages.count}{" "}
            <button type="button" disabled={pages.page >= pages.count} onClick={() => pages.onPage(pages.page + 1)}>
              Next
            </button>
          </p>
        )}
        {tabs && (
          <div role="tablist">
            {tabs.map((tab) => (
              <button key={tab.label} type="button" role="tab" aria-selected={tab.selected} onClick={tab.onSelect}>
                {tab.label} {tab.count}
              </button>
            ))}
          </div>
        )}
        <table aria-busy={status === "loading"}>
          <thead>
            <tr>
              {reorder && (
                <th>
                  <span style={hidden}>Order</span>
                </th>
              )}
              {columns.map((column) => (
                <th key={column}>{column}</th>
              ))}
              {actions > 0 && (
                <th colSpan={actions}>
                  <span style={hidden}>Actions</span>
                </th>
              )}
            </tr>
          </thead>
          <tbody>
            {rows.map((row, at) => (
              <tr key={row.id}>
                {reorder && (
                  <td>
                    {/* Moved a place at a time, with Alt and the arrow keys. */}
                    <button
                      type="button"
                      aria-label={[reorder.label, rowName(row.cells)].filter(Boolean).join(" ")}
                      onKeyDown={(event) => {
                        if (!event.altKey || (event.key !== "ArrowUp" && event.key !== "ArrowDown")) return;
                        event.preventDefault();
                        reorder.onMove(at, event.key === "ArrowUp" ? at - 1 : at + 1);
                      }}
                    >
                      ⠿
                    </button>
                  </td>
                )}
                {row.cells.map((cell, i) => (
                  <td key={i}>{i === 0 && row.link ? <a {...row.link}>{cell}</a> : cell}</td>
                ))}
                {row.actions.map((a) => (
                  <td key={a.label}>
                    <button
                      type="button"
                      aria-label={[a.label, rowName(row.cells)].filter(Boolean).join(" ")}
                      disabled={a.disabled}
                      onClick={a.onClick}
                    >
                      {a.label}
                    </button>
                  </td>
                ))}
                {row.actions.length < actions && <td colSpan={actions - row.actions.length} />}
              </tr>
            ))}
          </tbody>
        </table>
        {error && <p role="alert">{error}</p>}
      </>
    );
  },

  Cards: ({ status, cards }) => (
    <div aria-busy={status === "loading"}>
      {cards.map((card) => (
        <article key={card.id}>
          <h2>{card.link ? <a {...card.link}>{card.title}</a> : card.title}</h2>
          {card.details.map((detail, i) => (
            <p key={i}>{detail}</p>
          ))}
          {card.tally && (
            <p>
              {card.tally.reduce((n, part) => n + part.count, 0)} {card.noun ?? "in all"}
              {card.tally.map((part) => `, ${part.label} ${part.count}`).join("")}
            </p>
          )}
          {card.filters.length > 0 && (
            <ul>
              {card.filters.map((f) => (
                <li key={f.label}>
                  <a {...f.link}>{f.label}</a>
                </li>
              ))}
            </ul>
          )}
        </article>
      ))}
    </div>
  ),

  Board: ({ status, columns, error, onMove, search, tools, filtered }) => (
    <>
      {filtered && (
        <p>
          {filtered.label}{" "}
          <button type="button" onClick={filtered.onClear}>
            Clear
          </button>
        </p>
      )}
      {search && <input type="search" value={search.value} aria-label={search.label} placeholder={search.label} onChange={(event) => search.onChange(event.target.value)} />}
      {tools}
      <div aria-busy={status === "loading"}>
        {columns.map((column) => (
          <section key={column.id} aria-label={column.title}>
            <h3>
              {column.title} ({column.cards.length})
            </h3>
            <ul>
              {column.cards.map((card) => (
                <li key={card.id}>
                  {card.link ? <a {...card.link}>{card.title}</a> : card.title}
                  {card.details.map((detail, i) => (
                    <span key={i}> {detail}</span>
                  ))}
                  {/* Where it may go, a button for each, rather than a drag. */}
                  {onMove &&
                    card.reaches.map((to) => {
                      const target = columns.find((c) => c.id === to);
                      return target ? (
                        <button key={to} type="button" onClick={() => onMove(card.id, to)}>
                          Move to {target.title}
                        </button>
                      ) : null;
                    })}
                </li>
              ))}
            </ul>
          </section>
        ))}
      </div>
      {error && <p role="alert">{error}</p>}
    </>
  ),

  Crumbs: ({ items, current }) => (
    <nav aria-label="Breadcrumb">
      <ol>
        {items.map((item, at) => (
          <li key={at}>
            <a {...item.link}>{item.label}</a>
          </li>
        ))}
        <li aria-current="page">{current}</li>
      </ol>
    </nav>
  ),

  // The arrows as a list, each a button, and a form that adds one between two.
  Diagram: function PlainDiagram({ status, nodes, edges, onConnect, error }) {
    const [from, setFrom] = useState("");
    const [to, setTo] = useState("");
    const named = (id: string) => nodes.find((n) => n.id === id)?.label ?? id;
    return (
      <div aria-busy={status === "loading"}>
        <ul>
          {edges.map((edge) => (
            <li key={`${edge.from}-${edge.to}`}>
              {edge.onClick ? (
                <button type="button" onClick={edge.onClick}>
                  {edge.title}
                </button>
              ) : (
                edge.title
              )}
            </li>
          ))}
        </ul>
        {onConnect && (
          <form
            onSubmit={(event) => {
              event.preventDefault();
              if (from && to) onConnect(from, to);
            }}
          >
            <label>
              From{" "}
              <select value={from} onChange={(event) => setFrom(event.target.value)}>
                <option value="">Choose one</option>
                {nodes.map((n) => (
                  <option key={n.id} value={n.id}>
                    {n.label}
                  </option>
                ))}
              </select>
            </label>{" "}
            <label>
              to{" "}
              <select value={to} onChange={(event) => setTo(event.target.value)}>
                <option value="">Choose one</option>
                {nodes.map((n) => (
                  <option key={n.id} value={n.id}>
                    {n.label}
                  </option>
                ))}
              </select>
            </label>{" "}
            <button type="submit">Add{from && to ? ` ${named(from)} to ${named(to)}` : ""}</button>
          </form>
        )}
        {error && <p role="alert">{error}</p>}
      </div>
    );
  },

  Switch: ({ label, options }) => (
    <div role="group" aria-label={label}>
      {options.map((option) => (
        <button key={option.label} type="button" aria-pressed={option.selected} onClick={option.onSelect}>
          {option.label}
        </button>
      ))}
    </div>
  ),

  Grid: ({ status, corner, columns, rows, error }) => (
    <>
      <table aria-busy={status === "loading"}>
        <thead>
          <tr>
            <th>{corner}</th>
            {columns.map((column, i) => (
              <th key={i} scope="col">
                {column}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((row, i) => (
            <tr key={i}>
              <th scope="row">{row.label}</th>
              {row.cells.map((cell, j) => (
                <td key={j}>
                  {cell.self ? (
                    "—"
                  ) : cell.onClick ? (
                    <button type="button" aria-label={cell.label} onClick={cell.onClick}>
                      {cell.text || "·"}
                    </button>
                  ) : (
                    cell.text || "·"
                  )}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
      {error && <p role="alert">{error}</p>}
    </>
  ),

  Form: function PlainForm({ fields, submit, busy, error, onSubmit }) {
    const id = useId();
    return (
      <form
        onSubmit={(event) => {
          event.preventDefault();
          onSubmit();
        }}
      >
        {fields.map((field) => {
          const hint = `${id}-${field.name}-hint`;
          const described = [field.hint ? hint : "", error ? `${id}-error` : ""].filter(Boolean).join(" ") || undefined;
          const common = {
            id: `${id}-${field.name}`,
            name: field.name,
            value: field.value,
            "aria-describedby": described,
            "aria-invalid": error ? true : undefined,
          };
          return (
            <p key={field.name}>
              <label htmlFor={`${id}-${field.name}`}>{field.label}</label>
              {field.type === "markdown" ? (
                <textarea {...common} onChange={(event) => field.onChange(event.target.value)} />
              ) : field.type === "choices" && field.choices ? (
                // Several choices, each a box to tick, sent as the list of those ticked.
                <span role="group" aria-label={field.label}>
                  {field.choices.map(([value, shown]) => {
                    const ticked = field.value.split(",").map((item) => item.trim()).filter((item) => item !== "");
                    return (
                      <label key={value}>
                        <input
                          type="checkbox"
                          checked={ticked.includes(value)}
                          onChange={(event) => field.onChange((event.target.checked ? [...ticked, value] : ticked.filter((item) => item !== value)).join(", "))}
                        />
                        {shown}
                      </label>
                    );
                  })}
                </span>
              ) : field.type === "boolean" ? (
                <input
                  {...common}
                  type="checkbox"
                  value={undefined}
                  checked={field.value === "true"}
                  onChange={(event) => field.onChange(event.target.checked ? "true" : "false")}
                />
              ) : field.choices ? (
                <select {...common} onChange={(event) => field.onChange(event.target.value)}>
                  <option value="">Choose one</option>
                  {field.choices.map(([value, shown]) => (
                    <option key={value} value={value}>
                      {shown}
                    </option>
                  ))}
                </select>
              ) : (
                <input
                  {...common}
                  type={field.type === "list" ? "text" : field.type}
                  placeholder={field.type === "list" ? "separated by commas" : undefined}
                  onChange={(event) => field.onChange(event.target.value)}
                />
              )}
              {field.hint && <small id={hint}>{field.hint}</small>}
            </p>
          );
        })}
        {error && (
          <p role="alert" id={`${id}-error`}>
            {error}
          </p>
        )}
        <button type="submit" disabled={busy}>
          {submit}
        </button>
      </form>
    );
  },

  Button: ({ disabled, error, onClick, children }) => (
    <>
      <button type="button" disabled={disabled} onClick={onClick}>
        {children}
      </button>
      {error && <span role="alert">{error}</span>}
    </>
  ),

  Dialog: PlainDialog,

  // The plain set has no Markdown renderer, so it shows what was written as it is.
  Markdown: ({ status, source }) =>
    status === "live" && source ? <div style={{ whiteSpace: "pre-wrap" }}>{source}</div> : <div aria-busy={status === "loading"} />,

  Picture: ({ source }) => <img src={source} alt="" width={24} height={24} referrerPolicy="no-referrer" />,

  Live: ({ status, value }) =>
    status === "live" ? (
      <span>{value}</span>
    ) : (
      <span aria-busy="true">
        <span style={hidden}>not available right now</span>
      </span>
    ),
};
