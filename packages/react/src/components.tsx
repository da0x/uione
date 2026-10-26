// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The components a generated screen is made of. Each one does the work, like running
// a command, asking before it does, or hiding a value that can't be trusted, and
// then hands the drawing to the app's component set.

import { Children, createContext, isValidElement, useContext, useEffect, useRef, useState, useSyncExternalStore } from "react";
import { createPortal } from "react-dom";
import type { ReactElement, ReactNode } from "react";
import { useLocation, useNavigate, useParams } from "react-router";
import { partsOf } from "./keys.js";
import { ShownPath, fill, useConfirmContext, useFill, usePageTitle, useSignIn, useSite } from "./app.js";
import { hintOf, setPicked as pickRow, useKeys, usePickedId, useTool } from "./workspace.js";
import type { BoardCard, CanvasRegion, ChartProps, FieldProps, Filtered, Hue, LandingFeature, LandingProps, LinkProps, PipelineRow, TrackStep } from "./contract.js";
import { readValue, scaleTop, seriesOf, toneOfState, valueAt, type ChartUnit } from "./series.js";
import { fileName, useAuth, useFiles, useRunner, useSearch } from "./data.js";
import type { CommandInput, ViewState } from "./data.js";
import { action, dateOnly, label, runningTime, shortAddress, show, useLinks, useUI } from "./ui.js";
import { generate, on, randomSeed, roles, schemeTitle, schemes, written, type PaletteTheme, type Scheme } from "./palette.js";

export function Hero({ title, children }: { title: string; children: ReactNode }) {
  const ui = useUI();
  return <ui.Hero title={title}>{children}</ui.Hero>;
}

// On a front page, a section can have an icon, and be read as steps; its design
// draws them.
export function Section({ title, id, hue, children }: { title: string; id?: string; hue?: Hue; icon?: string; steps?: boolean; children: ReactNode }) {
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

// The short line over a hero's or a section's title, and a front page's one sentence
// that stands for the site: what a front page's design places, so they're drawn there.
export function Overline({ children }: { children: ReactNode }) {
  const ui = useUI();
  return <ui.Text>{children}</ui.Text>;
}

export function Statement({ children }: { children: ReactNode }) {
  const ui = useUI();
  return <ui.Text>{children}</ui.Text>;
}

// An address's :parameters filled from the page it's on: /projects/:project/reports
// on the page /projects/uione is /projects/uione/reports.
function useFilled(): (to: string) => string {
  const params = useParams();
  return (to) =>
    !to.startsWith("/") ? to : to.replace(/:([A-Za-z_]\w*)/g, (written, name: string) => {
      const value = params[name];
      return value === undefined ? written : encodeURIComponent(value);
    });
}

export function Link({ to, icon, keys, children }: { to: string; icon?: string; keys?: string[]; children: ReactNode }) {
  const ui = useUI();
  const link = useLinks();
  const filled = useFilled()(to);
  const navigate = useNavigate();
  const tool = useTool();
  useKeys(keys, () => void navigate(filled));
  // In a toolbar's group, a tool, as its buttons are.
  if (tool) {
    return (
      <ui.Button kind="secondary" icon={icon} tool={tool} hint={hintOf(typeof children === "string" ? children : "", keys)} onClick={() => void navigate(filled)}>
        {children}
      </ui.Button>
    );
  }
  return (
    <ui.Link {...link(filled)} icon={icon}>
      {children}
    </ui.Link>
  );
}

// A front page drawn in one of uione's designs: its hero, with the short line over
// its title; its sections of words, and of sections of words, as what it offers; its
// statement; its sections as steps; and its other sections, like one with a form, as
// what it asks. Anything else, like a table, comes first, as written, or where the
// design puts it.
type Part = ReactElement<{ title?: string; id?: string; icon?: string; steps?: boolean; children?: ReactNode }>;

function elementsOf(children: ReactNode): Part[] {
  return Children.toArray(children).filter((part): part is Part => isValidElement(part));
}

// What a section of words says: the short line over its title, its words, and the
// sections it holds; or nothing, when it holds anything else.
function offered(section: Part): LandingFeature | undefined {
  let overline: ReactNode;
  const text: ReactNode[] = [];
  const features: LandingFeature[] = [];
  for (const part of elementsOf(section.props.children)) {
    if (part.type === Overline) overline = part.props.children;
    else if (part.type === Text) text.push(part.props.children);
    else if (part.type === Section && !part.props.steps) {
      const inner = offered(part);
      if (!inner) return undefined;
      features.push(inner);
    } else return undefined;
  }
  return { title: section.props.title ?? "", ...(overline !== undefined ? { overline } : {}), ...(section.props.icon ? { icon: section.props.icon } : {}), text, features };
}

export function Landing({ design, children }: { design: string; children?: ReactNode }) {
  const ui = useUI();
  const link = useLinks();
  const filled = useFilled();
  const site = useSite();
  let title = "";
  let overline: ReactNode;
  let statement: ReactNode;
  const text: ReactNode[] = [];
  const links: (LinkProps & { label: ReactNode })[] = [];
  const features: LandingFeature[] = [];
  const steps: LandingProps["steps"] = [];
  const asks: LandingProps["asks"] = [];
  const before: ReactNode[] = [];
  for (const child of elementsOf(children)) {
    if (child.type === Hero) {
      title = child.props.title ?? "";
      for (const part of elementsOf(child.props.children)) {
        if (part.type === Overline) overline = part.props.children;
        if (part.type === Text) text.push(part.props.children);
        if (part.type === Link) {
          const to = (part.props as { to?: string }).to ?? "/";
          links.push({ ...link(filled(to)), label: part.props.children });
        }
      }
      continue;
    }
    if (child.type === Statement) {
      statement = child.props.children;
      continue;
    }
    if (child.type === Section) {
      const parts = elementsOf(child.props.children);
      const over = parts.find((part) => part.type === Overline)?.props.children;
      const marked = over !== undefined ? { overline: over } : {};
      const id = child.props.id !== undefined ? { id: child.props.id } : {};
      if (child.props.steps) {
        steps.push({
          title: child.props.title ?? "",
          ...id,
          ...marked,
          text: parts.filter((part) => part.type === Text).map((part) => part.props.children),
          steps: parts.filter((part) => part.type === Section).map((part) => ({ title: part.props.title ?? "", text: elementsOf(part.props.children).filter((p) => p.type === Text).map((p) => p.props.children) })),
        });
        continue;
      }
      const feature = offered(child);
      if (feature) {
        features.push(feature);
        continue;
      }
      asks.push({ title: child.props.title ?? "", ...id, ...marked, children: parts.filter((part) => part.type !== Overline) });
      continue;
    }
    before.push(child);
  }
  return (
    <ui.Landing
      design={design}
      name={site.name}
      icon={site.icon}
      {...(overline !== undefined ? { overline } : {})}
      title={title}
      text={text}
      links={links}
      features={features}
      {...(statement !== undefined ? { statement } : {})}
      steps={steps}
      asks={asks}
      before={before}
    />
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
export function Pages({ base, pages, document: printed }: { base: string; pages: DocPage[]; document?: boolean }) {
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
      document={printed}
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

// Whether a file is a picture, by its name, to show it as one.
function picture(path: string): boolean {
  return /\.(png|jpe?g|gif|webp|avif|svg)$/i.test(path);
}

// Files, by their paths: each a link that opens it, and a picture of it when it's
// an image, read from where the backend says, for whoever may read its record.
export function Files({ paths }: { paths: string[] }) {
  const ui = useUI();
  const store = useFiles();
  const [previews, setPreviews] = useState<Record<string, string>>({});
  const key = paths.join("\n");
  useEffect(() => {
    if (!store) return;
    let gone = false;
    for (const path of paths.filter(picture)) {
      store.address(path).then(
        (address) => !gone && setPreviews((current) => ({ ...current, [path]: address })),
        () => {},
      );
    }
    return () => {
      gone = true;
    };
    // The paths are what the key says.
  }, [store, key]);
  if (paths.length === 0) return null;
  return (
    <ui.Files
      items={paths.map((path) => ({
        name: fileName(path),
        preview: previews[path],
        onOpen: () => {
          // Opened at once, so the browser doesn't take it for a popup, and sent
          // where the file is once that's known.
          const opened = window.open("", "_blank");
          store?.address(path).then(
            (address) => {
              if (opened) opened.location.href = address;
            },
            () => opened?.close(),
          );
        },
      }))}
    />
  );
}

// A view's values, each beside what it is: a choice as it's shown, a list of words
// each on its own, a file as a link to it, and none of the ones with nothing in them.
export function Details({
  view,
  fields,
  choices = {},
  labels = [],
  files = [],
  tint,
}: {
  view: ViewState;
  fields: readonly (readonly [name: string, label: string])[];
  choices?: Record<string, Record<string, string>>;
  labels?: string[];
  files?: string[]; // the values holding a file, or a list of them
  tint?: Tint; // the box colored by how urgent what it's about is, like its priority
}) {
  const ui = useUI();
  if (view.status !== "live") return null;
  const items = fields.flatMap<{ label: string; value: ReactNode }>(([name, label]) => {
    const value = view.data?.[name];
    if (files.includes(name)) {
      const paths = (Array.isArray(value) ? value : [value]).filter((path): path is string => typeof path === "string" && path !== "");
      return paths.length ? [{ label, value: <Files paths={paths} /> }] : [];
    }
    if (labels.includes(name) && Array.isArray(value)) {
      const words = value.map((word) => show(word)).filter((word) => word !== "");
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

// A screen laid out in regions, gathered from the Regions inside it by name. A
// workspace fills the window, with nothing of the site around it, so its bar says
// the app's name and switches light and dark, as the site's would.
export function Layout({ name, children }: { name: string; children?: ReactNode }) {
  const ui = useUI();
  const site = useSite();
  useFill(name === "workspace");
  // Filling the screen, a workspace is only its work, like a picture on a wall.
  const full = useSyncExternalStore(
    (changed) => {
      if (typeof document === "undefined") return () => {};
      document.addEventListener("fullscreenchange", changed);
      return () => document.removeEventListener("fullscreenchange", changed);
    },
    () => typeof document !== "undefined" && Boolean(document.fullscreenElement),
    () => false,
  );
  const regions: Record<string, ReactNode> = {};
  Children.forEach(children, (child) => {
    if (isValidElement<{ name: string; children?: ReactNode }>(child) && child.type === Region && (!full || name !== "workspace" || child.props.name === "main")) {
      regions[child.props.name] = child.props.children;
    }
  });
  return <ui.Layout name={name} regions={regions} site={site.name} />;
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
    // The thread draws each comment's box, so its text has no frame of its own.
    body: <ui.Markdown status="live" source={typeof row.body === "string" ? row.body : ""} framed={false} />,
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
  fresh,
  onOpen,
}: {
  view: ViewState;
  list: string;
  title?: string; // what it's of, like "What's new", shown only while it holds something
  // Which changes are new, by each row and the views it reads, like when the person
  // last looked, as they were when the page opened.
  fresh?: { views: ViewState[]; test: (row: Row, read: (Record<string, unknown> | undefined)[]) => boolean };
  onOpen?: string; // a command run once it opens with something new in it, like one saying they've looked
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
  // The views it reads, as they were when the page opened: saying they've looked
  // moves the time on, but what was new stays marked until they leave.
  const [looked, setLooked] = useState<{ read: (Record<string, unknown> | undefined)[] }>();
  const [said, setSaid] = useState(false);
  const ready = fresh !== undefined && fresh.views.every((v) => v.status === "live");
  useEffect(() => {
    if (fresh && ready && !looked) setLooked({ read: fresh.views.map((v) => v.data) });
  }, [fresh, ready, looked]);
  const isNew = (row: Row) => looked !== undefined && fresh !== undefined && fresh.test(row, looked.read);
  const freshCount = view.status === "live" ? onceEach(rows).filter(isNew).length : 0;
  useEffect(() => {
    if (!onOpen || said || freshCount === 0) return;
    setSaid(true);
    void runner.run(onOpen, {});
  }, [onOpen, said, freshCount, runner]);
  const entries = onceEach(rows).map((row) => {
    // A change no one made, like a migration's, was made by the system.
    const entry = { id: row.id, who: show(row["created_by.name"]) || "System", when: when(row.created_at), fresh: isNew(row) || undefined };
    if (subject.length === 0) return { ...entry, what: changed(row.field, row.before, row.after, row.action) };
    // A number is said as one, like #12.
    const words = subject.map((column) => (column.endsWith(".number") || column === "number" ? `#${show(row[column])}` : show(row[column]))).filter((part) => part !== "" && part !== "#");
    const [head, tail] = phrase(row.field, row.before, row.after, row.action);
    const to = link ? links(rowLink(link, row, params, keyed, named)) : undefined;
    return { ...entry, what: head, subject: words.join(" "), after: tail, link: to && { href: to.href, onClick: to.onClick } };
  });
  return <ui.Timeline status={view.status} entries={entries} title={title} fresh={fresh ? freshCount : undefined} />;
}

// The names in an address's query that are what's typed to find something, rather
// than what a list is filtered by: find, and a table's search.
export function shownState(name: string): boolean {
  return name === "find" || name === "search";
}

// What several names share at their start, up to the last dash in it, like a
// board's stages' project and board: a-main- of a-main-to_do and a-main-done.
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

// What a page shows, said in its path after its own, like /tracker/boards/main/board:
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
// How long typing pauses before a search asks the server, so a word is asked for once
// it's whole, rather than at every letter.
const searchPause = 400;

export function Find({
  label: words,
  sources,
}: {
  label: string;
  sources: {
    view: ViewState;
    list: string;
    label: string;
    link?: string;
    keyed?: string[];
    by: string[];
    // A view's list found by the words of a field its rows don't hold, which the server searches.
    search?: { view: string; subject?: string };
  }[];
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const search = useSearch();
  const [query, setQuery] = useAddressState("find");
  const wanted = query.trim().toLowerCase();
  const number = /^#?(\d+)$/.exec(wanted)?.[1];
  // What the server found for each source, and for what it was asked.
  const [searched, setSearched] = useState<{ wanted: string; rows: Record<number, Row[]> }>({ wanted: "", rows: {} });
  const asked = sources.map((source) => (source.search ? `${source.search.view}:${source.search.subject ?? ""}:${source.list}` : "")).join("|");
  useEffect(() => {
    if (!search || !/[\p{L}\p{N}]{2}/u.test(wanted) || number !== undefined) return;
    let current = true;
    const pause = setTimeout(() => {
      sources.forEach((source, at) => {
        if (!source.search) return;
        search(source.search.view, source.search.subject, source.list, wanted).then(
          (rows) => current && setSearched((was) => ({ wanted, rows: { ...(was.wanted === wanted ? was.rows : {}), [at]: rowsOf(rows) } })),
          () => {}, // found by what the rows hold alone
        );
      });
    }, searchPause);
    return () => {
      current = false;
      clearTimeout(pause);
    };
    // The sources are new at each render; asked says what they search.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [search, wanted, number, asked]);
  const groups = !wanted
    ? []
    : sources.flatMap((source, at) => {
        const numbered = source.by.find((field) => field === "number" || field.endsWith(".number"));
        const named = source.by.find((field) => field !== numbered) ?? source.by[0];
        const held = rowsOf(source.view.data?.[source.list]).filter((row) =>
          number !== undefined && numbered
            ? String(row[numbered]) === number || source.by.some((field) => field !== numbered && show(row[field]).toLowerCase().includes(wanted))
            : source.by.some((field) => show(row[field]).toLowerCase().includes(wanted)),
        );
        const more = searched.wanted === wanted ? (searched.rows[at] ?? []).filter((row) => !held.some((own) => own.id === row.id)) : [];
        const found = [...held, ...more];
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

// A theme tried on the whole app: its colors in place of the site's, light and dark,
// as the site's own stylesheet sets them, in a stylesheet after it.
const triedId = "uione-tried-theme";

function triedStyle(theme: PaletteTheme): string {
  const mode = (dark: boolean) =>
    roles
      .flatMap((role) => {
        const color = dark ? theme.colors[role].dark : theme.colors[role].light;
        return [`--std-${role}: ${color};`, `--more-${role}: ${color};`];
      })
      .concat(
        ["std", "more"].map((set) => {
          const pair = dark ? "dark" : "light";
          return `--${set}-accent-ink: ${on(theme.colors.accent[pair], theme.colors.page[pair])};`;
        }),
      )
      .join(" ");
  return (
    `:root { ${mode(false)} }\n` +
    `@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { ${mode(true)} } }\n` +
    `:root[data-theme="dark"] { ${mode(true)} }\n`
  );
}

// The theme tried now, kept for as long as the page is open, whichever screen is on
// it, and told to whatever shows it, like a page saving it or applying it elsewhere.
let tried: PaletteTheme | undefined;
let triedScheme: Scheme = schemes[Math.floor(Math.random() * schemes.length)];
const triedReaders = new Set<() => void>();

function tryTheme(theme: PaletteTheme | undefined) {
  tried = theme;
  if (theme) triedScheme = theme.scheme;
  document.getElementById(triedId)?.remove();
  if (theme) {
    const style = document.createElement("style");
    style.id = triedId;
    style.textContent = triedStyle(theme);
    document.head.appendChild(style);
  }
  for (const told of triedReaders) told();
}

// The theme tried on the app now, if one is, and a way to try another, or none to go
// back to the app's own: for a page beside Themes, like one keeping themes someone
// liked.
export function useTriedTheme(): [PaletteTheme | undefined, (theme: PaletteTheme | undefined) => void] {
  const theme = useSyncExternalStore(
    (told) => {
      triedReaders.add(told);
      return () => triedReaders.delete(told);
    },
    () => tried,
    () => undefined,
  );
  return [theme, tryTheme];
}

// Themes made at random from a palette, each tried on the whole app at once, and
// written as a project says it, to copy into a .one file.
export function Themes() {
  const ui = useUI();
  const [theme] = useTriedTheme();
  const [copied, setCopied] = useState(false);
  useEffect(() => {
    if (!copied) return;
    const timer = setTimeout(() => setCopied(false), 1500);
    return () => clearTimeout(timer);
  }, [copied]);
  useEffect(() => setCopied(false), [theme]);
  const scheme = theme?.scheme ?? triedScheme;
  return (
    <ui.Themes
      scheme={scheme}
      schemes={schemes.map((s) => [s, schemeTitle(s)])}
      onScheme={(chosen) => {
        triedScheme = chosen as Scheme;
        tryTheme(generate(theme?.seed ?? randomSeed(), triedScheme));
      }}
      seed={theme?.seed}
      colors={theme ? roles.map((role) => [role, theme.colors[role].light, theme.colors[role].dark]) : []}
      written={theme ? written(theme) : ""}
      copied={copied}
      onNew={() => tryTheme(generate(randomSeed(), scheme))}
      onReset={() => tryTheme(undefined)}
      onCopy={() => {
        if (theme) void navigator.clipboard.writeText(written(theme)).then(() => setCopied(true));
      }}
    />
  );
}

// A value from a view, shown only while the view is live. While it's loading, stale
// or denied, the component set shows a placeholder instead, because an old number
// that looks current is worse than no number.
// A value of a view, or of a row the screen picked, like a picked layer's size.
export function Live({ view, row, field }: { view?: ViewState; row?: Record<string, unknown> | null; field: string }) {
  const ui = useUI();
  if (!view) return <ui.Live status="live" value={row ? show(row[field]) : ""} />;
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

// x / y and x % y, as every target works them out: what's left over as a clock
// counts, never below none, so -1 % 4 is 3, and either by 0 is 0.
export function divided(x: number, y: number): number {
  return y === 0 ? 0 : x / y;
}

export function remainder(x: number, y: number): number {
  return y === 0 ? 0 : x - y * Math.floor(x / y);
}

// A value as it's put in order: a time as its milliseconds, so times compare as
// times, a number as itself, and nothing before everything, like a person who has
// never looked.
export function ordered(value: unknown): number | string {
  if (value === null || value === undefined) return Number.NEGATIVE_INFINITY;
  if (typeof value === "number") return value;
  if (value instanceof Date) return value.getTime();
  if (typeof value === "string") return /^\d{4}-\d{2}-\d{2}/.test(value) ? (timeOf(value) ?? value) : value;
  return String(value);
}

// Whether a column is something's number, said as #12.
function numbered(key: string): boolean {
  return key === "number" || key.endsWith(".number");
}

// The people a row's column names, each by name and, from an https address only,
// picture: assignees.name beside assignees.picture.
function peopleOf(row: Row, key: string, picture: string): { name: string; picture?: string }[] {
  const names = Array.isArray(row[key]) ? (row[key] as unknown[]) : [row[key]];
  const seen = Array.isArray(row[picture]) ? (row[picture] as unknown[]) : [row[picture]];
  return names
    .map((name, at) => ({ name: show(name), face: seen[at] }))
    .filter(({ name }) => name !== "")
    .map(({ name, face }) => (typeof face === "string" && face.startsWith("https://") ? { name, picture: face } : { name }));
}

// Whether a list from a view has a value, like an issue's assignees having whoever
// is reading.
export function listHas(list: unknown, item: unknown): boolean {
  return Array.isArray(list) && list.includes(item);
}

// Whether a field holds none: it's missing or null, or it's empty text or an empty
// list, as an entity's field reads before it's given a value.
export function isNone(value: unknown): boolean {
  return value === undefined || value === null || value === "" || (Array.isArray(value) && value.length === 0);
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
  keys,
  given,
  pressed,
  thenOpen,
}: {
  name: string;
  id?: string;
  label?: string;
  when?: boolean;
  allowed?: boolean; // whether the person reading may run it; while they may not, it isn't there
  icon?: string; // drawn as this, like edit, its words still naming it
  keys?: string[]; // run by these too, like ctrl+s, while it's there
  given?: CommandInput; // what it's sent besides its id, worked out as the screen's drawn
  pressed?: boolean; // drawn pressed, like a choice already made
  thenOpen?: string; // a route like /:poster, opened with what it made
}) {
  const ui = useUI();
  const runner = useConfirmedRunner();
  const replier = useRunner();
  const navigate = useNavigate();
  const tool = useTool();
  const shown = when && allowed;
  const sent = { ...(given ?? {}), ...(id === undefined ? {} : { id }) };
  const run = () => {
    if (!thenOpen) return void runner.run(name, sent);
    // What it made, opened: its id in the route's last parameter.
    void replier.reply(name, sent).then((made) => {
      if (made?.id) void navigate(thenOpen.replace(/:[a-z_]+\*?$/, encodeURIComponent(made.id)));
    });
  };
  useKeys(keys, run, shown && !runner.busy(name));
  if (!shown) return null;
  const words = says ?? label(action(name));
  return (
    <ui.Button kind={icon || tool ? "secondary" : "primary"} icon={icon} tool={tool} pressed={pressed} hint={hintOf(words, keys)} disabled={runner.busy(name)} error={runner.error(name)} onClick={run}>
      {words}
    </ui.Button>
  );
}

// A button for each step from where something is now, like an issue's stage, that
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
  field: string; // what the command changes, like stage
  current?: ViewState; // the view holding where it is now
  steps: ViewState;
  list: string;
  shown?: string; // a step's own title, like Start work
  to?: string; // the title of where it goes, like to.title
  from?: string; // the title of where it starts, like from.title, to say where nothing leads on from
  here?: string; // the same, held by the current view, like stage_title, for where no step starts
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
// Where a row dragged among others goes, given the rows it now sits between: a
// number for its field between theirs, so only it changes. Between two at the same
// place, like stages added one after another, there's no number between them, so
// the whole list, every row in it, is numbered again in its new order. The list is
// in order by the field already.
function placed(list: Row[], field: string, row: Row, before: Row | undefined, after: Row | undefined): { id: string; value: number }[] {
  const number = (value: unknown) => (typeof value === "number" ? value : undefined);
  const [low, high] = [number(before?.[field]), number(after?.[field])];
  if (low !== undefined && high !== undefined && low < high) return [{ id: row.id, value: (low + high) / 2 }];
  if (low !== undefined && high === undefined && !after) return [{ id: row.id, value: low + 1 }];
  if (high !== undefined && low === undefined && !before) return [{ id: row.id, value: high - 1 }];
  if (!before && !after) return [];
  const rest = list.filter((other) => other.id !== row.id);
  const at = before ? rest.findIndex((other) => other.id === before.id) + 1 : rest.findIndex((other) => other.id === after?.id);
  const order = [...rest.slice(0, at), row, ...rest.slice(at)];
  return order.flatMap((other, i) => (other[field] === i + 1 ? [] : [{ id: other.id, value: i + 1 }]));
}

// A list put in order by a field, with the places just given to rows by dragging
// until the view says so too, so a dropped row stays where it was put.
function usePlacing(rows: Row[], field: string | undefined, data: unknown) {
  const [placing, setPlacing] = useState<Record<string, number>>({});
  useEffect(() => {
    setPlacing((now) => {
      const settled = Object.keys(now).filter((id) => rows.some((row) => row.id === id && row[field ?? ""] === now[id]));
      if (settled.length === 0) return now;
      return Object.fromEntries(Object.entries(now).filter(([id]) => !settled.includes(id)));
    });
    // Only the view's newer documents settle a place.
  }, [data]);
  const ordered =
    !field || Object.keys(placing).length === 0
      ? rows
      : rows
          .map((row) => (row.id in placing ? { ...row, [field]: placing[row.id] } : row))
          .map((row, i) => ({ row, i }))
          .sort((a, b) => {
            const [x, y] = [a.row[field], b.row[field]];
            return typeof x === "number" && typeof y === "number" && x !== y ? x - y : a.i - b.i;
          })
          .map(({ row }) => row);
  const place = (changes: { id: string; value: number }[]) => setPlacing((now) => ({ ...now, ...Object.fromEntries(changes.map((c) => [c.id, c.value])) }));
  const forget = (changes: { id: string; value: number }[]) =>
    setPlacing((now) => Object.fromEntries(Object.entries(now).filter(([id]) => !changes.some((c) => c.id === id))));
  return { ordered, place, forget };
}

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
  icon?: string; // delete "Remove" icon delete: drawn as that, its words said when pointed at
  allowed?: boolean; // whether the person reading may run it
  when?: (row: Record<string, unknown>) => boolean; // the rows it's on, like those that aren't the reader's own
  // A form it asks with first, opened in a dialog and started from the row, like a
  // rename asking for the new title; without one, the button runs it at once.
  form?: { fields: (string | FieldSpec)[]; submit?: string };
}

// Whether a row's time is before or after one counted from now, as a filter like
// updated_at > 7 days ago asks with updated_at>=-7d: minutes, hours, days or weeks, ago (-)
// or from now (+). A row with no time is kept by neither.
export function keptByTime(held: unknown, op: string, offset: string, now = Date.now()): boolean {
  const counted = /^([+-])(\d+)([mhdw])$/.exec(offset);
  const at = held instanceof Date ? held.getTime() : typeof held === "string" || typeof held === "number" ? new Date(held).getTime() : Number.NaN;
  if (!counted || Number.isNaN(at)) return false;
  const hours = { m: 1 / 60, h: 1, d: 24, w: 24 * 7 }[counted[3] as "m" | "h" | "d" | "w"];
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
    // field! keeps the rows that don't hold it, like person.service! true: whoever isn't a service.
    const not = name.endsWith("!");
    const field = not ? name.slice(0, -1) : name;
    const value = wanted === "me" ? me : wanted;
    const held = row[field];
    const holds = Array.isArray(held) ? held.map(String).includes(value) : held !== undefined && held !== null && String(held) === value;
    return not ? !holds : holds;
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

// What a search box says it finds by, in words: Search title, labels, stage and
// priority. A field read through another, like stage.title, is called by that other.
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
  {
    choices = {},
    labels = [],
    pictures = [],
    people = {},
  }: { choices?: Record<string, Record<string, string>>; labels?: string[]; pictures?: string[]; people?: Record<string, string> },
): ReactNode {
  const value = row[key];
  // A person, or several, each their picture beside their name.
  const picture = people[key];
  if (picture !== undefined) {
    const names = Array.isArray(value) ? value : [value];
    const seen = row[picture];
    const faces = Array.isArray(seen) ? seen : [seen];
    // Beside the picture, whether each is a service, like person.service.
    const said = row[picture.replace(/\.picture$/, ".service")];
    const services = Array.isArray(said) ? said : [said];
    return names
      .map((name, at) => [show(name), faces[at], services[at] === true] as const)
      .filter(([name]) => name !== "")
      .map(([name, face, service], at) => (
        <span key={at} style={{ display: "inline-flex", alignItems: "center", gap: "0.4em", marginInlineEnd: "0.75em" }}>
          {service ? <ui.Picture source="" service /> : typeof face === "string" && face.startsWith("https://") ? <ui.Picture source={face} /> : null}
          {name}
        </span>
      ));
  }
  const shown = typeof value === "string" ? choices[key]?.[value] : undefined;
  if (shown !== undefined) return shown;
  if (labels.includes(key) && Array.isArray(value)) return <ui.Labels items={value.map((item) => show(item)).filter((item) => item !== "")} />;
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

// A track over a list's records, like a project's milestones: the rows' field naming
// one, the view and list they're in, what each is called, and what else a card shows.
export interface TrackSpec {
  field: string; // like milestone
  view: ViewState;
  list: string; // like milestones
  shown?: string; // what a card is called, like title
  columns: Record<string, string>; // what else a card shows, like { due: "Due" }
  hideEmpty?: boolean; // a record none of the rows names isn't on it, unless it's picked
  pickedLast?: boolean; // the last record is picked until another is, like the newest milestone; every row is milestone/every
  // In a switch, drawn once above it rather than by each way the list is shown, which
  // still keeps to its pick.
  above?: boolean;
  // What each record's dot is worked out from, as a board's cards' are: the rows' field
  // naming their stage, the stages, and each stage's status, by a field of its own or
  // as written.
  by?: string;
  over?: ViewState;
  overList?: string;
  statusBy?: string;
  statuses?: Record<string, string>;
}

// A track that picks its last until another is says it's showing every row so.
const every = "every";

// The rows a track keeps, and the track itself, drawn above them. The record picked
// is in the page's path by its field's name, like /milestone/beta, so a link shows
// the same; picking it again shows every row.
function useTrack(track: TrackSpec | undefined, rows: Row[], noun: string, onPick?: () => void, statusOfRow?: (row: Row) => string | undefined) {
  const ui = useUI();
  const links = useLinks();
  const [part, setPart] = usePathNamed(track?.field ?? "track");
  if (!track) return { keeps: () => true, element: null };
  const records = rowsOf(track.view.data?.[track.list]);
  // Each row's dot from the stages the track's told of, where the list's own isn't given.
  if (!statusOfRow && track.by && track.over) {
    const stageStatus: Record<string, string> = { ...(track.statuses ?? {}) };
    if (track.statusBy) {
      for (const stage of rowsOf(track.over.data?.[track.overList ?? ""])) {
        if (stage[track.statusBy]) stageStatus[stage.id] = String(stage[track.statusBy]);
      }
    }
    const by = track.by;
    statusOfRow = (row) => going(row, stageStatus[String(row[by])], Date.now()).status?.is;
  }
  // Failed once any row in it has, and succeeded once every row has.
  const statusOfRecord = (id: string): TrackStep["status"] => {
    const inIt = rows.filter((row) => row[track.field] === id);
    if (!statusOfRow || inIt.length === 0) return undefined;
    const statuses = inIt.map((row) => statusOfRow!(row));
    if (statuses.includes("failed")) return { is: "failed", words: "Failed" };
    if (statuses.every((status) => status === "success")) return { is: "success", words: "Succeeded" };
    return undefined;
  };
  // A record is named in the path by what sets it apart, like beta of tracker-beta.
  const ids = records.map((record) => record.id);
  const start = ids.length > 1 ? sharedStart(ids) : (ids[0] ?? "").slice(0, (ids[0] ?? "").lastIndexOf("-") + 1);
  const named = part && part !== every ? records.find((record) => record.id.slice(start.length) === part)?.id : undefined;
  // Without rows of its own, a record is left out when the track says so, unless it's picked.
  const shownWith = (picked: string | undefined) =>
    track.hideEmpty ? records.filter((record) => record.id === picked || rows.some((row) => row[track.field] === record.id)) : records;
  // The last, until another is picked, or every row is.
  const picked = named ?? (track.pickedLast && !part ? shownWith(undefined).at(-1)?.id : undefined);
  const keeps = (row: Row) => !picked || row[track.field] === picked;
  const shownRecords = shownWith(picked);
  const element = !track.above && shownRecords.length > 0 && (
    <ui.Track
      label={label(track.field)}
      steps={shownRecords.map((record) => ({
        id: record.id,
        title: show(record[track.shown ?? "title"]) || record.id,
        details: Object.keys(track.columns)
          .map((key) => cellOf(ui, links, record, key, {}))
          .filter((detail) => detail !== ""),
        count: rows.filter((row) => row[track.field] === record.id).length,
        noun,
        ...(statusOfRecord(record.id) ? { status: statusOfRecord(record.id) } : {}),
        selected: record.id === picked,
        onSelect: () => {
          setPart(record.id === picked ? (track.pickedLast ? every : "") : record.id.slice(start.length));
          onPick?.();
        },
      }))}
    />
  );
  return { keeps, element };
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
  people = {},
  choices = {},
  labels = [],
  by,
  search = [],
  sort,
  page,
  reorder,
  tools: ownTools,
  hideEmpty = false,
  only,
  tint,
  hue,
  track,
  download,
  more,
  pick,
  empty,
  picture,
  fit = false,
}: {
  view: ViewState;
  pick?: string; // the screen's pick of this list, by name: a row's click picks it
  empty?: string; // what it says while it has no rows
  picture?: string; // a file each row holds, shown small before its cells
  fit?: boolean; // all of it fitted in, or filling its place
  more?: () => void; // asks for the next page of rows, for a list read a page at a time, like everyone's
  list?: string; // which of the view's lists, like comments
  columns: Record<string, string>;
  actions?: (string | RowAction)[];
  link?: string; // a route like /books/:book, which each row's id fills
  keyed?: string[]; // the key's parts before the last, when the link names them, like owner in /:owner/:project
  named?: Record<string, number>; // parameters a row fills with the last part of an id keyed by that many, like a board in /:project/boards/:board
  pictures?: string[];
  people?: Record<string, string>; // a person's column by their name, and their picture beside it, like { "author.name": "author.picture" }
  choices?: Record<string, Record<string, string>>; // a choice column's values, as they're shown, like private as Private
  labels?: string[]; // columns holding a list of words, each shown on its own, like an issue's labels
  by?: string; // a choice column whose choices are tabs, each showing the rows that have it
  search?: string[]; // fields a box finds rows by, like title and labels
  sort?: string; // the field rows are sorted by, with a - for largest or latest first, like -number
  page?: number; // how many rows a page has
  // Rows put in order by dragging, which runs the command with a number for the field
  // between its new neighbors', like a stage's position; the list is ordered by it.
  reorder?: { command: string; field: string; allowed?: boolean };
  tools?: ReactNode; // buttons beside its search, like New issue
  hideEmpty?: boolean; // not there at all while the list has no rows, like a person's reports
  only?: Record<string, string>; // the rows it keeps, worked out as it's shown, like { "due<=": "+2w" }
  tint?: Tint; // each row colored by how urgent it is, like its priority
  hue?: Hue; // its rows' links in this color, rather than the site's own
  track?: TrackSpec; // cards above it for a list's records, like milestones, picking one keeping the rows in it
  download?: string; // a button saving every row it keeps, in a file of this name, like audit.csv
}) {
  const ui = useUI();
  const auth = useAuth();
  const links = useLinks();
  const params = useParams();
  const pickedId = usePickedId(pick ?? "");
  const addresses = useAddresses(rowsOf(view.data?.[list]), picture);
  // What's typed in the search box, over the fields it searches, as they're shown,
  // the tab that's open and the page it's on, kept in the address under the list's
  // name, like issues.tab=done, so a link shows the same.
  const [query, setQuery] = useAddressState("search");
  const [page_, setPage_] = usePathNamed("page");
  const at = Math.max(1, Number(page_) || 1);
  const setAt = (n: number) => setPage_(n <= 1 ? "" : String(n));
  const wanted = query.trim().toLowerCase();
  const { matches, filtered } = useFilter();
  const placing = usePlacing(rowsOf(view.data?.[list]), reorder?.field, view.data);
  const own = placing.ordered.filter((row) => !only || kept(row, Object.entries(only), auth?.person?.uid ?? ""));
  const tracked = useTrack(track, own, list === "rows" ? "rows" : list, () => setAt(1));
  const found = own.filter(tracked.keeps).filter(matches).filter(
    (row) => !wanted || search.some((field) => show(typeof row[field] === "string" ? (choices[field]?.[row[field] as string] ?? row[field]) : row[field]).toLowerCase().includes(wanted)),
  );
  // With tabs, the first choice is shown first, like Open before Closed.
  const options = by ? Object.keys(choices[by] ?? {}) : [];
  // A tab is named in the address by what sets it apart from the others, so a tab of
  // records, like a board's stages, is in_progress rather than its whole id.
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
  // Every row it keeps, on every page, as comma-separated values.
  const tools =
    download || more ? (
      <>
        {ownTools}
        {download && (
          <ui.Button kind="secondary" onClick={() => saveFile(download, csvOf(sorted, columns, choices), "text/csv")}>
            Download CSV
          </ui.Button>
        )}
        {more && (
          <ui.Button kind="secondary" onClick={more}>
            Show more
          </ui.Button>
        )}
      </>
    ) : (
      ownTools
    );
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
  // Hidden while it's empty, and from whoever can't read it at all.
  // Hidden when empty is hidden until there's something to show, not drawn while it loads.
  if (hideEmpty && (view.status !== "live" || own.length === 0)) return null;
  // Put in order while the rows are in the list's own order and none is hidden by
  // what's typed. Within a tab or a page, a row's neighbors are still its neighbors in
  // the whole list, between which it goes.
  const arranged = reorder && reorder.allowed !== false && !wanted && !sort ? reorder : undefined;
  const move = (from: number, to: number) => {
    if (!arranged || from === to || to < 0 || to >= rows.length) return;
    const offset = page ? (current - 1) * page : 0;
    const row = sorted[offset + from];
    const rest = sorted.filter((other) => other.id !== row.id);
    const changes = placed(own, arranged.field, row, rest[offset + to - 1], rest[offset + to]);
    placing.place(changes);
    for (const change of changes) {
      void runner.run(arranged.command, { id: change.id, [arranged.field]: change.value }).then((done) => {
        if (!done) placing.forget([change]);
      });
    }
  };
  const asked = asking?.action.form;
  const says = asking ? (asking.action.label ?? label(action(asking.action.name))) : "";
  // Inside a table switched with a board, its search box and buttons are on the
  // switch's own line above, where they stay whichever is shown.
  const slot = useContext(ToolbarSlot);
  const searchBox = search.length
    ? {
        value: query,
        label: searchLabel(search, columns),
        onChange: (value: string) => {
          setQuery(value);
          setAt(1);
        },
      }
    : undefined;
  return (
    <>
      {tracked.element}
      {slot && createPortal(<ui.Toolbar search={searchBox} tools={tools} filtered={filtered} />, slot)}
      <ui.Table
        status={view.status}
        {...(hue ? { hue } : {})}
        {...(empty ? { empty } : {})}
        tabs={tabs}
        search={slot ? undefined : searchBox}
        pages={page && count > 1 ? { page: current, count, onPage: setAt } : undefined}
        reorder={arranged ? { label: "Move", onMove: move } : undefined}
        tools={slot ? undefined : tools}
        filtered={slot ? undefined : filtered}
        columns={Object.values(columns)}
        error={[...pressed.map((a) => a.name), ...(arranged ? [arranged.command] : [])].map((name) => runner.error(name)).find((e) => e !== undefined)}
        rows={rows.map((row) => ({
          id: row.id,
          link: link ? links(rowLink(link, row, params, keyed, named)) : undefined,
          cells: Object.keys(columns).map((key) => cellOf(ui, links, row, key, { choices, labels, pictures, people })),
          tone: toneOf(row, tint),
          ...(pick ? { picked: row.id === pickedId, onPick: () => pickRow(pick, row.id) } : {}),
          ...(picture ? { picture: { source: addresses[String(row[picture] ?? "")], fit } } : {}),
          actions: pressed.filter((a) => !a.when || a.when(row)).map((a) => ({
            label: a.label ?? label(action(a.name)),
            ...(a.icon ? { icon: a.icon } : {}),
            disabled: runner.busy(a.name),
            onClick: () => (a.form ? setAsking({ action: a, row }) : void runner.run(a.name, { id: row.id }, row)),
          })),
        }))}
      />
      {asking && asked && (
        <ui.Dialog open title={says} onClose={() => setAsking(undefined)}>
          <Form
            command={asking.action.name}
            // A row isn't offered as a pick in a form about itself, like a stage removed into itself.
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

// Rows as comma-separated values, a line for the columns' names and one for each
// row: a choice as it's shown, a time as its full date and time, and a list joined.
export function csvOf(rows: Row[], columns: Record<string, string>, choices: Record<string, Record<string, string>> = {}): string {
  const said = (value: unknown, key: string): string => {
    if (value instanceof Date) return value.toISOString();
    if (Array.isArray(value)) return value.map((item) => said(item, key)).join(", ");
    if (typeof value === "string") return choices[key]?.[value] ?? value;
    return show(value);
  };
  const quoted = (text: string) => (/[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text);
  const lines = [Object.values(columns).map(quoted).join(",")];
  for (const row of rows) lines.push(Object.keys(columns).map((key) => quoted(said(row[key], key))).join(","));
  return lines.join("\r\n") + "\r\n";
}

// Saves text as a file the browser downloads, by that name.
function saveFile(name: string, text: string, type: string) {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 0);
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

// A list's rows as cards in columns, like a project's issues in its stages: a column
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
  people = {},
  choices = {},
  labels = [],
  move,
  search = [],
  tools,
  tint,
  track,
  reorder,
  notify: notifyWritten = [],
  statuses: statusesWritten = {},
  notifyBy,
  statusBy,
  waitsOn,
}: {
  view: ViewState;
  list: string; // the cards, like issues
  by: string; // the field naming a card's column, like stage
  over: ViewState;
  overList: string; // the columns, like stages
  shown?: string; // what a column is called, like title
  columns: Record<string, string>; // what a card shows, its title first
  link?: string;
  keyed?: string[];
  named?: Record<string, number>; // parameters a row fills with the last part of an id keyed by that many, like a board in /:project/boards/:board
  pictures?: string[];
  people?: Record<string, string>; // a person's column by their name, and their picture beside it, like { "author.name": "author.picture" }
  choices?: Record<string, Record<string, string>>;
  labels?: string[];
  move?: BoardMove;
  search?: string[]; // fields a box finds cards by, like title and labels
  tools?: ReactNode; // buttons beside its search, like New issue
  tint?: Tint; // each card colored by how urgent it is, like its priority
  track?: TrackSpec; // cards above it for a list's records, like milestones, picking one keeping the cards in it
  // Cards dragged into order within their column, which runs the command with a
  // number for the field between its new neighbors', as a table's rows are.
  reorder?: { command: string; field: string; allowed?: boolean };
  notify?: string[]; // the columns that chime when a card arrives
  statuses?: Record<string, string>; // a column's status, the dot its cards show unless they say
  notifyBy?: { field: string; has: string }; // columns kept as records: the field that lists how they notify
  statusBy?: string; // and the field that's their status
  waitsOn?: string; // the field listing what a card waits on: while any hasn't succeeded, it's waiting, saying on what
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const runner = useConfirmedRunner();
  // Columns kept as records say for themselves whether they chime and their status.
  const overRows = rowsOf(over.data?.[overList]);
  const notify = notifyBy
    ? notifyWritten.concat(overRows.filter((row) => Array.isArray(row[notifyBy.field]) && (row[notifyBy.field] as unknown[]).includes(notifyBy.has)).map((row) => row.id))
    : notifyWritten;
  const statuses: Record<string, string> = { ...statusesWritten };
  if (statusBy) for (const row of overRows) if (typeof row[statusBy] === "string" && statusWords[statusOf(row[statusBy] as string)!]) statuses[row.id] = statusOf(row[statusBy] as string)!;
  useArrivals(view, list, by, notify, statuses);
  const cardRows = rowsOf(view.data?.[list]);
  const now = useNow(cardRows.some((card) => typeof (card.data as Record<string, unknown> | undefined)?.since === "string"));
  // Where each card moved is until the view says so too.
  const [moved, setMoved] = useState<Record<string, string>>({});
  // What's typed, kept in the address under the list's name, the same as its table's.
  const [query, setQuery] = useAddressState("search");
  const wanted = query.trim().toLowerCase();
  const { matches, filtered } = useFilter();
  const placing = usePlacing(rowsOf(view.data?.[list]), reorder?.field, view.data);
  const all = placing.ordered;
  const tracked = useTrack(track, all, list, undefined, (card) => going(card, statuses[String(moved[card.id] ?? card[by])], now).status?.is);
  const cards = all.filter(tracked.keeps).filter(matches).filter(
    (row) => !wanted || search.some((field) => show(typeof row[field] === "string" ? (choices[field]?.[row[field] as string] ?? row[field]) : row[field]).toLowerCase().includes(wanted)),
  );
  const things = rowsOf(over.data?.[overList]);
  // On a phone, the stage shown is in the path, the same part as its table's tab, so
  // /in_progress opens on it either way.
  const startOf = sharedStart(things.map((thing) => thing.id));
  const [part, setPart] = usePathPart(things.map((thing) => thing.id.slice(startOf.length)));
  const column = part ? startOf + part : "";
  const setColumn = (id: string) => setPart(id ? id.slice(startOf.length) : "");
  const mine = move ? rolesHeld(move.roles, move.within, move.place ?? "project", move.role ?? "role") : new Set<unknown>();
  const steps = move?.steps.status === "live" ? rowsOf(move.steps.data?.[move.list]) : [];
  const where = (card: Row) => moved[card.id] ?? card[by];
  // What a card waits on that hasn't reached a success end, by title.
  const waitingOn = (card: Row): string[] => {
    const listed = waitsOn && Array.isArray(card[waitsOn]) ? (card[waitsOn] as unknown[]) : [];
    return listed
      .map((id) => cardRows.find((other) => other.id === id))
      .filter((other): other is Row => Boolean(other) && statuses[String(where(other!))] !== "success")
      .map((other) => show(other[title ?? "title"]) || other.id);
  };
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
  // Put in order while none is hidden by what's typed: a card goes between the cards
  // of its column it's dropped between.
  const arranged = reorder && reorder.allowed !== false && !wanted ? reorder : undefined;
  const arrange = (id: string, next: string | undefined) => {
    const card = all.find((c) => c.id === id);
    if (!arranged || !card || id === next) return;
    const column = cards.filter((c) => where(c) === where(card) && c.id !== id);
    const at = next === undefined ? column.length : column.findIndex((c) => c.id === next);
    if (at < 0) return;
    const changes = placed(all, arranged.field, card, column[at - 1], column[at]);
    placing.place(changes);
    for (const change of changes) {
      void runner.run(arranged.command, { id: change.id, [arranged.field]: change.value }).then((done) => {
        if (!done) placing.forget([change]);
      });
    }
  };
  // Inside a board switched with a table, its search box and buttons are on the
  // switch's own line above, where they stay whichever is shown.
  const slot = useContext(ToolbarSlot);
  const searchBox = search.length ? { value: query, label: searchLabel(search, columns), onChange: setQuery } : undefined;
  return (
    <>
      {tracked.element}
      {slot && createPortal(<ui.Toolbar search={searchBox} tools={tools} filtered={filtered} />, slot)}
      <ui.Board
        status={status}
        error={(move ? runner.error(move.command) : undefined) ?? (arranged ? runner.error(arranged.command) : undefined)}
        tools={slot ? undefined : tools}
        filtered={slot ? undefined : filtered}
        column={column || undefined}
        onColumn={setColumn}
        search={slot ? undefined : searchBox}
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
        onArrange={arranged ? arrange : undefined}
        columns={things.map((thing) => ({
          id: thing.id,
          title: show(thing[shown]) || thing.id,
          notify: notify.includes(thing.id),
          cards: cards
            .filter((card) => where(card) === thing.id)
            .map((card) => ({
              id: card.id,
              title: title ? cellOf(ui, links, card, title, { choices, labels, pictures, people }) : card.id,
              link: link ? links(rowLink(link, card, params, keyed, named)) : undefined,
              // A number said as one, like #12; and who's working it apart, by name
              // and picture.
              details: rest
                .filter((key) => people[key] === undefined)
                .map((key) => (numbered(key) && show(card[key]) !== "" ? `#${show(card[key])}` : cellOf(ui, links, card, key, { choices, labels, pictures, people })))
                .filter((detail) => detail !== "")
                .concat(going(card, statuses[String(where(card))], now).extra)
                .concat(waitingOn(card).length ? [`waiting on ${waitingOn(card).join(", ")}`] : []),
              people: rest.filter((key) => people[key] !== undefined).flatMap((key) => peopleOf(card, key, people[key])),
              reaches: reaches(where(card)),
              tone: toneOf(card, tint),
              progress: going(card, statuses[String(where(card))], now).progress,
              status: waitingOn(card).length && (!going(card, statuses[String(where(card))], now).status || going(card, statuses[String(where(card))], now).status?.is === "inactive")
                ? { is: "waiting" as const, words: `Waiting on ${waitingOn(card).join(", ")}` }
                : going(card, statuses[String(where(card))], now).status,
            })),
        }))}
      />
    </>
  );
}

// A pipeline: a list's rows in a lane for each kind its lanes field says, each a row of
// the stages its kind goes through, as their path field lists: those naming its kind,
// and those naming none but where cards fail. Those before where it is are done; the
// one it failed in, as its data's at says, is red.
export function Pipeline({
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
  lanes = "labels",
  path = "kinds",
  statusBy,
  waitsOn,
  track,
}: {
  view: ViewState;
  list: string;
  by: string;
  over: ViewState;
  overList: string;
  shown?: string;
  columns: Record<string, string>;
  link?: string;
  keyed?: string[];
  named?: Record<string, number>;
  lanes?: string;
  path?: string;
  statusBy?: string;
  waitsOn?: string; // the field listing what a card waits on: while any hasn't succeeded, it's waiting, saying on what
  track?: TrackSpec; // in a switch, the track above, whose pick keeps its cards
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const every = rowsOf(view.data?.[list]);
  const kept = useTrack(track, every, list);
  const cards = every.filter(kept.keeps);
  const now = useNow(cards.some((card) => typeof (card.data as Record<string, unknown> | undefined)?.since === "string"));
  const words = (field: unknown): string[] => (Array.isArray(field) ? field.map((w) => show(w)) : show(field) ? [show(field)] : []);
  const stages = rowsOf(over.data?.[overList]).map((row) => ({
    id: row.id,
    title: show(row[shown]) || row.id,
    status: statusOf(statusBy ? show(row[statusBy]) : "") ?? "",
    kinds: words(row[path]),
  }));
  const kinds: string[] = [];
  for (const stage of stages) for (const kind of stage.kinds) if (!kinds.includes(kind)) kinds.push(kind);
  const pathOf = (kind: string) => stages.filter((stage) => stage.kinds.includes(kind) || (stage.kinds.length === 0 && stage.status !== "failed"));
  const [title, ...rest] = Object.keys(columns);
  const byLane = new Map<string, PipelineRow[]>();
  for (const card of cards) {
    const said = words(card[lanes]);
    const lane = kinds.find((kind) => said.includes(kind)) ?? "";
    const at = String(card[by] ?? "");
    const here = stages.find((stage) => stage.id === at);
    const data = (card.data ?? {}) as Record<string, unknown>;
    const failed = here?.status === "failed";
    const failedAt = failed ? show(data.at) : "";
    const steps = pathOf(lane);
    let index = steps.findIndex((stage) => stage.id === at || (failed && (stage.title === failedAt || stage.id === failedAt)));
    if (index < 0) index = steps.length;
    const row: PipelineRow = {
      id: card.id,
      title: title ? cellOf(ui, links, card, title, {}) : card.id,
      link: link ? links(rowLink(link, card, params, keyed, named)) : undefined,
      details: rest
        .map((key) => (numbered(key) && show(card[key]) !== "" ? `#${show(card[key])}` : cellOf(ui, links, card, key, {})))
        .filter((detail) => detail !== "")
        .concat(going(card, here?.status, now).extra),
      progress: going(card, here?.status, now).progress,
      steps: steps
        .map((stage, i) => ({
          id: stage.id,
          title: stage.title,
          at: (i < index ? "done" : i === index ? (failed ? "failed" : stage.status === "success" ? "done" : "now") : "later") as PipelineRow["steps"][number]["at"],
        }))
        .concat(failed && index === steps.length && here ? [{ id: here.id, title: here.title, at: "failed" as const }] : []),
      status: going(card, here?.status, now).status,
      note: typeof data.note === "string" && data.note ? data.note : undefined,
    };
    // What it waits on that hasn't reached a success end keeps it waiting, saying on what.
    const listed = waitsOn && Array.isArray(card[waitsOn]) ? (card[waitsOn] as unknown[]) : [];
    const undone = listed
      .map((id) => cards.find((other) => other.id === id))
      .filter((other): other is Row => Boolean(other) && stages.find((stage) => stage.id === String(other![by]))?.status !== "success")
      .map((other) => show(other[title ?? "title"]) || other.id);
    if (undone.length) {
      if (!row.status || row.status.is === "inactive") row.status = { is: "waiting", words: `Waiting on ${undone.join(", ")}` };
      row.note = `waiting on ${undone.join(", ")}`;
    }
    byLane.set(lane, [...(byLane.get(lane) ?? []), row]);
  }
  const capital = (word: string) => (word ? word[0].toUpperCase() + word.slice(1) : "Others");
  const shownLanes = [...kinds, ""].filter((kind) => byLane.has(kind)).map((kind) => ({ title: capital(kind), rows: byLane.get(kind)! }));
  const status = view.status !== "live" ? view.status : over.status;
  return <ui.Pipeline status={status} lanes={shownLanes} />;
}

// A canvas: a list's rows as cards placed anywhere, where their fields say, moved
// and resized, and placed by a command once let go. A card not placed yet is laid out
// in columns by its by field, a grid step apart, as a board would show it.
export function Canvas({
  view,
  list,
  columns,
  link,
  keyed,
  named,
  pictures = [],
  people = {},
  choices = {},
  labels = [],
  x,
  y,
  w,
  h,
  kind,
  place,
  grid,
  by,
  over,
  overList,
  tint,
  regions,
  move,
  resize,
  track,
  paste,
  picture,
}: {
  view: ViewState;
  list: string;
  columns: Record<string, string>; // what a card shows, its title first
  link?: string;
  keyed?: string[];
  named?: Record<string, number>;
  pictures?: string[];
  people?: Record<string, string>;
  choices?: Record<string, Record<string, string>>;
  labels?: string[];
  x: string; // the fields a card's place is kept in
  y: string;
  w: string;
  h: string;
  kind?: string; // the field saying how a card's drawn, like note or text
  place?: string; // the command a card's place is sent to
  grid?: { step: number; x: number; y: number };
  by?: string; // the field the unplaced are laid out by, like stage
  over?: ViewState;
  overList?: string; // its columns, in order, like stages
  tint?: Tint;
  regions?: string; // over's field keeping each region's width, a band down the canvas for each
  move?: string; // the command a card let go in another region is moved there by
  resize?: string; // the command a region's new width is sent to
  track?: TrackSpec; // in a switch, the track above, whose pick keeps its cards
  // A file pasted or dropped on it makes a card, by this command: the file in its
  // field into, a list when many, named from the file in named, where it landed.
  paste?: { command: string; into: string; many?: boolean; named?: string; given?: Record<string, unknown> };
  picture?: string; // the field whose first image a card shows
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const navigate = useNavigate();
  const runner = useConfirmedRunner();
  const store = useFiles();
  const every = rowsOf(view.data?.[list]);
  // Where each card's picture is read from, asked once for each file.
  const imageOf = (card: Row) => {
    const held = picture ? card[picture] : undefined;
    const paths = Array.isArray(held) ? held : held ? [held] : [];
    return paths.map(String).find((path) => /\.(png|jpe?g|gif|webp|avif|svg)$/i.test(path));
  };
  const [addresses, setAddresses] = useState<Record<string, string>>({});
  const images = picture ? every.map(imageOf).filter((path): path is string => Boolean(path)) : [];
  useEffect(() => {
    for (const path of images) {
      if (addresses[path] || !store) continue;
      store.address(path).then((address) => setAddresses((now) => ({ ...now, [path]: address })), () => {});
    }
  }, [images.join("\n")]);
  const [pasted, setPasted] = useState<string>();
  const onFiles =
    paste && store
      ? async (files: File[], px: number, py: number) => {
          setPasted(undefined);
          const step = grid?.step || 160;
          for (const [at, file] of files.entries()) {
            try {
              const path = await store.upload(file, () => {});
              const name = file.name.replace(/\.[^.]+$/, "");
              const fields: CommandInput = {
                ...(paste.given ?? {}),
                [paste.into]: paste.many ? [path] : path,
                [x]: px + at * (step / 4),
                [y]: py + at * (step / 4),
                [w]: step,
                [h]: step,
              };
              if (paste.named) fields[paste.named] = !name || name === "image" ? "Pasted image" : name;
              await runner.run(paste.command, fields);
            } catch (error) {
              setPasted(error instanceof Error ? error.message : String(error));
            }
          }
        }
      : undefined;
  const kept = useTrack(track, every, list);
  const rows = every.filter(kept.keeps);
  const now = useNow(rows.some((card) => typeof (card.data as Record<string, unknown> | undefined)?.since === "string"));
  const { matches } = useFilter();
  // Where each placed card is until the view says so too.
  const [placed, setPlaced] = useState<Record<string, [number, number, number, number]>>({});
  const number = (v: unknown) => (typeof v === "number" ? v : undefined);
  useEffect(() => {
    setPlaced((now) => {
      const kept = Object.entries(now).filter(([id, at]) => {
        const row = rows.find((r) => r.id === id);
        return !row || number(row[x]) !== at[0] || number(row[y]) !== at[1] || number(row[w]) !== at[2] || number(row[h]) !== at[3];
      });
      return kept.length === Object.keys(now).length ? now : Object.fromEntries(kept);
    });
  }, [view.data]);
  const order = by && over && overList ? rowsOf(over.data?.[overList]).map((row) => row.id) : [];
  const step = grid?.step || 160;
  // The regions, over's rows side by side from the grid's start, each as wide as it says
  // or as it's just been made.
  const [resized, setResized] = useState<Record<string, number>>({});
  const bands: CanvasRegion[] = [];
  if (regions && over && overList) {
    let at = grid?.x ?? 0;
    for (const row of rowsOf(over.data?.[overList])) {
      const width = resized[row.id] ?? ((typeof row[regions] === "number" && (row[regions] as number) > 0 ? (row[regions] as number) : 2 * step));
      bands.push({ id: row.id, title: show(row.title ?? row.name) || row.id, x: at, width });
      at += width;
    }
  }
  const down: Record<string, number> = {};
  const [title, ...rest] = Object.keys(columns);
  const cards = rows.filter(matches).map((card) => {
    // Its place among its column's cards, placed or not, so placing one moves no other.
    const column = by ? String(card[by] ?? "") : "";
    const inColumn = (down[column] = (down[column] ?? -1) + 1);
    const at = placed[card.id];
    let box = at ? { x: at[0], y: at[1], width: at[2], height: at[3] } : undefined;
    if (!box) {
      const width = number(card[w]) || 150;
      const height = number(card[h]) || 150;
      const cx = number(card[x]);
      const cy = number(card[y]);
      // Placed: with a size, which only a place gives, as a number never given reads 0.
      if (cx !== undefined && cy !== undefined && (number(card[w]) ?? 0) > 0 && (number(card[h]) ?? 0) > 0) {
        box = { x: cx, y: cy, width, height };
      } else {
        // Laid out: its column's place across, and its place in the column down; in its
        // region, when there are regions.
        if (!order.includes(column)) order.push(column);
        const band = bands.find((b) => b.id === column);
        box = {
          x: band ? band.x + Math.max(0, (band.width - width) / 2) : (grid?.x ?? 0) + order.indexOf(column) * step,
          y: (grid?.y ?? 0) + inColumn * step,
          width,
          height,
        };
      }
    }
    return {
      id: card.id,
      title: title ? cellOf(ui, links, card, title, { choices, labels, pictures, people }) : card.id,
      kind: kind ? String(card[kind] ?? "") : undefined,
      link: link ? links(rowLink(link, card, params, keyed, named)) : undefined,
      details: rest
        .map((key) => (numbered(key) && show(card[key]) !== "" ? `#${show(card[key])}` : cellOf(ui, links, card, key, { choices, labels, pictures, people })))
        .filter((detail) => detail !== "")
        .concat(going(card, undefined, now).extra),
      tone: toneOf(card, tint),
      status: going(card, undefined, now).status,
      progress: going(card, undefined, now).progress,
      picture: addresses[imageOf(card) ?? ""],
      ...box,
    };
  });
  const loading = view.status !== "live" ? view.status : over && over.status !== "live" ? over.status : "live";
  return (
    <ui.Canvas
      status={loading}
      cards={cards}
      grid={grid}
      error={pasted}
      onFiles={onFiles ? (files, px, py) => void onFiles(files, px, py) : undefined}
      onPlace={
        place
          ? (id, px, py, pw, ph) => {
              const card = rows.find((c) => c.id === id);
              if (!card) return;
              setPlaced((now) => ({ ...now, [id]: [px, py, pw, ph] }));
              // Let go in another region, it moves there, as a board's card does.
              const middle = px + pw / 2;
              const into = bands.find((b) => middle >= b.x && middle < b.x + b.width);
              if (move && by && into && card[by] !== into.id) void runner.run(move, { id, [by]: into.id }, card);
              void runner.run(place, { id, [x]: px, [y]: py, [w]: pw, [h]: ph }, card).then((done) => {
                if (!done) setPlaced((now) => Object.fromEntries(Object.entries(now).filter(([other]) => other !== id)));
              });
            }
          : undefined
      }
      regions={bands.length ? bands : undefined}
      onResize={
        resize && regions
          ? (id, width) => {
              setResized((now) => ({ ...now, [id]: width }));
              const row = rowsOf(over?.data?.[overList ?? ""]).find((r) => r.id === id);
              void runner.run(resize, { id, [regions]: width }, row).then((done) => {
                if (!done) setResized(({ [id]: _, ...rest }) => rest);
              });
            }
          : undefined
      }
      onOpen={link ? (id) => {
        const card = rows.find((c) => c.id === id);
        if (card) navigate(rowLink(link, card, params, keyed, named));
      } : undefined}
    />
  );
}

const statusWords: Record<string, string> = { inactive: "Not started", waiting: "Waiting", working: "Working", success: "Succeeded", failed: "Failed" };
// done is said success now; what was stored before reads as it.
const statusOf = (status: string | undefined) => (status === "done" ? "success" : status);

export { runningTime };

// A card's dot, running time and runner: what its holder reported in its data, else
// working while someone's lease on it holds, else its column's status.
function going(card: Row, column: string | undefined, now: number): { status?: BoardCard["status"]; extra: string[]; progress?: BoardCard["progress"] } {
  const data = (card.data ?? {}) as Record<string, unknown>;
  const stated = typeof data.status === "string" ? statusOf(data.status) : undefined;
  const said = stated && statusWords[stated] ? stated : undefined;
  const until = card.held_until ? new Date(String(card.held_until)).getTime() : 0;
  const held = Boolean(card.held_by) && until > now;
  const columnIs = statusOf(column);
  const is = said ?? (held ? "working" : columnIs && statusWords[columnIs] ? columnIs : undefined);
  const extra: string[] = [];
  if (typeof data.runner === "string" && data.runner) extra.push(`runner ${data.runner}`);
  if (typeof data.since === "string" && !Number.isNaN(Date.parse(data.since))) extra.push(runningTime((now - Date.parse(data.since)) / 1000));
  // How far it is in its stage, as its holder says: step 2 of 5. Only while they
  // hold it: let go or moved on, what they last said is about where it was.
  const step = Number(data.step), steps = Number(data.steps);
  const progress = held && typeof data.steps === "number" && steps > 0 && Number.isFinite(step) ? { step: Math.min(Math.max(step, 0), steps), steps } : undefined;
  return { status: is ? { is: is as NonNullable<BoardCard["status"]>["is"], words: statusWords[is]! } : undefined, extra, progress };
}

// The time now, ticking each second while something shows a running time.
function useNow(ticking: boolean): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!ticking) return;
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(timer);
  }, [ticking]);
  return now;
}

// A short tone when a card arrives in a column that notifies, once it's arrived; the
// cards there when the board first shows don't sound.
function useArrivals(view: ViewState, list: string, by: string, notify: string[], statuses: Record<string, string> = {}) {
  const seen = useRef<Map<string, string> | undefined>(undefined);
  useEffect(() => {
    if (notify.length === 0 || view.status !== "live") return;
    const now = new Map(rowsOf(view.data?.[list]).filter((card) => notify.includes(String(card[by]))).map((card) => [card.id, String(card[by])]));
    const before = seen.current;
    seen.current = now;
    if (!before) return;
    for (const [id, at] of now) {
      if (before.get(id) !== at) {
        // Where things fail, the falling tone; anywhere else, the rising one.
        tone(statuses[at] === "failed");
        return;
      }
    }
  }, [view, list, by, notify.join(" ")]);
}

// Two notes, quietly, through the page's audio: rising for something done, falling and
// lower for something failed.
function tone(failed = false) {
  try {
    const AudioContext = window.AudioContext ?? (window as unknown as { webkitAudioContext?: typeof window.AudioContext }).webkitAudioContext;
    if (!AudioContext) return;
    const audio = new AudioContext();
    (failed ? [440, 311] : [660, 880]).forEach((pitch, at) => {
      const note = audio.createOscillator();
      const loudness = audio.createGain();
      note.frequency.value = pitch;
      const start = audio.currentTime + at * 0.12;
      loudness.gain.setValueAtTime(0.0001, start);
      loudness.gain.exponentialRampToValueAtTime(0.08, start + 0.02);
      loudness.gain.exponentialRampToValueAtTime(0.0001, start + 0.3);
      note.connect(loudness).connect(audio.destination);
      note.start(start);
      note.stop(start + 0.32);
    });
    setTimeout(() => void audio.close(), 800);
  } catch {
    // No sound where the page can't make one.
  }
}

// One of a few ways to show the same list, like a table and a board, as the person
// picks; their pick is remembered in their browser.
export function Switched({
  id,
  label: says,
  options,
  icons = [],
  start = -1,
  track,
  view,
  list = "rows",
  children,
}: {
  id: string;
  label: string;
  options: string[];
  icons?: string[]; // drawn for each option instead of its name, like table and board
  start?: number; // the one shown until the person picks, like the one a board's style says; the first when it's -1
  // One track for every way the list is shown, drawn above the switch: its records'
  // dots from the stages it's told of, and its pick in the address, which each keeps to.
  track?: TrackSpec;
  view?: ViewState;
  list?: string;
  children: ReactNode;
}) {
  const ui = useUI();
  const tracked = useTrack(track && { ...track, above: false }, rowsOf(view?.data?.[list]), list);
  // Remembered for each page it's on, like each board, not once for them all.
  const { base } = useContext(ShownPath);
  const key = `uione:switch:${id}:${base}`;
  // What's shown is in the address, like issues.view=board, so a link shows the same;
  // the person's last pick is remembered too, and put in the address when they come
  // back without one.
  const named = (at: number) => (options[at] ?? "").toLowerCase();
  const [inAddress, setInAddress] = usePathPart(options.map((_, at) => named(at)));
  const fromAddress = options.findIndex((_, at) => named(at) === inAddress);
  // The person's pick, in the address, or else the one it starts on.
  const opening = start >= 0 && start < options.length ? start : 0;
  const picked = fromAddress >= 0 ? fromAddress : opening;
  const pick = (at: number) => {
    setInAddress(at === opening ? "" : named(at));
    try {
      localStorage.setItem(key, String(at));
    } catch {
      // Without storage, the pick lasts as long as the address.
    }
  };
  useEffect(() => {
    if (inAddress) return;
    try {
      const said = localStorage.getItem(key);
      const kept = said === null ? NaN : Number(said);
      if (Number.isInteger(kept) && kept >= 0 && kept < options.length && kept !== opening) setInAddress(named(kept));
    } catch {
      // Nothing remembered.
    }
    // Once, as the page opens.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const switcher = (
    <ui.Switch label={says} options={options.map((option, at) => ({ label: option, icon: icons[at], selected: at === picked, onSelect: () => pick(at) }))} />
  );
  // It sits above what's shown, on the track's line when there's room, with what's
  // shown's search box and buttons, which stay there whichever is shown.
  const [slot, setSlot] = useState<HTMLDivElement | null>(null);
  const shown = Children.toArray(children)[picked];
  if (!isValidElement<{ tools?: ReactNode }>(shown)) return shown ?? null;
  return (
    <ToolbarSlot.Provider value={slot}>
      <div style={{ display: "flex", flexWrap: "wrap", alignItems: "center", gap: "0.5rem 1rem" }}>
        {tracked.element && <div style={{ flex: "1 1 24rem", minWidth: 0 }}>{tracked.element}</div>}
        <div className="uione-toolbar" style={{ display: "flex", flexWrap: "wrap", alignItems: "center", justifyContent: "flex-end", gap: "0.5rem", marginLeft: "auto" }}>
          <div ref={setSlot} style={{ display: "contents" }} />
          {switcher}
        </div>
      </div>
      {shown}
    </ToolbarSlot.Provider>
  );
}

// Where a switched list's toolbar goes, above whichever is shown.
const ToolbarSlot = createContext<HTMLElement | null>(null);

// How a card sums up what it holds, like a board's issues: the list they're in, the
// field naming the card, and the field they're split by, shown by its title and in
// its order when the list has them.
type TallyState = "none" | "partial" | "done" | "failed";
// What a stage says of the rows in it: done once its status is success, failed once
// it's failed, not started in the first stage of all, and under way anywhere else.
function tallyState(status: string, first: boolean): TallyState {
  if (status === "success" || status === "done") return "done";
  if (status === "failed") return "failed";
  return first ? "none" : "partial";
}

export interface Tally {
  view: ViewState;
  list: string;
  by: string; // like board
  and: string; // like stage
  shown?: string; // like stage.title
  order?: string; // like stage.position
  status?: string; // like stage.status: what each part means, not started, under way, done or failed
  noun?: string; // what's counted, like issues
}

// Where each row's picture is read from, a file it holds in a field, asked once for
// each file.
function useAddresses(rows: Row[], field: string | undefined): Record<string, string> {
  const store = useFiles();
  const [addresses, setAddresses] = useState<Record<string, string>>({});
  const held = field ? rows.map((row) => row[field]).filter((path): path is string => typeof path === "string" && path !== "") : [];
  useEffect(() => {
    for (const path of held) {
      if (addresses[path] || !store) continue;
      store.address(path).then((address) => setAddresses((now) => ({ ...now, [path]: address })), () => {});
    }
  }, [held.join("\n")]);
  return addresses;
}

// picture poster.preview fit: the file a value holds, as wide as its place and 320
// tall, all of it fitted in or filling it.
export function Picture({ source, fit = false }: { source: unknown; fit?: boolean }) {
  const ui = useUI();
  const path = typeof source === "string" ? source : "";
  const addresses = useAddresses(path ? [{ id: path, path }] : [], "path");
  return <ui.FilePicture source={addresses[path]} fit={fit} />;
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
  people = {},
  choices = {},
  labels = [],
  tally,
  filters = [],
  hideEmpty = false,
  picture,
  fit = false,
}: {
  view: ViewState;
  list: string;
  columns: Record<string, string>; // what a card shows, its title first
  link?: string;
  keyed?: string[];
  named?: Record<string, number>;
  pictures?: string[];
  people?: Record<string, string>; // a person's column by their name, and their picture beside it, like { "author.name": "author.picture" }
  choices?: Record<string, Record<string, string>>;
  labels?: string[];
  tally?: Tally;
  filters?: { label: string; query: Record<string, string>; hue?: Hue }[];
  hideEmpty?: boolean; // not there at all while it has no cards, or can't be read, like a project's private boards
  picture?: string; // a file each row holds, shown on top of its card
  fit?: boolean; // all of it fitted in, or filling its place
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const [title, ...rest] = Object.keys(columns);
  const counted = tally ? rowsOf(tally.view.data?.[tally.list]) : [];
  const rows = rowsOf(view.data?.[list]);
  const addresses = useAddresses(rows, picture);
  // Hidden when empty is hidden until there's something to show, not drawn while it loads.
  if (hideEmpty && (view.status !== "live" || rows.length === 0)) return null;
  const sum = (card: Row) => {
    if (!tally) return undefined;
    const mine = counted.filter((row) => row[tally.by] === card.id);
    const groups = new Map<unknown, { label: string; count: number; order: number; state: TallyState }>();
    // The first stage anything is in, across the whole list: where work hasn't started.
    const firstOrder = tally.order ? Math.min(...counted.map((row) => Number(row[tally.order!])).filter((n) => Number.isFinite(n))) : Number.NaN;
    for (const row of mine) {
      const key = row[tally.and];
      const group = groups.get(key) ?? {
        label: show(tally.shown ? row[tally.shown] : key) || "None",
        count: 0,
        order: Number(tally.order ? row[tally.order] : 0) || 0,
        state: tallyState(tally.status ? show(row[tally.status]) : "", tally.order !== undefined && Number(row[tally.order]) === firstOrder),
      };
      group.count += 1;
      groups.set(key, group);
    }
    return [...groups.values()].sort((a, b) => a.order - b.order).map(({ label: name, count, state }) => ({ label: name, count, state }));
  };
  return (
    <ui.Cards
      status={view.status}
      cards={rows.map((row) => {
        const to = link ? rowLink(link, row, params, keyed, named) : undefined;
        return {
          id: row.id,
          title: title ? cellOf(ui, links, row, title, { choices, labels, pictures, people }) : row.id,
          link: to ? links(to) : undefined,
          // A choice is a tag beside its title, like Private; the rest are details.
          details: rest
            .filter((key) => !choices[key])
            .map((key) => cellOf(ui, links, row, key, { choices, labels, pictures, people }))
            .filter((detail) => detail !== ""),
          tags: rest.filter((key) => choices[key] && row[key] != null && row[key] !== "").map((key) => choices[key][String(row[key])] ?? show(row[key])),
          tally: sum(row),
          noun: tally?.noun,
          filters: to ? filters.map((f) => ({ label: f.label, link: links(`${to}?${new URLSearchParams({ is: f.label, ...f.query }).toString()}`), ...(f.hue ? { hue: f.hue } : {}) })) : [],
          ...(picture ? { picture: { source: addresses[String(row[picture] ?? "")], fit } } : {}),
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
// stages: a row and a column for each thing, in the list's order, and in each cell
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
  overList: string; // the things, like stages
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
// stages: a row and a column for each thing, in the list's order, and in each cell
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
// from one to another, labeled with what it shows. Pressing an arrow changes or
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
  range?: [number, number, number]; // for a slider: from, to, and the step between
}

// What a field shows for a value from a view: a date as 2026-09-30, the way a date
// input takes it, a list separated by commas, files a path a line, and anything
// else as text.
function asField(value: unknown, type?: string): string {
  if (value instanceof Date) {
    const pad = (n: number) => String(n).padStart(2, "0");
    if (dateOnly(value)) return `${value.getUTCFullYear()}-${pad(value.getUTCMonth() + 1)}-${pad(value.getUTCDate())}`;
    return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())}`;
  }
  if (Array.isArray(value)) return value.map((item) => asField(item)).join(type === "files" ? "\n" : ", ");
  return value === null || value === undefined ? "" : String(value);
}

// What a form sends for a field: a yes or no as true or false, a number as one,
// files as the list of their paths, and a list, written separated by commas, or
// several choices ticked, as the list of what's between them.
function asInput(value: string, type: string | undefined): unknown {
  if (type === "boolean") return value === "true";
  // A number as one, and nothing for an empty box.
  if (type === "number" || type === "slider") return value.trim() === "" ? undefined : Number.isNaN(Number(value)) ? value : Number(value);
  if (type === "files") return value.split("\n").filter((path) => path !== "");
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
  after,
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
  after?: string; // where to go once it's done, like the page above one it deleted the subject of; its :parameters are the page's
}) {
  const ui = useUI();
  const auth = useAuth();
  const signIn = useSignIn();
  const runner = useConfirmedRunner();
  const navigate = useNavigate();
  const filled = useFilled();
  const specs = fields.map((f) => (typeof f === "string" ? { name: f } : f));
  const empty = () => Object.fromEntries(specs.map((f) => [f.name, f.start ?? ""]));
  const [values, setValues] = useState<Record<string, string>>(empty);
  const [open, setOpen] = useState(false);
  // Files on their way up, for each field: each by its name, how far it's come, and
  // why it didn't arrive, if it didn't.
  const store = useFiles();
  const [uploading, setUploading] = useState<Record<string, { id: number; name: string; progress: number; error?: string }[]>>({});
  const uploads = useRef(0);
  const sending = Object.values(uploading).some((list) => list.some((u) => u.error === undefined));
  // An update form starts from what's stored, and follows it until someone types.
  // After an update it keeps what was sent until the view has a newer document,
  // rather than showing the old one for a moment.
  const [touched, setTouched] = useState(false);
  const [sent, setSent] = useState<ViewState["data"]>();
  const stored = from?.status === "live" ? from.data : undefined;
  useEffect(() => {
    if (!stored || touched || stored === sent) return;
    setValues(Object.fromEntries(specs.map((f) => [f.name, asField(stored[f.name], f.type)])));
    // The fields come from the screen and never change, so they aren't a dependency.
  }, [stored, touched, sent]);

  // A file chosen goes up at once, and its path is the field's once it's there: in
  // place of the one before, or beside the others.
  const choose = (f: FieldSpec, chosen: File[]) => {
    runner.clear(command);
    setTouched(true);
    for (const file of f.type === "files" ? chosen : chosen.slice(0, 1)) {
      const id = ++uploads.current;
      const change = (patch: Partial<{ progress: number; error: string }> | null) =>
        setUploading((current) => ({
          ...current,
          [f.name]: (current[f.name] ?? []).flatMap((u) => (u.id !== id ? [u] : patch === null ? [] : [{ ...u, ...patch }])),
        }));
      setUploading((current) => ({ ...current, [f.name]: [...(f.type === "files" ? (current[f.name] ?? []) : []), { id, name: file.name, progress: 0 }] }));
      if (!store) {
        change({ error: "this app keeps no files" });
        continue;
      }
      store.upload(file, (progress) => change({ progress })).then(
        (path) => {
          change(null);
          setValues((current) => ({
            ...current,
            [f.name]: f.type === "files" ? [...(current[f.name] ?? "").split("\n").filter((p) => p !== ""), path].join("\n") : path,
          }));
        },
        (error: unknown) => change({ error: error instanceof Error ? error.message : `${file.name} didn't upload` }),
      );
    }
  };

  const submit = async () => {
    if (sending) return;
    const typed = Object.fromEntries(specs.map((f) => [f.name, asInput(values[f.name] ?? "", f.type)]));
    const input = { ...typed, ...given };
    if (await runner.run(command, id === undefined ? input : { ...input, id })) {
      if (from) {
        setSent(stored);
        setTouched(false);
      } else {
        setValues(empty());
      }
      setUploading({});
      setOpen(false);
      onDone?.();
      if (after !== undefined) void navigate(filled(after));
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
          range: f.range,
          value: values[f.name] ?? "",
          hint: f.hint,
          onChange: (value) => {
            runner.clear(command);
            setTouched(true);
            setValues((current) => ({ ...current, [f.name]: value }));
          },
          ...(f.type === "file" || f.type === "files"
            ? {
                files: [
                  ...(values[f.name] ?? "")
                    .split("\n")
                    .filter((path) => path !== "")
                    .map((path) => ({ name: fileName(path) })),
                  ...(uploading[f.name] ?? []).map((u) => ({ name: u.name, progress: u.progress, error: u.error })),
                ],
                onFiles: (chosen: File[]) => choose(f, chosen),
                onRemove: (index: number) => {
                  const kept = (values[f.name] ?? "").split("\n").filter((path) => path !== "");
                  setTouched(true);
                  if (index < kept.length) {
                    setValues((current) => ({ ...current, [f.name]: kept.filter((_, i) => i !== index).join("\n") }));
                  } else {
                    const gone = (uploading[f.name] ?? [])[index - kept.length];
                    setUploading((current) => ({ ...current, [f.name]: (current[f.name] ?? []).filter((u) => u.id !== gone?.id) }));
                  }
                },
              }
            : {}),
        }),
      )}
      submit={says ?? label(action(command))}
      busy={runner.busy(command) || sending}
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

// What a chart shows of a run of numbers: its values, newest last, and how they read.
function chartOf(title: string, value: unknown, unit: ChartUnit, size: ChartProps["size"]): ChartProps {
  const values = seriesOf(value);
  const top = scaleTop(values, unit);
  const read = (n: number) => readValue(n, unit);
  return { title, values, top, latest: values.length ? read(values[values.length - 1]!) : "–", topSaid: read(top), read, size };
}

// Runs of numbers on a page, like an agent's CPU use over the last two minutes:
// chart agent_page.stats.cpu "CPU" percent, or several side by side.
export function Chart({ view, series }: { view: ViewState; series: { path: string; title: string; unit?: ChartUnit }[] }) {
  const ui = useUI();
  const charts = series.map((s) => chartOf(s.title, view.status === "live" ? valueAt(view.data, s.path) : undefined, s.unit ?? "", "large"));
  if (charts.length === 1) return <ui.Chart {...charts[0]!} />;
  return <ui.Charts charts={charts} />;
}

// Records whose status changes, each a tile, like a project's agents: monitor
// agents_page.agents { status status  doing doing  chart stats.cpu "CPU" percent }.
// One not heard from within seen's seconds is offline, whatever it last said.
export function Monitor({
  view,
  list,
  link,
  keyed,
  named,
  title = "title",
  status,
  doing,
  step,
  steps,
  seen,
  seenSeconds = 0,
  since,
  charts = [],
  choices = {},
}: {
  view: ViewState;
  list: string;
  link?: string;
  keyed?: string[];
  named?: Record<string, number>;
  title?: string;
  status: string;
  doing?: string;
  step?: string;
  steps?: string;
  seen?: string;
  seenSeconds?: number;
  since?: string; // when its status began, for how long it's been so
  charts?: { path: string; title: string; unit?: ChartUnit }[];
  choices?: Record<string, Record<string, string>>;
}) {
  const ui = useUI();
  const links = useLinks();
  const params = useParams();
  const now = useNow(true);
  const rows = rowsOf(view.data?.[list]);
  return (
    <ui.Monitor
      status={view.status}
      tiles={rows.map((row) => {
        const word = show(valueAt(row, status));
        const heard = seen ? Date.parse(String(valueAt(row, seen) ?? "")) : Number.NaN;
        const gone = seen !== undefined && seenSeconds > 0 && (Number.isNaN(heard) || now - heard > seenSeconds * 1000);
        const to = link ? rowLink(link, row, params, keyed, named) : undefined;
        const began = since ? Date.parse(String(valueAt(row, since) ?? "")) : Number.NaN;
        const at = step ? Number(valueAt(row, step)) : Number.NaN;
        const of = steps ? Number(valueAt(row, steps)) : Number.NaN;
        const said = doing ? show(valueAt(row, doing)) : "";
        return {
          id: row.id,
          title: show(valueAt(row, title)) || row.id,
          link: to ? links(to) : undefined,
          state: gone ? "Offline" : (choices[status]?.[word] ?? label(word)) || "Unknown",
          tone: gone ? "inactive" : toneOfState(word),
          since: !gone && !Number.isNaN(began) ? runningTime((now - began) / 1000) : undefined,
          doing: gone ? undefined : said || undefined,
          progress: !gone && Number.isFinite(at) && Number.isFinite(of) && of > 0 ? { step: Math.min(Math.max(at, 0), of), steps: of } : undefined,
          charts: charts.map((c) => chartOf(c.title, gone ? [] : valueAt(row, c.path), c.unit ?? "", "small")),
        };
      })}
    />
  );
}
