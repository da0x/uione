// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// A data source for a real backend, with the rules that keep a screen honest about
// how current its data is. It doesn't know about Firebase: it's given a Backend that
// can watch one document, and firebase.ts gives it one. That keeps these rules in
// one place, and lets the tests drive them with a backend that fails on demand.
//
// The rules:
//   - A view is live while the server keeps it current. When the connection drops,
//     it's stale, and it keeps what it last showed.
//   - A listener that fails is retried forever, waiting a little longer each time,
//     up to a limit. The wait is jittered, so many pages don't all retry at once, and
//     it goes back to short only when data actually arrives.
//   - A permission error means the view is denied. It's tried again when the
//     signed-in person changes, since they may be allowed now.
//   - After a minute in a hidden tab, a view stops listening, so a forgotten tab
//     costs nothing. It starts again the moment the tab is shown.

import type { AuthSource, CommandInput, DataSource, ViewData, ViewState } from "./data.js";

export interface Backend {
  // Watches the document at path. next gets its data, which is undefined when the
  // document doesn't exist yet, and whether that data came from the server rather
  // than from what was saved while offline. After fail, the watch has stopped.
  watch(
    path: string,
    next: (data: ViewData | undefined, current: boolean) => void,
    fail: (denied: boolean) => void,
  ): () => void;
  auth: AuthSource;
  // The signed-in person's ID token, sent with every command.
  token(): Promise<string | undefined>;
}

export interface LiveOptions {
  // Views with one document per person. They're read at views/<view>:<uid> for
  // whoever is signed in, so a screen never has to say whose it wants.
  personal?: string[];
  // Where commands are sent. waitlist::signup::create goes to /api/waitlist/signup/create.
  api?: string;
  // The first wait before a retry, and the longest, in milliseconds.
  retry?: { first: number; most: number };
  // How long a view keeps listening in a hidden tab, in milliseconds.
  dormant?: number;
  fetch?: typeof fetch;
}

// A wait before the next retry: doubling from first, never more than most, and
// somewhere between half and all of that, so retries spread out.
export function backoff(attempt: number, first: number, most: number): number {
  const full = Math.min(most, first * 2 ** attempt);
  return full / 2 + (Math.random() * full) / 2;
}

// A subject becomes part of a document's path, and the router decodes %2F in an
// address, so one taken from the address could name a different document, or a path
// Firestore refuses by throwing.
export function safeSubject(subject: string): boolean {
  if (subject === "" || subject === "." || subject === ".." || subject.includes("/")) return false;
  return !(subject.startsWith("__") && subject.endsWith("__"));
}

function uidOf(auth: AuthSource): string | null | undefined {
  const person = auth.person();
  return person === undefined ? undefined : (person?.uid ?? null);
}

export function liveSource(backend: Backend, options: LiveOptions = {}): DataSource {
  const { first, most } = options.retry ?? { first: 1000, most: 30000 };
  const dormantAfter = options.dormant ?? 60000;
  const api = options.api ?? "/api";
  const page = typeof document === "undefined" ? undefined : document;

  // Once someone's signed in, the backend is told, once for each person each time
  // the app opens: it does what signing in brings about, like joining the projects
  // an invitation to their email asked them to.
  // It starts with the first view opened, which connects anyway, so making the
  // source, as a page rendered ahead of time does, connects to nothing.
  let told: string | undefined;
  let telling = false;
  const tell = () => {
    if (telling) return;
    telling = true;
    backend.auth.watch((person) => {
      if (!person || told === person.uid) return;
      told = person.uid;
      void (async () => {
        const token = await backend.token().catch(() => undefined);
        if (!token) return;
        await (options.fetch ?? fetch)(`${api}/signin`, { method: "POST", headers: { Authorization: `Bearer ${token}` } }).catch(() => undefined);
      })();
    });
  };

  return {
    auth: backend.auth,

    subscribe(view, subject, emit) {
      tell();
      if (subject !== undefined && !safeSubject(subject)) {
        emit({ status: "denied", data: undefined });
        return () => {};
      }
      const personal = options.personal?.includes(view) ?? false;
      let uid = uidOf(backend.auth);
      let last: ViewData | undefined;
      let stop: (() => void) | undefined;
      let retry: ReturnType<typeof setTimeout> | undefined;
      let sleep: ReturnType<typeof setTimeout> | undefined;
      let attempt = 0;
      let asleep = false;
      let denied = false;
      let watching: string | undefined; // the document last listened to

      const send = (state: ViewState) => emit(state);
      // What a view shows when it can't be current: what it last showed, marked
      // stale, or still loading if it never showed anything.
      const fallBack = () =>
        send(last === undefined ? { status: "loading", data: undefined } : { status: "stale", data: last });

      const quiet = () => {
        clearTimeout(retry);
        retry = undefined;
        stop?.();
        stop = undefined;
      };

      const listen = () => {
        quiet();
        const wasDenied = denied;
        denied = false;
        let path = `views/${subject ? `${view}:${subject}` : view}`;
        if (personal) {
          // Until sign-in is known, there's no way to tell whose view to read.
          if (uid === undefined) {
            watching = undefined;
            last = undefined;
            return send({ status: "loading", data: undefined });
          }
          if (uid === null) {
            denied = true;
            watching = undefined;
            last = undefined;
            return send({ status: "denied", data: undefined });
          }
          path = `views/${view}:${uid}`;
        }
        // Another document, such as someone else's after a change of person, starts
        // from nothing: what the last one held must never show as this one's.
        if (path !== watching || wasDenied) {
          watching = path;
          last = undefined;
          send({ status: "loading", data: undefined });
        }
        const refuse = () => {
          denied = true;
          last = undefined;
          send({ status: "denied", data: undefined });
        };
        try {
          stop = backend.watch(
            path,
            (data, current) => {
              // Opening offline with nothing saved isn't a document that doesn't exist:
              // the server hasn't been asked yet.
              if (data === undefined && !current && last === undefined) return fallBack();
              last = data ?? {};
              if (current) attempt = 0;
              send({ status: current ? "live" : "stale", data: last });
            },
            (refused) => {
              stop = undefined;
              if (refused) return refuse();
              fallBack();
              retry = setTimeout(listen, backoff(attempt++, first, most));
            },
          );
        } catch {
          // A backend that throws rather than failing, like Firestore given a path it
          // won't read, would throw the same way again, so it isn't retried on a timer.
          stop = undefined;
          refuse();
        }
      };

      const onVisibility = () => {
        if (page?.visibilityState === "hidden") {
          clearTimeout(sleep);
          sleep = setTimeout(() => {
            quiet();
            asleep = true;
            if (!denied) fallBack();
          }, dormantAfter);
          return;
        }
        clearTimeout(sleep);
        sleep = undefined;
        // Shown again: start now, rather than wait out a retry.
        if (asleep || retry !== undefined) {
          asleep = false;
          listen();
        }
      };

      const unwatch = backend.auth.watch((person) => {
        const next = person?.uid ?? null;
        if (next === uid) return;
        uid = next;
        if (!asleep) listen();
      });

      listen();
      page?.addEventListener("visibilitychange", onVisibility);
      if (page?.visibilityState === "hidden") onVisibility();

      return () => {
        quiet();
        clearTimeout(sleep);
        unwatch();
        page?.removeEventListener("visibilitychange", onVisibility);
      };
    },

    async run(command, input: CommandInput) {
      const headers: Record<string, string> = { "Content-Type": "application/json" };
      let response: Response;
      try {
        // Getting the token can need the network too, to refresh it.
        const token = await backend.token();
        if (token) headers.Authorization = `Bearer ${token}`;
        response = await (options.fetch ?? fetch)(`${api}/${command.split("::").join("/")}`, {
          method: "POST",
          headers,
          body: JSON.stringify(input),
        });
      } catch {
        throw new Error("couldn't reach the server; check your connection and try again");
      }
      if (response.ok) return;
      const body: unknown = await response.json().catch(() => undefined);
      const message = (body as { error?: unknown } | undefined)?.error;
      throw new Error(typeof message === "string" ? message : "something went wrong on our side; try again");
    },
  };
}
