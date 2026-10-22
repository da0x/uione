// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

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
import type { ComponentType, ReactNode } from "react";
import { partsOf } from "./keys.js";

export type ViewStatus = "loading" | "live" | "stale" | "denied";

export type ViewData = Record<string, unknown>;

export interface ViewState {
  status: ViewStatus;
  data: ViewData | undefined;
}

export type CommandInput = Record<string, unknown>;

// What a command answers: the id of what it changed, and what it shows only once,
// like a service's new key, which is never kept anywhere a screen could read again.
export interface CommandReply {
  id?: string;
  key?: string;
  code?: string; // a code for a program on the person's computer, from authorize
}

export interface DataSource {
  // Starts listening to a view and calls emit with every new state. Returns the
  // function that stops listening.
  subscribe(view: string, subject: string | undefined, emit: (state: ViewState) => void): () => void;
  // Runs a command. It rejects with a readable message when the command fails.
  run(command: string, input: CommandInput): Promise<CommandReply | void>;
  // Who's signed in, for a source that has sign-in.
  auth?: AuthSource;
  // Files, for an app with a file field.
  files?: FileStore;
  // The rows of a view's list holding every word, as the list shows them, found by
  // the server in fields marked searchable, which the rows don't hold. It rejects
  // with a readable message when it can't.
  search?(view: string, subject: string | undefined, list: string, words: string): Promise<Record<string, unknown>[]>;
  // A page of everyone who has signed up, after the person named, for a role given
  // user::view, or denied for anyone else. It rejects with a readable message when
  // it can't.
  people?(view: string, after: string): Promise<PeoplePage | "denied">;
}

// A page of people, and the person the next page starts after, or "" at the end.
export interface PeoplePage {
  rows: Record<string, unknown>[];
  next: string;
}

// Where an app's files go. A file is uploaded on its own, then a command puts its
// path in a field, which attaches it to that record.
export interface FileStore {
  // Uploads a file, saying how far it's come, from 0 to 1, and gives its path. It
  // rejects with a readable message when it can't.
  upload(file: File, progress: (done: number) => void): Promise<string>;
  // Somewhere to read a file from for a few minutes, for whoever may read its
  // record; with download, a browser saves it rather than showing it.
  address(path: string, download?: boolean): Promise<string>;
}

// A file's name: the last part of its path.
export function fileName(path: string): string {
  return path.slice(path.lastIndexOf("/") + 1);
}

// A signed-in person. uid is the id their per-person views are stored under.
export interface Person {
  uid: string;
  name: string;
}

// A way of signing in, as a page offers it: by its name, like Google, and its mark.
export interface AuthenticationMethod {
  id: string; // as the project names it: google, github or microsoft
  name: string;
  Mark?: ComponentType;
}

// Sign-in. Until the source knows whether anyone is signed in, person is undefined;
// once it knows, it's the person or null.
export interface AuthSource {
  person(): Person | null | undefined;
  // The ways people sign in, in the order the project names them. With none, or
  // one, signing in starts at once; with more, the person chooses.
  methods?: AuthenticationMethod[];
  // Calls emit whenever the signed-in person changes. Returns the function that
  // stops listening.
  watch(emit: (person: Person | null) => void): () => void;
  // Signs in the given way, by its id, or the only one.
  signIn(method?: string): Promise<void>;
  signOut(): Promise<void>;
}

const DataContext = createContext<DataSource | null>(null);

// A key a command's answer showed, until the person closes it; then it's gone.
interface ShownOnce {
  key: string | undefined;
  show: (key: string | undefined) => void;
}

const ShownContext = createContext<ShownOnce>({ key: undefined, show: () => {} });

export function DataProvider({ source, children }: { source: DataSource; children: ReactNode }) {
  const [key, show] = useState<string>();
  return (
    <DataContext.Provider value={source}>
      <ShownContext.Provider value={{ key, show }}>{children}</ShownContext.Provider>
    </DataContext.Provider>
  );
}

// The key a command's answer is showing, and how to stop showing it.
export function useShownOnce(): ShownOnce {
  return useContext(ShownContext);
}

// Where the app's files go, when it keeps any.
export function useFiles(): FileStore | undefined {
  return useContext(DataContext)?.files;
}

// How to search a view's list by its words, when the source can. A source's search
// reads nothing of this, so it's the same function from one render to the next.
export function useSearch(): DataSource["search"] {
  return useContext(DataContext)?.search;
}

function useSource(): DataSource {
  const source = useContext(DataContext);
  if (!source) throw new Error("uione: views and commands need an App with a data source");
  return source;
}

const loading: ViewState = { status: "loading", data: undefined };

// What each view last was, for each data source, so a screen opened again, like a
// page's header on each of its tabs, starts from what it showed rather than from
// nothing, and the view's next state replaces it. It's kept for whoever's signed in
// at the time, so what one person saw never shows once someone else signs in, and
// only the latest few views are kept.
const remembered = new WeakMap<DataSource, Map<string, ViewState>>();
const remembers = 64;

function memoryOf(source: DataSource): Map<string, ViewState> {
  let memory = remembered.get(source);
  if (!memory) remembered.set(source, (memory = new Map()));
  return memory;
}

function remember(source: DataSource, key: string, state: ViewState) {
  const memory = memoryOf(source);
  memory.delete(key);
  if (state.status !== "live") return;
  memory.set(key, state);
  if (memory.size > remembers) memory.delete(memory.keys().next().value!);
}

// Everyone who has signed up, as a view whose rows come a page at a time, and more,
// which asks for the next page while there is one. It's read when the screen opens,
// not kept live, since it changes whenever anyone does anything.
export function usePeople(view: string): ViewState & { more?: () => void } {
  const source = useSource();
  const [state, setState] = useState<{ status: ViewStatus; rows: Record<string, unknown>[]; next: string; asking: boolean }>({
    status: "loading",
    rows: [],
    next: "",
    asking: true,
  });
  const ask = useCallback(
    (after: string, earlier: Record<string, unknown>[], gone: () => boolean = () => false) => {
      if (!source.people) {
        setState({ status: "denied", rows: [], next: "", asking: false });
        return;
      }
      source.people(view, after).then(
        (page) => {
          if (gone()) return;
          if (page === "denied") setState({ status: "denied", rows: [], next: "", asking: false });
          else setState({ status: "live", rows: [...earlier, ...page.rows], next: page.next, asking: false });
        },
        () => {
          if (!gone()) setState((was) => ({ ...was, status: "stale", asking: false }));
        },
      );
    },
    [source, view],
  );
  useEffect(() => {
    let gone = false;
    setState({ status: "loading", rows: [], next: "", asking: true });
    ask("", [], () => gone);
    return () => {
      gone = true;
    };
  }, [ask]);
  const { status, rows, next, asking } = state;
  const more =
    next !== "" && !asking
      ? () => {
          setState((was) => ({ ...was, asking: true }));
          ask(next, rows);
        }
      : undefined;
  return { status, data: status === "loading" || status === "denied" ? undefined : { rows }, ...(more ? { more } : {}) };
}

// The live state of a view. `subject` picks one document of a view that has one per
// entity or per person, like a member's loans.
//
// The document of something that moved, as a board does when it's renamed, says
// only where it went: the view follows it there, and the address changes as the
// name did, so an old link opens the board under its new name.
export function useView(view: string, subject?: string): ViewState {
  const source = useSource();
  const [moved, setMoved] = useState<{ from: string; to: string }>();
  const followed = subject !== undefined && moved?.from === subject ? moved.to : subject;
  // The state remembers which view it's for, so the render right after switching,
  // say from one member's loans to another's, never shows the old one's rows.
  const reader = source.auth?.person()?.uid ?? "";
  const key = `${reader}:${view}:${followed ?? ""}`;
  const start = () => memoryOf(source).get(key) ?? loading;
  const [state, setState] = useState<{ key: string; view: ViewState }>(() => ({ key, view: start() }));
  useEffect(() => {
    setState({ key, view: start() });
    return source.subscribe(view, followed, (next) => {
      const to = next.data?.moved;
      if (typeof to === "string" && to !== "" && subject !== undefined && followed !== undefined) {
        readdress(followed, to);
        setMoved({ from: subject, to });
        return;
      }
      remember(source, key, next);
      setState({ key, view: next });
    });
  }, [source, view, subject, followed, key]);
  return state.key === key ? state.view : start();
}

// The page's address with what named something that moved changed to what names it
// now: /da0x/boards/sprint becomes /da0x/boards/sprint_2 once the board is renamed.
// An id is made of parts, some of which are the address's own parts; those that
// changed are swapped where they are.
export function readdress(from: string, to: string): void {
  if (typeof window === "undefined") return;
  const { pathname, search, hash } = window.location;
  const segments = pathname.split("/");
  const decoded = segments.map((segment) => {
    try {
      return decodeURIComponent(segment);
    } catch {
      return segment;
    }
  });
  for (let count = segments.length; count >= 1; count--) {
    const before = partsOf(from, count);
    const after = partsOf(to, count);
    if (!before || !after) continue;
    const swaps = before.map((part, i) => [part, after[i]!] as const).filter(([was, is]) => was !== is);
    if (swaps.length === 0 || !swaps.every(([was]) => decoded.includes(was))) continue;
    const next = decoded.map((segment, i) => {
      const swap = swaps.find(([was]) => was === segment);
      return swap ? encodeURIComponent(swap[1]) : segments[i];
    });
    window.history.replaceState(window.history.state, "", next.join("/") + search + hash);
    window.dispatchEvent(new PopStateEvent("popstate", { state: window.history.state }));
    return;
  }
}

export interface AuthState {
  person: Person | null | undefined;
  methods: AuthenticationMethod[];
  signIn: (method?: string) => Promise<void>;
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
  return { person, methods: auth.methods ?? [], signIn: (method) => auth.signIn(method), signOut: () => auth.signOut() };
}

// Runs commands by name, and keeps track of which are running and which failed. One
// runner can serve many commands, like the actions on every row of a table.
export interface Runner {
  run: (command: string, input?: CommandInput) => Promise<boolean>;
  busy: (command: string) => boolean;
  error: (command: string) => string | undefined;
  // Forgets why a command last failed, once that no longer describes what's on screen.
  clear: (command: string) => void;
}

export function useRunner(): Runner {
  const source = useSource();
  const { show } = useContext(ShownContext);
  const [running, setRunning] = useState<ReadonlySet<string>>(new Set());
  const [errors, setErrors] = useState<Readonly<Record<string, string>>>({});
  const clear = useCallback((command: string) => {
    setErrors((current) => {
      if (!Object.hasOwn(current, command)) return current;
      const next = { ...current };
      delete next[command];
      return next;
    });
  }, []);
  const run = useCallback(
    async (command: string, input: CommandInput = {}) => {
      setRunning((current) => new Set(current).add(command));
      clear(command);
      try {
        const reply = await source.run(command, input);
        if (reply?.key) show(reply.key);
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
    [source, clear, show],
  );
  return { run, busy: (command) => running.has(command), error: (command) => errors[command], clear };
}

// Gives a program on this computer, like the app's command, a code that signs it in
// as the person signed in here, made for the program's challenge.
export function useAuthorize(): (challenge: string) => Promise<string> {
  const source = useSource();
  return useCallback(
    async (challenge: string) => {
      const reply = await source.run("authorize", { challenge });
      if (!reply?.code) throw new Error("the server didn't give a code; try again");
      return reply.code;
    },
    [source],
  );
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
