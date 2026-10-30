// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// @uione/react: the runtime every uione screen is built on.

export { App, Built, BuiltLink, Crumbs, Heading, Subtitle, accentOf, screen, useFill, usePageTitle, useSignIn, useSite, useTitle } from "./app.js";
export { Applies, Choose, Either, FullScreen, Group, History, Messages, Options, Toggle, Tools, setChosen, useChosen, hintOf, keyShown, setPicked, setToggled, useInGroup, useKeys, useOff, usePicked, usePickedId, useTool, useToggled } from "./workspace.js";
export { useSaying } from "./said.js";
export { Equation } from "./equation.js";
export { layOutMath, mathFont, parseMath, writtenMath } from "./math.js";
export type { MathLaidOut, MathNode, MathPlaced, MathProblem } from "./math.js";
export { Collage, CollageButton, Layers } from "./collage-view.js";
export { Paper, PaperButton } from "./paper-view.js";
export { OnOpen, screenSize } from "./opening.js";
export type { OpeningLine } from "./opening.js";
export type { Said, Saying } from "./said.js";
export type { AppProps, Screen, SiteFooter, ScreenInfo, TitlePart, Badge } from "./app.js";
export type { NoticeSpec } from "./notices.js";

export { Actions, Board, Calendar, Canvas, Cards, DatedTimeline, Picture, Chart, Monitor, Pipeline, Code, Command, Copy, Details, Files, Find, Landing, useAddressState, toneOf, Diagram, Grid, Layout, Region, Confirm, Form, Hero, Link, Live, Markdown, Menu, Pages, Overline, Section, Statement, StepLink, Steps, Switched, Table, Text, Themes, Thread, Timeline, useTriedTheme, changed, csvOf, done, holds, keptByTime, timeNamed, sameValue, allows, isNone, listChoices, listHas, markdownOf, ordered, phrase, divided, remainder } from "./components.js";
export type { BoardMove, CopiedField, Tally, CopiedList, DocPage, FieldSpec, GridCommand, RowAction } from "./components.js";

export { fileName, useAuth, useCommand, useFiles, useRunner, usePeople, usePerson, useSearch, useView } from "./data.js";
export type {
  AuthSource,
  AuthState,
  AuthenticationMethod,
  CommandInput,
  CommandState,
  DataSource,
  FileStore,
  PeoplePage,
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
export { buildLinks, dayTitle, markLength, marksAlong, mosaicRows, mosaicTile, rulerOf, timeGroups, timelineStep } from "./contract.js";
export { generate as paletteTheme, named as paletteNamed, nameOf as paletteName, schemeTitle as paletteSchemeTitle, schemes as paletteSchemes, written as writtenTheme } from "./palette.js";
export type { PaletteTheme, Scheme as PaletteScheme } from "./palette.js";
