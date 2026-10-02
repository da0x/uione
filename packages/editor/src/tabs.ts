// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A .one file is indented with tabs, one per level, and how wide a tab looks is the
// reader's choice. Tab inserts a tab, and indenting a block adds one.

import { indentWithTab } from "@codemirror/commands";
import { indentUnit } from "@codemirror/language";
import { Compartment, EditorState } from "@codemirror/state";
import type { Extension } from "@codemirror/state";
import { EditorView, keymap } from "@codemirror/view";

export type TabWidth = 2 | 4 | 6 | 8;

const width = new Compartment();

export function tabs(initial: TabWidth = 4): Extension {
  return [indentUnit.of("\t"), keymap.of([indentWithTab]), width.of(EditorState.tabSize.of(initial))];
}

// Shows tabs at another width; the file is unchanged.
export function setTabWidth(view: EditorView, to: TabWidth) {
  view.dispatch({ effects: width.reconfigure(EditorState.tabSize.of(to)) });
}
