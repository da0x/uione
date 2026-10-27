// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The kinds a paper knows, by name.

import type { BlockKind } from "./kinds.js";
import { code } from "./code.js";
import { math } from "./math.js";
import { heading, quote, subtitle, text, title } from "./words.js";

const kinds: Record<string, BlockKind> = { title, subtitle, heading, text, quote, math, code };

// The kind of that name, or text when there's none.
export const kindOf = (name: string): BlockKind => kinds[name] ?? text;

export type { BlockKind, Setting, Showing } from "./kinds.js";
