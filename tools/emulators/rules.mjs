// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Loads Firestore rules into the emulator. It only enforces rules it has been given,
// and without them it allows everything, so a test run without this would prove
// nothing about who can read what.
//
//   node tools/emulators/rules.mjs site/build/firestore.rules

import { readFile } from "node:fs/promises";

const [path] = process.argv.slice(2);
if (!path) {
  console.error("usage: node tools/emulators/rules.mjs <firestore.rules>");
  process.exit(2);
}

const response = await fetch("http://localhost:8080/emulator/v1/projects/demo-uione:securityRules", {
  method: "PUT",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ rules: { files: [{ name: "firestore.rules", content: await readFile(path, "utf8") }] } }),
});
if (!response.ok) {
  console.error(`the emulator refused the rules in ${path}:\n${await response.text()}`);
  process.exit(1);
}
