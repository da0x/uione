// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// A component set that draws everything as plain, semantic HTML with no styling. The
// tests use it, and it shows that nothing in @uione/react depends on any one look.

import { useId } from "react";
import type { ComponentSet } from "./contract.js";

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

  Link: ({ href, onClick, children }) => (
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

  Table: ({ status, columns, rows, error }) => (
    <>
      <table aria-busy={status === "loading"}>
        <thead>
          <tr>
            {columns.map((column) => (
              <th key={column}>{column}</th>
            ))}
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
                  <button type="button" disabled={a.disabled} onClick={a.onClick}>
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
        {fields.map((field) => (
          <p key={field.name}>
            <label htmlFor={`${id}-${field.name}`}>{field.label}</label>
            {field.type === "markdown" ? (
              <textarea
                id={`${id}-${field.name}`}
                name={field.name}
                value={field.value}
                onChange={(event) => field.onChange(event.target.value)}
              />
            ) : (
              <input
                id={`${id}-${field.name}`}
                name={field.name}
                type={field.type === "list" ? "text" : field.type}
                placeholder={field.type === "list" ? "separated by commas" : undefined}
                value={field.value}
                onChange={(event) => field.onChange(event.target.value)}
              />
            )}
            {field.hint && <small>{field.hint}</small>}
          </p>
        ))}
        {error && <p role="alert">{error}</p>}
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

  Dialog: ({ open, title, children }) =>
    open ? (
      <div role="dialog" aria-modal="true" aria-label={title}>
        <h2>{title}</h2>
        {children}
      </div>
    ) : null,

  // The plain set has no Markdown renderer, so it shows what was written as it is.
  Markdown: ({ status, source }) =>
    status === "live" && source ? <div style={{ whiteSpace: "pre-wrap" }}>{source}</div> : <div aria-busy={status === "loading"} />,

  Picture: ({ source }) => <img src={source} alt="" width={24} height={24} referrerPolicy="no-referrer" />,

  Live: ({ status, value }) =>
    status === "live" ? <span>{value}</span> : <span aria-busy="true" aria-label="not available right now" />,
};
