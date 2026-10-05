// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Signing in with a Microsoft account, as authentication microsoft says: a personal
// one, like Outlook.com's, or one from work or school, since Firebase asks
// Microsoft's common sign-in, which takes both.

import { OAuthProvider } from "firebase/auth";
import type { FirebaseError } from "firebase/app";
import type { FirebaseAuthenticationMethod } from "./method.js";

// Microsoft's four squares.
function MicrosoftMark() {
  return (
    <svg viewBox="0 0 21 21" width="18" height="18" aria-hidden="true">
      <rect x="1" y="1" width="9" height="9" fill="#F25022" />
      <rect x="11" y="1" width="9" height="9" fill="#7FBA00" />
      <rect x="1" y="11" width="9" height="9" fill="#00A4EF" />
      <rect x="11" y="11" width="9" height="9" fill="#FFB900" />
    </svg>
  );
}

export const microsoft: FirebaseAuthenticationMethod = {
  id: "microsoft",
  name: "Microsoft",
  Mark: MicrosoftMark,
  provider: () => {
    const provider = new OAuthProvider("microsoft.com");
    provider.setCustomParameters({ prompt: "select_account" });
    return provider;
  },
  credentialFrom: (refused) => OAuthProvider.credentialFromError(refused as FirebaseError),
};
