// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// A data source that keeps everything in memory. Tests use it, and so does an app
// running before it has a backend. A view with no data stays loading, exactly as a
// real view would before its first document arrives.

import type { AuthSource, AuthenticationMethod, CommandInput, DataSource, Person, ViewData, ViewState, ViewStatus } from "./data.js";

export interface MemorySource extends DataSource {
  // Replaces a view's data and marks it live, telling everyone listening.
  set(view: string, data: ViewData, subject?: string): void;
  // Changes a view's status without changing its data, such as to stale.
  status(view: string, status: ViewStatus, subject?: string): void;
  // Every command run so far, in order.
  readonly runs: { command: string; input: CommandInput }[];
  // The way each sign-in used, by its id, in order.
  readonly signIns: (string | undefined)[];
}

export interface MemoryOptions {
  views?: Record<string, ViewData>;
  commands?: Record<string, (input: CommandInput) => void | Promise<void>>;
  // Gives the source sign-in, starting with this person signed in, or no one when
  // it's null. Signing in makes anyone the person "you", whichever way.
  person?: Person | null;
  // The ways it offers to sign in; one, unnamed, when it doesn't say.
  methods?: AuthenticationMethod[];
}

export function memorySource(options: MemoryOptions = {}): MemorySource {
  const key = (view: string, subject?: string) => (subject ? `${view}:${subject}` : view);
  const states = new Map<string, ViewState>();
  const listeners = new Map<string, Set<(state: ViewState) => void>>();
  const runs: { command: string; input: CommandInput }[] = [];
  const signIns: (string | undefined)[] = [];

  for (const [view, data] of Object.entries(options.views ?? {})) {
    states.set(view, { status: "live", data });
  }

  const publish = (k: string, state: ViewState) => {
    states.set(k, state);
    for (const emit of listeners.get(k) ?? []) emit(state);
  };

  let auth: AuthSource | undefined;
  if (options.person !== undefined) {
    let person = options.person;
    const watchers = new Set<(person: Person | null) => void>();
    const become = async (next: Person | null) => {
      person = next;
      for (const emit of watchers) emit(person);
    };
    auth = {
      person: () => person,
      watch(emit) {
        watchers.add(emit);
        return () => {
          watchers.delete(emit);
        };
      },
      methods: options.methods,
      signIn: (method) => {
        signIns.push(method);
        return become({ uid: "you", name: "You" });
      },
      signOut: () => become(null),
    };
  }

  return {
    runs,
    signIns,
    auth,

    subscribe(view, subject, emit) {
      const k = key(view, subject);
      if (!listeners.has(k)) listeners.set(k, new Set());
      listeners.get(k)!.add(emit);
      const current = states.get(k);
      if (current) emit(current);
      return () => {
        listeners.get(k)?.delete(emit);
      };
    },

    async run(command, input) {
      runs.push({ command, input });
      await options.commands?.[command]?.(input);
    },

    set(view, data, subject) {
      publish(key(view, subject), { status: "live", data });
    },

    status(view, status, subject) {
      const k = key(view, subject);
      publish(k, { status, data: states.get(k)?.data });
    },
  };
}
