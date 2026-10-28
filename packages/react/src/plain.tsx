// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A component set that draws everything as plain, semantic HTML with no styling. The
// tests use it, and it shows that nothing in @uione/react depends on any one look.

import { useEffect, useId, useRef, useState } from "react";
import type { CSSProperties, ReactNode } from "react";
import type { ComponentSet, DialogProps } from "./contract.js";
import { Surface } from "./surface.js";

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

// What a tally's part means, said: not started, under way, done or failed.
const tallyWords: Record<string, string> = { none: "not started", partial: "under way", done: "done", failed: "failed" };

export const plain: ComponentSet = {
  Page: ({ name, icon, home, nav, title, untitled, account, heading, crumbs, subtitle, badges, footer, children }) => (
    <>
      <header>
        <a {...home}>
          {icon && <img src={icon} alt="" width={24} height={24} />}
          {name}
          {badges?.filter((badge) => badge.count > 0).map((badge) => ` (${badge.count} ${badge.label})`)}
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
        {!untitled && <h1>{title}</h1>}
        {subtitle}
        {heading}
        {children}
      </main>
      {footer && (
        <footer data-layout={footer.layout}>
          {footer.children}
        </footer>
      )}
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

  Group: ({ name, children }) => (
    <div role="group" aria-label={name}>
      {children}
    </div>
  ),

  Messages: ({ text, problem }) => (
    <span role="status" data-problem={problem ? "" : undefined}>
      {text}
    </span>
  ),

  Layout: ({ name, regions, site }) => (
    <>
      {Object.entries(regions).map(([region, content]) => (
        <div key={region} data-region={region}>
          {name === "workspace" && region === "bar" && site && <strong>{site} </strong>}
          {content}
        </div>
      ))}
    </>
  ),

  Labels: ({ items }) => <span>{items.join(", ")}</span>,

  // Every design, plainly: the hero, what it offers, its statement, its steps, then
  // what it asks.
  Landing: ({ design, overline, title, text, links, features, statement, steps, asks, before }) => {
    const offered = (list: typeof features, depth: number): ReactNode =>
      list.map((f, i) => (
        <section key={i}>
          {f.overline && <p>{f.overline}</p>}
          {depth === 2 ? <h2>{f.title}</h2> : <h3>{f.title}</h3>}
          {f.text && <p>{f.text}</p>}
          {offered(f.features, depth + 1)}
        </section>
      ));
    return (
      <div data-landing={design}>
        {before}
        <section>
          {overline && <p>{overline}</p>}
          <h1>{title}</h1>
          {text.map((words, i) => (
            <p key={i}>{words}</p>
          ))}
          {links.map((l, i) => (
            <a key={i} href={l.href} onClick={l.onClick}>
              {l.label}
            </a>
          ))}
        </section>
        {offered(features, 2)}
        {statement && <blockquote>{statement}</blockquote>}
        {steps.map((s, i) => (
          <section key={i} id={s.id}>
            {s.overline && <p>{s.overline}</p>}
            <h2>{s.title}</h2>
            <ol>
              {s.steps.map((step, at) => (
                <li key={at}>
                  <h3>{step.title}</h3>
                  <p>{step.text}</p>
                </li>
              ))}
            </ol>
          </section>
        ))}
        {asks.map((a, i) => (
          <section key={i} id={a.id}>
            {a.overline && <p>{a.overline}</p>}
            <h2>{a.title}</h2>
            {a.children}
          </section>
        ))}
      </div>
    );
  },

  Files: ({ items }) => (
    <span>
      {items.map((item, i) => (
        <span key={i}>
          {item.preview && <img src={item.preview} alt="" width={48} height={48} />}
          <button type="button" onClick={item.onOpen}>
            {item.name}
          </button>{" "}
        </span>
      ))}
    </span>
  ),

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

  Find: ({ label, query, onChange, groups }) => (
    <section>
      <input type="search" aria-label={label} placeholder={label} value={query} onChange={(event) => onChange(event.target.value)} />
      {groups.map((group) => (
        <ul key={group.label} aria-label={group.label}>
          {group.results.map((result) => (
            <li key={result.id}>{result.link ? <a {...result.link}>{result.text}</a> : result.text}</li>
          ))}
          {group.more > 0 && <li>and {group.more} more</li>}
        </ul>
      ))}
      {query.trim() && groups.length === 0 && <p>Nothing matches.</p>}
    </section>
  ),

  Timeline: ({ status, entries, title, fresh }) =>
    status !== "live" || entries.length === 0 ? null : (
      <ul aria-label={fresh ? `${title ?? "Changes"}, ${fresh} new` : title}>
        {entries.map((entry) => (
          <li key={entry.id}>
            {entry.fresh && "New: "}
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

  Pages: ({ pages, title, html, document }) => (
    <>
      {pages.length > 1 && (
        <nav aria-label="Pages">
          {pages.map((page) => (
            <a key={page.href} href={page.href} onClick={page.onClick} aria-current={page.current ? "page" : undefined}>
              {page.title}
            </a>
          ))}
        </nav>
      )}
      {html === undefined ? (
        <p>There's no page here.</p>
      ) : (
        <article aria-label={title} data-document={document || undefined} dangerouslySetInnerHTML={{ __html: html }} />
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

  Table: ({ status, columns, rows, error, tabs, search, pages, reorder, tools, filtered, empty }) => {
    const actions = Math.max(0, ...rows.map((row) => row.actions.length));
    const pictured = rows.some((row) => row.picture);
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
              {pictured && (
                <th>
                  <span style={hidden}>Picture</span>
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
              <tr key={row.id} aria-selected={row.onPick ? row.picked === true : undefined} onClick={row.onPick}>
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
                {pictured && <td>{row.picture?.source && <img src={row.picture.source} alt="" width={40} height={40} style={{ objectFit: row.picture.fit ? "contain" : "cover" }} />}</td>}
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
        {empty && status === "live" && rows.length === 0 && <p>{empty}</p>}
        {error && <p role="alert">{error}</p>}
      </>
    );
  },

  // A run of numbers as its title, latest value and scale, and the numbers themselves.
  Chart: ({ title, values, latest, topSaid, read }) => (
    <figure aria-label={title}>
      <figcaption>
        {title}: {latest} (of {topSaid})
      </figcaption>
      <ol data-series="">
        {values.map((v, i) => (
          <li key={i}>{read(v)}</li>
        ))}
      </ol>
    </figure>
  ),
  Charts: ({ charts }) => (
    <section>
      {charts.map((chart) => (
        <figure key={chart.title} aria-label={chart.title}>
          <figcaption>
            {chart.title}: {chart.latest} (of {chart.topSaid})
          </figcaption>
        </figure>
      ))}
    </section>
  ),
  Monitor: ({ status, tiles }) => (
    <section aria-busy={status === "loading"}>
      {tiles.map((tile) => (
        <article key={tile.id} data-tone={tile.tone}>
          <h3>{tile.link ? <a {...tile.link}>{tile.title}</a> : tile.title}</h3>
          <p>
            {tile.state}
            {tile.since ? ` ${tile.since}` : ""}
          </p>
          {tile.doing && <p>{tile.doing}</p>}
          {tile.progress && (
            <p>
              step {tile.progress.step} of {tile.progress.steps}
            </p>
          )}
          {tile.charts.map((chart) => (
            <p key={chart.title}>
              {chart.title}: {chart.latest}
            </p>
          ))}
        </article>
      ))}
    </section>
  ),
  FilePicture: ({ source, fit }) => <div style={{ height: 320 }}>{source && <img src={source} alt="" style={{ width: "100%", height: "100%", objectFit: fit ? "contain" : "cover" }} />}</div>,
  Cards: ({ status, cards }) => (
    <div aria-busy={status === "loading"}>
      {cards.map((card) => (
        <article key={card.id}>
          {card.picture?.source && <img src={card.picture.source} alt="" />}
          <h2>{card.link ? <a {...card.link}>{card.title}</a> : card.title}</h2>
          {card.tags && card.tags.length > 0 && <p>{card.tags.join(", ")}</p>}
          {card.details.map((detail, i) => (
            <p key={i}>{detail}</p>
          ))}
          {card.tally && (
            <p>
              {card.tally.reduce((n, part) => n + part.count, 0)}{" "}
              {card.noun ? (card.tally.reduce((n, part) => n + part.count, 0) === 1 ? card.noun.replace(/s$/, "") : card.noun) : "in all"}
              {card.tally.map((part) => `, ${part.label} ${part.count}${part.state ? ` ${tallyWords[part.state]}` : ""}`).join("")}
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

  Track: ({ label, steps }) => (
    <div role="group" aria-label={label}>
      {steps.map((step, i) => (
        <span key={step.id}>
          {i > 0 && <span aria-hidden="true"> → </span>}
          <button type="button" aria-pressed={step.selected} onClick={step.onSelect}>
            {step.status && <span role="img" aria-label={step.status.words}>● </span>}
            <strong>{step.title}</strong>
            {step.details.map((detail, at) => (
              <span key={at}> {detail}</span>
            ))}{" "}
            {step.count} {step.count === 1 ? step.noun.replace(/s$/, "") : step.noun}
          </button>
        </span>
      ))}
    </div>
  ),

  Toolbar: ({ search, tools, filtered }) => (
    <>
      {filtered && (
        <p>
          {filtered.label} <button type="button" onClick={filtered.onClear}>Show all</button>
        </p>
      )}
      {search && <input type="search" value={search.value} onChange={(event) => search.onChange(event.target.value)} placeholder={`${search.label}…`} aria-label={search.label} />}
      {tools}
    </>
  ),
  Pipeline: ({ status, lanes }) => (
    <div aria-busy={status === "loading"}>
      {lanes.map((lane) => (
        <section key={lane.title}>
          <h3>{lane.title}</h3>
          <ul>
            {lane.rows.map((row) => (
              <li key={row.id}>
                {row.link ? <a {...row.link}>{row.title}</a> : row.title}:{" "}
                {row.steps.map((step) => (step.at === "done" ? "✓ " : step.at === "failed" ? "✗ " : step.at === "now" ? "● " : "○ ") + step.title).join(" › ")}
                {row.note && ` (${row.note})`}
              </li>
            ))}
          </ul>
        </section>
      ))}
    </div>
  ),
  Canvas: ({ status, cards, grid, error, onPlace, onOpen, regions, onResize, onFiles }) => (
    <div aria-busy={status === "loading"}>
      {error && <p role="alert">{error}</p>}
      <Surface
        cards={cards}
        grid={grid}
        onPlace={onPlace}
        onOpen={onOpen}
        regions={regions}
        onResize={onResize}
        onFiles={onFiles}
        card={(card) => (
          <div style={{ padding: 8, overflow: "hidden", height: "100%", boxSizing: "border-box" }}>
            {card.picture && <img src={card.picture} alt="" style={{ display: "block", maxWidth: "100%" }} />}
            {card.status && <span title={card.status.words}>● </span>}
            {card.link ? <a {...card.link}>{card.title}</a> : <strong>{card.title}</strong>}
            {card.details.map((d, i) => (
              <div key={i}>{d}</div>
            ))}
          </div>
        )}
      />
    </div>
  ),
  // What's said as rows' times come: each a note, its title, what's under it, its
  // buttons, and a way to put it away.
  Notices: ({ notices }) => (
    <aside aria-label="Notices">
      {notices.map((n) => (
        <section key={n.key} role="status" aria-label={n.title}>
          <strong>{n.title}</strong>
          {n.body && <p>{n.body}</p>}
          {n.buttons.map((b) => (
            <button key={b.label} type="button" onClick={b.onClick}>
              {b.label}
            </button>
          ))}
          <button type="button" aria-label="Put away" onClick={n.onClose}>
            ×
          </button>
        </section>
      ))}
    </aside>
  ),
  // A calendar: what it's on and the ways back and on, then each day, a region named
  // by it, its rows a list, each picked with a click; a row is dragged to another day.
  Calendar: ({ status, title, days, error, back, next, onToday, onMove }) => (
    <div aria-busy={status === "loading"}>
      <h2>{title}</h2>
      <button type="button" onClick={back.onClick}>
        {back.label}
      </button>
      <button type="button" onClick={onToday}>
        Today
      </button>
      <button type="button" onClick={next.onClick}>
        {next.label}
      </button>
      {error && <p role="alert">{error}</p>}
      {days.map((day) => (
        <section
          key={day.date}
          aria-label={day.date}
          data-today={day.today ? "" : undefined}
          onDragOver={(event) => onMove && event.preventDefault()}
          onDrop={(event) => onMove && onMove(event.dataTransfer.getData("text/plain"), day.date)}
        >
          <h3>{day.label}</h3>
          <ul>
            {day.cards.map((card) => (
              <li
                key={card.id}
                aria-selected={card.onPick ? card.picked === true : undefined}
                onClick={card.onPick}
                draggable={onMove ? true : undefined}
                onDragStart={(event) => event.dataTransfer.setData("text/plain", card.id)}
              >
                {card.time && <time>{card.time}</time>} {card.title}
                {card.details.map((detail, i) => (
                  <span key={i}> · {detail}</span>
                ))}
              </li>
            ))}
          </ul>
          {day.more > 0 && <p>{day.more} more</p>}
        </section>
      ))}
    </div>
  ),
  Board: ({ status, columns, error, onMove, onArrange, search, tools, filtered }) => (
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
              {column.cards.map((card, at) => (
                <li key={card.id} aria-selected={card.onPick ? card.picked === true : undefined} onClick={card.onPick}>
                  {card.link ? <a {...card.link}>{card.title}</a> : card.title}
                  {card.details.length > 0 && (
                    <span data-details="">
                      {card.details.map((detail, i) => (
                        <span key={i}>{detail}</span>
                      ))}
                    </span>
                  )}
                  {card.people && card.people.length > 0 && (
                    <span data-people="">
                      {card.people.map((person, i) => (
                        <span key={i}>
                          {person.picture && <img src={person.picture} alt="" width={20} height={20} referrerPolicy="no-referrer" />} {person.name}
                        </span>
                      ))}
                    </span>
                  )}
                  {/* Up and down its column, a button each, rather than a drag. */}
                  {onArrange && at > 0 && (
                    <button type="button" onClick={() => onArrange(card.id, column.cards[at - 1].id)}>
                      Move up
                    </button>
                  )}
                  {onArrange && at < column.cards.length - 1 && (
                    <button type="button" onClick={() => onArrange(card.id, column.cards[at + 2]?.id)}>
                      Move down
                    </button>
                  )}
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
              ) : field.type === "file" || field.type === "files" ? (
                // What's there, each with how far it's come or why it didn't, then a
                // way to choose more.
                <span>
                  {field.files?.map((file, i) => (
                    <span key={i}>
                      {file.name}
                      {file.error ? `: ${file.error}` : file.progress !== undefined ? ` ${Math.round(file.progress * 100)}%` : ""}{" "}
                      <button type="button" aria-label={`Remove ${file.name}`} onClick={() => field.onRemove?.(i)}>
                        Remove
                      </button>{" "}
                    </span>
                  ))}
                  <input
                    id={common.id}
                    name={field.name}
                    type="file"
                    multiple={field.type === "files"}
                    aria-describedby={described}
                    onChange={(event) => {
                      const chosen = Array.from(event.target.files ?? []);
                      event.target.value = "";
                      if (chosen.length) field.onFiles?.(chosen);
                    }}
                  />
                </span>
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
                  {!field.required && <option value="">Choose one</option>}
                  {field.choices.map(([value, shown]) => (
                    <option key={value} value={value}>
                      {shown}
                    </option>
                  ))}
                </select>
              ) : field.type === "date" ? (
                // A day, and a time of day if it has one, as 2026-10-28 or 2026-10-28T09:00.
                <span>
                  <input
                    {...common}
                    type="date"
                    value={field.value.slice(0, 10)}
                    onChange={(event) => field.onChange(event.target.value === "" ? "" : event.target.value + (field.value.includes("T") ? field.value.slice(10) : ""))}
                  />
                  <input
                    type="time"
                    aria-label={`${field.label}, at`}
                    value={field.value.includes("T") ? field.value.slice(11, 16) : ""}
                    disabled={field.value === ""}
                    onChange={(event) => field.onChange(field.value.slice(0, 10) + (event.target.value === "" ? "" : "T" + event.target.value))}
                  />
                </span>
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

  Button: ({ disabled, error, onClick, children, pressed, hint }) => (
    <>
      <button type="button" disabled={disabled} onClick={onClick} aria-pressed={pressed} title={hint}>
        {children}
      </button>
      {error && <span role="alert">{error}</span>}
    </>
  ),

  Dialog: PlainDialog,

  // The plain set has no Markdown renderer, so it shows what was written as it is.
  Markdown: ({ status, source }) =>
    status === "live" && source ? <div style={{ whiteSpace: "pre-wrap" }}>{source}</div> : <div aria-busy={status === "loading"} />,

  Picture: ({ source, service }) => (service ? <span role="img" aria-label="A service" title="A service">🤖</span> : <img src={source} alt="" width={24} height={24} referrerPolicy="no-referrer" />),

  Live: ({ status, value }) =>
    status === "live" ? (
      <span>{value}</span>
    ) : (
      <span aria-busy="true">
        <span style={hidden}>not available right now</span>
      </span>
    ),

  Themes: ({ scheme, schemes, onScheme, seed, colors, written, copied, onNew, onReset, onCopy }) => (
    <section>
      <label>
        Scheme{" "}
        <select value={scheme} onChange={(event) => onScheme(event.target.value)}>
          {schemes.map(([value, shown]) => (
            <option key={value} value={value}>
              {shown}
            </option>
          ))}
        </select>
      </label>{" "}
      <button type="button" onClick={onNew}>
        New theme
      </button>
      {seed !== undefined && (
        <>
          {" "}
          <button type="button" onClick={onReset}>
            Back to the app's theme
          </button>
          <ul>
            {colors.map(([role, light, dark]) => (
              <li key={role}>
                {role} {light} {dark}
              </li>
            ))}
          </ul>
          <pre>{written}</pre>
          <button type="button" onClick={onCopy}>
            {copied ? "Copied" : "Copy"}
          </button>
        </>
      )}
    </section>
  ),
};
