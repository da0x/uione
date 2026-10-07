// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// A component set that draws everything as plain, semantic HTML with no styling. The
// tests use it, and it shows that nothing in @uione/react depends on any one look.

import { useEffect, useId, useRef } from "react";
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
  Page: ({ name, icon, home, nav, title, account, children }) => (
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
        {title !== name && <h1>{title}</h1>}
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

  Table: ({ status, columns, rows, error, tabs }) => {
    const actions = Math.max(0, ...rows.map((row) => row.actions.length));
    return (
      <>
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
            {rows.map((row) => (
              <tr key={row.id}>
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
              </tr>
            ))}
          </tbody>
        </table>
        {error && <p role="alert">{error}</p>}
      </>
    );
  },

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
