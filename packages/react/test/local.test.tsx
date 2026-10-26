// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// @uione/react/local, held to what one::local does for the native apps, on the
// language's own library example: ids made of keys and counted per book, a unique key
// refused, defaults and choices, a body that reads and changes another entity in the
// same step, a date after another, views ordered through a function, with a value
// worked out for each row and a page per book, and undo and redo.

import { vi } from "vitest";
import { Rows, all, by, command, entity, first, localPackage, localSource, memoryStore, none, nothing, row, subject, view, where } from "../src/local.js";
import type { CommandReply, ViewState } from "../src/data.js";
import type { LocalStore, Row, Value } from "../src/local.js";

function sortTitle(title: Value): Value {
  const t = typeof title === "string" ? title : "";
  for (const article of ["The ", "An ", "A "]) if (t.startsWith(article)) return t.slice(article.length);
  return t;
}

// examples/library/main.one, as the compiler writes it for storage local.
function library() {
  const book = entity("library::book", [
    { name: "title", required: true },
    { name: "author", required: true },
    { name: "shelfmark", required: true, key: true, unique: true },
    { name: "status", choices: ["on_shelf", "lent", "withdrawn"], initial: "on_shelf" },
    { name: "summary" },
  ]);
  const loan = entity("library::loan", [
    { name: "book", required: true, key: true, refers: "library::book" },
    { name: "number", key: true, serial: "book" },
    { name: "member", required: true },
    { name: "lent_at", now: true },
    { name: "due_at", required: true, after: "lent_at" },
    { name: "returned_at" },
  ]);
  const bookCreate = command("library::book::create").creates("library::book");
  const update = command("library::book::update").updates("library::book").fields(["title", "author", "summary"]);
  const withdraw = command("library::book::withdraw")
    .on("library::book")
    .does((c, b) => {
      if (b["status"] !== "on_shelf") return c.fail("only a book on the shelf can be withdrawn");
      b["status"] = "withdrawn";
      return c.done();
    });
  const loanCreate = command("library::loan::create")
    .creates("library::loan")
    .does((c, l) => {
      const b = c.read("library::book", String(l["book"]));
      if (!b || b["status"] !== "on_shelf") return c.fail("that book is not on the shelf");
      return c.update("library::book", String(l["book"]), { status: "lent" });
    });
  const checkin = command("library::loan::checkin")
    .on("library::loan")
    .does((c, l) => {
      if (!nothing(l["returned_at"])) return c.fail("that book is already back");
      l["returned_at"] = c.now();
      return c.update("library::book", String(l["book"]), { status: "on_shelf" });
    });
  const shelf = view("library::shelf")
    .each(all("library::book").except("status", "withdrawn"))
    .order(by("title", sortTitle))
    .fields(["shelfmark", "title", "author", "status"])
    .value("lent_to", first("library::loan").where("book", row).where("returned_at", none), "member");
  const desk = view("library::desk").each(where("library::loan", "returned_at", none)).order("due_at").fields(["book.title", "member", "due_at"]);
  const bookPage = view("library::book_page")
    .per("library::book")
    .copy("title", "title")
    .copy("author", "author")
    .count("times_lent", where("library::loan", "book", subject))
    .list("loans", where("library::loan", "book", subject))
    .order("-number")
    .fields(["number", "member", "returned_at"]);
  return localPackage("library", [book, loan], [bookCreate, update, withdraw, loanCreate, checkin], [shelf, desk, bookPage]);
}

// A source on a store of its own, the library installed, and every command's answer.
async function app(store: LocalStore = memoryStore()) {
  const source = localSource({ name: "library", packages: [library()], store });
  await source.opened;
  const run = async (command: string, sent: Row = {}): Promise<{ id?: string; error?: string }> => {
    try {
      const reply = (await source.run(command, sent)) as CommandReply;
      return { id: reply.id };
    } catch (error) {
      return { error: (error as Error).message };
    }
  };
  // A command a test relies on, which has to be done.
  const ok = async (command: string, sent: Row = {}) => {
    const got = await run(command, sent);
    expect(got.error, command).toBeUndefined();
    return got.id;
  };
  const shown = (name: string, about?: string): Row | undefined => {
    let seen: ViewState | undefined;
    const stop = source.subscribe(name, about, (state) => (seen = state));
    stop();
    return seen?.data as Row | undefined;
  };
  const find = (entityName: string, id: string) => source.rows().find(entityName, id);
  return { source, store, run, ok, shown, find };
}

const book = (title: string, shelfmark: string): Row => ({ title, author: "Someone", shelfmark });
const due = "2999-01-01T00:00:00.000000000Z";

describe("a local package", () => {
  it("makes a book with its defaults, named by its key, and refuses its key twice", async () => {
    const a = await app();
    expect(await a.run("library::book::create", book("The Hobbit", "HIS-0142"))).toEqual({ id: "HIS-0142" });
    const kept = a.find("library::book", "HIS-0142");
    expect(kept?.["status"]).toBe("on_shelf");
    expect(kept?.["created_at"]).toBeTruthy();
    expect(await a.run("library::book::create", book("Another", "HIS-0142"))).toEqual({ error: "Shelfmark is already taken" });
    expect(await a.run("library::book::create", { title: "No author", shelfmark: "X-1" })).toEqual({ error: "Author is required" });
    expect(await a.run("library::book::create", { title: "T", author: "A", shelfmark: "Y-1", status: "lost" })).toEqual({
      error: "Status has to be one of on_shelf, lent, withdrawn",
    });
  });

  it("counts loans per book, and lending takes the book off the shelf in the same step", async () => {
    const a = await app();
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    await a.ok("library::book::create", book("Emma", "LIT-0002"));
    expect(await a.run("library::loan::create", { book: "SCI-0001", member: "maya", due_at: due })).toEqual({ id: "SCI-0001-1" });
    expect(a.find("library::book", "SCI-0001")?.["status"]).toBe("lent");
    expect(await a.run("library::loan::create", { book: "SCI-0001", member: "omar", due_at: due })).toEqual({ error: "that book is not on the shelf" });
    await a.ok("library::loan::checkin", { id: "SCI-0001-1" });
    expect(await a.run("library::loan::checkin", { id: "SCI-0001-1" })).toEqual({ error: "that book is already back" });
    expect(await a.run("library::loan::create", { book: "SCI-0001", member: "omar", due_at: due })).toEqual({ id: "SCI-0001-2" });
    expect(await a.run("library::loan::create", { book: "LIT-0002", member: "omar", due_at: due })).toEqual({ id: "LIT-0002-1" });
    expect((await a.run("library::loan::create", { book: "LIT-0002", member: "omar", due_at: "2000-01-01T00:00:00.000000000Z" })).error).toBeTruthy();
    expect(await a.run("library::loan::checkin", { id: "nothing-9" })).toEqual({ error: "there's no such loan" });
  });

  it("changes only what an update's forms ask for, and a body's refusal keeps nothing", async () => {
    const a = await app();
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    await a.ok("library::book::update", { id: "SCI-0001", title: "Dune Messiah", status: "lent", shelfmark: "Z-9" });
    const kept = a.find("library::book", "SCI-0001")!;
    expect(kept["title"]).toBe("Dune Messiah");
    expect(kept["status"]).toBe("on_shelf");
    expect(kept["shelfmark"]).toBe("SCI-0001");
    await a.ok("library::book::withdraw", { id: "SCI-0001" });
    expect(await a.run("library::book::withdraw", { id: "SCI-0001" })).toEqual({ error: "only a book on the shelf can be withdrawn" });
  });

  it("leaves out of the shelf what's withdrawn, orders it by title as it's said, and says who has each", async () => {
    const a = await app();
    await a.ok("library::book::create", book("The Hobbit", "FAN-0001"));
    await a.ok("library::book::create", book("Emma", "LIT-0002"));
    await a.ok("library::book::create", book("A Wrinkle in Time", "SCI-0003"));
    await a.ok("library::book::create", book("Gone", "OUT-0004"));
    await a.ok("library::book::withdraw", { id: "OUT-0004" });
    await a.ok("library::loan::create", { book: "LIT-0002", member: "maya", due_at: due });
    const rows = a.shown("library::shelf")!["rows"] as Row[];
    expect(rows.map((r) => r["title"])).toEqual(["Emma", "The Hobbit", "A Wrinkle in Time"]);
    expect(rows[0]!["id"]).toBe("LIT-0002");
    expect(rows[0]!["lent_to"]).toBe("maya");
    expect(rows[1]!["lent_to"]).toBeNull();
    expect("summary" in rows[0]!).toBe(false); // a row holds what its view lists, and its id
    const desk = a.shown("library::desk")!["rows"] as Row[];
    expect(desk).toHaveLength(1);
    expect(desk[0]!["book.title"]).toBe("Emma");
  });

  it("copies the book onto its page, counts and lists its loans, and has no page for no book", async () => {
    const a = await app();
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    for (let i = 0; i < 3; i++) {
      await a.ok("library::loan::create", { book: "SCI-0001", member: "maya", due_at: due });
      await a.ok("library::loan::checkin", { id: `SCI-0001-${i + 1}` });
    }
    const page = a.shown("library::book_page", "SCI-0001")!;
    expect(page["title"]).toBe("Dune");
    expect(page["times_lent"]).toBe(3);
    const loans = page["loans"] as Row[];
    expect(loans.map((l) => l["number"])).toEqual([3, 2, 1]);
    expect(loans[0]!["returned_at"]).toBeTruthy();
    expect(a.shown("library::book_page", "NOT-0000")).toEqual({});
  });

  it("tells a view as commands change it, and only when they do", async () => {
    const a = await app();
    const sizes: number[] = [];
    a.source.subscribe("library::shelf", undefined, (state) => sizes.push((state.data?.["rows"] as Row[]).length));
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    await a.ok("library::book::create", book("Emma", "LIT-0002"));
    await a.ok("library::loan::create", { book: "LIT-0002", member: "maya", due_at: due });
    await a.ok("library::book::withdraw", { id: "SCI-0001" });
    // The loan changed who has Emma, so the shelf was told, though it holds as many.
    expect(sizes).toEqual([0, 1, 2, 2, 1]);
    const desk: number[] = [];
    a.source.subscribe("library::desk", undefined, (state) => desk.push((state.data?.["rows"] as Row[]).length));
    // A summary is on no desk row, so the desk isn't told of it.
    await a.ok("library::book::update", { id: "SCI-0001", summary: "Spice" });
    expect(desk).toEqual([1]);
  });

  it("shows a view as loading until the log is read through", async () => {
    const source = localSource({ name: "library", packages: [library()], store: memoryStore() });
    const states: string[] = [];
    source.subscribe("library::shelf", undefined, (state) => states.push(state.status));
    await source.opened;
    expect(states).toEqual(["loading", "live"]);
  });

  it("takes either way in a query, and a list held", () => {
    const kept = new Rows();
    kept.apply({
      command: "",
      at: "",
      changes: [
        { entity: "x::card", id: "a", fields: { color: "red", tags: ["one", "two"] } },
        { entity: "x::card", id: "b", fields: { color: "blue", tags: [] } },
        { entity: "x::card", id: "c", fields: { color: "green" } },
      ],
    });
    const m = localPackage("x", [entity("x::card", [{ name: "color" }, { name: "tags", list: true }])], [], []);
    expect(m.picked(where("x::card", "color", "red").either(where("x::card", "color", "blue")), kept, undefined, "")).toHaveLength(2);
    expect(m.picked(all("x::card").has("tags", "two"), kept, undefined, "")).toHaveLength(1);
    expect(m.picked(where("x::card", "tags", none), kept, undefined, "")).toHaveLength(2);
    expect(m.picked(all("x::card").except("color", "red"), kept, undefined, "")).toHaveLength(2);
  });

  it("checks a number in a range as its line says", async () => {
    const source = localSource({
      name: "x",
      packages: [localPackage("x", [entity("x::dial", [{ name: "volume", range: { from: 0, to: 100, by: 5 } }])], [command("x::dial::create").creates("x::dial")], [])],
      store: memoryStore(),
    });
    await expect(source.run("x::dial::create", { volume: 120 })).rejects.toThrow("Volume is from 0 to 100");
    await expect(source.run("x::dial::create", { volume: 7 })).rejects.toThrow("Volume goes in steps of 5 from 0");
    await expect(source.run("x::dial::create", { volume: 35 })).resolves.toBeTruthy();
  });
});

describe("undo and redo", () => {
  it("takes back each step, takes it again, and keeps both, so the log read again agrees", async () => {
    const store = memoryStore();
    const a = await app(store);
    const title = () => a.find("library::book", "SCI-0001")?.["title"] ?? "gone";
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    await a.ok("library::book::update", { id: "SCI-0001", title: "Dune Messiah" });
    expect(title()).toBe("Dune Messiah");
    expect(await a.run("one::undo")).toEqual({ id: "library::book::update" });
    expect(title()).toBe("Dune");
    expect(await a.run("one::undo")).toEqual({ id: "library::book::create" });
    expect(title()).toBe("gone");
    expect(await a.run("one::undo")).toEqual({ error: "There's nothing to undo." });
    await a.ok("one::redo");
    await a.ok("one::redo");
    expect(title()).toBe("Dune Messiah");
    expect(await a.run("one::redo")).toEqual({ error: "There's nothing to redo." });
    await a.ok("one::undo");
    const reopened = await app(store);
    expect(reopened.find("library::book", "SCI-0001")?.["title"]).toBe("Dune");
    // The history is the page's, while it's open.
    expect(await reopened.run("one::undo")).toEqual({ error: "There's nothing to undo." });
  });

  it("undoes a step joined to the one before with it, never one apart, and keeps what that changed", async () => {
    const a = await app();
    const field = (id: string, name: string) => a.find("library::book", id)?.[name];
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    await a.ok("library::book::create", book("Emma", "LIT-0002"));
    await a.ok("library::book::update", { id: "SCI-0001", title: "Dune Messiah" });
    await a.ok("library::book::update", { id: "LIT-0002", title: "Persuasion", "one:history": "joined" });
    await a.ok("library::book::update", { id: "SCI-0001", summary: "Spice", "one:history": "apart" });
    expect(field("SCI-0001", "one:history")).toBeUndefined();
    expect(await a.run("one::undo")).toEqual({ id: "library::book::update" });
    expect(field("SCI-0001", "title")).toBe("Dune");
    expect(field("LIT-0002", "title")).toBe("Emma");
    expect(field("SCI-0001", "summary")).toBe("Spice"); // changed apart, so it stays
    expect(await a.run("one::undo")).toEqual({ id: "library::book::create" });
    expect(a.find("library::book", "LIT-0002")).toBeUndefined();
  });

  it("has nothing to redo after a step taken since an undo", async () => {
    const a = await app();
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    await a.ok("one::undo");
    await a.ok("library::book::create", book("Emma", "LIT-0002"));
    expect(await a.run("one::redo")).toEqual({ error: "There's nothing to redo." });
  });
});

describe("a command apart from undo", () => {
  it("is kept, but never a step undo takes back", async () => {
    const notes = localPackage(
      "x",
      [entity("x::note", [{ name: "title" }, { name: "preview" }])],
      [command("x::note::create").creates("x::note"), command("x::note::preview").on("x::note").fields(["preview"]).apart()],
      [],
    );
    const source = localSource({ name: "x", packages: [notes], store: memoryStore() });
    const made = (await source.run("x::note::create", { title: "Dune" })) as CommandReply;
    await source.run("x::note::preview", { id: made.id, preview: "files/abc" });
    expect(source.rows().find("x::note", made.id!)?.["preview"]).toBe("files/abc");
    // Undo takes back the create, the step before, and the preview with what it made.
    expect(await source.run("one::undo", {})).toEqual({ id: "x::note::create" });
    expect(source.rows().find("x::note", made.id!)).toBeUndefined();
  });
});

describe("the same app in two tabs", () => {
  it("runs each command on what the other kept, and shows what the other did", async () => {
    const store = memoryStore();
    const a = await app(store);
    const b = await app(store);
    let told: ViewState | undefined;
    b.source.subscribe("library::book_page", "SCI-0001", (state) => (told = state));
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    // b hears of it, and shows it.
    await vi.waitFor(() => expect((told?.data as Row | undefined)?.["title"]).toBe("Dune"));
    // b's command reads the book a made, and a hears of what b did.
    await b.ok("library::loan::create", { book: "SCI-0001", member: "maya", due_at: due });
    await vi.waitFor(() => expect(a.find("library::book", "SCI-0001")?.["status"]).toBe("lent"));
    // a's next step starts from b's: the book is lent, so it can't be lent twice.
    expect(await a.run("library::loan::create", { book: "SCI-0001", member: "omar", due_at: due })).toEqual({ error: "that book is not on the shelf" });
    expect(store.events.length).toBe(2);
  });

  it("catches up before a command even when it hasn't heard", async () => {
    const store = memoryStore();
    const a = await app(store);
    const b = await app(store);
    await a.ok("library::book::create", book("Dune", "SCI-0001"));
    // Before b could hear, its own create of the same key is refused, as it would be in one tab.
    expect(await b.run("library::book::create", book("Dune", "SCI-0001"))).toEqual({ error: "Shelfmark is already taken" });
  });
});

describe("a local app's files", () => {
  it("keeps each file once by its SHA-256, reads it back, and lets go of what nothing names", async () => {
    const store = memoryStore();
    const notes = localPackage(
      "x",
      [entity("x::note", [{ name: "picture" }])],
      [command("x::note::create").creates("x::note"), command("x::note::delete").deletes("x::note")],
      [],
    );
    const source = localSource({ name: "x", packages: [notes], store });
    const progress: number[] = [];
    const path = await source.files.upload(new File(["abc"], "a.txt", { type: "text/plain" }), (done) => progress.push(done));
    expect(path).toBe("files/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect(progress).toEqual([1]);
    expect(await source.files.upload(new File(["abc"], "b.txt"), () => {})).toBe(path);
    expect(store.files.size).toBe(1);
    const made = (await source.run("x::note::create", { picture: path })) as CommandReply;
    const created = URL.createObjectURL;
    URL.createObjectURL = (blob: Blob) => `blob:${blob.size}`;
    try {
      expect(await source.files.address(path)).toBe("blob:3");
    } finally {
      URL.createObjectURL = created;
    }
    await source.run("x::note::delete", { id: made.id });
    // Undoing could bring it back while the page is open; read again, it's gone.
    await localSource({ name: "x", packages: [notes], store }).opened;
    expect(store.files.size).toBe(0);
  });
});
