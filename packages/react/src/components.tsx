// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// The components a generated screen is made of. Each one does the work, like running
// a command, asking before it does, or hiding a value that can't be trusted, and
// then hands the drawing to the app's component set.

import { Children, cloneElement, isValidElement, useContext, useEffect, useRef, useState } from "react";
import type { ReactNode } from "react";
import { useLocation, useNavigate, useParams } from "react-router";
import { partsOf } from "./keys.js";
import { ShownPath, fill, useConfirmContext, usePageTitle, useSignIn } from "./app.js";
import type { FieldProps, Filtered, Hue } from "./contract.js";
import { useAuth, useRunner } from "./data.js";
import type { CommandInput, ViewState } from "./data.js";
import { action, dateOnly, label, shortAddress, show, useLinks, useUI } from "./ui.js";

export function Hero({ title, children }: { title: string; children: ReactNode }) {
  const ui = useUI();
  return <ui.Hero title={title}>{children}</ui.Hero>;
}

export function Section({ title, id, hue, children }: { title: string; id?: string; hue?: Hue; children: ReactNode }) {
  const ui = useUI();
  return (
    <ui.Section title={title} id={id} {...(hue ? { hue } : {})}>
      {children}
    </ui.Section>
  );
}

export function Text({ children }: { children: ReactNode }) {
  const ui = useUI();
  return <ui.Text>{children}</ui.Text>;
}

// A link's :parameters come from the page it's on: /projects/:project/reports on the
// page /projects/uione goes to /projects/uione/reports.
export function Link({ to, icon, children }: { to: string; icon?: string; children: ReactNode }) {
  const ui = useUI();
  const link = useLinks();
  const params = useParams();
  const filled = to.replace(/:([A-Za-z_]\w*)/g, (written, name: string) => {
    const value = params[name];
    return value === undefined ? written : encodeURIComponent(value);
  });
  return (
    <ui.Link {...link(filled)} icon={icon}>
      {children}
    </ui.Link>
  );
}

export function Code({ lang, source }: { lang: string; source: string }) {
  const ui = useUI();
  return <ui.Code lang={lang} source={withoutLicense(source)} />;
}

// Code on a page is there to be read as an example, so a license header at the top
// of the file it comes from isn't shown: the header is about the file, not the idea.
export function withoutLicense(source: string): string {
  const lines = source.split("\n");
  if (!/^(\/\/|#) Copyright /.test(lines[0] ?? "")) return source;
  const end = lines.findIndex((line) => /SPDX-License-Identifier: /.test(line));
  if (end < 0) return source;
  let rest = end + 1;
  while (rest < lines.length && lines[rest]!.trim() === "") rest++;
  return lines.slice(rest).join("\n");
}

export interface DocPage {
  slug: string;
  title: string;
  html: string;
}

// A set of pages, one per address under `base`, like /docs/language. With no page in
// the address, the first one is shown.
export function Pages({ base, pages }: { base: string; pages: DocPage[] }) {
  const ui = useUI();
  const link = useLinks();
  const { page } = useParams();
  const { hash } = useLocation();
  const current = page === undefined ? pages[0] : pages.find((p) => p.slug === page);
  // An address naming a section of the page, like /language/reference#built-in-types,
  // goes to it once the page is drawn, since the browser looked for it before.
  useEffect(() => {
    if (!hash) return;
    const target = document.getElementById(decodeURIComponent(hash.slice(1)));
    target?.scrollIntoView?.({ block: "start" });
  }, [hash, current?.slug]);
  return (
    <ui.Pages
      pages={pages.map((p) => ({ title: p.title, current: p === current, ...link(`${base}/${p.slug}`) }))}
      title={current?.title}
      html={current?.html}
    />
  );
}

// Links down the side of the screen, each filled from the page's address like any
// link, the one for the page it's on marked, with the rest of the screen beside them.
export function Menu({ links, children }: { links: { to: string; label: string }[]; children: ReactNode }) {
  const ui = useUI();
  const link = useLinks();
  const params = useParams();
  const { pathname } = useLocation();
  const filled = links.map(({ to, label }) => {
    const address = to.replace(/:([A-Za-z_]\w*)/g, (written, name: string) => {
      const value = params[name];
      return value === undefined ? written : encodeURIComponent(value);
    });
    return { title: label, current: address === pathname, ...link(address) };
  });
  return <ui.Menu links={filled}>{children}</ui.Menu>;
}

// A Markdown field of a view, shown rendered by the component set.
export function Markdown({ view, field, plain }: { view: ViewState; field: string; plain?: boolean }) {
  const ui = useUI();
  const value = view.data?.[field];
  return <ui.Markdown status={view.status} source={typeof value === "string" ? value : undefined} plain={plain} />;
}

// A view's values, each beside what it is: a choice as it's shown, a list of words
// each on its own, and none of the ones with nothing in them.
export function Details({
  view,
  fields,
  choices = {},
  labels = [],
  tint,
}: {
  view: ViewState;
  fields: readonly (readonly [name: string, label: string])[];
  choices?: Record<string, Record<string, string>>;
  labels?: string[];
  tint?: Tint; // the box colored by how urgent what it's about is, like its priority
}) {
  const ui = useUI();
  if (view.status !== "live") return null;
  const items = fields.flatMap<{ label: string; value: ReactNode }>(([name, label]) => {
    const value = view.data?.[name];
    if (labels.includes(name) && Array.isArray(value)) {
      const words = value.map(show).filter((word) => word !== "");
      return words.length ? [{ label, value: <ui.Labels items={words} /> }] : [];
    }
    const said = typeof value === "string" ? (choices[name]?.[value] ?? value) : show(value);
    return said === "" ? [] : [{ label, value: said }];
  });
  return items.length ? <ui.Details items={items} tone={toneOf(view.data ?? {}, tint)} /> : null;
}

// One region of a screen's layout, like main: what's in it, which Layout places.
export function Region({ children }: { name: string; children?: ReactNode }) {
  return <>{children}</>;
}

// A screen laid out in regions, gathered from the Regions inside it by name.
export function Layout({ name, children }: { name: string; children?: ReactNode }) {
  const ui = useUI();
  const regions: Record<string, ReactNode> = {};
  Children.forEach(children, (child) => {
    if (isValidElement<{ name: string; children?: ReactNode }>(child) && child.type === Region) regions[child.props.name] = child.props.children;
  });
  return <ui.Layout name={name} regions={regions} />;
}

// Buttons one after another, in a row.
export function Actions({ children }: { children: ReactNode }) {
  const ui = useUI();
  return <ui.Actions>{children}</ui.Actions>;
}

// A time as milliseconds, from a date, its text or a number, or undefined.
function timeOf(value: unknown): number | undefined {
  const at = value instanceof Date ? value.getTime() : typeof value === "string" || typeof value === "number" ? new Date(value).getTime() : Number.NaN;
  return Number.isNaN(at) ? undefined : at;
}

// When something was written or changed, with the time of day, as a thread or a
// timeline says it.
function when(value: unknown): string {
  const at = value instanceof Date ? value : typeof value === "string" || typeof value === "number" ? new Date(value) : undefined;
  return at && !Number.isNaN(at.getTime()) ? at.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" }) : "";
}

// One of a view's lists as a thread: each row's author, picture, time and body, the
// body rendered as Markdown.
export function Thread({ view, list }: { view: ViewState; list: string }) {
  const ui = useUI();
  const entries = (view.status === "live" ? rowsOf(view.data?.[list]) : []).map((row) => ({
    id: row.id,
    author: show(row["author.name"]),
    picture: typeof row["author.picture"] === "string" ? (row["author.picture"] as string) : undefined,
    when: when(row.created_at),
    body: <ui.Markdown status="live" source={typeof row.body === "string" ? row.body : ""} />,
  }));
  return <ui.Thread status={view.status} entries={entries} />;
}

// A command's action as something done, like close as closed: the past tense of
// its first word, and the rest as it is, like take_over as took over.
const irregular: Record<string, string> = {
  take: "took", make: "made", give: "gave", send: "sent", leave: "left", begin: "began", write: "wrote", set: "set", put: "put",
  run: "ran", find: "found", hold: "held", keep: "kept", get: "got", bring: "brought", buy: "bought", pay: "paid", lead: "led",
};
const stressedLast = new Set(["transfer", "refer", "prefer", "defer", "confer", "infer", "admit", "commit", "submit", "permit", "omit", "compel", "expel", "occur", "recur", "regret", "control"]);
export function done(action: string): string {
  const [verb = "", ...rest] = action.split("_");
  const past = irregular[verb]
    ?? (verb.endsWith("e") ? `${verb}d`
    : /[^aeiou]y$/.test(verb) ? `${verb.slice(0, -1)}ied`
    : /^[^aeiou]*[aeiou][^aeiouwxy]$/.test(verb) ? `${verb}${verb.at(-1)}ed`
    // Words stressed on their last syllable double it too, though offer and visit don't.
    : stressedLast.has(verb) ? `${verb}${verb.at(-1)}ed`
    : `${verb}ed`);
  return [past, ...rest].join(" ");
}

// What a change did, in words, around what it changed: making it, a field set,
// cleared or changed, or, made by a command of its own like close, what that
// command did. [before, after] the thing, like ["changed title of", "from a to b"].
export function phrase(field: unknown, before: unknown, after: unknown, command?: unknown): [string, string] {
  // A change kept for something of its own, like a comment on an issue: its field
  // names what the command made, and it says so, like added a comment to.
  const parts = typeof command === "string" ? command.split("::") : [];
  const made = parts.length >= 2 ? parts[parts.length - 2] : "";
  if (typeof field === "string" && field !== "" && field === made) {
    const thing = made.replaceAll("_", " ");
    const did = parts[parts.length - 1];
    if (did === "create") return [`added a ${thing} to`, ""];
    if (did === "delete") return [`removed a ${thing} from`, ""];
    return [`changed a ${thing} on`, ""];
  }
  const verb = typeof command === "string" ? action(command) : "";
  // A command of its own says what it did, and from what to what when it says
  // that, like moved this from Reported to Triaged.
  if (verb && verb !== "create" && verb !== "update" && verb !== "delete") {
    // Where what it did says it already, like closed or reopened, it isn't said again.
    const [was, is] = [show(before), show(after)];
    const says = typeof field === "string" && field !== "" && was !== "" && is !== "" && !done(verb).toLowerCase().includes(is.toLowerCase());
    return [done(verb), says ? `from ${was} to ${is}` : ""];
  }
  if (typeof field !== "string" || field === "") return ["created", ""];
  const name = field.replaceAll("_", " ");
  const was = show(before);
  const is = show(after);
  // Something long, like a page's text, isn't said over: only that it changed.
  const long = (text: string) => text.length > 80 || text.includes("\n");
  if (long(was) || long(is)) return [was === "" ? `wrote ${name} of` : is === "" ? `cleared ${name} of` : `changed ${name} of`, ""];
  if (was === "") return [`set ${name} of`, `to ${is}`];
  if (is === "") return [`cleared ${name} of`, ""];
  return [`changed ${name} of`, `from ${was} to ${is}`];
}

// The same said of "this", as a thing's own timeline says it: closed this, changed
// status from open to closed.
export function changed(field: unknown, before: unknown, after: unknown, command?: unknown): string {
  const [head, tail] = phrase(field, before, after, command);
  return (head.endsWith(" of") ? `${head.slice(0, -3)}${tail ? ` ${tail}` : ""}` : `${head} this${tail ? ` ${tail}` : ""}`).trim();
}

// A list of changes with each command said once: one like close, setting status and
// closed_at together, keeps only its first change.
function onceEach(rows: Row[]): Row[] {
  // A field that went from nothing to nothing, as an older backend recorded when it
  // first wrote one empty, didn't change.
  const nothing = (value: unknown) => value === undefined || value === null || value === "" || (Array.isArray(value) && value.length === 0);
  // A change kept for something of its own, like a comment, says so with nothing
  // before or after, and stays.
  const made = (row: Row) => typeof row.action === "string" && row.action.split("::").at(-2) === row.field;
  rows = rows.filter((row) => made(row) || !(typeof row.field === "string" && row.field !== "" && "before" in row && "after" in row && nothing(row.before) && nothing(row.after)));
  return rows.filter((row, i) => {
    const before = rows[i - 1];
    const own = typeof row.action === "string" && !["create", "update", "delete"].includes(action(row.action));
    return !(own && before && before.action === row.action && String(before.created_at) === String(row.created_at));
  });
}

// One of a view's lists of an entity's changes as a timeline, oldest first as the
// view orders it. A command that changes several fields at once, like close
// setting status and closed_at, is said once.
export function Timeline({
  view,
  list,
  subject = [],
  link,
  keyed,
  named,
  title,
  since,
  seen,
}: {
  view: ViewState;
  list: string;
  title?: string; // what it's of, like "What's new", shown only while it holds something
  // When the person last looked, a value of a view: the changes after it are new.
  since?: { view: ViewState; field: string };
  seen?: string; // the command that says they've looked, run once there's something new
  subject?: string[]; // the columns naming what each change was to, like issue.number and issue.title
  link?: string; // where each change's subject is, like /:project/issues/:issue
  keyed?: string[]; // the key's parts before the last, when the link names them, like project
  named?: Record<string, number>; // parameters a row fills with the last part of an id keyed by that many, like a board in /:project/boards/:board
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const runner = useRunner();
  const rows = view.status === "live" ? rowsOf(view.data?.[list]) : [];
  // When they last looked, as it was when the page opened: saying they've looked
  // moves it on, but what was new stays marked until they leave. Never is before
  // everything.
  const [looked, setLooked] = useState<{ at: number }>();
  const [said, setSaid] = useState(false);
  const sinceStatus = since?.view.status;
  const sinceValue = since ? since.view.data?.[since.field] : undefined;
  useEffect(() => {
    if (since && !looked && sinceStatus === "live") setLooked({ at: timeOf(sinceValue) ?? Number.NEGATIVE_INFINITY });
  }, [since, looked, sinceStatus, sinceValue]);
  const fresh = (row: Row) => looked !== undefined && (timeOf(row.created_at) ?? 0) > looked.at;
  const freshCount = view.status === "live" ? onceEach(rows).filter(fresh).length : 0;
  useEffect(() => {
    if (!seen || said || freshCount === 0) return;
    setSaid(true);
    void runner.run(seen, {});
  }, [seen, said, freshCount, runner]);
  const entries = onceEach(rows).map((row) => {
    // A change no one made, like a migration's, was made by the system.
    const entry = { id: row.id, who: show(row["created_by.name"]) || "System", when: when(row.created_at), fresh: fresh(row) || undefined };
    if (subject.length === 0) return { ...entry, what: changed(row.field, row.before, row.after, row.action) };
    // A number is said as one, like #12.
    const words = subject.map((column) => (column.endsWith(".number") || column === "number" ? `#${show(row[column])}` : show(row[column]))).filter((part) => part !== "" && part !== "#");
    const [head, tail] = phrase(row.field, row.before, row.after, row.action);
    const to = link ? links(rowLink(link, row, params, keyed, named)) : undefined;
    return { ...entry, what: head, subject: words.join(" "), after: tail, link: to && { href: to.href, onClick: to.onClick } };
  });
  return <ui.Timeline status={view.status} entries={entries} title={title} fresh={since ? freshCount : undefined} />;
}

// The names in an address's query that are what's typed to find something, rather
// than what a list is filtered by: find, and a table's search.
export function shownState(name: string): boolean {
  return name === "find" || name === "search";
}

// What several names share at their start, up to the last dash in it, like a
// board's phases' project and board: a-main- of a-main-to_do and a-main-done.
function sharedStart(names: string[]): string {
  if (names.length < 2) return "";
  let start = names[0] ?? "";
  for (const name of names) while (!name.startsWith(start)) start = start.slice(0, -1);
  return start.slice(0, start.lastIndexOf("-") + 1);
}

// What's on screen kept in the page's address, under a name, like ?find=dates, so a
// link to the page shows what the person sees: setting it replaces the address
// rather than adding a step to go back through, and the default isn't written.
export function useAddressState(name: string, fallback = ""): [string, (value: string) => void] {
  const { search, pathname, hash } = useLocation();
  const navigate = useNavigate();
  const inAddress = new URLSearchParams(search).get(name) ?? fallback;
  // Shown at once as it's set, since the address changes a moment later, and taken
  // from the address when that changes some other way, like going back.
  const [value, setValue] = useState(inAddress);
  const written = useRef(inAddress);
  useEffect(() => {
    if (inAddress !== written.current) {
      written.current = inAddress;
      setValue(inAddress);
    }
  }, [inAddress]);
  caughtUp(pathname, search);
  const set = (next: string) => {
    setValue(next);
    written.current = next === "" ? fallback : next;
    // From any change still on its way, so two set together both stay.
    const now = pending ?? { path: pathname, search };
    const query = new URLSearchParams(now.search);
    if (next === fallback || next === "") query.delete(name);
    else query.set(name, next);
    const after = query.toString() ? `?${query.toString()}` : "";
    pending = { path: now.path, search: after };
    void navigate(`${now.path}${after}${hash}`, { replace: true });
  };
  return [value, set];
}

// The address last written, path and query, which the page hasn't caught up with
// yet: a second change made with the first, like a search and going back to its
// first page, starts from it rather than from the address as it was.
let pending: { path: string; search: string } | undefined;
function caughtUp(pathname: string, search: string) {
  if (pending && pending.path === pathname && pending.search === search) pending = undefined;
}

// What a page shows, said in its path after its own, like /neotrac/boards/main/board:
// the part of it that's one of these options, or the fallback when none is. Setting
// it writes that part, or takes it out for the fallback, keeping the others, and
// replaces the address rather than adding a step to go back through.
export function usePathPart(options: string[], fallback = ""): [string, (value: string) => void] {
  const { base, parts, open } = useContext(ShownPath);
  const [own, setOwn] = useState(fallback);
  const inPath = parts.find((part) => options.includes(part)) ?? fallback;
  const write = usePathWriter((now) => {
    const at = now.findIndex((part) => options.includes(part));
    const kept = now.filter((part) => !options.includes(part));
    return (value) => {
      if (value === fallback || value === "") return kept;
      const next = [...kept];
      next.splice(at < 0 ? next.length : Math.min(at, next.length), 0, value);
      return next;
    };
  }, base);
  return open ? [inPath, write] : [own, setOwn];
}

// A part said by its name and then its value, like page/2: the value, or "" when
// it's not there.
export function usePathNamed(name: string): [string, (value: string) => void] {
  const { base, parts, open } = useContext(ShownPath);
  const [own, setOwn] = useState("");
  const at = parts.indexOf(name);
  const inPath = at >= 0 ? (parts[at + 1] ?? "") : "";
  const write = usePathWriter((now) => {
    const where = now.indexOf(name);
    const kept = where >= 0 ? [...now.slice(0, where), ...now.slice(where + 2)] : now;
    return (value) => (value ? [...kept, name, value] : kept);
  }, base);
  return open ? [inPath, write] : [own, setOwn];
}

// Writes what a page shows into its path from the parts as they are now, counting
// a change still on its way, so two set together both stay.
function usePathWriter(change: (now: string[]) => (value: string) => string[], base: string): (value: string) => void {
  const { pathname, search, hash } = useLocation();
  const navigate = useNavigate();
  const { parts } = useContext(ShownPath);
  caughtUp(pathname, search);
  if (pendingParts && pendingParts.base === base && pendingParts.path === pathname) pendingParts = undefined;
  return (value) => {
    const now = pendingParts && pendingParts.base === base ? pendingParts.parts : parts;
    const next = change(now)(value);
    const path = `${base === "/" ? "" : base}/${next.map(encodeURIComponent).join("/")}`.replace(/\/$/, "") || "/";
    const query = pending ? pending.search : search;
    pendingParts = { base, parts: next, path };
    pending = { path, search: query };
    void navigate(`${path}${query}${hash}`, { replace: true });
  };
}

// The parts of a page's path last written, which it hasn't caught up with yet.
let pendingParts: { base: string; parts: string[]; path: string } | undefined;

// A box finding rows of several lists as it's typed in, like a project's issues by
// number, title and labels, and its wiki pages by title: each list's first eight,
// under its label, each opening its page. #12 or 12 finds the twelfth by number.
// What's typed is kept in the address, so a link shows the same results.
export function Find({
  label: words,
  sources,
}: {
  label: string;
  sources: { view: ViewState; list: string; label: string; link?: string; keyed?: string[]; by: string[] }[];
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const [query, setQuery] = useAddressState("find");
  const wanted = query.trim().toLowerCase();
  const number = /^#?(\d+)$/.exec(wanted)?.[1];
  const groups = !wanted
    ? []
    : sources.flatMap((source) => {
        const numbered = source.by.find((field) => field === "number" || field.endsWith(".number"));
        const named = source.by.find((field) => field !== numbered) ?? source.by[0];
        const found = rowsOf(source.view.data?.[source.list]).filter((row) =>
          number !== undefined && numbered
            ? String(row[numbered]) === number || source.by.some((field) => field !== numbered && show(row[field]).toLowerCase().includes(wanted))
            : source.by.some((field) => show(row[field]).toLowerCase().includes(wanted)),
        );
        if (found.length === 0) return [];
        const results = found.slice(0, 8).map((row) => {
          const to = source.link ? links(rowLink(source.link, row, params, source.keyed)) : undefined;
          const text = [numbered && row[numbered] !== undefined ? `#${show(row[numbered])}` : "", named ? show(row[named]) : ""].filter(Boolean).join(" ");
          return { id: row.id, text: text || row.id, link: to && { href: to.href, onClick: to.onClick } };
        });
        return [{ label: source.label, results, more: found.length - results.length }];
      });
  return <ui.Find label={words} query={query} onChange={setQuery} groups={groups} />;
}

// What a view holds, as Markdown to paste somewhere else whole: the page's title,
// each value, a markdown one as written under its name, and each list, a
// conversation as who wrote what and when, changes as what happened, each command
// once, and rows as their values.
export type CopiedField = readonly [name: string, label: string, kind?: "markdown"];
export type CopiedList = readonly [name: string, label: string, kind: "thread" | "changes" | "rows", columns: readonly string[]];

export function markdownOf(
  title: string,
  data: Record<string, unknown> | undefined,
  fields: readonly CopiedField[],
  lists: readonly CopiedList[],
  choices: Record<string, Record<string, string>> = {},
): string {
  const out: string[] = [];
  if (title) out.push(`# ${title}`, "");
  const value = (name: string) => {
    const stored = data?.[name];
    return (typeof stored === "string" ? choices[name]?.[stored] : undefined) ?? show(stored);
  };
  const said = fields.filter(([name, , kind]) => kind !== "markdown" && value(name) !== "");
  for (const [name, label] of said) out.push(`**${label}:** ${value(name)}  `);
  if (said.length) out.push("");
  for (const [name, label, kind] of fields) {
    const value = data?.[name];
    if (kind === "markdown" && typeof value === "string" && value.trim()) out.push(`## ${label}`, "", value.trim(), "");
  }
  for (const [name, label, kind, columns] of lists) {
    const rows = kind === "changes" ? onceEach(rowsOf(data?.[name])) : rowsOf(data?.[name]);
    if (rows.length === 0) continue;
    out.push(`## ${label}`, "");
    for (const row of rows) {
      if (kind === "thread") {
        out.push(`**${show(row["author.name"])}** · ${when(row.created_at)}`, "", String(row.body ?? "").trim(), "");
      } else if (kind === "changes") {
        out.push(`- ${show(row["created_by.name"]) || "System"} ${changed(row.field, row.before, row.after, row.action)} · ${when(row.created_at)}`);
      } else {
        out.push(`- ${columns.map((column) => show(row[column])).filter((value) => value !== "").join(" · ")}`);
      }
    }
    if (kind !== "thread") out.push("");
  }
  return out.join("\n").trim() + "\n";
}

// A button copying what a view holds, as Markdown, saying so for a moment.
export function Copy({
  view,
  label: says = "Copy",
  fields,
  lists,
  choices,
}: {
  view: ViewState;
  label?: string;
  fields: readonly CopiedField[];
  lists: readonly CopiedList[];
  choices?: Record<string, Record<string, string>>; // a choice's values, as they're shown
}) {
  const ui = useUI();
  const title = usePageTitle();
  const [copied, setCopied] = useState(false);
  useEffect(() => {
    if (!copied) return;
    const timer = setTimeout(() => setCopied(false), 1500);
    return () => clearTimeout(timer);
  }, [copied]);
  if (view.status !== "live") return null;
  return (
    <ui.Button kind="secondary" onClick={() => void navigator.clipboard.writeText(markdownOf(title, view.data, fields, lists, choices)).then(() => setCopied(true))}>
      {copied ? "Copied" : says}
    </ui.Button>
  );
}

// A value from a view, shown only while the view is live. While it's loading, stale
// or denied, the component set shows a placeholder instead, because an old number
// that looks current is worse than no number.
export function Live({ view, field }: { view: ViewState; field: string }) {
  const ui = useUI();
  const value = view.status === "live" ? show(view.data?.[field]) : "";
  return <ui.Live status={view.status} value={value} />;
}

function useConfirmedRunner() {
  const runner = useRunner();
  const { questions, ask } = useConfirmContext();
  const run = async (command: string, input: CommandInput = {}, about: CommandInput = input) => {
    const question = questions.get(command);
    if (question !== undefined && !(await ask(fill(question, about)))) {
      // Declining is a choice about this attempt, so the last one's failure goes.
      runner.clear(command);
      return false;
    }
    return runner.run(command, input);
  };
  return { run, busy: runner.busy, error: runner.error, clear: runner.clear };
}

// Asks before `command` runs, wherever on the screen it's run from.
export function Confirm({ command, question }: { command: string; question: string }) {
  const { questions } = useConfirmContext();
  useEffect(() => {
    questions.set(command, question);
    return () => {
      questions.delete(command);
    };
  }, [questions, command, question]);
  return null;
}

// Whether a list from a view has a value, like an issue's assignees having whoever
// is reading.
export function listHas(list: unknown, item: unknown): boolean {
  return Array.isArray(list) && list.includes(item);
}

// Whether a person holds one of some roles within something, like maintainer in a
// project, as the view of their roles says: its rows each name where, in a field
// like project, and the role. Not while it hasn't arrived, so a button that may not
// apply isn't shown early.
export function holds(roles: ViewState, field: string, within: string | undefined, granting: readonly string[]): boolean {
  if (roles.status !== "live" || !within) return false;
  const rows = Array.isArray(roles.data?.rows) ? (roles.data.rows as Record<string, unknown>[]) : [];
  return rows.some((row) => row[field] === within && typeof row.role === "string" && granting.includes(row.role));
}

// A view's list as a form's choices: each row's id, shown by one of its values, like
// a project's roles by their titles.
// value: the field each row offers, like a member's person, rather than the row
// itself; each one once, however many rows hold it.
export function listChoices(view: ViewState, list: string, shown: string, value?: string): [string, string][] {
  if (view.status !== "live") return [];
  const seen = new Set<string>();
  const choices: [string, string][] = [];
  for (const row of rowsOf(view.data?.[list])) {
    const offered = value ? row[value] : row.id;
    if (typeof offered !== "string" || offered === "" || seen.has(offered)) continue;
    seen.add(offered);
    choices.push([offered, show(row[shown]) || offered]);
  }
  return choices;
}

// Whether a person may run a command within something, like an issue's project,
// where its roles are its own: one of the roles they hold there, as the view of
// their roles says, allows it now, in the list each row carries, like role.may.
export function allows(roles: ViewState, field: string, within: string | undefined, command: string, list: string): boolean {
  if (roles.status !== "live" || !within) return false;
  const rows = Array.isArray(roles.data?.rows) ? (roles.data.rows as Record<string, unknown>[]) : [];
  return rows.some((row) => row[field] === within && Array.isArray(row[list]) && (row[list] as unknown[]).includes(command));
}

// A button that runs a command on its own, with nothing to fill in, on the entity
// with that id when it acts on one. It says what it does, as its screen names it or
// as its command is named, and isn't there at all while it doesn't apply.
export function Command({
  name,
  id,
  label: says,
  when = true,
  allowed = true,
  icon,
}: {
  name: string;
  id?: string;
  label?: string;
  when?: boolean;
  allowed?: boolean; // whether the person reading may run it; while they may not, it isn't there
  icon?: string; // drawn as this, like edit, its words still naming it
}) {
  const ui = useUI();
  const runner = useConfirmedRunner();
  if (!when || !allowed) return null;
  return (
    <ui.Button kind={icon ? "secondary" : "primary"} icon={icon} disabled={runner.busy(name)} error={runner.error(name)} onClick={() => void runner.run(name, id === undefined ? {} : { id })}>
      {says ?? label(action(name))}
    </ui.Button>
  );
}

// A button for each step from where something is now, like an issue's phase, that
// one of the person's roles may take: "Start work" or "Move to In review", each
// moving it to the step's to. Steps are rows holding from and to, and the roles that
// may take them; the person's roles come from the view of their roles, here.
export function Steps({
  command,
  id,
  field,
  current,
  steps,
  list,
  shown,
  to,
  from,
  here: named,
  held,
  roles,
  within,
  place = "project",
  role = "role",
}: {
  command: string;
  id?: string;
  field: string; // what the command changes, like phase
  current?: ViewState; // the view holding where it is now
  steps: ViewState;
  list: string;
  shown?: string; // a step's own title, like Start work
  to?: string; // the title of where it goes, like to.title
  from?: string; // the title of where it starts, like from.title, to say where nothing leads on from
  here?: string; // the same, held by the current view, like phase_title, for where no step starts
  held?: string; // the step's list of roles that may take it
  roles?: ViewState; // the view of the person's roles
  within?: string; // the project they're held in
  place?: string; // the field of a role's row naming the project
  role?: string; // the field of a role's row naming the role
}) {
  const ui = useUI();
  const runner = useConfirmedRunner();
  if (current?.status !== "live" || steps.status !== "live") return null;
  const now = current.data?.[field];
  const mine = rolesHeld(roles, within, place, role);
  const leaving = rowsOf(steps.data?.[list]).filter((step) => step.from === now);
  const open = leaving.filter((step) => !held || (Array.isArray(step[held]) && (step[held] as unknown[]).some((r) => mine.has(r))));
  // Someone who holds a role there is told when none of theirs moves it on, rather
  // than shown nothing; someone who holds none had no moves to look for.
  if (open.length === 0) {
    if (!held || mine.size === 0 || now === undefined || now === null || now === "") return null;
    const here = (named && show(current.data?.[named])) || (from && leaving.length > 0 ? show(leaving[0][from]) : "");
    return <ui.Text>{here ? `No moves from ${here} for your roles` : "No moves from here for your roles"}</ui.Text>;
  }
  return (
    <>
      {open.map((step) => (
        <ui.Button
          key={step.id}
          kind="primary"
          disabled={runner.busy(command)}
          error={runner.error(command)}
          onClick={() => void runner.run(command, { ...(id === undefined ? {} : { id }), [field]: step.to })}
        >
          {(shown && show(step[shown])) || (to && show(step[to]) ? `Move to ${show(step[to])}` : label(action(command)))}
        </ui.Button>
      ))}
    </>
  );
}

type Row = Record<string, unknown> & { id: string };

// A view's data comes from outside the app, so a list that isn't a list, or a row
// without an id to act on, is left out rather than break the screen.
function rowsOf(value: unknown): Row[] {
  if (!Array.isArray(value)) return [];
  return value.filter(
    (row): row is Row => row !== null && typeof row === "object" && !Array.isArray(row) && typeof (row as Row).id === "string",
  );
}

// How rows are put in order by a field: numbers and dates as such, words as a reader
// would sort them, and nothing last; with a - in front, largest or latest first.
function ordering(sort: string): (a: Row, b: Row) => number {
  const descending = sort.startsWith("-");
  const field = descending ? sort.slice(1) : sort;
  const key = (value: unknown) => (value instanceof Date ? value.getTime() : value);
  return (a, b) => {
    const x = key(a[field]);
    const y = key(b[field]);
    if (x === y) return 0;
    if (x === undefined || x === null || x === "") return 1;
    if (y === undefined || y === null || y === "") return -1;
    const order = typeof x === "number" && typeof y === "number" ? x - y : show(x).localeCompare(show(y), undefined, { numeric: true });
    return descending ? -order : order;
  };
}

// The address a row opens. The last parameter is the row; any before it, like the
// project in /projects/:project/issues/:issue, are the ones this screen was opened
// with, or the row's own field of that name.
//
// When what it opens is named by its key's parts, like /:owner/:project for a
// project keyed by its owner and its name, keyed lists the parts before the last,
// and the row's id is split into them.
function rowLink(route: string, row: Row, params: Readonly<Record<string, string | undefined>>, keyed: readonly string[] = [], named: Record<string, number> = {}): string {
  const parameter = /:([A-Za-z_]\w*)/g;
  const names = [...route.matchAll(parameter)].map((m) => m[1] as string);
  const last = names[names.length - 1];
  const own = (name: string) => {
    const field = row[name];
    return typeof field === "string" && field !== "" ? field : undefined;
  };
  // A row that holds the thing a parameter names, like a membership's project or a
  // change's issue, links to it; otherwise the last parameter is the row itself.
  const opened = (last ? own(last) : undefined) ?? row.id;
  const parts = keyed.length > 0 ? partsOf(opened, keyed.length + 1) : undefined;
  const values: Record<string, string> = {};
  if (parts) keyed.forEach((name, i) => (values[name] = parts[i] as string));
  return route.replace(parameter, (_, name: string) => {
    if (name === last) return encodeURIComponent(parts ? (parts[parts.length - 1] as string) : opened);
    // A row's board, by its id, fills /:board with the board's own name.
    const held = own(name);
    const part = held && named[name] ? partsOf(held, named[name])?.at(-1) : undefined;
    return encodeURIComponent(values[name] ?? params[name] ?? part ?? held ?? row.id);
  });
}

// A view's rows, or one of its named lists, one column per field. Each command in
// `actions` becomes a button on every row, run with that row's id: named for the
// command, or as its label says, and not there at all for someone it isn't allowed. A column in
// `pictures`, like member.picture, shows the person's picture rather than its
// address, and only when it's an https address.
// A command on each row of a table, like member::delete "Remove".
export interface RowAction {
  name: string;
  label?: string;
  allowed?: boolean; // whether the person reading may run it
  when?: (row: Record<string, unknown>) => boolean; // the rows it's on, like those that aren't the reader's own
  // A form it asks with first, opened in a dialog and started from the row, like a
  // rename asking for the new title; without one, the button runs it at once.
  form?: { fields: (string | FieldSpec)[]; submit?: string };
}

// Whether a row's time is before or after one counted from now, as a filter like
// updated_at > 7 days ago asks with updated_at>=-7d: hours, days or weeks, ago (-)
// or from now (+). A row with no time is kept by neither.
export function keptByTime(held: unknown, op: string, offset: string, now = Date.now()): boolean {
  const counted = /^([+-])(\d+)([hdw])$/.exec(offset);
  const at = held instanceof Date ? held.getTime() : typeof held === "string" || typeof held === "number" ? new Date(held).getTime() : Number.NaN;
  if (!counted || Number.isNaN(at)) return false;
  const hours = { h: 1, d: 24, w: 24 * 7 }[counted[3] as "h" | "d" | "w"];
  const then = now + (counted[1] === "-" ? -1 : 1) * Number(counted[2]) * hours * 3600 * 1000;
  return op === "<" ? at < then : op === ">" ? at > then : op === "<=" ? at <= then : at >= then;
}

// A row's tone, as Trac colored a ticket's priority: where its choice is among the
// field's choices, the first the most urgent, spread over 1 to 5, so three choices
// are 1, 3 and 5. A row without one is plain, 3.
export interface Tint {
  field: string; // like priority
  order: string[]; // its choices, the most urgent first
}
export function toneOf(row: Record<string, unknown>, tint: Tint | undefined): number | undefined {
  if (!tint) return undefined;
  const at = tint.order.indexOf(String(row[tint.field] ?? ""));
  if (at < 0) return 3;
  return tint.order.length === 1 ? 3 : Math.round(1 + (at * 4) / (tint.order.length - 1));
}

// Whether a row is kept by a filter's parts, each a field and what it holds, like
// author=me, or a time it's before or after, like updated_at>=-7d.
function kept(row: Row, picks: [string, string][], me: string): boolean {
  return picks.every(([name, wanted]) => {
    const compared = /^(.*?)(<=|>=|<|>)$/.exec(name);
    if (compared) return keptByTime(row[compared[1]], compared[2], wanted);
    const value = wanted === "me" ? me : wanted;
    const held = row[name];
    return Array.isArray(held) ? held.includes(value) : held === value;
  });
}

// What a list is filtered by from the page's address, as a card's link opens it:
// ?is=Opened by me&author=me keeps the rows whose author is whoever is reading, and
// says so in words that can be cleared. Without one, every row is kept.
function useFilter() {
  const { search, pathname } = useLocation();
  const navigate = useNavigate();
  const auth = useAuth();
  const query = new URLSearchParams(search);
  const picks = [...query.entries()].filter(([name]) => name !== "is" && !shownState(name));
  const matches = (row: Row) => kept(row, picks, auth?.person?.uid ?? "");
  const filtered: Filtered | undefined =
    picks.length > 0 ? { label: query.get("is") ?? picks.map(([name, value]) => `${label(name)}: ${value}`).join(", "), onClear: () => void navigate(pathname) } : undefined;
  return { matches, filtered };
}

// What a search box says it finds by, in words: Search title, labels, phase and
// priority. A field read through another, like phase.title, is called by that other.
function searchLabel(search: string[], columns: Record<string, string>): string {
  const words = search.map((field) => (columns[field] ?? label(field.includes(".") ? field.slice(0, field.lastIndexOf(".")) : field)).toLowerCase());
  return `Search ${words.length > 1 ? `${words.slice(0, -1).join(", ")} and ${words.at(-1)}` : words.join("")}`;
}

// A row's value as a table's cell or a card shows it: a choice as it's shown, a list
// of words each on its own, a web address as a short link, a person's picture as
// the picture, and anything else as text.
function cellOf(
  ui: ReturnType<typeof useUI>,
  links: ReturnType<typeof useLinks>,
  row: Row,
  key: string,
  { choices = {}, labels = [], pictures = [] }: { choices?: Record<string, Record<string, string>>; labels?: string[]; pictures?: string[] },
): ReactNode {
  const value = row[key];
  const shown = typeof value === "string" ? choices[key]?.[value] : undefined;
  if (shown !== undefined) return shown;
  if (labels.includes(key) && Array.isArray(value)) return <ui.Labels items={value.map(show).filter((item) => item !== "")} />;
  // A web address is a link, to wherever it is, shown shortened.
  if (!pictures.includes(key) && typeof value === "string" && /^https?:\/\/\S+$/i.test(value)) {
    return (
      <ui.Link {...links(value)}>
        <span title={value}>{shortAddress(value)}</span>
      </ui.Link>
    );
  }
  if (!pictures.includes(key)) return show(value);
  return typeof value === "string" && value.startsWith("https://") ? <ui.Picture source={value} /> : "";
}

export function Table({
  view,
  list = "rows",
  columns,
  actions = [],
  link,
  keyed,
  named,
  pictures = [],
  choices = {},
  labels = [],
  by,
  search = [],
  sort,
  page,
  reorder,
  tools,
  hideEmpty = false,
  only,
  tint,
}: {
  view: ViewState;
  list?: string; // which of the view's lists, like comments
  columns: Record<string, string>;
  actions?: (string | RowAction)[];
  link?: string; // a route like /books/:book, which each row's id fills
  keyed?: string[]; // the key's parts before the last, when the link names them, like owner in /:owner/:project
  named?: Record<string, number>; // parameters a row fills with the last part of an id keyed by that many, like a board in /:project/boards/:board
  pictures?: string[];
  choices?: Record<string, Record<string, string>>; // a choice column's values, as they're shown, like private as Private
  labels?: string[]; // columns holding a list of words, each shown on its own, like an issue's labels
  by?: string; // a choice column whose choices are tabs, each showing the rows that have it
  search?: string[]; // fields a box finds rows by, like title and labels
  sort?: string; // the field rows are sorted by, with a - for largest or latest first, like -number
  page?: number; // how many rows a page has
  // Rows put in order by dragging, which runs the command with a number for the field
  // between its new neighbors', like a phase's position; the list is ordered by it.
  reorder?: { command: string; field: string; allowed?: boolean };
  tools?: ReactNode; // buttons beside its search, like New issue
  hideEmpty?: boolean; // not there at all while the list has no rows, like a person's reports
  only?: Record<string, string>; // the rows it keeps, worked out as it's shown, like { "due<=": "+2w" }
  tint?: Tint; // each row colored by how urgent it is, like its priority
}) {
  const ui = useUI();
  const auth = useAuth();
  const links = useLinks();
  const params = useParams();
  // What's typed in the search box, over the fields it searches, as they're shown,
  // the tab that's open and the page it's on, kept in the address under the list's
  // name, like issues.tab=done, so a link shows the same.
  const [query, setQuery] = useAddressState("search");
  const [page_, setPage_] = usePathNamed("page");
  const at = Math.max(1, Number(page_) || 1);
  const setAt = (n: number) => setPage_(n <= 1 ? "" : String(n));
  const wanted = query.trim().toLowerCase();
  const { matches, filtered } = useFilter();
  const own = rowsOf(view.data?.[list]).filter((row) => !only || kept(row, Object.entries(only), auth?.person?.uid ?? ""));
  const found = own.filter(matches).filter(
    (row) => !wanted || search.some((field) => show(typeof row[field] === "string" ? (choices[field]?.[row[field] as string] ?? row[field]) : row[field]).toLowerCase().includes(wanted)),
  );
  // With tabs, the first choice is shown first, like Open before Closed.
  const options = by ? Object.keys(choices[by] ?? {}) : [];
  // A tab is named in the address by what sets it apart from the others, so a tab of
  // records, like a board's phases, is in_progress rather than its whole id.
  const common = sharedStart(options);
  const [tab, setTab] = usePathPart(options.map((option) => option.slice(common.length)));
  const picked = options.find((option) => option.slice(common.length) === tab);
  const setPicked = (option: string) => setTab(option === options[0] ? "" : option.slice(common.length));
  const chosen = picked ?? options[0];
  const tabbed = by && chosen !== undefined ? found.filter((row) => row[by] === chosen) : found;
  const sorted = sort ? [...tabbed].sort(ordering(sort)) : tabbed;
  const count = page ? Math.max(1, Math.ceil(sorted.length / page)) : 1;
  const current = Math.min(at, count);
  const rows = page ? sorted.slice((current - 1) * page, current * page) : sorted;
  const tabs = by
    ? options.map((option) => ({
        label: choices[by]?.[option] ?? option,
        count: found.filter((row) => row[by] === option).length,
        selected: option === chosen,
        onSelect: () => {
          setPicked(option);
          setAt(1);
        },
      }))
    : undefined;
  const runner = useConfirmedRunner();
  const pressed: RowAction[] = actions.map((a) => (typeof a === "string" ? { name: a } : a)).filter((a) => a.allowed !== false);
  // The row whose button opened its form, while it's open.
  const [asking, setAsking] = useState<{ action: RowAction; row: Row }>();
  // What's refused to someone signed out, like their own projects, means nothing to
  // them, so it isn't drawn; signing in shows it.
  if (view.status === "denied" && auth && !auth.person) return null;
  if (hideEmpty && view.status === "live" && own.length === 0) return null;
  // Put in order only when every row is shown, so a row's neighbors are its own.
  const arranged = reorder && reorder.allowed !== false && !by && !wanted && !page && !sort ? reorder : undefined;
  const move = (from: number, to: number) => {
    if (!arranged || from === to || to < 0 || to >= rows.length) return;
    const rest = rows.filter((_, i) => i !== from);
    const before = rest[to - 1]?.[arranged.field];
    const after = rest[to]?.[arranged.field];
    const number = (value: unknown) => (typeof value === "number" ? value : undefined);
    const [low, high] = [number(before), number(after)];
    // Between two at the same place, like phases added one after another, there's no
    // number between them: every row is numbered again in its new order instead.
    if (low !== undefined && low === high) {
      const order = [...rest.slice(0, to), rows[from], ...rest.slice(to)];
      order.forEach((row, at) => {
        if (row[arranged.field] !== at + 1) void runner.run(arranged.command, { id: row.id, [arranged.field]: at + 1 });
      });
      return;
    }
    const place = low !== undefined && high !== undefined ? (low + high) / 2 : low !== undefined ? low + 1 : high !== undefined ? high - 1 : 1;
    void runner.run(arranged.command, { id: rows[from].id, [arranged.field]: place });
  };
  const asked = asking?.action.form;
  const says = asking ? (asking.action.label ?? label(action(asking.action.name))) : "";
  return (
    <>
      <ui.Table
        status={view.status}
        tabs={tabs}
        search={
          search.length
            ? {
                value: query,
                label: searchLabel(search, columns),
                onChange: (value: string) => {
                  setQuery(value);
                  setAt(1);
                },
              }
            : undefined
        }
        pages={page && count > 1 ? { page: current, count, onPage: setAt } : undefined}
        reorder={arranged ? { label: "Move", onMove: move } : undefined}
        tools={tools}
        filtered={filtered}
        columns={Object.values(columns)}
        error={[...pressed.map((a) => a.name), ...(arranged ? [arranged.command] : [])].map((name) => runner.error(name)).find((e) => e !== undefined)}
        rows={rows.map((row) => ({
          id: row.id,
          link: link ? links(rowLink(link, row, params, keyed, named)) : undefined,
          cells: Object.keys(columns).map((key) => cellOf(ui, links, row, key, { choices, labels, pictures })),
          tone: toneOf(row, tint),
          actions: pressed.filter((a) => !a.when || a.when(row)).map((a) => ({
            label: a.label ?? label(action(a.name)),
            disabled: runner.busy(a.name),
            onClick: () => (a.form ? setAsking({ action: a, row }) : void runner.run(a.name, { id: row.id }, row)),
          })),
        }))}
      />
      {asking && asked && (
        <ui.Dialog open title={says} onClose={() => setAsking(undefined)}>
          <Form
            command={asking.action.name}
            // A row isn't offered as a pick in a form about itself, like a phase removed into itself.
            fields={asked.fields.map((field) =>
              typeof field === "string" || field.type !== "pick" ? field : { ...field, choices: field.choices?.filter(([value]) => value !== asking.row.id) },
            )}
            from={{ status: "live", data: asking.row }}
            id={asking.row.id}
            submit={asked.submit ?? says}
            onDone={() => setAsking(undefined)}
          />
        </ui.Dialog>
      )}
    </>
  );
}

// The roles a person holds where something is, like their roles in a project, from
// the view of their roles.
function rolesHeld(roles: ViewState | undefined, within: string | undefined, place: string, role: string): Set<unknown> {
  if (roles?.status !== "live" || !Array.isArray(roles.data?.rows)) return new Set();
  return new Set((roles.data.rows as Record<string, unknown>[]).filter((row) => row[place] === within).map((row) => row[role]));
}

// How a board's cards move: by a command along a list of steps, like issue::move
// along a project's steps, each step from one column to another for the roles it names.
export interface BoardMove {
  command: string;
  steps: ViewState;
  list: string;
  held?: string; // the step's list of roles that may take it
  roles?: ViewState; // the view of the person's roles
  within?: string; // where they're held, like the project
  place?: string;
  role?: string;
}

// A list's rows as cards in columns, like a project's issues in its phases: a column
// for each of the over list's things, in its order, and each card in the one its
// field names. With a move, a card goes to a column a step from its own leads to,
// for one of the person's roles; it's shown there at once, and back if the move fails.
export function Board({
  view,
  list,
  by,
  over,
  overList,
  shown = "title",
  columns,
  link,
  keyed,
  named,
  pictures = [],
  choices = {},
  labels = [],
  move,
  search = [],
  tools,
  tint,
}: {
  view: ViewState;
  list: string; // the cards, like issues
  by: string; // the field naming a card's column, like phase
  over: ViewState;
  overList: string; // the columns, like phases
  shown?: string; // what a column is called, like title
  columns: Record<string, string>; // what a card shows, its title first
  link?: string;
  keyed?: string[];
  named?: Record<string, number>; // parameters a row fills with the last part of an id keyed by that many, like a board in /:project/boards/:board
  pictures?: string[];
  choices?: Record<string, Record<string, string>>;
  labels?: string[];
  move?: BoardMove;
  search?: string[]; // fields a box finds cards by, like title and labels
  tools?: ReactNode; // buttons beside its search, like New issue
  tint?: Tint; // each card colored by how urgent it is, like its priority
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const runner = useConfirmedRunner();
  // Where each card moved is until the view says so too.
  const [moved, setMoved] = useState<Record<string, string>>({});
  // What's typed, kept in the address under the list's name, the same as its table's.
  const [query, setQuery] = useAddressState("search");
  const wanted = query.trim().toLowerCase();
  const { matches, filtered } = useFilter();
  const all = rowsOf(view.data?.[list]);
  const cards = all.filter(matches).filter(
    (row) => !wanted || search.some((field) => show(typeof row[field] === "string" ? (choices[field]?.[row[field] as string] ?? row[field]) : row[field]).toLowerCase().includes(wanted)),
  );
  const things = rowsOf(over.data?.[overList]);
  // On a phone, the phase shown is in the path, the same part as its table's tab, so
  // /in_progress opens on it either way.
  const startOf = sharedStart(things.map((thing) => thing.id));
  const [part, setPart] = usePathPart(things.map((thing) => thing.id.slice(startOf.length)));
  const column = part ? startOf + part : "";
  const setColumn = (id: string) => setPart(id ? id.slice(startOf.length) : "");
  const mine = move ? rolesHeld(move.roles, move.within, move.place ?? "project", move.role ?? "role") : new Set<unknown>();
  const steps = move?.steps.status === "live" ? rowsOf(move.steps.data?.[move.list]) : [];
  const where = (card: Row) => moved[card.id] ?? card[by];
  const reaches = (at: unknown) =>
    steps
      .filter((step) => step.from === at && (!move?.held || (Array.isArray(step[move.held]) && (step[move.held] as unknown[]).some((r) => mine.has(r)))))
      .map((step) => String(step.to));
  useEffect(() => {
    setMoved((now) => {
      const settled = Object.keys(now).filter((id) => all.some((card) => card.id === id && card[by] === now[id]));
      if (settled.length === 0) return now;
      return Object.fromEntries(Object.entries(now).filter(([id]) => !settled.includes(id)));
    });
    // Only the view's newer documents settle a move.
  }, [view.data]);
  const [title, ...rest] = Object.keys(columns);
  const status = over.status === "live" ? view.status : over.status;
  return (
    <ui.Board
      status={status}
      error={move ? runner.error(move.command) : undefined}
      tools={tools}
      filtered={filtered}
      column={column || undefined}
      onColumn={setColumn}
      search={
        search.length
          ? { value: query, label: searchLabel(search, columns), onChange: setQuery }
          : undefined
      }
      onMove={
        move
          ? (id, to) => {
              const card = all.find((c) => c.id === id);
              if (!card || !reaches(where(card)).includes(to)) return;
              setMoved((now) => ({ ...now, [id]: to }));
              void runner.run(move.command, { id, [by]: to }, card).then((done) => {
                if (!done) setMoved((now) => Object.fromEntries(Object.entries(now).filter(([other]) => other !== id)));
              });
            }
          : undefined
      }
      columns={things.map((thing) => ({
        id: thing.id,
        title: show(thing[shown]) || thing.id,
        cards: cards
          .filter((card) => where(card) === thing.id)
          .map((card) => ({
            id: card.id,
            title: title ? cellOf(ui, links, card, title, { choices, labels, pictures }) : card.id,
            link: link ? links(rowLink(link, card, params, keyed, named)) : undefined,
            details: rest.map((key) => cellOf(ui, links, card, key, { choices, labels, pictures })).filter((detail) => detail !== ""),
            reaches: reaches(where(card)),
            tone: toneOf(card, tint),
          })),
      }))}
    />
  );
}

// One of a few ways to show the same list, like a table and a board, as the person
// picks; their pick is remembered in their browser.
export function Switched({
  id,
  label: says,
  options,
  icons = [],
  children,
}: {
  id: string;
  label: string;
  options: string[];
  icons?: string[]; // drawn for each option instead of its name, like table and board
  children: ReactNode;
}) {
  const ui = useUI();
  const key = `uione:switch:${id}`;
  // What's shown is in the address, like issues.view=board, so a link shows the same;
  // the person's last pick is remembered too, and put in the address when they come
  // back without one.
  const named = (at: number) => (options[at] ?? "").toLowerCase();
  const [inAddress, setInAddress] = usePathPart(options.map((_, at) => named(at)).slice(1));
  const fromAddress = options.findIndex((_, at) => named(at) === inAddress);
  const picked = fromAddress >= 0 ? fromAddress : 0;
  const pick = (at: number) => {
    setInAddress(at === 0 ? "" : named(at));
    try {
      localStorage.setItem(key, String(at));
    } catch {
      // Without storage, the pick lasts as long as the address.
    }
  };
  useEffect(() => {
    if (inAddress) return;
    try {
      const kept = Number(localStorage.getItem(key));
      if (Number.isInteger(kept) && kept > 0 && kept < options.length) setInAddress(named(kept));
    } catch {
      // Nothing remembered.
    }
    // Once, as the page opens.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const switcher = (
    <ui.Switch label={says} options={options.map((option, at) => ({ label: option, icon: icons[at], selected: at === picked, onSelect: () => pick(at) }))} />
  );
  // It sits in the toolbar of what's shown, beside its own buttons.
  const shown = Children.toArray(children)[picked];
  if (!isValidElement<{ tools?: ReactNode }>(shown)) return shown ?? null;
  return cloneElement(shown, {
    tools: (
      <>
        {shown.props.tools}
        {switcher}
      </>
    ),
  });
}

// How a card sums up what it holds, like a board's issues: the list they're in, the
// field naming the card, and the field they're split by, shown by its title and in
// its order when the list has them.
export interface Tally {
  view: ViewState;
  list: string;
  by: string; // like board
  and: string; // like phase
  shown?: string; // like phase.title
  order?: string; // like phase.position
  noun?: string; // what's counted, like issues
}

// A list's rows as large cards, like a project's boards: each its title and link,
// what else it shows, a tally of what it holds, and links that open it filtered,
// like Opened by me.
export function Cards({
  view,
  list,
  columns,
  link,
  keyed,
  named,
  pictures = [],
  choices = {},
  labels = [],
  tally,
  filters = [],
}: {
  view: ViewState;
  list: string;
  columns: Record<string, string>; // what a card shows, its title first
  link?: string;
  keyed?: string[];
  named?: Record<string, number>;
  pictures?: string[];
  choices?: Record<string, Record<string, string>>;
  labels?: string[];
  tally?: Tally;
  filters?: { label: string; query: Record<string, string>; hue?: Hue }[];
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const [title, ...rest] = Object.keys(columns);
  const counted = tally ? rowsOf(tally.view.data?.[tally.list]) : [];
  const sum = (card: Row) => {
    if (!tally) return undefined;
    const mine = counted.filter((row) => row[tally.by] === card.id);
    const groups = new Map<unknown, { label: string; count: number; order: number }>();
    for (const row of mine) {
      const key = row[tally.and];
      const group = groups.get(key) ?? { label: show(tally.shown ? row[tally.shown] : key) || "None", count: 0, order: Number(tally.order ? row[tally.order] : 0) || 0 };
      group.count += 1;
      groups.set(key, group);
    }
    return [...groups.values()].sort((a, b) => a.order - b.order).map(({ label: name, count }) => ({ label: name, count }));
  };
  return (
    <ui.Cards
      status={view.status}
      cards={rowsOf(view.data?.[list]).map((row) => {
        const to = link ? rowLink(link, row, params, keyed, named) : undefined;
        return {
          id: row.id,
          title: title ? cellOf(ui, links, row, title, { choices, labels, pictures }) : row.id,
          link: to ? links(to) : undefined,
          details: rest.map((key) => cellOf(ui, links, row, key, { choices, labels, pictures })).filter((detail) => detail !== ""),
          tally: sum(row),
          noun: tally?.noun,
          filters: to ? filters.map((f) => ({ label: f.label, link: links(`${to}?${new URLSearchParams({ is: f.label, ...f.query }).toString()}`), ...(f.hue ? { hue: f.hue } : {}) })) : [],
        };
      })}
    />
  );
}

// A command a grid's cell runs: the form it asks with, and whether the person may.
export interface GridCommand {
  name: string;
  fields?: (string | FieldSpec)[];
  given?: Record<string, string | undefined>; // sent without being asked, like the project from the page's address
  submit?: string;
  allowed?: boolean;
}

// What goes between two of a list's things, like the moves between a project's
// phases: a row and a column for each thing, in the list's order, and in each cell
// the entry from the row's to the column's, if there is one. Pressing an empty cell
// asks create's form, with the two filled in; pressing a full one asks update's,
// started from the entry, with remove beside it.
// What goes between two of a list's things, as a grid and a diagram both show it:
// the things, the entries between them, and the dialog that adds one between two,
// or changes or removes one.
interface Between {
  view: ViewState;
  list: string; // the entries, like steps
  from: string; // the field naming where an entry goes from, like from
  to: string; // the field naming where it goes to, like to
  cell?: string; // what's shown of an entry, like roles.title
  over: ViewState;
  overList: string; // the things, like phases
  shown?: string; // what a thing is called, like title
  create?: GridCommand;
  update?: GridCommand;
  remove?: GridCommand;
}

function useBetween({ view, list, from, to, cell, over, overList, shown = "title", create, update, remove }: Between) {
  const ui = useUI();
  const runner = useConfirmedRunner();
  const [asking, setAsking] = useState<{ a: Row; b: Row; entry?: Row }>();
  const things = rowsOf(over.data?.[overList]);
  const entries = rowsOf(view.data?.[list]);
  const may = (command?: GridCommand) => (command && command.allowed !== false ? command : undefined);
  const [adds, changes, removes] = [may(create), may(update), may(remove)];
  const name = (thing: Row) => show(thing[shown]) || thing.id;
  const between = (a: Row, b: Row) => entries.find((e) => e[from] === a.id && e[to] === b.id);
  const says = (entry: Row) => (cell ? show(entry[cell]) : "") || "✓";
  // Pressing what's between two opens what may be done there, if anything may.
  const opens = (a: Row, b: Row) => {
    const entry = between(a, b);
    return (entry ? changes || removes : adds) ? () => setAsking({ a, b, entry }) : undefined;
  };
  const close = () => setAsking(undefined);
  const entry = asking?.entry;
  const dialog = asking && (
    <ui.Dialog open title={`${name(asking.a)} to ${name(asking.b)}`} onClose={close}>
      {entry && changes && <Form command={changes.name} fields={changes.fields ?? []} from={{ status: "live", data: entry }} id={entry.id} submit={changes.submit} onDone={close} />}
      {!entry && adds && (
        <Form command={adds.name} fields={adds.fields ?? []} given={{ ...adds.given, [from]: asking.a.id, [to]: asking.b.id }} submit={adds.submit} onDone={close} />
      )}
      {entry && removes && (
        <ui.Button
          kind="secondary"
          disabled={runner.busy(removes.name)}
          error={runner.error(removes.name)}
          onClick={() => void runner.run(removes.name, { id: entry.id }, entry).then((done) => done && close())}
        >
          {removes.submit ?? "Remove"}
        </ui.Button>
      )}
    </ui.Dialog>
  );
  return {
    things,
    entries,
    name,
    between,
    says,
    opens,
    dialog,
    status: over.status === "live" ? view.status : over.status,
    error: [create, update, remove].map((c) => (c ? runner.error(c.name) : undefined)).find((e) => e !== undefined),
  };
}

// What goes between two of a list's things, like the moves between a project's
// phases: a row and a column for each thing, in the list's order, and in each cell
// the entry from the row's to the column's, if there is one. Pressing an empty cell
// asks create's form, with the two filled in; pressing a full one asks update's,
// started from the entry, with remove beside it.
export function Grid({ corner, ...between }: Between & { corner?: string }) {
  const ui = useUI();
  const { things, name, between: entryOf, says, opens, dialog, status, error } = useBetween(between);
  return (
    <>
      <ui.Grid
        status={status}
        corner={corner ?? `${label(between.from)}, ${label(between.to).toLowerCase()}`}
        columns={things.map(name)}
        error={error}
        rows={things.map((a) => ({
          label: name(a),
          cells: things.map((b) => {
            if (a.id === b.id) return { text: "", label: `${name(a)} to itself`, self: true };
            const found = entryOf(a, b);
            const text = found ? says(found) : "";
            return { text, label: found ? `${name(a)} to ${name(b)}: ${text}` : `Add ${name(a)} to ${name(b)}`, onClick: opens(a, b) };
          }),
        }))}
      />
      {dialog}
    </>
  );
}

// The same drawn: each thing a box, in the list's order, and each entry an arrow
// from one to another, labelled with what it shows. Pressing an arrow changes or
// removes it; drawing one from a box to another adds it.
export function Diagram(between: Between) {
  const ui = useUI();
  const { things, entries, name, says, opens, dialog, status, error } = useBetween(between);
  const thing = (id: unknown) => things.find((t) => t.id === id);
  return (
    <>
      <ui.Diagram
        status={status}
        error={error}
        nodes={things.map((t) => ({ id: t.id, label: name(t) }))}
        edges={entries.flatMap((entry) => {
          const [a, b] = [thing(entry[between.from]), thing(entry[between.to])];
          if (!a || !b) return [];
          return [{ from: a.id, to: b.id, label: says(entry), title: `${name(a)} to ${name(b)}: ${says(entry)}`, onClick: opens(a, b) }];
        })}
        onConnect={(from, to) => {
          const [a, b] = [thing(from), thing(to)];
          if (a && b && a.id !== b.id) opens(a, b)?.();
        }}
      />
      {dialog}
    </>
  );
}

export interface FieldSpec {
  name: string;
  label?: string;
  type?: string;
  hint?: string;
  choices?: [string, string][]; // for a choice: each one, and how it's shown
  start?: string; // what a new one starts as, like the field's starting choice
  required?: boolean; // a choice that's always one of them, like a priority
}

// What a field shows for a value from a view: a date as 2026-09-30, the way a date
// input takes it, a list separated by commas, and anything else as text.
function asField(value: unknown): string {
  if (value instanceof Date) {
    const pad = (n: number) => String(n).padStart(2, "0");
    if (dateOnly(value)) return `${value.getUTCFullYear()}-${pad(value.getUTCMonth() + 1)}-${pad(value.getUTCDate())}`;
    return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())}`;
  }
  if (Array.isArray(value)) return value.map(asField).join(", ");
  return value === null || value === undefined ? "" : String(value);
}

// What a form sends for a field: a yes or no as true or false, and a list, written
// separated by commas, or several choices ticked, as the list of what's between them.
function asInput(value: string, type: string | undefined): unknown {
  if (type === "boolean") return value === "true";
  if (type !== "list" && type !== "choices") return value;
  return value
    .split(",")
    .map((item) => item.trim())
    .filter((item) => item !== "");
}

// A form for a command. Inline by default. With `button`, it's a button that opens
// the form in a dialog, which is how a screen offers to create something.
export function Form({
  command,
  fields,
  button = false,
  from,
  id,
  given = {},
  submit: says,
  opener,
  when = true,
  allowed,
  authenticated = false,
  onDone,
  icon,
}: {
  command: string;
  fields: (string | FieldSpec)[];
  button?: boolean;
  from?: ViewState; // for an update: the view whose document the fields start from
  id?: string; // for an update: the entity it changes
  given?: Record<string, string | undefined>; // sent without being asked for, like the project an issue is made in
  submit?: string; // what its button says, rather than the command's name, like Save changes
  opener?: string; // what the button that opens it says, like New issue, when that's not its submit
  when?: boolean; // whether it applies now; while it doesn't, neither it nor its button is there
  allowed?: boolean; // whether the person reading may send it, when only some people may; while they may not, it isn't there
  authenticated?: boolean; // its command needs the person signed in, so someone who isn't is asked to sign in instead
  onDone?: () => void; // called once what it sent is done, like closing the dialog a row's button opened it in
  icon?: string; // the button that opens it drawn as this, like edit, its words still naming it
}) {
  const ui = useUI();
  const auth = useAuth();
  const signIn = useSignIn();
  const runner = useConfirmedRunner();
  const specs = fields.map((f) => (typeof f === "string" ? { name: f } : f));
  const empty = () => Object.fromEntries(specs.map((f) => [f.name, f.start ?? ""]));
  const [values, setValues] = useState<Record<string, string>>(empty);
  const [open, setOpen] = useState(false);
  // An update form starts from what's stored, and follows it until someone types.
  // After an update it keeps what was sent until the view has a newer document,
  // rather than showing the old one for a moment.
  const [touched, setTouched] = useState(false);
  const [sent, setSent] = useState<ViewState["data"]>();
  const stored = from?.status === "live" ? from.data : undefined;
  useEffect(() => {
    if (!stored || touched || stored === sent) return;
    setValues(Object.fromEntries(specs.map((f) => [f.name, asField(stored[f.name])])));
    // The fields come from the screen and never change, so they aren't a dependency.
  }, [stored, touched, sent]);

  const submit = async () => {
    const typed = Object.fromEntries(specs.map((f) => [f.name, asInput(values[f.name] ?? "", f.type)]));
    const input = { ...typed, ...given };
    if (await runner.run(command, id === undefined ? input : { ...input, id })) {
      if (from) {
        setSent(stored);
        setTouched(false);
      } else {
        setValues(empty());
      }
      setOpen(false);
      onDone?.();
    }
  };

  const form = (
    <ui.Form
      fields={specs.map(
        (f): FieldProps => ({
          name: f.name,
          label: f.label ?? label(f.name),
          type: f.type ?? "text",
          choices: f.choices,
          required: f.required,
          value: values[f.name] ?? "",
          hint: f.hint,
          onChange: (value) => {
            runner.clear(command);
            setTouched(true);
            setValues((current) => ({ ...current, [f.name]: value }));
          },
        }),
      )}
      submit={says ?? label(action(command))}
      busy={runner.busy(command)}
      error={runner.error(command)}
      onSubmit={() => void submit()}
    />
  );

  // Someone signed out is asked to sign in, rather than shown what they couldn't
  // send; while it isn't known yet whether they are, nothing is shown.
  if (!when) return null;
  // Someone signed out is asked to sign in only for what anyone signed in may do;
  // what takes a role, signing in wouldn't let them do.
  if (allowed !== undefined && !auth?.person) return null;
  if (authenticated && auth && !auth.person) {
    if (auth.person === undefined) return null;
    return (
      <ui.Button kind="secondary" onClick={signIn.begin}>
        Sign in to {(says ?? label(action(command))).toLowerCase()}
      </ui.Button>
    );
  }
  if (allowed === false) return null;
  if (!button) return form;
  return (
    <>
      <ui.Button kind={icon ? "secondary" : "primary"} icon={icon} onClick={() => setOpen(true)}>
        {opener ?? says ?? label(action(command))}
      </ui.Button>
      <ui.Dialog
        open={open}
        title={opener ?? says ?? label(action(command))}
        onClose={() => {
          setOpen(false);
          runner.clear(command);
        }}
      >
        {form}
      </ui.Dialog>
    </>
  );
}
