// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The print shop's press, an agent, working on the emulators: a foreman moves cards,
// and the press takes each that arrives in a stage it works, runs its program, and
// moves it on. An order waits on its plates, a failing program sends a plate to
// Failed, and a press that stops lets go of what it held once its lease runs out,
// for another press to take.
//
//   tools/emulators/run && compiler/build/one build examples/print_shop
//   node tools/agent-end-to-end.mjs

import { spawn, execFileSync } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const shopFolder = join(root, "examples/print_shop");
const project = "demo-uione";
const emulators = { firestore: "localhost:8080", auth: "localhost:9099" };
const port = 8098;
const server = `http://localhost:${port}`;
const work = mkdtempSync(join(tmpdir(), "uione-agent-"));
const running = [];

let step = 0;
function pass(what) {
  console.log(`ok ${++step}  ${what}`);
}

async function until(check, what, ms = 20000) {
  const end = Date.now() + ms;
  while (!(await check())) {
    if (Date.now() > end) throw new Error(`timed out waiting until ${what}`);
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
}

function key(...parts) {
  return parts.map((p, i) => (i > 0 ? encodeURIComponent(p).replaceAll("-", "%2D") : encodeURIComponent(p))).join("-");
}

// A person in the Auth emulator, and their token.
async function person(name) {
  const response = await fetch(`http://${emulators.auth}/identitytoolkit.googleapis.com/v1/accounts:signUp?key=demo`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ email: `${name}-${Date.now()}@example.com`, password: "password", displayName: name, returnSecureToken: true }),
  });
  if (!response.ok) throw new Error(`couldn't create ${name}: ${await response.text()}`);
  return (await response.json()).idToken;
}

// Runs a command as someone; what it answers, or what it refused with. Told to slow
// down for a moment, it waits as long as it's told and asks again, as the clients do.
async function run(token, command, fields) {
  for (;;) {
    const response = await fetch(`${server}/api/shop/${command.replace("::", "/")}`, {
      method: "POST",
      headers: { "Content-Type": "application/json", Authorization: `Bearer ${token}` },
      body: JSON.stringify(fields),
    });
    const said = await response.json().catch(() => ({}));
    if (response.status === 429 && said.retry_after > 0 && said.retry_after <= 10) {
      await new Promise((resolve) => setTimeout(resolve, said.retry_after * 1000));
      continue;
    }
    return { ok: response.ok, said };
  }
}

async function must(token, command, fields) {
  const { ok, said } = await run(token, command, fields);
  if (!ok) throw new Error(`${command} was refused: ${JSON.stringify(said)}`);
  return said;
}

// A record as stored, read past the rules.
async function stored(collection, id) {
  const response = await fetch(`http://${emulators.firestore}/v1/projects/${project}/databases/(default)/documents/${collection}/${encodeURIComponent(id)}`, {
    headers: { Authorization: "Bearer owner" },
  });
  if (!response.ok) return {};
  const fields = (await response.json()).fields ?? {};
  return Object.fromEntries(Object.entries(fields).map(([k, v]) => [k, v.stringValue ?? v.timestampValue ?? v.nullValue ?? v]));
}

function agent(name, key, extra = {}) {
  const child = spawn(join(work, "press-build/press"), [], {
    cwd: shopFolder,
    env: {
      ...process.env,
      ONE_KEY: key,
      ONE_SERVER: server,
      ONE_FIREBASE_PROJECT: project,
      ONE_FIRESTORE_HOST: emulators.firestore,
      ONE_AUTH_HOST: emulators.auth,
      XDG_STATE_HOME: join(work, name),
      ...extra,
    },
    stdio: ["ignore", "inherit", "pipe"],
  });
  child.said = "";
  child.stderr.on("data", (d) => (child.said += d));
  running.push(child);
  return child;
}

try {
  execFileSync(join(root, "compiler/build/one"), ["build", shopFolder], { stdio: "ignore" });
  execFileSync("node", [join(root, "tools/emulators/rules.mjs"), join(shopFolder, "build/firestore.rules")], { stdio: "ignore" });
  const api = join(shopFolder, "build/api");
  execFileSync("go", ["mod", "tidy"], { cwd: api, stdio: "ignore" });
  execFileSync("go", ["build", "-o", join(work, "api"), "."], { cwd: api });
  const backend = spawn(join(work, "api"), [], {
    env: { ...process.env, PORT: String(port), FIRESTORE_EMULATOR_HOST: emulators.firestore, FIREBASE_AUTH_EMULATOR_HOST: emulators.auth, GOOGLE_CLOUD_PROJECT: project },
    stdio: "ignore",
  });
  running.push(backend);
  await until(async () => (await fetch(`${server}/health`).catch(() => ({ ok: false }))).ok, "the backend answers");
  const agentFolder = join(shopFolder, "build/agents/press");
  // The client's TLS as CI builds it: OpenSSL where Botan 3 isn't packaged.
  const tls = process.env.ONE_CLIENT_TLS ?? "botan";
  execFileSync("cmake", ["-S", agentFolder, "-B", join(work, "press-build"), "-DCMAKE_BUILD_TYPE=Release", `-DONE_CLIENT_TLS=${tls}`], {
    env: { ...process.env, UIONE: root },
    stdio: "ignore",
  });
  execFileSync("cmake", ["--build", join(work, "press-build"), "-j2"], { stdio: "ignore" });
  pass("one build writes the press, and it builds against uione's client");

  const foreman = await person("Foreman");
  const shop = `shop${Date.now()}`;
  await must(foreman, "shop::create", { slug: shop, title: "Harbor Print" });
  const cover = (await must(foreman, "plate::create", { shop, name: "Cover" })).id;
  const back = (await must(foreman, "plate::create", { shop, name: "Back" })).id;
  const cracked = (await must(foreman, "plate::create", { shop, name: "Cracked" })).id;
  const order = (await must(foreman, "order::create", { shop, title: "Menus", plates: [cover, back] })).id;
  const press = (await must(foreman, "service::create", { shop, title: "Press", role: key(shop, "press_operator") })).id;
  const { key: pressKey } = await must(foreman, "service::key", { id: press });
  pass("a foreman makes a shop, its plates, an order printed from two of them, and the press's key");

  const waiting = await run(foreman, "order::move", { id: order, stage: "printing" });
  if (waiting.ok || !String(waiting.said.error ?? JSON.stringify(waiting.said)).includes("waiting on Cover, Back")) {
    throw new Error(`an order waits on its plates: ${JSON.stringify(waiting)}`);
  }
  pass("an order can't go to the press while its plates aren't etched: waiting on Cover, Back");

  const first = agent("first", pressKey);
  await must(foreman, "plate::move", { id: cover, stage: "etching" });
  await must(foreman, "plate::move", { id: back, stage: "etching" });
  await until(async () => (await stored("shop_plate", cover)).stage === "etched" && (await stored("shop_plate", back)).stage === "etched", "the press etches both plates");
  pass("plates moved to Etching are taken by the press, etched, and moved to Etched");

  await must(foreman, "order::move", { id: order, stage: "printing" });
  await until(async () => (await stored("shop_order", order)).stage === "printed", "the press prints the order");
  pass("with its plates etched, the order goes to the press, and comes out printed");

  await must(foreman, "plate::move", { id: cracked, stage: "etching" });
  await until(async () => (await stored("shop_plate", cracked)).stage === "failed", "a failing etch sends the plate to Failed");
  // What the press last said of it stays until someone moves it: it failed.
  const said = (record) => Object.fromEntries(Object.entries(record.data?.mapValue?.fields ?? {}).map(([k, v]) => [k, v.stringValue ?? v.doubleValue ?? v.booleanValue]));
  if (!first.said.includes("the plate cracked")) throw new Error(`the press says why it failed: ${first.said}`);
  if (said(await stored("shop_plate", cracked)).status !== "failed") throw new Error("a plate that failed says so");
  pass("a program that fails sends what it worked on to Failed, and the press says why");

  const slow = (await must(foreman, "plate::create", { shop, name: "Slow" })).id;
  await must(foreman, "plate::move", { id: slow, stage: "etching" });
  await until(async () => (await stored("shop_plate", slow)).held_by, "the press takes the slow plate");
  await until(async () => said(await stored("shop_plate", slow)).status === "working", "the press says it's working on the slow plate");
  const working = said(await stored("shop_plate", slow));
  if (!working.runner || !/^\d{4}-\d\d-\d\dT/.test(working.since)) throw new Error(`the press says which runner and since when: ${JSON.stringify(working)}`);
  pass(`the press says of what it holds: working, since ${working.since}, runner ${working.runner}`);
  first.kill("SIGKILL");
  agent("second", pressKey);
  await until(async () => (await stored("shop_plate", slow)).stage === "etched", "another press takes it once the lease runs out", 120000);
  if (said(await stored("shop_plate", slow)).status) throw new Error("what's moved on says nothing of who worked it");
  pass("a press that stops lets go when its lease runs out, and another takes what it held");

  // Two presses, each working one plate at a time: four plates moved at once are
  // shared between them, each asking first when it's less busy.
  for (const r of running.splice(2)) r.kill();
  const left = agent("left", pressKey, { ONE_AGENT_PARALLEL: "1" });
  const right = agent("right", pressKey, { ONE_AGENT_PARALLEL: "1" });
  await new Promise((resolve) => setTimeout(resolve, 2000));
  const batch = [];
  for (const name of ["One", "Two", "Three", "Four"]) batch.push((await must(foreman, "plate::create", { shop, name })).id);
  for (const id of batch) await must(foreman, "plate::move", { id, stage: "etching" });
  await until(async () => (await Promise.all(batch.map((id) => stored("shop_plate", id)))).every((p) => p.stage === "etched"), "both presses etch the four plates", 60000);
  const by = (press) => (press.said.match(/: running tools\/etch/g) ?? []).length;
  if (by(left) === 0 || by(right) === 0 || by(left) + by(right) !== 4) throw new Error(`both presses share the work, each plate once: left ${by(left)}, right ${by(right)}`);
  pass(`two presses share four plates between them, ${by(left)} and ${by(right)}`);

  // A burst: many plates made and moved at once, worked by three presses on the
  // same key, four at a time each. Every one is etched, once, and nothing's left
  // held or saying it's working. BURST=n says how many; 20 unless said, as many as
  // one person may make at once before being asked to slow down.
  for (const r of running.splice(2)) r.kill();
  const presses = ["a", "b", "c"].map((name) => agent(`burst-${name}`, pressKey, { ONE_AGENT_PARALLEL: "4" }));
  await new Promise((resolve) => setTimeout(resolve, 2000));
  const count = Number(process.env.BURST ?? 20);
  const started = Date.now();
  const burst = await Promise.all(Array.from({ length: count }, (_, i) => must(foreman, "plate::create", { shop, name: `Burst ${i + 1}` }).then((r) => r.id)));
  await Promise.all(burst.map((id) => must(foreman, "plate::move", { id, stage: "etching" })));
  const made = Date.now() - started;
  await until(
    async () => (await Promise.all(burst.map((id) => stored("shop_plate", id)))).every((p) => p.stage === "etched"),
    `the presses etch all ${count} plates`,
    count * 3000,
  );
  const took = Date.now() - started;
  const runs = burst.length && presses.map((press) => (press.said.match(/Burst \d+ \([^)]*\) in etching: running/g) ?? []).length);
  const each = new Map();
  for (const press of presses) for (const m of press.said.matchAll(/(Burst \d+) \([^)]*\) in etching: running/g)) each.set(m[1], (each.get(m[1]) ?? 0) + 1);
  // A press that ended by itself, by a crash or a signal, would leave its plate to a
  // lease running out, and another press to run it again.
  const ended = presses.filter((press) => press.exitCode !== null || press.signalCode !== null);
  if (ended.length) throw new Error(`every press keeps working through the burst: ${ended.map((p) => p.signalCode ?? `exit ${p.exitCode}`).join(", ")}`);
  const twice = [...each].filter(([, n]) => n > 1).map(([name]) => name);
  if (twice.length) throw new Error(`each plate is etched once, but ${twice.join(", ")} ran more than once`);
  if (each.size !== count) throw new Error(`every plate is etched by a press: ${each.size} of ${count}`);
  const unfinished = (await Promise.all(burst.map((id) => stored("shop_plate", id)))).filter((p) => p.held_by || said(p).status);
  if (unfinished.length) throw new Error(`nothing is left held or working: ${unfinished.length} are`);
  const slowed = presses.reduce((n, press) => n + (press.said.match(/slow down|couldn't keep it just now|couldn't say how it's going/g) ?? []).length, 0);
  pass(`a burst of ${count} plates, made and moved in ${(made / 1000).toFixed(1)}s, etched once each in ${(took / 1000).toFixed(1)}s by three presses (${runs.join(", ")}); asked to slow down ${slowed} times`);
  console.log("all passed");
} catch (e) {
  console.error(`FAIL  ${e.message}`);
  for (const child of running) if (child.said) console.error(child.said);
  process.exitCode = 1;
} finally {
  for (const child of running) child.kill();
}
