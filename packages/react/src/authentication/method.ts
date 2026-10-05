// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// A way of signing in through Firebase, like Google: what a page shows for it, the
// provider Firebase signs in with, and how to read back the credential a sign-in
// was refused with, so it can be added to the account that email already has. Each
// way is a module of its own, and an app has the ones its project names.

import type { AuthCredential, AuthProvider } from "firebase/auth";
import type { AuthenticationMethod } from "../data.js";

export interface FirebaseAuthenticationMethod extends AuthenticationMethod {
  provider: () => AuthProvider;
  credentialFrom: (refused: unknown) => AuthCredential | null;
}
