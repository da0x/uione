// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// @uione/react/firebase: views read live from Firestore, commands sent to the Go
// backend, and signing in the ways the project names. Firebase is a peer dependency, so an app that doesn't
// use this never loads it.
//
// Nothing connects while a page is rendered on a server, since nothing listens
// there. In a browser it connects as soon as the page asks who's signed in, which
// every page does to draw its account area.

import { initializeAnalytics, isSupported, logEvent, setConsent } from "firebase/analytics";
import type { Analytics as FirebaseAnalytics } from "firebase/analytics";
import { initializeApp } from "firebase/app";
import type { FirebaseApp, FirebaseOptions } from "firebase/app";
import { connectAuthEmulator, getAuth, linkWithCredential, onAuthStateChanged, signInWithPopup, signOut } from "firebase/auth";
import type { Auth, AuthCredential, User } from "firebase/auth";
import { Timestamp, connectFirestoreEmulator, doc, getFirestore, onSnapshot } from "firebase/firestore";
import type { Firestore } from "firebase/firestore";
import type { Analytics } from "./contract.js";
import type { AuthSource, DataSource, Person, ViewData } from "./data.js";
import { google } from "./authentication/google.js";
import type { FirebaseAuthenticationMethod } from "./authentication/method.js";
import { liveSource } from "./live.js";
import type { LiveOptions } from "./live.js";

export interface FirebaseSourceOptions extends LiveOptions {
  // The Firebase project, or an app that's already set up.
  config?: FirebaseOptions;
  app?: FirebaseApp;
  // The local emulators, as host:port, for working without the cloud.
  emulators?: { firestore: string; auth: string };
  // The ways people sign in, as the project block's authentication lines name them;
  // Google when it doesn't say.
  authentication?: FirebaseAuthenticationMethod[];
}

export { google } from "./authentication/google.js";
export { github } from "./authentication/github.js";
export { microsoft } from "./authentication/microsoft.js";
export type { FirebaseAuthenticationMethod } from "./authentication/method.js";

// Firestore times become Dates, so a screen can show them like any other date.
function plain(value: unknown): unknown {
  if (value instanceof Timestamp) return value.toDate();
  if (Array.isArray(value)) return value.map(plain);
  if (value !== null && typeof value === "object" && Object.getPrototypeOf(value) === Object.prototype) {
    return Object.fromEntries(Object.entries(value).map(([k, v]) => [k, plain(v)]));
  }
  return value;
}

function personOf(user: User | null): Person | null {
  return user ? { uid: user.uid, name: user.displayName ?? user.email ?? "Signed in" } : null;
}

// Counting visitors with Firebase Analytics, which is Google Analytics underneath,
// in the same Firebase app the data comes from, as its settings name it, measurement
// ID included. It starts in Google's consent mode with everything refused, so a
// visitor is counted without cookies until they agree, and records each screen
// itself rather than only the first page load.
export function firebaseAnalytics(config: FirebaseOptions): Analytics {
  let started: Promise<FirebaseAnalytics | undefined> | undefined;
  const start = () =>
    (started ??= (async () => {
      if (!(await isSupported().catch(() => false))) return undefined;
      setConsent({ analytics_storage: "denied", ad_storage: "denied", ad_user_data: "denied", ad_personalization: "denied" });
      return initializeAnalytics(initializeApp(config), { config: { send_page_view: false } });
    })());
  return {
    consent: (agreed) => void start().then((a) => a && setConsent({ analytics_storage: agreed ? "granted" : "denied" })),
    page: (path, title) =>
      void start().then((a) => a && logEvent(a, "page_view", { page_path: path, page_title: title, page_location: window.location.href })),
  };
}

export function firebaseSource(options: FirebaseSourceOptions): DataSource {
  let connected: { db: Firestore; auth: Auth } | undefined;
  let person: Person | null | undefined;
  const watchers = new Set<(person: Person | null) => void>();
  const methods = options.authentication?.length ? options.authentication : [google];
  // A sign-in refused because its email already has an account here, another way:
  // once the person signs in that way, this way is added to the account, so either
  // works from then on.
  let waiting: { method: string; credential: AuthCredential } | undefined;

  const connect = () => {
    if (connected) return connected;
    // initializeApp hands back the same app when it's called again with the same
    // settings, and refuses different ones rather than quietly using the wrong project.
    const app = options.app ?? initializeApp(options.config ?? {});
    const db = getFirestore(app);
    const auth = getAuth(app);
    if (options.emulators) {
      const [host, port] = options.emulators.firestore.split(":");
      connectFirestoreEmulator(db, host!, Number(port));
      connectAuthEmulator(auth, `http://${options.emulators.auth}`, { disableWarnings: true });
    }
    onAuthStateChanged(auth, (user) => {
      person = personOf(user);
      for (const emit of watchers) emit(person);
    });
    connected = { db, auth };
    return connected;
  };

  const auth: AuthSource = {
    person: () => person,
    watch(emit) {
      watchers.add(emit);
      connect();
      if (person !== undefined) emit(person);
      return () => {
        watchers.delete(emit);
      };
    },
    methods: methods.map(({ id, name, Mark }) => ({ id, name, Mark })),
    async signIn(id) {
      const method = methods.find((m) => m.id === id) ?? methods[0]!;
      let signedIn;
      try {
        signedIn = await signInWithPopup(connect().auth, method.provider());
      } catch (refused) {
        const credential = (refused as { code?: unknown } | null)?.code === "auth/account-exists-with-different-credential" ? method.credentialFrom(refused) : null;
        if (credential) waiting = { method: method.id, credential };
        throw refused;
      }
      if (waiting && waiting.method !== method.id) {
        // Adding it can fail, say when it's on another account already; signing in
        // worked either way.
        await linkWithCredential(signedIn.user, waiting.credential).catch(() => {});
      }
      waiting = undefined;
      // What only the provider can say, like a GitHub account's verified emails, is
      // asked once, now, while its access is at hand; the backend keeps the answer.
      const access = method.access?.from(signedIn);
      if (method.access && access) {
        const token = await signedIn.user.getIdToken();
        await (options.fetch ?? fetch)(`${options.api ?? "/api"}/signin`, {
          method: "POST",
          headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
          body: JSON.stringify({ [method.access.name]: access }),
        }).catch(() => undefined);
      }
    },
    async signOut() {
      await signOut(connect().auth);
    },
  };

  return liveSource(
    {
      auth,
      watch(path, next, fail) {
        return onSnapshot(
          doc(connect().db, path),
          { includeMetadataChanges: true },
          (snap) => next(snap.exists() ? (plain(snap.data()) as ViewData) : undefined, !snap.metadata.fromCache),
          (error) => fail(error.code === "permission-denied"),
        );
      },
      async token() {
        // Right after the page loads, the saved sign-in may not be restored yet.
        const { auth } = connect();
        await auth.authStateReady();
        return auth.currentUser?.getIdToken();
      },
    },
    options,
  );
}
