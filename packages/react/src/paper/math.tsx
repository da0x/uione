// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An equation: its words in one::math's notation, set as math. While the caret's in it,
// it's its words, in a well; otherwise its equation, aligned as the block says. Both are
// in the one place, so it keeps the room of the larger and the page doesn't move.

import { Equation } from "../equation.js";
import { parseMath } from "../math.js";
import type { BlockKind } from "./kinds.js";
import { inCode, well } from "./kinds.js";

export const math: BlockKind = {
  look: inCode,
  room: [10, 18],
  joins: false,
  showing: ({ words, focused, readOnly, zoom, align, toWords }) => {
    const set = words.trim() !== "" && "node" in parseMath(words);
    const raw = !set || (focused && !readOnly);
    return {
      raw,
      // Not shown while its equation is, but still there to take the caret, as a hidden
      // element can't.
      words: raw ? well : { opacity: 0 },
      over: set ? (
        <div
          style={{ gridArea: "1 / 1", alignSelf: "center", textAlign: align, fontSize: 16 * zoom, fontStyle: "normal", cursor: readOnly ? "default" : "text", visibility: raw ? "hidden" : "visible" }}
          onMouseDown={(e) => {
            if (readOnly) return;
            e.preventDefault();
            toWords();
          }}
          // A tap on a phone: its click is what lets the caret in, and the keyboard up.
          onClick={() => {
            if (!readOnly) toWords();
          }}
        >
          <Equation written={words} displayed />
        </div>
      ) : undefined,
    };
  },
};
