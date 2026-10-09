// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

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
  account?: ReactNode; // who's signed in, drawn with Account, when the app has sign-in
  heading?: ReactNode; // where a screen's own buttons go on the title's row, at its end
  crumbs?: ReactNode; // where the pages above a screen go, above its title
  subtitle?: ReactNode; // where a screen's words about itself go, under its title
  unread?: number; // how many things are new to the person reading, said beside the app's name
  footer?: FooterProps; // at the foot of the page, as the site's footer says
  children: ReactNode;
}

// What a site was built from: the uione release, and the commit and repository the
// deploy found, which its footer can show and link.
export interface Build {
  version: string; // like 0.7.0
  commit?: string; // like d9d95fd0...
  repository?: string; // where the commit can be read, like https://github.com/da0x/neotrac
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
}

export interface RowAction {
  label: string;
  onClick: () => void;
  disabled: boolean;
}

export interface TableRow {
  id: string;
  link?: LinkProps; // the page this row opens, when the table has a link
  cells: ReactNode[];
  actions: RowAction[];
  tone?: number; // how urgent it is, as Trac colored a ticket's priority: 1 the most, up to 5
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

// Cards in columns, like a project's issues in its phases, a column for each in
// order. A card is moved to a column it may go to, by dragging it or with Alt+← and
// Alt+→; while one is dragged, the columns it can't go to are dimmed.
export interface BoardProps {
  status: ViewStatus;
  columns: BoardColumn[];
  error?: string;
  onMove?: (card: string, column: string) => void; // absent when no card can be moved
  search?: TableSearch; // a box that finds cards by what's typed
  tools?: ReactNode; // buttons beside its search, like New issue
  filtered?: Filtered;
  // On a phone, the column shown, one a page, by its id, and how to show another, so
  // the address can say which.
  column?: string;
  onColumn?: (column: string) => void;
}

// Large cards, a third of a wide page each and the whole of a narrow one, like a
// project's boards: each with its title, a summary of what's in it, and a few links
// that open it filtered.
export interface CardsProps {
  status: ViewStatus;
  cards: Card[];
}

export interface Card {
  id: string;
  title: ReactNode;
  link?: LinkProps;
  details: ReactNode[];
  tally?: { label: string; count: number }[]; // how what it holds splits, like its issues by phase
  noun?: string; // what it holds, counted, like issues
  filters: { label: string; link: LinkProps; hue?: Hue }[];
}

export interface BoardColumn {
  id: string;
  title: string;
  cards: BoardCard[];
}

export interface BoardCard {
  id: string;
  title: ReactNode;
  link?: LinkProps; // the page it opens
  details: ReactNode[]; // what else it shows, like its priority and labels
  reaches: string[]; // the columns it may be moved to
  tone?: number; // how urgent it is, 1 the most, up to 5, as a table's row
}

// Where a page is among the pages above it, like Projects › neotrac › Product:
// each of those linked, and the page itself last.
export interface CrumbsProps {
  items: { label: string; link: LinkProps }[];
  current: string;
}

// Things as boxes in a row, in order, and what goes between them as arrows, like a
// workflow's phases and the moves between them: forward above, back below. An
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
// phases: a row and a column for each, and a cell for each pair, the row's first.
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

export interface FieldProps {
  name: string;
  label: string;
  type: string; // text, markdown, email, date, number, boolean (true or false), list (written separated by commas), choice, or pick (one of a list's records, however many)
  choices?: [string, string][]; // for a choice: each one, and how it's shown
  required?: boolean; // a choice that's always one of them, offering no empty one
  value: string;
  hint?: string;
  onChange: (value: string) => void;
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
}

export interface DialogProps {
  open: boolean;
  title: string;
  onClose: () => void;
  children: ReactNode;
}

// A person's picture, small and round, beside their name. It's decorative: the
// name says who it is.
export interface PictureProps {
  source: string; // an https address
}

export interface MarkdownProps {
  status: ViewStatus;
  // Untrusted: Markdown someone wrote, shown rendered once it's live. A component
  // set must render it without any raw HTML in it, and keep only links to safe
  // protocols (https, http, mailto), never javascript: or data:.
  source: string | undefined;
  plain?: boolean; // as running words, like a page's subtitle, rather than a document in a frame
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
  Switch: ComponentType<SwitchProps>;
  Crumbs: ComponentType<CrumbsProps>;
  Diagram: ComponentType<DiagramProps>;
  Cards: ComponentType<CardsProps>;
  Labels: ComponentType<LabelsProps>;
  Details: ComponentType<DetailsProps>;
  Thread: ComponentType<ThreadProps>;
  Timeline: ComponentType<TimelineProps>;
  Find: ComponentType<FindProps>;
  Form: ComponentType<FormProps>;
  Button: ComponentType<ButtonProps>;
  Actions: ComponentType<ActionsProps>;
  Layout: ComponentType<LayoutProps>;
  Dialog: ComponentType<DialogProps>;
  Live: ComponentType<LiveProps>;
  Markdown: ComponentType<MarkdownProps>;
  Picture: ComponentType<PictureProps>;
  Consent: ComponentType<ConsentProps>;
}
