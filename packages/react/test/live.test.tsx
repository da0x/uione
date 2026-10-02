// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// The liveness rules, driven with a backend that fails on demand.

import { afterEach, beforeEach, vi } from "vitest";
import type { AuthSource, Person, ViewData, ViewState } from "../src/index.js";
import { backoff, liveSource as untracked } from "../src/live.js";
import type { Backend } from "../src/live.js";

interface Watch {
  path: string;
  next: (data: ViewData | undefined, current: boolean) => void;
  fail: (denied: boolean) => void;
  stopped: boolean;
}

// person is undefined while sign-in isn't known yet.
function fakeBackend(...start: [person?: Person | null]) {
  let person = start.length === 0 ? null : start[0];
  const watches: Watch[] = [];
  const watchers = new Set<(person: Person | null) => void>();
  const auth: AuthSource = {
    person: () => person,
    watch(emit) {
      watchers.add(emit);
      return () => void watchers.delete(emit);
    },
    signIn: async () => {},
    signOut: async () => {},
  };
  const backend: Backend = {
    auth,
    watch(path, next, fail) {
      // Like Firestore, a watch that fails has stopped.
      const w: Watch = { path, next, fail: (denied) => ((w.stopped = true), fail(denied)), stopped: false };
      watches.push(w);
      return () => {
        w.stopped = true;
      };
    },
    token: async () => (person ? `token-for-${person.uid}` : undefined),
  };
  const become = (next: Person | null) => {
    person = next;
    for (const emit of watchers) emit(next);
  };
  const open = () => watches.filter((w) => !w.stopped);
  return { backend, watches, open, become };
}

function record() {
  const states: ViewState[] = [];
  return { states, emit: (s: ViewState) => states.push(s), last: () => states[states.length - 1] };
}

function hide(hidden: boolean) {
  Object.defineProperty(document, "visibilityState", { value: hidden ? "hidden" : "visible", configurable: true });
  document.dispatchEvent(new Event("visibilitychange"));
}

// Every subscription a test makes is stopped after it, so none of them keeps
// listening to the page and reacts to the next test's events.
const stops: (() => void)[] = [];
function liveSource(...args: Parameters<typeof untracked>) {
  const source = untracked(...args);
  const subscribe = source.subscribe.bind(source);
  source.subscribe = (...given) => {
    const stop = subscribe(...given);
    stops.push(stop);
    return stop;
  };
  return source;
}

beforeEach(() => {
  vi.useFakeTimers();
  vi.spyOn(Math, "random").mockReturnValue(1); // the longest wait, so tests know it
});

afterEach(() => {
  for (const stop of stops.splice(0)) stop();
  vi.useRealTimers();
  vi.restoreAllMocks();
  hide(false);
});

describe("a live view", () => {
  it("is live with what the server sends, and empty before its document exists", () => {
    const { backend, watches } = fakeBackend();
    const view = record();
    liveSource(backend).subscribe("waitlist::signups", undefined, view.emit);
    expect(watches[0]!.path).toBe("views/waitlist::signups");

    watches[0]!.next(undefined, true);
    expect(view.last()).toEqual({ status: "live", data: {} });
    watches[0]!.next({ total: 3 }, true);
    expect(view.last()).toEqual({ status: "live", data: { total: 3 } });
  });

  it("is stale, keeping what it showed, while only the offline copy is there", () => {
    const { backend, watches } = fakeBackend();
    const view = record();
    liveSource(backend).subscribe("waitlist::signups", undefined, view.emit);
    watches[0]!.next({ total: 3 }, true);
    watches[0]!.next({ total: 3 }, false);
    expect(view.last()).toEqual({ status: "stale", data: { total: 3 } });
    watches[0]!.next({ total: 4 }, true);
    expect(view.last()).toEqual({ status: "live", data: { total: 4 } });
  });

  it("retries forever, waiting longer each time, and short again only once data arrives", () => {
    const { backend, watches, open } = fakeBackend();
    const view = record();
    liveSource(backend, { retry: { first: 1000, most: 8000 } }).subscribe("waitlist::signups", undefined, view.emit);
    watches[0]!.next({ total: 3 }, true);

    const failAndWait = (wait: number) => {
      const before = watches.length;
      open()[0]!.fail(false);
      expect(view.last()).toEqual({ status: "stale", data: { total: 3 } });
      vi.advanceTimersByTime(wait - 1);
      expect(watches.length).toBe(before); // not yet
      vi.advanceTimersByTime(1);
      expect(watches.length).toBe(before + 1);
    };
    failAndWait(1000);
    failAndWait(2000);
    failAndWait(4000);
    failAndWait(8000);
    failAndWait(8000); // no longer than the most
    for (let i = 0; i < 50; i++) failAndWait(8000); // and it never gives up

    open()[0]!.next({ total: 3 }, false); // the offline copy isn't data arriving
    failAndWait(8000);
    open()[0]!.next({ total: 3 }, true);
    failAndWait(1000);
  });

  it("spreads its retries out", () => {
    vi.mocked(Math.random).mockReturnValue(0);
    expect(backoff(3, 1000, 30000)).toBe(4000);
    vi.mocked(Math.random).mockReturnValue(0.5);
    expect(backoff(3, 1000, 30000)).toBe(6000);
  });

  it("is denied when the rules refuse it, and tried again when someone else signs in", () => {
    const { backend, watches, open, become } = fakeBackend(null);
    const view = record();
    liveSource(backend).subscribe("library::loans", undefined, view.emit);
    watches[0]!.fail(true);
    expect(view.last()).toEqual({ status: "denied", data: undefined });
    vi.advanceTimersByTime(60000);
    expect(watches.length).toBe(1); // a refusal isn't retried on a timer

    become({ uid: "ada", name: "Ada" });
    expect(open().length).toBe(1);
    expect(watches.length).toBe(2);
  });

  it("goes back to loading, rather than show what it never had, when it fails before any data", () => {
    const { backend, watches } = fakeBackend();
    const view = record();
    liveSource(backend).subscribe("waitlist::signups", undefined, view.emit);
    watches[0]!.fail(false);
    expect(view.last()).toEqual({ status: "loading", data: undefined });
    hide(true);
    vi.advanceTimersByTime(60000);
    expect(view.last()).toEqual({ status: "loading", data: undefined });
  });

  it("starts loading again when someone signs in after it was denied", () => {
    const { backend, watches, become } = fakeBackend(null);
    const view = record();
    liveSource(backend).subscribe("library::loans", undefined, view.emit);
    watches[0]!.fail(true);
    become({ uid: "ada", name: "Ada" });
    expect(view.last()).toEqual({ status: "loading", data: undefined });
  });

  it("carries on when sign-in reports the same person again, as it does on a token refresh", () => {
    const { backend, watches, become } = fakeBackend({ uid: "ada", name: "Ada" });
    liveSource(backend).subscribe("waitlist::signups", undefined, () => {});
    become({ uid: "ada", name: "Ada" });
    expect(watches.length).toBe(1);
  });

  it("stops listening after a minute in a hidden tab, and starts again when it's shown", () => {
    const { backend, watches, open } = fakeBackend();
    const view = record();
    liveSource(backend).subscribe("waitlist::signups", undefined, view.emit);
    watches[0]!.next({ total: 3 }, true);

    hide(true);
    vi.advanceTimersByTime(59999);
    expect(open().length).toBe(1);
    vi.advanceTimersByTime(1);
    expect(open().length).toBe(0);
    expect(view.last()).toEqual({ status: "stale", data: { total: 3 } });

    hide(false);
    expect(open().length).toBe(1); // at once, not after a wait
    expect(watches.length).toBe(2);
  });

  it("keeps listening through a short look away", () => {
    const { backend, watches, open } = fakeBackend();
    liveSource(backend).subscribe("waitlist::signups", undefined, () => {});
    hide(true);
    vi.advanceTimersByTime(30000);
    hide(false);
    vi.advanceTimersByTime(60000);
    expect(open().length).toBe(1);
    expect(watches.length).toBe(1);
  });

  it("tries at once, rather than wait out a retry, when the tab is shown", () => {
    const { backend, watches } = fakeBackend();
    liveSource(backend, { retry: { first: 20000, most: 20000 } }).subscribe("waitlist::signups", undefined, () => {});
    watches[0]!.fail(false);
    hide(true);
    hide(false);
    expect(watches.length).toBe(2);
    vi.advanceTimersByTime(20000);
    expect(watches.length).toBe(2); // and the retry it replaced doesn't also run
  });

  it("stops everything when no one's listening", () => {
    const { backend, watches, open, become } = fakeBackend();
    const stop = liveSource(backend).subscribe("waitlist::signups", undefined, () => {});
    watches[0]!.fail(false);
    stop();
    vi.advanceTimersByTime(60000);
    hide(true);
    hide(false);
    become({ uid: "ada", name: "Ada" });
    expect(open().length).toBe(0);
    expect(watches.length).toBe(1);
  });
});

describe("a view with one document per person", () => {
  const personal = { personal: ["studio::projects"] };

  it("reads the signed-in person's document, without being told whose", () => {
    const { backend, watches } = fakeBackend({ uid: "ada", name: "Ada" });
    liveSource(backend, personal).subscribe("studio::projects", undefined, () => {});
    expect(watches[0]!.path).toBe("views/studio::projects:ada");
  });

  it("waits until sign-in is known, is denied to no one, and follows whoever signs in", () => {
    const { backend, watches, open, become } = fakeBackend(undefined);
    const view = record();
    liveSource(backend, personal).subscribe("studio::projects", undefined, view.emit);
    expect(view.last()).toEqual({ status: "loading", data: undefined });
    expect(watches.length).toBe(0);

    become(null);
    expect(view.last()).toEqual({ status: "denied", data: undefined });
    expect(watches.length).toBe(0);

    become({ uid: "ada", name: "Ada" });
    open()[0]!.next({ rows: [{ id: "1", name: "Ada's" }] }, true);
    become({ uid: "grace", name: "Grace" });
    expect(open().map((w) => w.path)).toEqual(["views/studio::projects:grace"]);
    // Ada's projects never show as Grace's, even for a moment, or after a failure.
    expect(view.last()).toEqual({ status: "loading", data: undefined });
    open()[0]!.fail(false);
    expect(view.last()).toEqual({ status: "loading", data: undefined });

    become(null);
    expect(open().length).toBe(0);
    expect(view.last()).toEqual({ status: "denied", data: undefined }); // nothing of Grace's is left showing
  });
});

describe("commands", () => {
  function reply(status: number, body: unknown) {
    return vi.fn(async (..._: Parameters<typeof fetch>) => new Response(JSON.stringify(body), { status }));
  }

  it("are sent to the API with the person's token", async () => {
    const { backend } = fakeBackend({ uid: "ada", name: "Ada" });
    const fetch = reply(200, { id: "x" });
    await liveSource(backend, { fetch }).run("studio::project::create", { name: "Mine" });
    const [url, init] = fetch.mock.calls[0]!;
    expect(url).toBe("/api/studio/project/create");
    expect(init?.method).toBe("POST");
    expect(init?.body).toBe('{"name":"Mine"}');
    expect((init?.headers as Record<string, string>).Authorization).toBe("Bearer token-for-ada");
  });

  it("carry no token when no one is signed in", async () => {
    const { backend } = fakeBackend(null);
    const fetch = reply(200, {});
    await liveSource(backend, { fetch }).run("waitlist::signup::create", {});
    expect((fetch.mock.calls[0]![1]?.headers as Record<string, string>).Authorization).toBeUndefined();
  });

  it("fail with the server's message, or a plain one when there isn't one", async () => {
    const { backend } = fakeBackend();
    await expect(liveSource(backend, { fetch: reply(400, { error: "Email is required" }) }).run("a::b::c", {})).rejects.toThrow(
      "Email is required",
    );
    const broken = vi.fn(async () => new Response("<html>", { status: 502 }));
    await expect(liveSource(backend, { fetch: broken }).run("a::b::c", {})).rejects.toThrow("something went wrong on our side");
    const offline = vi.fn(async () => {
      throw new TypeError("Failed to fetch");
    });
    await expect(liveSource(backend, { fetch: offline }).run("a::b::c", {})).rejects.toThrow("couldn't reach the server");
    backend.token = async () => {
      throw new Error("Firebase: Error (auth/network-request-failed).");
    };
    await expect(liveSource(backend, { fetch: reply(200, {}) }).run("a::b::c", {})).rejects.toThrow("couldn't reach the server");
  });
});

describe("a view opened offline", () => {
  it("is still loading, not empty, when nothing was saved and the server hasn't answered", () => {
    const { backend, watches } = fakeBackend();
    const view = record();
    liveSource(backend).subscribe("waitlist::signups", undefined, view.emit);
    watches[0]!.next(undefined, false);
    expect(view.last()).toEqual({ status: "loading", data: undefined });
    watches[0]!.next(undefined, true);
    expect(view.last()).toEqual({ status: "live", data: {} });
  });
});

describe("a view's subject", () => {
  it("is refused, without reading anything, when it would name some other document", () => {
    for (const subject of ["a/b", "", ".", "..", "__name__"]) {
      const { backend, watches } = fakeBackend();
      const view = record();
      liveSource(backend).subscribe("library::book_page", subject, view.emit);
      expect(view.last()).toEqual({ status: "denied", data: undefined });
      expect(watches.length).toBe(0);
    }
  });

  it("is read when it's an ordinary id", () => {
    const { backend, watches } = fakeBackend();
    liveSource(backend).subscribe("library::book_page", "__b1", () => {});
    expect(watches[0]!.path).toBe("views/library::book_page:__b1");
  });
});

describe("a backend that throws rather than failing", () => {
  it("leaves the view denied instead of throwing", () => {
    const { backend } = fakeBackend();
    backend.watch = () => {
      throw new Error("Invalid document reference");
    };
    const view = record();
    expect(() => liveSource(backend).subscribe("library::book_page", "b1", view.emit)).not.toThrow();
    expect(view.last()).toEqual({ status: "denied", data: undefined });
  });
});
