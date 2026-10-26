// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The data source of a web app whose project says storage local (decision 0015):
// everything is kept in the browser, in an IndexedDB database named for the app.
// Each command's event is appended to a log there, each file it's given is kept
// once by its SHA-256, and its views are built from the entities the events have
// left, each told to the screens showing it only when it changed. The commands and
// views themselves are what the compiler writes, on local-package.ts; this is what
// they have in common, and what one::local does for the native apps.
//
// The same app may be open in several tabs at once, each with its own source on the
// one database. Each catches up with what the others kept before it runs a command,
// taking turns through a Web Lock so two never keep a step from the same rows, and
// tells the others through a BroadcastChannel when it's kept one, so they catch up
// and show it too.

import type { CommandInput, CommandReply, DataSource, FileStore, ViewState } from "./data.js";
import { Rows, Step, failed, nowText } from "./local-package.js";
import type { Change, CommandHandler, LocalEvent, LocalPackage, Row, ViewBuilder } from "./local-package.js";

export * from "./local-package.js";

// Where a local app's log and files are kept. In a browser it's IndexedDB; a test
// keeps them in memory.
export interface LocalStore {
  // Every event kept, in the order they happened, after the first `seen`: those
  // another page has kept since this one last read.
  read(seen?: number): Promise<LocalEvent[]>;
  // Adds an event to the log, kept before it resolves.
  append(event: LocalEvent): Promise<void>;
  // Keeps a file under what names it, files/<sha-256>.
  keep(path: string, file: Blob): Promise<void>;
  file(path: string): Promise<Blob | undefined>;
  // Every kept file but those named let go of, as nothing names them any more; how
  // many.
  forgetFilesBut(named: Set<string>): Promise<number>;
}

// A store that keeps nothing once the page is gone, for tests.
export function memoryStore(): LocalStore & { readonly events: LocalEvent[]; readonly files: Map<string, Blob> } {
  const events: LocalEvent[] = [];
  const files = new Map<string, Blob>();
  return {
    events,
    files,
    read: async (seen = 0) => structuredClone(events.slice(seen)),
    async append(event) {
      events.push(structuredClone(event));
    },
    async keep(path, file) {
      files.set(path, file);
    },
    file: async (path) => files.get(path),
    async forgetFilesBut(named) {
      let forgotten = 0;
      for (const path of [...files.keys()]) {
        if (named.has(path)) continue;
        files.delete(path);
        forgotten++;
      }
      return forgotten;
    },
  };
}

// The browser's own store: a database named for the app, with the log in events, in
// the order they were added, and each file in files, by what names it.
export function indexedStore(name: string): LocalStore {
  let opening: Promise<IDBDatabase> | undefined;
  const open = () =>
    (opening ??= new Promise<IDBDatabase>((done, failed) => {
      const request = indexedDB.open(name, 1);
      request.onupgradeneeded = () => {
        request.result.createObjectStore("events", { autoIncrement: true });
        request.result.createObjectStore("files");
      };
      request.onsuccess = () => done(request.result);
      request.onerror = () => failed(new Error(`this browser won't keep ${name}'s data: ${request.error?.message ?? "it refused"}`));
    }));
  // One transaction, resolved with what its request found once it's done, when
  // what it wrote is kept.
  const transact = async <T>(store: string, mode: IDBTransactionMode, work: (s: IDBObjectStore) => IDBRequest<T> | void): Promise<T> => {
    const db = await open();
    return new Promise<T>((done, failed) => {
      const t = db.transaction(store, mode, { durability: "strict" });
      const request = work(t.objectStore(store));
      t.oncomplete = () => done(request?.result as T);
      t.onerror = () => failed(new Error(`${name}'s data couldn't be kept: ${t.error?.message ?? "the browser refused"}`));
      t.onabort = t.onerror;
    });
  };
  // Those after the first seen, stepped over without reading them.
  const after = async (seen: number) => {
    const db = await open();
    return new Promise<LocalEvent[]>((done, failed) => {
      const out: LocalEvent[] = [];
      const t = db.transaction("events", "readonly");
      const request = t.objectStore("events").openCursor();
      let skipped = false;
      request.onsuccess = () => {
        const cursor = request.result;
        if (!cursor) return;
        if (!skipped) {
          skipped = true;
          cursor.advance(seen);
          return;
        }
        out.push(cursor.value as LocalEvent);
        cursor.continue();
      };
      t.oncomplete = () => done(out);
      t.onerror = () => failed(new Error(`${name}'s data couldn't be read: ${t.error?.message ?? "the browser refused"}`));
      t.onabort = t.onerror;
    });
  };
  return {
    read: (seen = 0) => (seen === 0 ? transact<LocalEvent[]>("events", "readonly", (s) => s.getAll()) : after(seen)),
    append: (event) => transact("events", "readwrite", (s) => void s.add(event)),
    keep: (path, file) => transact("files", "readwrite", (s) => void s.put(file, path)),
    file: (path) => transact<Blob | undefined>("files", "readonly", (s) => s.get(path)),
    async forgetFilesBut(named) {
      const kept = await transact<IDBValidKey[]>("files", "readonly", (s) => s.getAllKeys());
      const gone = kept.map(String).filter((path) => !named.has(path));
      if (gone.length > 0) await transact("files", "readwrite", (s) => gone.forEach((path) => s.delete(path)));
      return gone.length;
    },
  };
}

export interface LocalOptions {
  // The app's name, which names its database.
  name: string;
  // Its namespaces' entities, commands and views, as the compiler writes them.
  packages: LocalPackage[];
  // Where it's kept, when not in the browser's IndexedDB, like memoryStore in a test.
  store?: LocalStore;
}

export interface LocalSource extends DataSource {
  files: FileStore;
  // Resolves once the log is read through, or rejects with why it couldn't be.
  readonly opened: Promise<void>;
  // The entities as the events so far have left them.
  rows(): Rows;
}

// How to put one row back as it was: its fields that changed, their values before
// (null where a field wasn't there); or removed, when it was made; or all of it,
// when it was removed.
interface PuttingBack {
  entity: string;
  id: string;
  how: "fields" | "removing" | "making";
  was: Row;
}

interface HistoryStep {
  command: string;
  back: PuttingBack[];
}

interface Listener {
  view: string;
  subject: string;
  emit: (state: ViewState) => void;
  last: string; // what it was told last, as JSON, so it's told only of a change
}

// The source. Commands run one at a time, in the order they're asked: each sees what
// the one before it left. Besides its own commands it has two: one::undo takes back
// the last step taken while the page is open, and one::redo takes it again, each
// answering with the command it undid or redid as its id, and each kept as an event
// like any other. A step is a command, unless the command's fields say
// "one:history": "joined", which makes it part of the step before, as one action
// that runs several does; or "apart", which leaves it out of the history, as
// bookkeeping like a preview is. Undoing an update puts back only the fields it
// changed, so a change made apart since stays.
export function localSource(options: LocalOptions): LocalSource {
  const store = options.store ?? indexedStore(options.name);
  const rows = new Rows();
  const commands = new Map<string, CommandHandler>();
  const views = new Map<string, ViewBuilder>();
  const listeners = new Set<Listener>();
  const undoing: HistoryStep[] = [];
  const redoing: HistoryStep[] = [];
  const addresses = new Map<string, string>();
  const locks = (globalThis.navigator as Navigator | undefined)?.locks;
  // Told to the other tabs whenever a step is kept here.
  const others = typeof BroadcastChannel === "function" ? new BroadcastChannel(`${options.name} kept`) : undefined;
  (others as { unref?: () => void } | undefined)?.unref?.(); // a test's page doesn't stay open for it
  let seen = 0; // how many events of the log are in rows
  let open = false;
  let broken: Error | undefined;

  // Commands that stand apart from undo, as if each were sent "one:history": "apart".
  const apart = new Set<string>();
  for (const p of options.packages) {
    p.install({
      on: (name, run, standsApart) => {
        commands.set(name, run);
        if (standsApart) apart.add(name);
      },
      define: (name, build) => views.set(name, build),
    });
  }

  const built = (view: string, subject: string): ViewState => {
    const build = views.get(view);
    if (!build) return { status: "denied", data: undefined };
    // What a view is per that's gone reads as a document that isn't there.
    return { status: "live", data: build(rows, subject) ?? {} };
  };

  const tell = (l: Listener) => {
    if (!open) return;
    const state = built(l.view, l.subject);
    const seen = state.status + JSON.stringify(state.data);
    if (seen === l.last) return;
    l.last = seen;
    l.emit(state);
  };

  // A listener may stop listening, or another start, as it's told.
  const tellAll = () => [...listeners].forEach((l) => listeners.has(l) && tell(l));

  // What other tabs have kept since this one last read, and so what they show.
  const catchUp = async () => {
    const kept = await store.read(seen);
    for (const e of kept) rows.apply(e);
    seen += kept.length;
    if (kept.length > 0) tellAll();
  };

  // Work that reads the log and adds to it, while no other tab does.
  const alone = <T>(work: () => Promise<T>): Promise<T> => (locks ? (locks.request(`${options.name} log`, work) as Promise<T>) : work());

  const opened = (async () => {
    try {
      const kept = await store.read();
      for (const e of kept) rows.apply(e);
      seen = kept.length;
      // What nothing names any more, like a picture deleted or a preview drawn again,
      // goes, now, while there's no undoing to bring it back: unless another tab is
      // open, whose undoing could, or which has kept a file it hasn't named yet.
      // Each tab holds its share of a lock while it's open, so this one's the only
      // one when it can have the whole of it.
      if (!locks) await store.forgetFilesBut(rows.filesNamed());
      else {
        const tabs = `${options.name} tabs`;
        await locks.request(tabs, { ifAvailable: true }, (only) => (only ? store.forgetFilesBut(rows.filesNamed()) : undefined));
        void locks.request(tabs, { mode: "shared" }, () => new Promise<void>(() => {}));
      }
      open = true;
      tellAll();
    } catch (error) {
      broken = error instanceof Error ? error : new Error(String(error));
      for (const l of listeners) l.emit({ status: "denied", data: undefined });
      throw broken;
    }
  })();
  // Whoever doesn't wait for it hears of it as a view denied or a command refused.
  opened.catch(() => {});

  // Each row a change touches, as it is before the change: what puts it back.
  const backFrom = (changes: Change[]): PuttingBack[] => {
    const back: PuttingBack[] = [];
    for (const c of changes) {
      const before = rows.find(c.entity, c.id);
      if (!before) {
        if (!c.removed) back.push({ entity: c.entity, id: c.id, how: "removing", was: {} });
        continue;
      }
      if (c.removed) {
        back.push({ entity: c.entity, id: c.id, how: "making", was: structuredClone(before) });
        continue;
      }
      const was: Row = {};
      const now = c.fields ?? {};
      for (const [name, value] of Object.entries(now)) {
        if (JSON.stringify(before[name] ?? null) !== JSON.stringify(value)) was[name] = structuredClone(before[name] ?? null);
      }
      for (const [name, value] of Object.entries(before)) if (!(name in now)) was[name] = structuredClone(value);
      if (Object.keys(was).length > 0) back.push({ entity: c.entity, id: c.id, how: "fields", was });
    }
    return back.reverse();
  };

  // What puts rows back as they were, as changes to the rows as they are now.
  const changesFor = (back: PuttingBack[]): Change[] => {
    const out: Change[] = [];
    const left = new Map<string, Row | null>(); // as each is left by those before it
    const key = (b: PuttingBack) => `${b.entity}\u0000${b.id}`;
    const rowOf = (b: PuttingBack): Row | null => {
      if (left.has(key(b))) return left.get(key(b))!;
      const now = rows.find(b.entity, b.id);
      return now ? structuredClone(now) : null;
    };
    for (const b of back) {
      if (b.how === "removing") {
        out.push({ entity: b.entity, id: b.id, removed: true });
        left.set(key(b), null);
        continue;
      }
      const r = b.how === "making" ? structuredClone(b.was) : rowOf(b);
      if (r === null) continue; // gone since, so there's nothing to put back
      if (b.how === "fields") {
        for (const [name, was] of Object.entries(b.was)) {
          if (was === null) delete r[name];
          else r[name] = structuredClone(was);
        }
      }
      r["id"] = b.id;
      out.push({ entity: b.entity, id: b.id, fields: r });
      left.set(key(b), r);
    }
    return out;
  };

  // The last step of one history put back, kept as an event, and what puts it back
  // again onto the other.
  const takeBack = async (from: HistoryStep[], onto: HistoryStep[], name: string): Promise<CommandReply> => {
    const taken = from.pop();
    if (!taken) throw new Error(`There's nothing to ${name}.`);
    const changes = changesFor(taken.back);
    const back = backFrom(changes);
    const e: LocalEvent = { command: `one::${name}`, at: nowText(), changes };
    try {
      await store.append(e);
    } catch (error) {
      from.push(taken);
      throw error;
    }
    rows.apply(e);
    seen++;
    others?.postMessage(null);
    onto.push({ command: taken.command, back });
    tellAll();
    return { id: taken.command };
  };

  const runNow = async (command: string, input: CommandInput): Promise<CommandReply> => {
    await opened;
    if (command === "one::undo") return takeBack(undoing, redoing, "undo");
    if (command === "one::redo") return takeBack(redoing, undoing, "redo");
    const handler = commands.get(command);
    if (!handler) throw new Error(`there's no command ${command}`);
    // The history's word on it is the source's, not the command's.
    const sent = { ...(input as Row) };
    const history = sent["one:history"] ?? (apart.has(command) ? "apart" : undefined);
    delete sent["one:history"];
    const changes = new Step(rows);
    const answer = handler(structuredClone(sent) as Row, changes);
    if (failed(answer)) throw new Error(answer.error);
    if (changes.changes.length === 0) return { id: answer.id };
    const back = backFrom(changes.changes);
    const e: LocalEvent = { command, at: nowText(), changes: changes.changes };
    await store.append(e); // nothing changed when it fails, since nothing was kept
    rows.apply(e);
    seen++;
    others?.postMessage(null);
    if (history === "joined" && undoing.length > 0) {
      // Taken back before what was already in the step, since it came after.
      undoing[undoing.length - 1]!.back.unshift(...back);
    } else if (history !== "apart") {
      undoing.push({ command, back });
      redoing.length = 0;
    }
    tellAll();
    return { id: answer.id };
  };

  let queue: Promise<unknown> = Promise.resolve();
  const inTurn = <T>(work: () => Promise<T>): Promise<T> => {
    const done = queue.then(work);
    queue = done.catch(() => {});
    return done;
  };

  if (others) others.onmessage = () => void inTurn(() => opened.then(catchUp)).catch(() => {});

  const files: FileStore = {
    async upload(file, progress) {
      const bytes = await read(file);
      const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
      const path = `files/${[...digest].map((b) => b.toString(16).padStart(2, "0")).join("")}`;
      await opened;
      await store.keep(path, file);
      progress(1);
      return path;
    },
    async address(path) {
      const known = addresses.get(path);
      if (known) return known;
      const blob = await store.file(path);
      if (!blob) throw new Error(`${path} isn't kept here`);
      const url = URL.createObjectURL(blob);
      addresses.set(path, url);
      return url;
    },
  };

  return {
    opened,
    files,
    rows: () => rows,

    subscribe(view, subject, emit) {
      const l: Listener = { view, subject: subject ?? "", emit, last: "" };
      listeners.add(l);
      if (broken) emit({ status: "denied", data: undefined });
      else if (open) tell(l);
      else emit({ status: "loading", data: undefined });
      return () => {
        listeners.delete(l);
      };
    },

    run: (command, input) =>
      inTurn(() =>
        alone(async () => {
          await opened;
          await catchUp();
          return runNow(command, input);
        }),
      ),
  };
}

// A file's bytes, where a Blob can't say them itself.
function read(file: Blob): Promise<ArrayBuffer> {
  if (typeof file.arrayBuffer === "function") return file.arrayBuffer();
  return new Promise((done, failed) => {
    const reader = new FileReader();
    reader.onload = () => done(reader.result as ArrayBuffer);
    reader.onerror = () => failed(reader.error);
    reader.readAsArrayBuffer(file);
  });
}
