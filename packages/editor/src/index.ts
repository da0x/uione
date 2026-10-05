// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An editor for .one files. The CodeMirror extensions work on their own; the React
// components put them together with the code each line becomes.

export { definitions, reference } from "./definitions.js";
export type { DefinitionsOptions } from "./definitions.js";
export { fromLine } from "./generated.js";
export { highlighting, loadTheme, loadThemes, previewOf } from "./highlight.js";
export { darkThemes, defaultThemes, lightThemes, themesOf } from "./themes.js";
export type { CodeTheme, Themes } from "./themes.js";
export { placed, problems } from "./problems.js";
export type { ProblemsOptions } from "./problems.js";
export { place, readSpot, spotOf, writeSpot } from "./spot.js";
export type { Point, Spot } from "./spot.js";
export { setTabWidth, tabs } from "./tabs.js";
export type { TabWidth } from "./tabs.js";
export { Diff, Editor, Generated, Workbench } from "./react.js";
export { LookControls, Toolbar, fonts, useLook, usePageDark } from "./toolbar.js";
export type { Look } from "./toolbar.js";
export type { DiffProps, EditorProps, GeneratedProps, WorkbenchProps } from "./react.js";
