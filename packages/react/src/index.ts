// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// @uione/react: the runtime every uione screen is built on.

export { App, accentOf, screen, usePageTitle, useSignIn, useTitle } from "./app.js";
export type { AppProps, Screen, ScreenInfo, TitlePart } from "./app.js";

export { Actions, Board, Code, Command, Copy, Details, Grid, Layout, Region, Confirm, Form, Hero, Link, Live, Markdown, Menu, Pages, Section, Steps, Switched, Table, Text, Thread, Timeline, changed, done, holds, allows, listChoices, listHas, markdownOf, phrase } from "./components.js";
export type { BoardMove, CopiedField, CopiedList, DocPage, FieldSpec, GridCommand, RowAction } from "./components.js";

export { useAuth, useCommand, useRunner, useView } from "./data.js";
export type {
  AuthSource,
  AuthState,
  AuthenticationMethod,
  CommandInput,
  CommandState,
  DataSource,
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
