// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// @uione/react/firebase: views read live from Firestore, commands sent to the Go
// backend, and Google sign-in. Firebase is a peer dependency, so an app that doesn't
// use this never loads it.
//
// Nothing connects while a page is rendered on a server, since nothing listens
// there. In a browser it connects as soon as the page asks who's signed in, which
// every page does to draw its account area.

import { initializeApp } from "firebase/app";
import type { FirebaseApp, FirebaseOptions } from "firebase/app";
import { GithubAuthProvider, GoogleAuthProvider, connectAuthEmulator, getAuth, onAuthStateChanged, signInWithPopup, signOut } from "firebase/auth";
import type { Auth, User } from "firebase/auth";
import { Timestamp, connectFirestoreEmulator, doc, getFirestore, onSnapshot } from "firebase/firestore";
import type { Firestore } from "firebase/firestore";
import type { AuthSource, DataSource, Person, ViewData } from "./data.js";
import { liveSource } from "./live.js";
import type { LiveOptions } from "./live.js";

export interface FirebaseSourceOptions extends LiveOptions {
  // The Firebase project, or an app that's already set up.
  config?: FirebaseOptions;
  app?: FirebaseApp;
  // The local emulators, as host:port, for working without the cloud.
  emulators?: { firestore: string; auth: string };
  // How people sign in, as the project block's signin says: google, the default, or github.
  signin?: "google" | "github";
}

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

export function firebaseSource(options: FirebaseSourceOptions): DataSource {
  let connected: { db: Firestore; auth: Auth } | undefined;
  let person: Person | null | undefined;
  const watchers = new Set<(person: Person | null) => void>();

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
    async signIn() {
      await signInWithPopup(connect().auth, options.signin === "github" ? new GithubAuthProvider() : new GoogleAuthProvider());
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
