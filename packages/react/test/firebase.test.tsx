// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { beforeEach, describe, expect, it, vi } from "vitest";

// Firebase itself is replaced: a popup that signs in, or refuses as Firebase does
// when the email already has an account another way.
const popup = vi.fn();
const link = vi.fn(async () => ({}));
vi.mock("firebase/app", () => ({ initializeApp: () => ({}) }));
vi.mock("firebase/firestore", () => ({ Timestamp: class {}, connectFirestoreEmulator: () => {}, doc: () => ({}), getFirestore: () => ({}), onSnapshot: () => () => {} }));
vi.mock("firebase/analytics", () => ({}));
vi.mock("firebase/auth", () => {
  class Provider {
    constructor(public providerId: string) {}
    setCustomParameters() {}
  }
  const refused = (id: string) => (e: { credential?: string }) => (e.credential ? { providerId: id, token: e.credential } : null);
  return {
    GoogleAuthProvider: Object.assign(class extends Provider { constructor() { super("google.com"); } }, { credentialFromError: refused("google.com") }),
    GithubAuthProvider: Object.assign(class extends Provider { constructor() { super("github.com"); } }, { credentialFromError: refused("github.com") }),
    OAuthProvider: Object.assign(class extends Provider {}, { credentialFromError: refused("microsoft.com") }),
    connectAuthEmulator: () => {},
    getAuth: () => ({}),
    onAuthStateChanged: () => () => {},
    signInWithPopup: (...args: unknown[]) => popup(...args),
    linkWithCredential: (...args: unknown[]) => (link as (...a: unknown[]) => unknown)(...args),
    signOut: async () => {},
  };
});

const { firebaseSource, github, google, microsoft } = await import("../src/firebase.js");

describe("signing in with Firebase", () => {
  beforeEach(() => {
    popup.mockReset();
    link.mockClear();
  });

  it("offers the ways the project names, in its order, and signs in with each one's provider", async () => {
    const source = firebaseSource({ config: {}, authentication: [github, microsoft] });
    expect(source.auth!.methods!.map((m) => m.name)).toEqual(["GitHub", "Microsoft"]);
    popup.mockResolvedValue({ user: { uid: "ada" } });
    await source.auth!.signIn("microsoft");
    expect((popup.mock.calls[0]![1] as { providerId: string }).providerId).toBe("microsoft.com");
    await source.auth!.signIn();
    expect((popup.mock.calls[1]![1] as { providerId: string }).providerId).toBe("github.com");
  });

  it("is Google when the project doesn't say", () => {
    expect(firebaseSource({ config: {} }).auth!.methods!.map((m) => m.id)).toEqual(["google"]);
  });

  it("adds a way refused for an email that has an account, once the person signs in the way they did before", async () => {
    const source = firebaseSource({ config: {}, authentication: [google, github] });
    popup.mockRejectedValueOnce({ code: "auth/account-exists-with-different-credential", credential: "from-github" });
    await expect(source.auth!.signIn("github")).rejects.toMatchObject({ code: "auth/account-exists-with-different-credential" });
    expect(link).not.toHaveBeenCalled();
    popup.mockResolvedValueOnce({ user: { uid: "ada" } });
    await source.auth!.signIn("google");
    expect(link).toHaveBeenCalledWith({ uid: "ada" }, { providerId: "github.com", token: "from-github" });
    // Only once: it's been added.
    popup.mockResolvedValueOnce({ user: { uid: "ada" } });
    await source.auth!.signIn("google");
    expect(link).toHaveBeenCalledTimes(1);
  });
});
