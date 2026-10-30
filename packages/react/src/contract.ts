// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The contract between @uione/react and a component set. @uione/react decides what's
// on a screen and how it behaves. A component set decides only how it looks, so
// every prop here is already worked out: text to show, values to fill in, and
// functions to call. A component set never fetches, routes or runs commands itself.

import type { ComponentType, MouseEvent, ReactNode } from "react";
import type { AuthenticationMethod, ViewStatus } from "./data.js";

export interface LinkProps {
  href: string;
  onClick?: (event: MouseEvent<HTMLAnchorElement>) => void;
  // It leaves the app, for another site: it opens in a new tab, and says so with
  // the link-external mark.
  external?: boolean;
}

export interface NavItem extends LinkProps {
  label: string;
  current: boolean;
}

export interface PageProps {
  name: string;
  icon?: string; // the address of the app's icon, shown beside its name
  home: LinkProps;
  nav: NavItem[];
  title: string;
  // Whether the screen is titled with just the app's name, like its home page, so the
  // title isn't said again under the name. Never judged by the title shown: a project
  // named like the app is still titled.
  untitled: boolean;
  account?: ReactNode; // who's signed in, drawn with Account, when the app has sign-in
  heading?: ReactNode; // where a screen's own buttons go on the title's row, at its end
  crumbs?: ReactNode; // where the pages above a screen go, above its title
  subtitle?: ReactNode; // where a screen's words about itself go, under its title
  // Numbers beside the app's name, like what's new to the person reading, each said
  // with what it counts, like unread; each is shown when more than none.
  badges?: readonly { count: number; label: string }[];
  footer?: FooterProps; // at the foot of the page, as the site's footer says
  display?: boolean; // whether to offer display settings beside light and dark
  children: ReactNode;
}

// What a site was built from: the uione release, and the commit and repository the
// deploy found, which its footer can show and link.
export interface Build {
  version: string; // like 0.18.0
  commit?: string; // like d9d95fd0...
  repository?: string; // where the commit can be read, like https://github.com/someone/tracker
}

// Where what a site was built from can be read: its uione release's notes on GitHub,
// and its commit in its own repository, when that's known.
export function buildLinks(build: Build): { release?: string; source?: string } {
  return {
    ...(build.version ? { release: `https://github.com/da0x/uione/releases/tag/v${build.version}` } : {}),
    ...(build.commit && build.repository ? { source: `${build.repository}/commit/${build.commit}` } : {}),
  };
}

// What's at the foot of every page, as the site's footer declares it: in one quiet
// line, its parts apart, or in columns, each a section.
export interface FooterProps {
  layout: "bar" | "columns";
  children: ReactNode;
}

export interface AccountProps {
  name: string | undefined; // who's signed in, or undefined for no one
  ready: boolean; // false until it's known whether anyone is signed in
  error?: string; // why signing in or out just failed
  onSignIn: () => void;
  onSignOut: () => void;
}

// Choosing how to sign in, when a site offers more than one way: a button for each,
// with its mark, like Continue with Google.
export interface SignInProps {
  methods: AuthenticationMethod[];
  busy?: string; // the way being signed in with now, by its id
  error?: string; // why signing in just failed
  onChoose: (method: string) => void;
}

// What people wrote, one after another, each with who wrote it and when, like an
// issue's comments.
export interface ThreadEntry {
  id: string;
  author: string;
  picture?: string; // an https address of the author's picture
  when: string; // when it was written, as it's shown
  body: ReactNode;
}

export interface ThreadProps {
  status: ViewStatus;
  entries: ThreadEntry[];
}

// What happened to something, one change after another, each said in words: who,
// what they did, like "closed this", and when.
export interface TimelineEntry {
  id: string;
  fresh?: boolean; // made since the person last looked
  who: string;
  what: string; // like "closed this", or, with a subject, the words before it, like "closed"
  when: string;
  subject?: string; // what changed, like "#12 Copy an issue whole", for a timeline of many things
  link?: LinkProps; // where the subject is
  after?: string; // the words after the subject, like "from open to closed"
}

export interface TimelineProps {
  status: ViewStatus;
  entries: TimelineEntry[];
  title?: string; // what it's of, said above it, like "What's new"; with none, nothing is shown while it's empty
  fresh?: number; // how many are new since the person last looked
}

// A screen laid out in regions, like two_columns' main and side: what's in each, by
// its name. A component set draws the layouts it knows, and anything else as one
// column, its regions one after another.
export interface LayoutProps {
  name: string;
  regions: Record<string, ReactNode>;
  // The app's own name, which a workspace's bar starts with, as a site's bar does.
  site?: string;
  // Only its picture, a workspace's main all of it: its other regions over it, see
  // through, while they're awake, as they are for a moment after the pointer moves;
  // while they're not, hidden but still there, so their keys still work, and the
  // pointer hidden too. The picture never moves for them. A drawer that's open, like
  // side when about, stays shown, as it was asked for, while the rest rests.
  alone?: { awake: boolean; drawers?: string[] };
}

// Buttons that sit together in a row, like Edit and Close issue.
export interface ActionsProps {
  children: ReactNode;
}

export interface HeroProps {
  title: string;
  children: ReactNode;
}

// The colors a section or a filter is drawn in, from uione's library, each in the
// component set's own shade of it, light and dark.
export type Hue = "blue" | "teal" | "green" | "amber" | "red" | "violet";

export interface SectionProps {
  title: string;
  id?: string;
  hue?: Hue; // its heading and links in this color, rather than the site's own
  children: ReactNode;
}

export interface TextProps {
  children: ReactNode;
}

export interface LinkViewProps extends LinkProps {
  children: ReactNode;
  icon?: string; // drawn as an icon, like a button's, its words still naming it
}

export interface CodeProps {
  lang: string;
  source: string;
}

export interface PageLink extends LinkProps {
  title: string;
  current: boolean;
}

// Links down the side of a screen, with the rest of the screen beside them, like a
// project's settings: General, Deployments.
export interface MenuProps {
  links: PageLink[];
  children: ReactNode;
}

export interface PagesProps {
  pages: PageLink[];
  title: string | undefined;
  // Trusted: HTML made when the app was built, from the app's own docs, so it can
  // be drawn as it is. Undefined when the address names no page.
  html: string | undefined;
  // Set as a printed document, like a license: a sheet in the theme's document font,
  // its titles centered.
  document?: boolean;
}

export interface RowAction {
  label: string;
  onClick: () => void;
  disabled: boolean;
  icon?: string; // drawn as that icon, its label said when pointed at
}

export interface TableRow {
  id: string;
  link?: LinkProps; // the page this row opens, when the table has a link
  cells: ReactNode[];
  actions: RowAction[];
  tone?: number; // how urgent it is, as Trac colored a ticket's priority: 1 the most, up to 5
  // Picked with a click, when the screen picks a row of the list: what's picked is
  // drawn as it, and onPick picks this one.
  picked?: boolean;
  onPick?: () => void;
  // A small picture before its cells, all of it fitted in or filling its place; its
  // address, none while it's found or when the row has none.
  picture?: { source?: string; fit: boolean };
}

export interface TableProps {
  status: ViewStatus;
  columns: string[];
  rows: TableRow[];
  error?: string; // why the last action on a row failed
  tabs?: TableTab[]; // its rows by one of their choices, like Open and Closed, one shown at a time
  search?: TableSearch; // a box that finds rows by what's typed
  pages?: TablePages; // its rows a page at a time
  reorder?: TableReorder; // its rows put in order by the person, when they may
  tools?: ReactNode; // buttons beside its search, like New issue
  filtered?: Filtered; // the filter its rows are picked by, like Opened by me, which can be cleared
  hue?: Hue; // its rows' links in this color, rather than the site's own
  empty?: string; // what it says while it has no rows, rather than the usual words
}

// What a list is filtered by, from the page's address, and how to stop filtering.
export interface Filtered {
  label: string;
  onClear: () => void;
}

// Rows a person puts in order: dragged by their handles, or moved with Alt+↑ and
// Alt+↓ on a handle. onMove says which row went where, by their places in rows.
export interface TableReorder {
  label: string; // what a handle says, before the row's name, like Move
  onMove: (from: number, to: number) => void;
}

// Cards in columns, like a project's issues in its stages, a column for each in
// order. A card is moved to a column it may go to, by dragging it or with Alt+← and
// Alt+→; while one is dragged, the columns it can't go to are dimmed.
export interface BoardProps {
  status: ViewStatus;
  columns: BoardColumn[];
  error?: string;
  onMove?: (card: string, column: string) => void; // absent when no card can be moved
  // Puts a card in its column just before another of its cards, or last when there's
  // none; absent when cards aren't put in order.
  onArrange?: (card: string, next: string | undefined) => void;
  search?: TableSearch; // a box that finds cards by what's typed
  tools?: ReactNode; // buttons beside its search, like New issue
  filtered?: Filtered;
  // On a phone, the column shown, one a page, by its id, and how to show another, so
  // the address can say which.
  column?: string;
  onColumn?: (column: string) => void;
}

// A calendar: what it's on, like October 2026, and the ways back, to today and on; its
// days, a week side by side or a month's grid, each with the rows on it, a day alone
// first, then by their time of day, each picked with a click and dragged to another
// day (decision 0023).
export interface CalendarProps {
  status: ViewStatus;
  title: string;
  month: boolean;
  days: CalendarDay[];
  error?: string;
  back: { label: string; onClick: () => void }; // like Last week
  next: { label: string; onClick: () => void };
  onToday: () => void;
  onMove?: (card: string, date: string) => void; // absent when no row can be moved
}

export interface CalendarDay {
  date: string; // 2026-10-28
  label: string; // Wed 28, or 28 in a month's grid
  today: boolean;
  inMonth: boolean; // in a month's grid, a day of that month, not one around it
  cards: CalendarCard[];
  more: number; // how many more there are than it shows, in a month's grid
}

export interface CalendarCard {
  id: string;
  title: ReactNode;
  time: string; // like 09:00, or nothing for a day alone
  details: ReactNode[];
  tone?: number; // how urgent it is, 1 the most, up to 5, as a table's row
  picked?: boolean;
  onPick?: () => void;
}

// What's said as rows' times come, whatever screen is open (decision 0024): each a
// title, what's under it, and buttons acting on its row, kept in a corner of the page
// until a button's pressed or it's put away.
export interface NoticesProps {
  notices: Notice[];
}

export interface Notice {
  key: string;
  title: string;
  body: string;
  buttons: { label: string; onClick: () => void }[];
  onClose: () => void;
}

// A pipeline, like a release's: a lane for each kind of card, in the order their stages
// come, and in it a row for each card, its kind's stages along it, each done, the one
// it's at now, one still to come, or the one it failed in.
export interface PipelineProps {
  status: ViewStatus;
  lanes: PipelineLane[];
}

export interface PipelineLane {
  title: string;
  rows: PipelineRow[];
}

export interface PipelineRow {
  id: string;
  title: ReactNode;
  link?: LinkProps;
  details: ReactNode[];
  steps: { id: string; title: string; at: "done" | "now" | "later" | "failed" }[];
  // How it's going where it is now, as a board's card's dot says it.
  status?: { is: "inactive" | "waiting" | "working" | "success" | "failed"; words: string };
  note?: string; // what its holder last said, like waiting on the packages
  progress?: { step: number; steps: number }; // how far it is in its stage, like 2 of 5
}

// Cards placed anywhere on a surface that pans and zooms, like an idea board: each
// where it says, dragged to move it or by its corner to resize it, and placed once
// let go, snapped to the grid when there's one. A double click opens a card.
export interface CanvasProps {
  status: ViewStatus;
  cards: CanvasCard[];
  grid?: { step: number; x: number; y: number }; // where a card snaps once let go
  error?: string;
  onPlace?: (card: string, x: number, y: number, width: number, height: number) => void; // absent when cards can't be moved
  onOpen?: (card: string) => void;
  // Bands down it, like a board's columns: a card let go in another moves there, and
  // the line between two is dragged to resize one, when onResize says so.
  regions?: CanvasRegion[];
  onResize?: (region: string, width: number) => void;
  // Files pasted on it, or dropped, each to make a card where they landed, in the
  // canvas's own units; absent when nothing's made from them.
  onFiles?: (files: File[], x: number, y: number) => void;
}

export interface CanvasRegion {
  id: string;
  title: string;
  x: number;
  width: number;
}

export interface CanvasCard {
  id: string;
  title: ReactNode;
  kind?: string; // how it's drawn, like note or text
  link?: LinkProps;
  details: ReactNode[];
  tone?: number;
  status?: { is: "inactive" | "waiting" | "working" | "success" | "failed"; words: string };
  picture?: string; // an image it holds, as an address to draw it from
  progress?: { step: number; steps: number }; // how far it is in its stage, like 2 of 5
  x: number;
  y: number;
  width: number;
  height: number;
}

// Large cards, a third of a wide page each and the whole of a narrow one, like a
// project's boards: each with its title, a summary of what's in it, and a few links
// that open it filtered.
// As a mosaic, each card is its picture, filling the tile mosaicTile gives it, what
// else it shows over its foot.
// Under headings by a date, each group is its heading and the cards from its first, a
// mosaic's tiles starting again under each, its heading's id what a timeline scrolls to.
export interface CardsProps {
  status: ViewStatus;
  cards: Card[];
  mosaic?: boolean;
  groups?: CardGroup[];
}
export interface CardGroup {
  id: string;
  title: string;
  first: number;
  count: number;
}

// A list ordered by a date, under a heading for each year, month or day of it as it's
// read here, in the list's own order: each heading's key, like 2026-10, what it says,
// like October 2026, and the rows under it, from the first. Rows that don't say a date
// are under one heading saying so. Worked out in one pass. The same as the desktop's.
export type TimeUnit = "year" | "month" | "day";
export interface TimeGroup {
  key: string;
  title: string;
  first: number;
  count: number;
}
const monthNames = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
// The day a time says as it's read here, as a number like 20261012, or 0 when it says none.
function dayNumber(said: unknown): number {
  const when = said instanceof Date ? said : typeof said === "string" && said ? new Date(said) : undefined;
  if (!when || Number.isNaN(when.getTime())) return 0;
  // A day alone is that day wherever it's read, as a calendar places it.
  const alone = when.getUTCHours() === 0 && when.getUTCMinutes() === 0 && when.getUTCSeconds() === 0 && when.getUTCMilliseconds() === 0;
  const y = alone ? when.getUTCFullYear() : when.getFullYear();
  const m = (alone ? when.getUTCMonth() : when.getMonth()) + 1;
  const d = alone ? when.getUTCDate() : when.getDate();
  return y * 10000 + m * 100 + d;
}
const divisors: Record<TimeUnit, number> = { year: 10000, month: 100, day: 1 };
export function timeGroups(rows: Record<string, unknown>[], field: string, unit: TimeUnit): TimeGroup[] {
  const out: TimeGroup[] = [];
  const pad = (n: number, width = 2) => String(n).padStart(width, "0");
  // What the last heading is of, as a number, so a row under it is only counted.
  let last = -1;
  rows.forEach((row, i) => {
    const day = dayNumber(row[field]);
    const of = day === 0 ? 0 : 1 + Math.floor(day / divisors[unit]);
    if (out.length > 0 && of === last) {
      out[out.length - 1]!.count += 1;
      return;
    }
    last = of;
    let key = "none";
    let title = "No date";
    if (of !== 0) {
      const y = Math.floor(day / 10000);
      const m = Math.floor(day / 100) % 100;
      if (unit === "year") (key = pad(y, 4)), (title = String(y));
      else if (unit === "month") (key = `${pad(y, 4)}-${pad(m)}`), (title = `${monthNames[m - 1]} ${y}`);
      else (key = `${pad(y, 4)}-${pad(m)}-${pad(day % 100)}`), (title = dayTitle(day));
    }
    out.push({ key, title, first: i, count: 1 });
  });
  return out;
}
// What a timeline drawn along them as a ruler marks: each day that holds any, in the
// list's order, as a number like 20261012, the heading it's under, how far into that
// heading's rows it starts, from 0 to 1, and how many it holds; and each year and its
// first day. Rows that don't say a date are no day. Worked out once each time the list
// changes, in one pass. The same as the desktop's.
export interface RulerDay {
  day: number;
  group: number;
  along: number;
  count: number;
}
export interface DatedRuler {
  days: RulerDay[];
  years: { year: number; first: number }[];
}
export function rulerOf(rows: Record<string, unknown>[], field: string, unit: TimeUnit): DatedRuler {
  const out: DatedRuler = { days: [], years: [] };
  // The headings are as timeGroups makes them: the runs of rows of the same year, month
  // or day, or of no date. Each day is how far into its heading's rows it starts, made a
  // share of them once the heading's last row is passed.
  let heading = -1;
  let last = -1;
  let group = 0;
  let first = 0;
  let opened = 0;
  const ended = (end: number) => {
    for (let d = opened; d < out.days.length; d++) out.days[d]!.along /= end - first;
  };
  rows.forEach((row, i) => {
    const day = dayNumber(row[field]);
    const of = day === 0 ? 0 : 1 + Math.floor(day / divisors[unit]);
    if (i === 0 || of !== heading) {
      if (i > 0) ended(i), (group += 1);
      (heading = of), (first = i), (opened = out.days.length);
    }
    if (day === last && day !== 0) {
      out.days[out.days.length - 1]!.count += 1;
      return;
    }
    last = day;
    if (day === 0) return;
    const year = Math.floor(day / 10000);
    if (out.years.length === 0 || out.years[out.years.length - 1]!.year !== year) out.years.push({ year, first: out.days.length });
    out.days.push({ day, group, along: i - first, count: 1 });
  });
  if (rows.length > 0) ended(rows.length);
  return out;
}
// What a day like 20261012 says: October 12, 2026.
export function dayTitle(day: number): string {
  return `${monthNames[(Math.floor(day / 100) % 100) - 1]} ${day % 100}, ${Math.floor(day / 10000)}`;
}
// How long a day's mark is on a ruler `wide` across: a few pixels for one, and the same
// few longer each time it holds twice as many, so most days read apart and one of
// hundreds is at most the ruler's width.
export function markLength(count: number, wide: number): number {
  return Math.min(wide, 3 + 5 * Math.log2(1 + count));
}
// The marks of a ruler `tall` high, each day at its place down it, from 0 to 1: days
// closer than two pixels are one mark of them all.
export function marksAlong(days: { at: number; count: number }[], tall: number): { y: number; count: number }[] {
  const out: { y: number; count: number }[] = [];
  for (const d of days) {
    const y = Math.round(d.at * tall);
    const last = out[out.length - 1];
    if (last && y - last.y < 2) last.count += d.count;
    else out.push({ y, count: d.count });
  }
  return out;
}

// A timeline of cards under headings by a date, drawn along them as a scrollbar is: as
// tall as what scrolls them, its top their top. Each day that has any, where it is down
// it, from 0 to 1, and how many it holds, each a mark from its right edge as long as
// markLength says, merged by marksAlong; each year, a line across with its number under
// it; and where they're scrolled to, a line across in the accent. Pressed or dragged,
// onScrub scrolls them to a place down it as it moves, and dayAt says the day there,
// shown beside it while it's pressed or pointed at. With the keyboard, onStep goes to
// the heading or year after or before, or to either end.
export type TimelineStep = "next" | "previous" | "next year" | "previous year" | "first" | "last";
export interface DatedTimelineProps {
  days: { at: number; count: number }[];
  years: { title: string; at: number }[];
  at: number;
  dayAt: (at: number) => string;
  onScrub: (at: number) => void;
  onStep: (to: TimelineStep) => void;
}
// The keys that move a timeline: the arrows, j and k, to the heading after or before;
// Page Down and Page Up to the year; Home and End to either end.
const timelineSteps: Record<string, TimelineStep> = { ArrowDown: "next", j: "next", ArrowUp: "previous", k: "previous", PageDown: "next year", PageUp: "previous year", Home: "first", End: "last" };
export function timelineStep(key: string): TimelineStep | undefined {
  return timelineSteps[key];
}

// Where the nth card of a mosaic goes, in its columns and rows, counted from 0: every 8
// cards are two blocks, one above the other, each three equal columns by two rows as
// tall as a column is wide. In the first, its first card is a column wide and both
// rows tall at the left, its second two columns wide on the top row at the right, and
// its third and fourth a tile each under that; the second block is the first
// mirrored, its first card on the right. Worked out from n alone, so nothing moves as
// pictures come in. The same as the desktop's.
export interface MosaicPlace {
  column: number;
  row: number;
  columns: number;
  rows: number;
}
const firstBlock: MosaicPlace[] = [
  { column: 0, row: 0, columns: 1, rows: 2 },
  { column: 1, row: 0, columns: 2, rows: 1 },
  { column: 1, row: 1, columns: 1, rows: 1 },
  { column: 2, row: 1, columns: 1, rows: 1 },
];
const secondBlock: MosaicPlace[] = [
  { column: 2, row: 0, columns: 1, rows: 2 },
  { column: 0, row: 0, columns: 2, rows: 1 },
  { column: 0, row: 1, columns: 1, rows: 1 },
  { column: 1, row: 1, columns: 1, rows: 1 },
];
export function mosaicTile(n: number): MosaicPlace {
  const tile = n % 8;
  const at = tile < 4 ? firstBlock[tile]! : secondBlock[tile - 4]!;
  return { ...at, row: at.row + Math.floor(n / 8) * 4 + (tile < 4 ? 0 : 2) };
}
// How many rows a mosaic of `count` cards takes: the last block ends with them.
export function mosaicRows(count: number): number {
  return Math.ceil(count / 4) * 2;
}

export interface Card {
  id: string;
  title: ReactNode;
  link?: LinkProps;
  details: ReactNode[];
  tags?: string[]; // its choices shown beside its title, like Private
  // How what it holds splits, like its issues by stage, and what each part means:
  // not started, under way, done or failed, so a bar shows how far along it is.
  tally?: { label: string; count: number; state?: "none" | "partial" | "done" | "failed" }[];
  noun?: string; // what it holds, counted, like issues
  filters: { label: string; link: LinkProps; hue?: Hue }[];
  // A picture on top of it, all of it fitted in or filling its place; its address,
  // none while it's found or when there's none.
  picture?: { source?: string; fit: boolean };
}

// A run of numbers over time, newest at the right, as btop draws one: its title, its
// latest value, and the top of its scale. A tile's chart is small; a page's large.
export interface ChartProps {
  title: string;
  values: number[]; // newest last
  top: number; // the top of its scale
  latest: string; // its newest value, as it reads, like 42% or 1.5 GB
  topSaid: string; // the top of its scale, as it reads
  read: (value: number) => string; // any value as it reads, for what's pointed at
  size: "small" | "large";
}

// Records whose status changes from one thing to another, like agents or machines,
// each a tile: its name, its status as a dot and a word and for how long, what it's
// doing and how far, and small charts of what it reports.
export interface MonitorProps {
  status: ViewStatus;
  tiles: MonitorTile[];
}

export interface MonitorTile {
  id: string;
  title: ReactNode;
  link?: LinkProps;
  state: string; // its status as it reads, like Working or Offline
  tone: "inactive" | "waiting" | "working" | "success" | "failed";
  since?: string; // how long it's been so, like 4m
  doing?: string;
  progress?: { step: number; steps: number };
  charts: ChartProps[];
}

// A list's records as large cards in a row, in their order, joined by arrows, like a
// project's milestones as a timeline: picking one shows only the rows in it, and
// picking it again shows them all.
export interface TrackProps {
  label: string; // what the cards are, like Milestone
  steps: TrackStep[];
}

export interface TrackStep {
  id: string;
  title: ReactNode;
  details: ReactNode[]; // what else it shows, like its due date
  count: number; // how many rows are in it
  noun: string; // what's counted, like issues
  selected: boolean;
  onSelect: () => void;
  // Succeeded, as a green dot, once every row in it is at a success end; failed, as a
  // red one, once any row in it has failed.
  status?: { is: "success" | "failed"; words: string };
}

export interface BoardColumn {
  id: string;
  title: string;
  cards: BoardCard[];
  notify?: boolean; // it chimes when a card arrives, and shows a bell
}

export interface BoardCard {
  id: string;
  title: ReactNode;
  link?: LinkProps; // the page it opens
  details: ReactNode[]; // what else it shows, like its number and labels
  people?: { name: string; picture?: string }[]; // who's working it, shown apart from its details
  reaches: string[]; // the columns it may be moved to
  tone?: number; // how urgent it is, 1 the most, up to 5, as a table's row
  // How it's going, as a dot, with its words for those who can't see it: inactive,
  // waiting, working, done or failed.
  status?: { is: "inactive" | "waiting" | "working" | "success" | "failed"; words: string };
  progress?: { step: number; steps: number }; // how far it is in its stage, as its holder says, like 2 of 5
  // Picked with a click, when the screen picks a card of the board, as a table's row is.
  picked?: boolean;
  onPick?: () => void;
}

// Where a page is among the pages above it, like Projects › tracker › Product:
// each of those linked, and the page itself last.
export interface CrumbsProps {
  items: { label: string; link: LinkProps }[];
  current: string;
}

// Things as boxes in a row, in order, and what goes between them as arrows, like a
// workflow's stages and the moves between them: forward above, back below. An
// arrow is pressed to change it, and one is drawn from a box to another to add it.
export interface DiagramProps {
  status: ViewStatus;
  nodes: { id: string; label: string }[];
  edges: DiagramEdge[];
  onConnect?: (from: string, to: string) => void;
  error?: string;
}

export interface DiagramEdge {
  from: string;
  to: string;
  label: string; // what it shows, like the roles that may take a move
  title: string; // what it is, said in full, like Triage to Ready: Product owner
  onClick?: () => void;
}

// One of a few ways to show the same thing, like a table or a board, the one
// shown marked.
export interface SwitchProps {
  label: string; // what's switched, like "Show issues as"
  options: { label: string; icon?: string; selected: boolean; onSelect: () => void }[]; // icon: table or board, drawn instead of the label
}

// What goes between two of the same things, like the moves between a project's
// stages: a row and a column for each, and a cell for each pair, the row's first.
export interface GridProps {
  status: ViewStatus;
  corner: string; // what the rows and columns are, like "From, to"
  columns: string[];
  rows: GridRow[];
  error?: string;
}

export interface GridRow {
  label: string;
  cells: GridCell[];
}

export interface GridCell {
  text: string; // what's there, like the roles that may take a move; empty for nothing
  label: string; // what pressing it does, like Add Triage to Ready
  onClick?: () => void; // absent for someone who can't change it
  self?: boolean; // a thing and itself, where nothing goes
}

// A list's toolbar on a line of its own, above it: what it's filtered by, its search
// box and its buttons, where a list shown as a table or a board, as one picks, keeps
// them in one place whichever is shown.
export interface ToolbarProps {
  search?: TableSearch;
  tools?: ReactNode;
  filtered?: Filtered;
}

export interface TableSearch {
  value: string;
  label: string; // what it searches, like "Search title and labels"
  onChange: (value: string) => void;
}

export interface TablePages {
  page: number; // from 1
  count: number;
  onPage: (page: number) => void;
}

// One of a table's tabs: a choice, how many rows have it, and whether it's the one
// shown.
export interface TableTab {
  label: string;
  count: number;
  selected: boolean;
  onSelect: () => void;
}

// A thing's values, each beside what it is, like an issue's status and who
// implemented it; the ones with nothing in them aren't given.
// A box that finds rows of several lists as it's typed in, like a project's issues
// and wiki pages: each list's results under its label, each a link, the first few.
export interface FindResult {
  id: string;
  text: string; // what it's called, like #12 Copy an issue whole
  link?: LinkProps;
}
export interface FindGroup {
  label: string; // like Issues
  results: FindResult[];
  more: number; // how many more were found than are shown
}
export interface FindProps {
  label: string; // what it finds, like Search this project
  query: string;
  onChange: (query: string) => void;
  groups: FindGroup[]; // only the lists with something found
}

export interface DetailsProps {
  items: { label: string; value: ReactNode }[];
  tone?: number; // how urgent what it's about is, 1 the most, up to 5, as a table's row
}

// A few words, each on its own, like an issue's labels.
export interface LabelsProps {
  items: string[];
}

// One of what a front page offers: a section of words, with the short line over its
// title, its icon, and what it holds in turn, like a group of services, each with its
// own.
export interface LandingFeature {
  title: string;
  overline?: ReactNode;
  icon?: string; // one of the icons, like database
  text: ReactNode;
  features: LandingFeature[];
}

// A front page in one of uione's designs: waveform, studio, aurora, grid, relay,
// constellation or folio. Its hero's title, the short line over it, its words and its links;
// its sections of words, as what it offers; its statement, the one sentence that
// stands for the site; its sections as steps, done one after another; and the rest,
// like a section with a form, as what it asks of its reader.
export interface LandingProps {
  design: string;
  name: string; // the site's name
  icon?: string; // the address of its icon
  overline?: ReactNode;
  title: string;
  text: ReactNode[];
  links: (LinkProps & { label: ReactNode })[];
  features: LandingFeature[];
  statement?: ReactNode;
  steps: { title: string; id?: string; overline?: ReactNode; text: ReactNode; steps: { title: string; text: ReactNode }[] }[];
  asks: { title: string; id?: string; overline?: ReactNode; children: ReactNode }[];
  before?: ReactNode; // what the screen has that isn't any of these, like a table: first, or where the design puts it
  // Each part in the order the screen writes it, for a design that keeps it: a
  // feature, a step or an ask is the next of its list.
  order?: ("hero" | "feature" | "statement" | "steps" | "ask")[];
}

// Files, each a link that opens it, and a picture of it when it's an image.
export interface FilesProps {
  items: { name: string; preview?: string; onOpen: () => void }[];
}

export interface FieldProps {
  name: string;
  label: string;
  type: string; // text, markdown, email, date, number, slider (a number in range), boolean (true or false), list (written separated by commas), choice, pick (one of a list's records, however many), file, or files
  choices?: [string, string][]; // for a choice: each one, and how it's shown
  required?: boolean; // a choice that's always one of them, offering no empty one
  range?: [number, number, number]; // for a slider: from, to, and the step between
  value: string;
  hint?: string;
  onChange: (value: string) => void;
  // For a file or files: what's there, each by its name, with how far it's come
  // while it uploads, from 0 to 1, or why it didn't; what to do with files chosen;
  // and taking one away.
  files?: { name: string; progress?: number; error?: string }[];
  onFiles?: (files: File[]) => void;
  onRemove?: (index: number) => void;
}

export interface FormProps {
  fields: FieldProps[];
  submit: string;
  busy: boolean;
  error: string | undefined;
  onSubmit: () => void;
}

export interface ButtonProps {
  kind?: "primary" | "secondary" | "danger";
  disabled?: boolean;
  error?: string; // why the last press failed
  onClick: () => void;
  children: ReactNode;
  icon?: string; // drawn instead of its words, which still name it, like edit
  // In a workspace's toolbar: large, its icon over its words, or small, its icon
  // alone, its words said when it's pointed at; either way, a flat tool, not a button.
  tool?: "large" | "small" | "tiny"; // tiny: smaller still, as a list's row's buttons are
  pressed?: boolean; // a toggle, or a choice already made, drawn as pressed
  hint?: string; // said when it's pointed at, like "Undo (Ctrl+Z)"
  swatch?: string; // in a toolbar, drawn as a circle of this color, like #ffffff, its words said when it's pointed at
}

// A named part of a workspace's toolbar, like Rotate, with a line between it and the
// next, its name under its tools; small, its tools are icons, two rows of them; at
// its end, at the toolbar's far end.
export interface GroupProps {
  name: string;
  small?: boolean;
  across?: number; // a small one's tools in rows this long, rather than two rows, the first the longer
  atEnd?: boolean;
  unsaid?: boolean; // its name not written under it, as its one tool says it already
  children: ReactNode;
}

// A tool that opens its choices over everything, as a menu does: drawn as a tool, and
// while `open` its choices under it, closed by Esc or a press elsewhere.
export interface OptionsProps {
  label: string;
  icon?: string;
  tool?: "large" | "small";
  open: boolean;
  onToggle: () => void;
  onClose: () => void;
  children: ReactNode;
}

// One of a tool's choices: what it gives drawn small, a page of its shape like "page
// 1920 1080", a color, or its icon; its words, and what it's for under them; tinted
// while it's what holds now.
export interface ChoiceProps {
  label: string;
  about?: string;
  swatch?: string;
  icon?: string;
  pressed?: boolean;
  disabled?: boolean;
  onClick: () => void;
}

// A button's value along a slider, like opacity from 0 to 1: its words, the slider from
// where it is, what it says beside it, a percent of the way from 0 to 1, and `onSettle`
// told each time it's let go.
export interface SliderProps {
  label: string;
  value: number;
  from: number;
  to: number;
  by: number;
  percent?: boolean;
  disabled?: boolean;
  onSettle: (value: number) => void;
}

// A button's value any color: its words, a color picker from the color it is, written
// #rrggbb, and `onSettle` told each one chosen, as a press is let go.
export interface ColorPickerProps {
  label: string;
  value: string;
  disabled?: boolean;
  onSettle: (hex: string) => void;
}

// What was just done, like "Undid place", or why a command was turned down, said
// until the next.
export interface MessagesProps {
  text: string;
  problem?: boolean;
}

export interface DialogProps {
  open: boolean;
  title: string;
  onClose: () => void;
  children: ReactNode;
}

// A person's picture, small and round, beside their name. It's decorative: the
// name says who it is. A service, a program given a role like an agent, has a robot
// in its circle instead.
export interface PictureProps {
  source: string; // an https address, or nothing for a service
  service?: boolean;
}

export interface MarkdownProps {
  status: ViewStatus;
  // Untrusted: Markdown someone wrote, shown rendered once it's live. A component
  // set must render it without any raw HTML in it, and keep only links to safe
  // protocols (https, http, mailto), never javascript: or data:.
  source: string | undefined;
  plain?: boolean; // as running words, like a page's subtitle, rather than a document in a frame
  framed?: boolean; // in a frame of its own, as it is unless something around it frames it already, like a comment's box
}

export interface LiveProps {
  status: ViewStatus;
  value: string;
}

// Counting visitors, like Firebase Analytics: told each screen the app shows, and
// whether the visitor agreed to be counted with cookies. Until they do, it counts
// without them, as Google's consent mode does.
export interface Analytics {
  consent(agreed: boolean): void;
  page(path: string, title: string): void;
}

// Asking a visitor whether they may be counted, once; their answer is kept.
export interface ConsentProps {
  onAnswer: (agreed: boolean) => void;
}

// Themes made at random from a palette: the scheme picked, the theme tried on the
// whole app, if one is, each of its colors, and the theme as a project says it.
export interface ThemesProps {
  scheme: string;
  schemes: [string, string][]; // each scheme, and how it's called
  onScheme: (scheme: string) => void;
  seed: number | undefined; // the theme tried, or none while the app's own is drawn
  colors: [string, string, string][]; // each role, its light color and its dark one
  written: string; // define theme …, to paste into a .one file
  copied: boolean;
  onNew: () => void;
  onReset: () => void;
  onCopy: () => void;
}

// A file's picture, as wide as its place and 320 tall, all of it fitted in or filling
// it; its address, once it's known.
export interface FilePictureProps {
  source?: string;
  fit: boolean;
}

export interface ComponentSet {
  Page: ComponentType<PageProps>;
  Account: ComponentType<AccountProps>;
  SignIn: ComponentType<SignInProps>;
  Hero: ComponentType<HeroProps>;
  Section: ComponentType<SectionProps>;
  Text: ComponentType<TextProps>;
  Link: ComponentType<LinkViewProps>;
  Code: ComponentType<CodeProps>;
  Pages: ComponentType<PagesProps>;
  Menu: ComponentType<MenuProps>;
  Table: ComponentType<TableProps>;
  Grid: ComponentType<GridProps>;
  Board: ComponentType<BoardProps>;
  Calendar: ComponentType<CalendarProps>;
  Notices: ComponentType<NoticesProps>;
  Canvas: ComponentType<CanvasProps>;
  Pipeline: ComponentType<PipelineProps>;
  Toolbar: ComponentType<ToolbarProps>;
  Switch: ComponentType<SwitchProps>;
  Crumbs: ComponentType<CrumbsProps>;
  Diagram: ComponentType<DiagramProps>;
  Cards: ComponentType<CardsProps>;
  FilePicture: ComponentType<FilePictureProps>;
  Chart: ComponentType<ChartProps>;
  Charts: ComponentType<{ charts: ChartProps[] }>; // several, side by side
  Monitor: ComponentType<MonitorProps>;
  Track: ComponentType<TrackProps>;
  Labels: ComponentType<LabelsProps>;
  Files: ComponentType<FilesProps>;
  Landing: ComponentType<LandingProps>;
  Details: ComponentType<DetailsProps>;
  Thread: ComponentType<ThreadProps>;
  Timeline: ComponentType<TimelineProps>;
  DatedTimeline: ComponentType<DatedTimelineProps>;
  Find: ComponentType<FindProps>;
  Form: ComponentType<FormProps>;
  Button: ComponentType<ButtonProps>;
  Actions: ComponentType<ActionsProps>;
  Group: ComponentType<GroupProps>;
  Options: ComponentType<OptionsProps>;
  Choice: ComponentType<ChoiceProps>;
  Slider: ComponentType<SliderProps>;
  ColorPicker: ComponentType<ColorPickerProps>;
  Messages: ComponentType<MessagesProps>;
  Layout: ComponentType<LayoutProps>;
  Dialog: ComponentType<DialogProps>;
  Live: ComponentType<LiveProps>;
  Themes: ComponentType<ThemesProps>;
  Markdown: ComponentType<MarkdownProps>;
  Picture: ComponentType<PictureProps>;
  Consent: ComponentType<ConsentProps>;
}
