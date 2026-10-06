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

// @uione/react stays an ordinary import, as it would be when published. If it were
// bundled in, the app would get its own copy, and its screens couldn't see the App
// rendered here. @uione/radix is bundled, since it imports its fonts' stylesheets,
// which only a bundler reads.
// Built in development mode, so the app uses the emulators' settings and never a real
// project's, even after a deploy has written .env.production next to it.
await build({
  root: web,
  mode: "development",
  logLevel: "warn",
  build: { ssr: "src/app.tsx", outDir: "build-ssr" },
  ssr: { external: ["@uione/react"], noExternal: ["@uione/radix"] },
});

const { createElement } = await import("react");
const { renderToString } = await import("react-dom/server");
const { App } = await import("@uione/react");
const { site } = await import(pathToFileURL(join(web, "build-ssr/app.js")).href);

const pages = {
  "/": ['src="/icon.svg"', "Tab width", ">Default</option>", "Write a whole app in your browser.", 'class="shiki', "namespace library", "Join the waitlist", 'type="email"'],
  "/install": ["Install one", "curl -fsSL https://www.uione.io/install.sh | sh", "Select an editor"],
  "/install/docker": ["ghcr.io/da0x/uione"],
  "/language": ["Start", "Your first project"],
  "/language/overview": ["Overview", "One architecture"],
  "/language/reference": ["Tab width", "Reference", "A <code>.one</code> file is a list of declarations", 'class="one-code', 'class="shiki'],
  "/releases": ["0.6.5", "timeline"],
  "/mission": ["An application should be as short as what it does."],
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
