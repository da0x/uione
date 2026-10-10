// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site/ by one. Do not edit.

import { App } from "@uione/react";
import { firebaseSource } from "@uione/react/firebase";
import { radix } from "@uione/radix";
import { SiteFooter } from "./footer";
import { about } from "./screens/about";
import { home } from "./screens/home";
import { install } from "./screens/install";
import { language } from "./screens/language";
import { mission } from "./screens/mission";
import { releases } from "./screens/releases";

// Views are read live from Firestore, and commands go to the Go backend. While
// developing, both are the local emulators. A production build uses the real
// project, with the settings the deploy writes to .env.production.
const local = {
  config: { projectId: "demo-uione", apiKey: "demo", authDomain: "demo-uione.firebaseapp.com" },
  emulators: { firestore: "localhost:8080", auth: "localhost:9099" },
};
const cloud = {
  config: {
    projectId: import.meta.env.VITE_FIREBASE_PROJECT_ID,
    apiKey: import.meta.env.VITE_FIREBASE_API_KEY,
    appId: import.meta.env.VITE_FIREBASE_APP_ID,
    authDomain: import.meta.env.VITE_FIREBASE_AUTH_DOMAIN,
  },
};
const data = firebaseSource({ ...(import.meta.env.DEV ? local : cloud), personal: ["studio::projects"] });

export const site = { name: "uione", icon: "/icon.svg", screens: [about, home, install, language, mission, releases], ui: radix, data, authentication: false, build: { version: "0.7.8", commit: import.meta.env.VITE_UIONE_COMMIT, repository: import.meta.env.VITE_UIONE_REPOSITORY }, footer: { layout: "bar" as const, content: SiteFooter } };

export default function Site() {
  return <App {...site} />;
}
