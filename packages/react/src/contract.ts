// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// The contract between @uione/react and a component set. @uione/react decides what's
// on a screen and how it behaves. A component set decides only how it looks, so
// every prop here is already worked out: text to show, values to fill in, and
// functions to call. A component set never fetches, routes or runs commands itself.

import type { ComponentType, MouseEvent, ReactNode } from "react";
import type { ViewStatus } from "./data.js";

export interface LinkProps {
  href: string;
  onClick?: (event: MouseEvent<HTMLAnchorElement>) => void;
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
  children: ReactNode;
}

export interface AccountProps {
  name: string | undefined; // who's signed in, or undefined for no one
  ready: boolean; // false until it's known whether anyone is signed in
  error?: string; // why signing in or out just failed
  onSignIn: () => void;
  onSignOut: () => void;
}

export interface HeroProps {
  title: string;
  children: ReactNode;
}

export interface SectionProps {
  title: string;
  id?: string;
  children: ReactNode;
}

export interface TextProps {
  children: ReactNode;
}

export interface LinkViewProps extends LinkProps {
  children: ReactNode;
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
}

export interface TableProps {
  status: ViewStatus;
  columns: string[];
  rows: TableRow[];
  error?: string; // why the last action on a row failed
}

export interface FieldProps {
  name: string;
  label: string;
  type: string; // text, markdown, email, date, number, list (written separated by commas), or choice
  choices?: [string, string][]; // for a choice: each one, and how it's shown
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
}

export interface LiveProps {
  status: ViewStatus;
  value: string;
}

export interface ComponentSet {
  Page: ComponentType<PageProps>;
  Account: ComponentType<AccountProps>;
  Hero: ComponentType<HeroProps>;
  Section: ComponentType<SectionProps>;
  Text: ComponentType<TextProps>;
  Link: ComponentType<LinkViewProps>;
  Code: ComponentType<CodeProps>;
  Pages: ComponentType<PagesProps>;
  Menu: ComponentType<MenuProps>;
  Table: ComponentType<TableProps>;
  Form: ComponentType<FormProps>;
  Button: ComponentType<ButtonProps>;
  Dialog: ComponentType<DialogProps>;
  Live: ComponentType<LiveProps>;
  Markdown: ComponentType<MarkdownProps>;
  Picture: ComponentType<PictureProps>;
}
