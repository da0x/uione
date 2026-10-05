// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// @uione/react: the runtime every uione screen is built on.

export { App, screen, useSignIn } from "./app.js";
export type { AppProps, Screen, ScreenInfo } from "./app.js";

export { Code, Command, Confirm, Form, Hero, Link, Live, Markdown, Menu, Pages, Section, Table, Text } from "./components.js";
export type { DocPage, FieldSpec } from "./components.js";

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
