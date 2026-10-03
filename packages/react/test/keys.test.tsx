// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { keyOf, partsOf } from "../src/keys.js";

// What the one library makes of each, from Go's url.PathEscape.
const made: [string[], string][] = [
  [["da0x", "neotrac"], "da0x-neotrac"],
  [["engine", "x-1"], "engine-x%2D1"],
  [["my-name", "neo-trac"], "my-name-neo%2Dtrac"],
  [["a b", "c/d"], "a%20b-c%2Fd"],
  [["x", "!'()*$&+,:;=@~._"], "x-%21%27%28%29%2A$&+%2C:%3B=@~._"],
  [["über", "ünï"], "%C3%BCber-%C3%BCn%C3%AF"],
  [["q?", "#h"], "q%3F-%23h"],
];

describe("ids made from a key's parts", () => {
  it("are made the way the one library makes them, and split back into their parts", () => {
    for (const [parts, id] of made) {
      expect(keyOf(parts)).toBe(id);
      expect(partsOf(id, parts.length)).toEqual(parts);
    }
  });

  it("aren't made while a part is missing", () => {
    expect(keyOf(["da0x", undefined])).toBeUndefined();
    expect(keyOf(["", "neotrac"])).toBeUndefined();
    expect(partsOf("neotrac", 2)).toBeUndefined();
  });
});
