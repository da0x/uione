// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// @uione/react: the runtime every uione screen is built on.

export { App, Built, BuiltLink, Crumbs, Heading, Subtitle, accentOf, screen, usePageTitle, useSignIn, useSite, useTitle } from "./app.js";
export type { AppProps, Screen, SiteFooter, ScreenInfo, TitlePart, Badge } from "./app.js";

export { Actions, Board, Cards, Code, Command, Copy, Details, Files, Find, Landing, useAddressState, toneOf, Diagram, Grid, Layout, Region, Confirm, Form, Hero, Link, Live, Markdown, Menu, Pages, Section, Steps, Switched, Table, Text, Thread, Timeline, changed, done, holds, keptByTime, allows, listChoices, listHas, markdownOf, ordered, phrase } from "./components.js";
export type { BoardMove, CopiedField, Tally, CopiedList, DocPage, FieldSpec, GridCommand, RowAction } from "./components.js";

export { fileName, useAuth, useCommand, useFiles, useRunner, useView } from "./data.js";
export type {
  AuthSource,
  AuthState,
  AuthenticationMethod,
  CommandInput,
  CommandState,
  DataSource,
  FileStore,
  Person,
  Runner,
  ViewData,
  ViewState,
  ViewStatus,
} from "./data.js";

export { memorySource } from "./memory.js";
export type { MemoryOptions, MemorySource } from "./memory.js";

export { label, shortAddress, show, useLinks, useParam, useUI } from "./ui.js";
export { keyOf, partsOf } from "./keys.js";

export type * from "./contract.js";
export { buildLinks } from "./contract.js";
