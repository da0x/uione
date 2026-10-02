// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// How screens read and write. A screen reads views and runs commands, and never talks
// to a backend directly: a DataSource does that. The source decides where data
// comes from, which is Firebase in a real app and memory in tests.
//
// A view is always in one of four states, and screens have to be honest about which:
//   loading  nothing has arrived yet
//   live     the data is current, and changes will keep arriving
//   stale    the data was current, but the connection has been lost for now
//   denied   the person isn't allowed to see it

import { createContext, useCallback, useContext, useEffect, useState } from "react";
import type { ReactNode } from "react";

export type ViewStatus = "loading" | "live" | "stale" | "denied";

export type ViewData = Record<string, unknown>;

export interface ViewState {
  status: ViewStatus;
  data: ViewData | undefined;
}

export type CommandInput = Record<string, unknown>;

export interface DataSource {
  // Starts listening to a view and calls emit with every new state. Returns the
  // function that stops listening.
  subscribe(view: string, subject: string | undefined, emit: (state: ViewState) => void): () => void;
  // Runs a command. It rejects with a readable message when the command fails.
  run(command: string, input: CommandInput): Promise<void>;
  // Who's signed in, for a source that has sign-in.
  auth?: AuthSource;
}

// A signed-in person. uid is the id their per-person views are stored under.
export interface Person {
  uid: string;
  name: string;
}

// Sign-in. Until the source knows whether anyone is signed in, person is undefined;
// once it knows, it's the person or null.
export interface AuthSource {
  person(): Person | null | undefined;
  // Calls emit whenever the signed-in person changes. Returns the function that
  // stops listening.
  watch(emit: (person: Person | null) => void): () => void;
  signIn(): Promise<void>;
  signOut(): Promise<void>;
}

const DataContext = createContext<DataSource | null>(null);

export function DataProvider({ source, children }: { source: DataSource; children: ReactNode }) {
  return <DataContext.Provider value={source}>{children}</DataContext.Provider>;
}

function useSource(): DataSource {
  const source = useContext(DataContext);
  if (!source) throw new Error("uione: views and commands need an App with a data source");
  return source;
}

const loading: ViewState = { status: "loading", data: undefined };

// The live state of a view. `subject` picks one document of a view that has one per
// entity or per person, like a member's loans.
export function useView(view: string, subject?: string): ViewState {
  const source = useSource();
  // The state remembers which view it's for, so the render right after switching,
  // say from one member's loans to another's, shows loading rather than the old rows.
  const key = `${view}:${subject ?? ""}`;
  const [state, setState] = useState<{ key: string; view: ViewState }>({ key, view: loading });
  useEffect(() => {
    setState({ key, view: loading });
    return source.subscribe(view, subject, (next) => setState({ key, view: next }));
  }, [source, view, subject, key]);
  return state.key === key ? state.view : loading;
}

export interface AuthState {
  person: Person | null | undefined;
  signIn: () => Promise<void>;
  signOut: () => Promise<void>;
}

// Who's signed in, kept current. It's undefined when the data source has no sign-in.
export function useAuth(): AuthState | undefined {
  const { auth } = useSource();
  const [person, setPerson] = useState(() => auth?.person());
  useEffect(() => {
    if (!auth) return;
    setPerson(auth.person());
    return auth.watch(setPerson);
  }, [auth]);
  if (!auth) return undefined;
  return { person, signIn: () => auth.signIn(), signOut: () => auth.signOut() };
}

// Runs commands by name, and keeps track of which are running and which failed. One
// runner can serve many commands, like the actions on every row of a table.
export interface Runner {
  run: (command: string, input?: CommandInput) => Promise<boolean>;
  busy: (command: string) => boolean;
  error: (command: string) => string | undefined;
}

export function useRunner(): Runner {
  const source = useSource();
  const [running, setRunning] = useState<ReadonlySet<string>>(new Set());
  const [errors, setErrors] = useState<Readonly<Record<string, string>>>({});
  const run = useCallback(
    async (command: string, input: CommandInput = {}) => {
      setRunning((current) => new Set(current).add(command));
      setErrors((current) => {
        const next = { ...current };
        delete next[command];
        return next;
      });
      try {
        await source.run(command, input);
        return true;
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        setErrors((current) => ({ ...current, [command]: message }));
        return false;
      } finally {
        setRunning((current) => {
          const next = new Set(current);
          next.delete(command);
          return next;
        });
      }
    },
    [source],
  );
  return { run, busy: (command) => running.has(command), error: (command) => errors[command] };
}

export interface CommandState {
  run: (input?: CommandInput) => Promise<boolean>;
  busy: boolean;
  error: string | undefined;
}

// One command, ready to run. run() resolves to whether it succeeded; when it fails,
// error holds the message to show.
export function useCommand(command: string): CommandState {
  const runner = useRunner();
  return {
    run: (input) => runner.run(command, input),
    busy: runner.busy(command),
    error: runner.error(command),
  };
}
