// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a page says it just did, like "Undid place", or why a command was turned
// down: one thing at a time, the next replacing it, shown by a workspace's messages.

import { createContext, useContext } from "react";

export interface Said {
  text: string;
  problem?: boolean;
}

export interface Saying {
  said: Said | undefined;
  say: (said: Said | undefined) => void;
}

export const SayingContext = createContext<Saying>({ said: undefined, say: () => {} });

export function useSaying(): Saying {
  return useContext(SayingContext);
}
