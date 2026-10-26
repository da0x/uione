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
export interface CardsProps {
  status: ViewStatus;
  cards: Card[];
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
  latest: string; // its newest value, as it reads, like 42% or 1.5 GiB
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

// A front page in one of uione's designs: waveform, studio, aurora, grid, relay or
// constellation. Its hero's title, the short line over it, its words and its links;
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
  tool?: "large" | "small";
  pressed?: boolean; // a toggle, or a choice already made, drawn as pressed
  hint?: string; // said when it's pointed at, like "Undo (Ctrl+Z)"
}

// A named part of a workspace's toolbar, like Rotate, with a line between it and the
// next, its name under its tools; small, its tools are icons, two rows of them; at
// its end, at the toolbar's far end.
export interface GroupProps {
  name: string;
  small?: boolean;
  atEnd?: boolean;
  children: ReactNode;
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
  Find: ComponentType<FindProps>;
  Form: ComponentType<FormProps>;
  Button: ComponentType<ButtonProps>;
  Actions: ComponentType<ActionsProps>;
  Group: ComponentType<GroupProps>;
  Messages: ComponentType<MessagesProps>;
  Layout: ComponentType<LayoutProps>;
  Dialog: ComponentType<DialogProps>;
  Live: ComponentType<LiveProps>;
  Themes: ComponentType<ThemesProps>;
  Markdown: ComponentType<MarkdownProps>;
  Picture: ComponentType<PictureProps>;
  Consent: ComponentType<ConsentProps>;
}
