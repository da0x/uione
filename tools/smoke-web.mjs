// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Renders the generated site in Node at each of its addresses, and checks each page
// has what it should. It builds the app for the server first, so this tests the same
// code the browser gets, not a copy.
//
//   node tools/smoke-web.mjs      after one build site and yarn install

import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { build } from "vite";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const web = join(root, "site/build/web");

// The @uione packages stay ordinary imports, as they would be when published. If
// they were bundled in, the app would get its own copy of @uione/react, and its
// screens couldn't see the App rendered here.
// Built in development mode, so the app uses the emulators' settings and never a real
// project's, even after a deploy has written .env.production next to it.
await build({
  root: web,
  mode: "development",
  logLevel: "warn",
  build: { ssr: "src/app.tsx", outDir: "build-ssr" },
  ssr: { external: ["@uione/react", "@uione/radix"] },
});

const { createElement } = await import("react");
const { renderToString } = await import("react-dom/server");
const { App } = await import("@uione/react");
const { site } = await import(pathToFileURL(join(web, "build-ssr/app.js")).href);

const pages = {
  "/": ['src="/icon.svg"', "Tab width", ">Default</option>", "Write the feature once.", 'class="shiki', "namespace library", "Join the waitlist", 'type="email"'],
  "/docs": ["Overview", "One architecture"],
  "/docs/language": ["Tab width", "Language", "A <code>.one</code> file is a list of declarations", 'class="one-code', 'class="shiki'],
  "/studio": ["Studio", "Loading…", ">Create<"],
};

// What a page must not show.
const absent = {
  "/": ["Copyright", "SPDX-License-Identifier"],
};

let failed = 0;
for (const [location, expected] of Object.entries(pages)) {
  const html = renderToString(createElement(App, { ...site, location }));
  const missing = expected.filter((text) => !html.includes(text));
  const shown = (absent[location] ?? []).filter((text) => html.includes(text));
  if (missing.length > 0 || shown.length > 0) {
    failed++;
    if (missing.length > 0) console.log(`FAIL  ${location} is missing ${missing.map((m) => JSON.stringify(m)).join(", ")}`);
    if (shown.length > 0) console.log(`FAIL  ${location} shows ${shown.map((m) => JSON.stringify(m)).join(", ")}`);
  }
}
console.log(`${Object.keys(pages).length - failed}/${Object.keys(pages).length} pages render with what they should`);
process.exit(failed ? 1 : 0);
