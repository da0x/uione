// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Code: kept as it's typed, in the code face, in a well. Enter starts a new line, and
// Enter on an empty last line leaves it for a block after it.

import type { BlockKind } from "./kinds.js";
import { inCode, well } from "./kinds.js";

export const code: BlockKind = {
  look: inCode,
  room: [10, 18],
  enter: (words, caret, selecting) => (!selecting && caret === words.length && words.endsWith("\n") ? "leave" : "line"),
  showing: () => ({ raw: true, words: well }),
};
