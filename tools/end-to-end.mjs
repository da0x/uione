// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Runs the site the way it runs for real, against the emulators: the generated Go
// backend, the generated rules, and @uione/react's Firebase source, with the browser
// left out. Each step waits for the change to arrive by itself, the way a screen
// would, rather than asking for it again.
//
//   tools/emulators/run && compiler/build/one build site && yarn install
//   node tools/end-to-end.mjs

import { spawn, execFileSync } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { initializeApp } from "firebase/app";
import { connectAuthEmulator, getAuth, signInWithEmailAndPassword } from "firebase/auth";
import { doc, getDoc, getFirestore } from "firebase/firestore";
import { firebaseSource } from "@uione/react/firebase";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const project = "demo-uione";
const config = { projectId: project, apiKey: "demo" };
const emulators = { firestore: "localhost:8080", auth: "localhost:9099" };
const port = 8091;
const api = `http://localhost:${port}/api`;

let step = 0;
function pass(what) {
  console.log(`ok ${++step}  ${what}`);
}

// Waits until check() is true, and fails with what it was waiting for if it isn't.
async function until(check, what, ms = 10000) {
  const end = Date.now() + ms;
  while (!(await check())) {
    if (Date.now() > end) throw new Error(`timed out waiting until ${what}`);
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

// Follows a view through a data source, keeping its latest state.
function follow(source, view) {
  const latest = { state: { status: "loading", data: undefined } };
  latest.stop = source.subscribe(view, undefined, (state) => (latest.state = state));
  return latest;
}

// A person in the Auth emulator, signed in on an app of their own.
async function person(name) {
  const email = `${name}@example.com`;
  const response = await fetch(`http://${emulators.auth}/identitytoolkit.googleapis.com/v1/accounts:signUp?key=demo`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ email, password: "password", displayName: name, returnSecureToken: true }),
  });
  if (!response.ok) throw new Error(`couldn't create ${name}: ${await response.text()}`);
  const app = initializeApp(config, name);
  const auth = getAuth(app);
  connectAuthEmulator(auth, `http://${emulators.auth}`, { disableWarnings: true });
  const { user } = await signInWithEmailAndPassword(auth, email, "password");
  return { app, uid: user.uid, source: firebaseSource({ app, emulators, api, personal: ["studio::projects"] }) };
}

// Start clean: no documents, no people, and the site's rules in force.
await fetch(`http://${emulators.firestore}/emulator/v1/projects/${project}/databases/(default)/documents`, { method: "DELETE" });
await fetch(`http://${emulators.auth}/emulator/v1/projects/${project}/accounts`, { method: "DELETE" });
execFileSync("node", [join(root, "tools/emulators/rules.mjs"), join(root, "site/build/firestore.rules")], { stdio: "inherit" });

const binary = join(mkdtempSync(join(tmpdir(), "uione-end-to-end-")), "api");
execFileSync("go", ["build", "-o", binary, "."], { cwd: join(root, "site/build/api"), stdio: "inherit" });
const server = spawn(binary, [], {
  env: {
    ...process.env,
    PORT: String(port),
    FIRESTORE_EMULATOR_HOST: emulators.firestore,
    FIREBASE_AUTH_EMULATOR_HOST: emulators.auth,
  },
  stdio: ["ignore", "inherit", "inherit"],
});

let failed = false;
try {
  await until(
    () => fetch(`http://localhost:${port}/health`).then((r) => r.ok, () => false),
    "the backend answers",
  );

  // 1. Someone signed out watches the waitlist count while someone else joins.
  const anyone = firebaseSource({ config, emulators, api, personal: ["studio::projects"] });
  const count = follow(anyone, "waitlist::signups");
  await until(() => count.state.status === "live", "the count is live");
  const before = count.state.data.total;
  if (before !== 0) throw new Error(`the count started at ${before}, not 0`);
  await anyone.run("waitlist::signup::create", { email: "Ada@Example.com" });
  await until(() => count.state.data?.total === 1, "the count goes up by itself");
  pass("joining the waitlist moves the count on a page that's already open");

  // 2. The same address, written differently, is the same signup.
  await anyone.run("waitlist::signup::create", { email: " ada@example.com " });
  await new Promise((resolve) => setTimeout(resolve, 1000));
  if (count.state.data.total !== 1) throw new Error(`joining twice made the count ${count.state.data.total}`);
  pass("joining twice with the same address counts once");

  // 3. A bad address comes back as a message a form can show.
  const refused = await anyone.run("waitlist::signup::create", { email: "not an address" }).then(
    () => "",
    (e) => e.message,
  );
  if (refused !== "Email isn't an email address") throw new Error(`a bad address gave "${refused}"`);
  pass("a bad address is refused with a readable message");

  // 4. Someone signed out can't create a project, and can't see anyone's.
  const signedOut = await anyone.run("studio::project::create", { name: "Nobody's" }).then(
    () => "",
    (e) => e.message,
  );
  if (signedOut === "") throw new Error("someone signed out created a project");
  const nobody = follow(anyone, "studio::projects");
  await until(() => nobody.state.status === "denied", "the signed-out studio is denied");
  pass(`someone signed out can't create a project ("${signedOut}")`);

  // 5. A signed-in person sees their own new project arrive, and no one else does.
  const ada = await person("ada");
  const grace = await person("grace");
  const adas = follow(ada.source, "studio::projects");
  const graces = follow(grace.source, "studio::projects");
  await until(() => adas.state.status === "live" && graces.state.status === "live", "both studios are live");
  await ada.source.run("studio::project::create", { name: "Ada's first" });
  await until(() => adas.state.data?.rows?.some((row) => row.name === "Ada's first"), "Ada's project appears in her view");
  const created = adas.state.data.rows[0].created_at;
  if (!(created instanceof Date)) throw new Error(`created_at arrived as ${typeof created}, not a Date`);
  await new Promise((resolve) => setTimeout(resolve, 500));
  if ((graces.state.data?.rows ?? []).length !== 0) throw new Error("Grace's view shows a project she didn't make");
  pass("a new project appears live in its owner's view, and only there");

  // 6. Asking for someone else's view directly is refused by the rules.
  const db = getFirestore(grace.app); // already pointed at the emulator by her source
  const denied = await getDoc(doc(db, "views", `studio::projects:${ada.uid}`)).then(
    () => "",
    (e) => e.code,
  );
  if (denied !== "permission-denied") throw new Error(`Grace reading Ada's projects gave "${denied || "her projects"}"`);
  pass("the rules refuse one person's projects to another");

  for (const view of [count, nobody, adas, graces]) view.stop();
} catch (e) {
  failed = true;
  console.log(`FAIL  ${e.message}`);
} finally {
  server.kill();
}
console.log(failed ? "the site doesn't work end to end" : `${step} steps pass end to end`);
// Firebase keeps connections open, so leave rather than wait for them.
process.exit(failed ? 1 : 0);
