// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// @uione/react: the runtime every uione screen is built on.

export { App, Built, BuiltLink, Crumbs, Heading, Subtitle, accentOf, screen, usePageTitle, useSignIn, useSite, useTitle } from "./app.js";
export type { AppProps, Screen, SiteFooter, ScreenInfo, TitlePart, Badge } from "./app.js";

export { Actions, Board, Canvas, Cards, Chart, Monitor, Pipeline, Code, Command, Copy, Details, Files, Find, Landing, useAddressState, toneOf, Diagram, Grid, Layout, Region, Confirm, Form, Hero, Link, Live, Markdown, Menu, Pages, Section, Steps, Switched, Table, Text, Themes, Thread, Timeline, useTriedTheme, changed, done, holds, keptByTime, allows, isNone, listChoices, listHas, markdownOf, ordered, phrase } from "./components.js";
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

export { Surface } from "./surface.js";
export { memorySource } from "./memory.js";
export type { MemoryOptions, MemorySource } from "./memory.js";

export { label, shortAddress, show, useLinks, useParam, useUI } from "./ui.js";
export { keyOf, partsOf } from "./keys.js";
export { readValue, scaleTop, seriesOf, toneOfState, valueAt } from "./series.js";
export type { ChartUnit } from "./series.js";

export type * from "./contract.js";
export { buildLinks } from "./contract.js";
export { generate as paletteTheme, named as paletteNamed, nameOf as paletteName, schemeTitle as paletteSchemeTitle, schemes as paletteSchemes, written as writtenTheme } from "./palette.js";
export type { PaletteTheme, Scheme as PaletteScheme } from "./palette.js";
