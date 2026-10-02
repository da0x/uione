// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An editor for .one files. The CodeMirror extensions work on their own; the React
// components put them together with the code each line becomes.

export { fromLine } from "./generated.js";
export { highlighting } from "./highlight.js";
export { placed, problems } from "./problems.js";
export type { ProblemsOptions } from "./problems.js";
export { setTabWidth, tabs } from "./tabs.js";
export type { TabWidth } from "./tabs.js";
export { Editor, Generated, Workbench } from "./react.js";
export type { EditorProps, GeneratedProps, WorkbenchProps } from "./react.js";
