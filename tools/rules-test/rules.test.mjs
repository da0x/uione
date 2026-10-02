// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The site's generated Firestore rules, tested against the emulator: each rule is a
// real read or write, made as a particular person, that has to succeed or fail.
//
//   one build site, tools/emulators/run, then yarn workspace uione-rules-test test

import { assertFails, assertSucceeds, initializeTestEnvironment } from "@firebase/rules-unit-testing";
import { deleteDoc, doc, getDoc, setDoc } from "firebase/firestore";
import { readFileSync } from "node:fs";
import { after, before, beforeEach, describe, it } from "node:test";

const [host, port] = (process.env.FIRESTORE_EMULATOR_HOST ?? "localhost:8080").split(":");
const rules = readFileSync(new URL("../../site/build/firestore.rules", import.meta.url), "utf8");

let env;

before(async () => {
  env = await initializeTestEnvironment({ projectId: "demo-uione", firestore: { rules, host, port: Number(port) } });
});
after(() => env.cleanup());

// Documents as the backend writes them, put in place with the rules switched off.
beforeEach(async () => {
  await env.clearFirestore();
  await env.withSecurityRulesDisabled(async (context) => {
    const db = context.firestore();
    await setDoc(doc(db, "views/waitlist::signups"), { total: 3, public: true, owner_uid: "", required_permission: "" });
    await setDoc(doc(db, "views/studio::projects:alice"), { rows: [], public: false, owner_uid: "alice", required_permission: "" });
    await setDoc(doc(db, "views/library::shelf"), { rows: [], public: false, owner_uid: "", required_permission: "book:view" });
    await setDoc(doc(db, "views/tracker::project_page:secret"), { public: false, owner_uid: "", required_permission: "", readers: ["alice", "dave"] });
    await setDoc(doc(db, "waitlist_signup/ada@example.com"), { email: "ada@example.com" });
    await setDoc(doc(db, "roles/reader"), { permissions: ["book:view"] });
    await setDoc(doc(db, "users/carol"), { role_id: "reader" });
  });
});

const anyone = () => env.unauthenticatedContext().firestore();
const as = (uid) => env.authenticatedContext(uid).firestore();

describe("reading", () => {
  it("lets anyone read a public view, signed in or not", async () => {
    await assertSucceeds(getDoc(doc(anyone(), "views/waitlist::signups")));
    await assertSucceeds(getDoc(doc(as("bob"), "views/waitlist::signups")));
  });

  it("never lets a browser read an entity, only views", async () => {
    await assertFails(getDoc(doc(anyone(), "waitlist_signup/ada@example.com")));
    await assertFails(getDoc(doc(as("alice"), "waitlist_signup/ada@example.com")));
  });

  it("lets only its owner read a per-person view", async () => {
    await assertSucceeds(getDoc(doc(as("alice"), "views/studio::projects:alice")));
    await assertFails(getDoc(doc(as("bob"), "views/studio::projects:alice")));
    await assertFails(getDoc(doc(anyone(), "views/studio::projects:alice")));
  });

  it("lets someone read a view their role grants, and nobody else", async () => {
    await assertSucceeds(getDoc(doc(as("carol"), "views/library::shelf")));
    await assertFails(getDoc(doc(as("bob"), "views/library::shelf")));
    await assertFails(getDoc(doc(anyone(), "views/library::shelf")));
  });

  it("lets the readers a document lists read it, and nobody else", async () => {
    await assertSucceeds(getDoc(doc(as("alice"), "views/tracker::project_page:secret")));
    await assertSucceeds(getDoc(doc(as("dave"), "views/tracker::project_page:secret")));
    await assertFails(getDoc(doc(as("bob"), "views/tracker::project_page:secret")));
    await assertFails(getDoc(doc(anyone(), "views/tracker::project_page:secret")));
  });

  it("reads a view that doesn't exist yet as empty, rather than refusing", async () => {
    await assertSucceeds(getDoc(doc(as("bob"), "views/studio::projects:bob")));
  });
});

describe("writing", () => {
  it("refuses every write from a browser, even to your own view", async () => {
    await assertFails(setDoc(doc(as("alice"), "views/studio::projects:alice"), { rows: ["mine"] }));
    await assertFails(setDoc(doc(anyone(), "views/waitlist::signups"), { total: 1000 }));
    await assertFails(setDoc(doc(anyone(), "waitlist_signup/eve@example.com"), { email: "eve@example.com" }));
    await assertFails(setDoc(doc(as("bob"), "users/bob"), { role_id: "reader" }));
    await assertFails(deleteDoc(doc(as("alice"), "views/studio::projects:alice")));
  });
});
