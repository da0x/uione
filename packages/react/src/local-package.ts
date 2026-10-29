// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What the compiler writes for a web app whose project says storage local: its
// entities, commands and views described in a few lines each, as the native apps
// describe them on one::local, and run in the browser by localSource. A view and a
// command's defaults are data this works through; only a command's body is code.
//
//     const book = entity("library::book", [
//       { name: "title", required: true },
//       { name: "shelfmark", required: true, key: true, unique: true },
//       { name: "status", choices: ["on_shelf", "lent", "withdrawn"], initial: "on_shelf" },
//     ]);
//     const withdraw = command("library::book::withdraw").on("library::book").does((c, b) => {
//       if (b["status"] !== "on_shelf") return c.fail("only a book on the shelf can be withdrawn");
//       b["status"] = "withdrawn";
//       return c.done();
//     });
//     const shelf = view("library::shelf")
//       .each(all("library::book").except("status", "withdrawn"))
//       .order(by("title", sortTitle))
//       .fields(["shelfmark", "title", "author", "status"])
//       .value("lent_to", first("library::loan").where("book", row).where("returned_at", none), "member");
//
//     localPackage("library", [book, loan], [bookCreate, withdraw], [shelf]);
//
// What it can't do is everything about people, who don't sign in to a local app
// (decision 0015): no me, no views per person, no roles or readers.

import { keyOf } from "./keys.js";
export { keyOf };

// A value as a row keeps it: what JSON holds.
export type Value = null | boolean | number | string | Value[] | { [field: string]: Value };
export type Row = { [field: string]: Value };

// What failed, and why, as a person reads it.
export interface Failure {
  error: string;
}

export function failed(outcome: unknown): outcome is Failure {
  return typeof outcome === "object" && outcome !== null && typeof (outcome as Failure).error === "string";
}

// What a condition compares a field with, besides a value: the entity a view per
// entity is for, a field of it, the row a value is worked out for, or nothing.
export class Marker {
  constructor(
    readonly kind: "subject" | "subject_field" | "row" | "none",
    readonly field = "",
  ) {}
}
export const subject = new Marker("subject");
export const row = new Marker("row");
export const none = new Marker("none");
export function subjectField(field: string): Marker {
  return new Marker("subject_field", field);
}
export type Compared = Value | Marker;

function isArray(v: Value | undefined): v is Value[] {
  return Array.isArray(v);
}

function isMap(v: Value | undefined): v is Row {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

// Whether a stored value holds nothing: it's missing or null, or empty text or an
// empty list, as a field reads before it's given a value.
export function nothing(v: Value | undefined): boolean {
  if (v === undefined || v === null) return true;
  if (typeof v === "string") return v === "";
  if (isArray(v)) return v.length === 0;
  return false;
}

// x / y and x % y in a command's body, as every target works them out: what's left
// over as a clock counts, never below none, so -1 % 4 is 3, and either by 0 is 0.
export function divided(x: number, y: number): number {
  return y === 0 ? 0 : x / y;
}

export function remainder(x: number, y: number): number {
  return y === 0 ? 0 : x - y * Math.floor(x / y);
}

// Orders two values of the same kind: false before true, numbers and text in
// their usual order, times as RFC 3339 text. Nothing comes first.
export function compare(a: Value | undefined, b: Value | undefined): number {
  const aNull = a === undefined || a === null;
  const bNull = b === undefined || b === null;
  if (aNull) return bNull ? 0 : -1;
  if (bNull) return 1;
  if (typeof a === "boolean" && typeof b === "boolean") return a === b ? 0 : a ? 1 : -1;
  if (typeof a === "number" && typeof b === "number") return a < b ? -1 : b < a ? 1 : 0;
  if (typeof a === "string" && typeof b === "string") return a < b ? -1 : a === b ? 0 : 1;
  return 0;
}

function alike(a: Value | undefined, b: Value | undefined): boolean {
  if (typeof a === "number" && typeof b === "number") return a === b;
  const kind = (v: Value | undefined) => (v === undefined || v === null ? "null" : isArray(v) ? "array" : typeof v);
  if (kind(a) !== kind(b)) return false;
  if (isArray(a) || isMap(a)) return JSON.stringify(a) === JSON.stringify(b);
  return compare(a, b) === 0;
}

// == in a command's body: the same value, or both nothing when either is none.
export function same(a: Value | undefined, b: Value | undefined): boolean {
  if (nothing(a) || nothing(b)) return nothing(a) && nothing(b);
  return alike(a, b);
}

// A value as a condition: true, or anything but nothing and false.
export function truthy(v: Value | undefined): boolean {
  return typeof v === "boolean" ? v : !nothing(v);
}

// A value as text, or "" when it isn't text.
export function text(v: Value | undefined): string {
  return typeof v === "string" ? v : "";
}

// A value as a number, or 0 when it isn't one.
export function number(v: Value | undefined): number {
  if (typeof v === "number") return v;
  if (typeof v === "boolean") return v ? 1 : 0;
  return 0;
}

// A field's name as a person reads it: due_at is "Due at".
export function label(name: string): string {
  const words = name.replaceAll("_", " ");
  return words.charAt(0).toUpperCase() + words.slice(1);
}

// A number as a .one file writes it, 5 or 0.5, and anything else as its text.
function written(v: Value | undefined): string {
  return typeof v === "number" ? String(v) : text(v);
}

// The last part of an entity's name, as a person reads it: library::book is book.
function called(entity: string): string {
  const words = label(entity.slice(entity.lastIndexOf("::") + (entity.includes("::") ? 2 : 0)));
  return words.charAt(0).toLowerCase() + words.slice(1);
}

function noSuch(entity: string): string {
  return `there's no such ${called(entity)}`;
}

// A copy of a value, so what a command changes is its own until the step is kept.
function copied<T extends Value>(v: T): T {
  return structuredClone(v);
}

const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

// An id nobody chose, as Firestore makes them: twenty letters and digits.
export function madeUpId(): string {
  const random = crypto.getRandomValues(new Uint8Array(20));
  let id = "";
  for (const b of random) id += alphabet[b % alphabet.length];
  return id;
}

// Now, as RFC 3339 in UTC, always with nine digits of the second, so two times are
// in order as text. A browser's clock counts milliseconds, so what's said within
// one is counted on in the digits below it, and every time said is later than the
// one before.
let lastMillis = 0;
let within = 0;
export function nowText(): string {
  const millis = Date.now();
  if (millis > lastMillis) {
    lastMillis = millis;
    within = 0;
  } else {
    within++;
  }
  const whole = new Date(lastMillis).toISOString(); // 2026-10-25T12:00:00.123Z
  return `${whole.slice(0, 23)}${String(within).padStart(6, "0")}Z`;
}

// One field of an entity, as its .one line says it.
export interface Field {
  name: string;
  required?: boolean;
  key?: boolean; // part of the id, in order (decision 0009)
  unique?: boolean; // a key that's taken is refused rather than made again
  choices?: string[];
  initial?: Value; // = value
  now?: boolean; // = now
  serial?: string; // serial, counted within this field, or "" for all
  refers?: string; // what it points at, like library::book
  after?: string; // a field it has to come after
  list?: boolean;
  range?: { from: number; to: number; by: number }; // from 0 to 100 by 5
}

export interface EntityDescription {
  name: string;
  fields: Field[];
}

export function entity(name: string, fields: Field[]): EntityDescription {
  return { name, fields };
}

function fieldOf(e: EntityDescription | undefined, name: string): Field | undefined {
  return e?.fields.find((f) => f.name === name);
}

// Picks the rows of an entity a view, a value or a body reads: every condition of
// one way, or of another given with either.
export interface Condition {
  field: string; // read through what it points at with a dot: board.name
  wanted: Compared;
  test: "is" | "is_not" | "holds";
}

export class Query {
  constructor(
    readonly entity: string,
    readonly conditions: readonly Condition[] = [],
    readonly others: readonly Query[] = [],
  ) {}

  // field holds wanted
  where(field: string, wanted: Compared): Query {
    return new Query(this.entity, [...this.conditions, { field, wanted, test: "is" }], this.others);
  }
  // field doesn't
  except(field: string, wanted: Compared): Query {
    return new Query(this.entity, [...this.conditions, { field, wanted, test: "is_not" }], this.others);
  }
  // its list holds wanted
  has(field: string, wanted: Compared): Query {
    return new Query(this.entity, [...this.conditions, { field, wanted, test: "holds" }], this.others);
  }
  // or picked this other way
  either(other: Query): Query {
    return new Query(this.entity, this.conditions, [...this.others, other]);
  }
}

export function all(entity: string): Query {
  return new Query(entity);
}
export function where(entity: string, field: string, wanted: Compared): Query {
  return all(entity).where(field, wanted);
}
// all, read where only the earliest made is wanted
export function first(entity: string): Query {
  return all(entity);
}

// What a list is ordered by: a field, -field for the other way round, or a field
// passed through a function, like a title without its "The".
export interface By {
  field: string;
  through?: (v: Value) => Value;
  descending: boolean;
}

export function by(name: string, through?: (v: Value) => Value, descending = false): By {
  if (through) return { field: name, through, descending };
  return name.startsWith("-") ? { field: name.slice(1), descending: true } : { field: name, descending: false };
}

export interface ValueOf {
  name: string;
  from: Query;
  field: string;
}

export interface Listing {
  name: string; // rows, for each
  rows: Query;
  order: By[];
  limit: number;
  fields: string[];
  values: ValueOf[];
}

// A row of one of a view's lists compared with another of its values, by ==, !=, <,
// <=, > or >=, as in ["created_at", ">", "seen"].
export type RowTest = [string, string, string];

// How many rows of one of its lists pass every test, or, with a field, what that field
// of theirs adds up to: what its list holds, after its limit.
export interface Tallied {
  name: string;
  list: string;
  field: string;
  tests: RowTest[];
}

export class ViewDescription {
  perKind = ""; // the entity it has a document per, if any
  readonly copies: [string, string][] = [];
  readonly counts: [string, Query][] = [];
  readonly lists: Listing[] = [];
  readonly tallies: Tallied[] = [];

  constructor(readonly name: string) {}

  each(rows: Query): this {
    return this.list("rows", rows);
  }
  list(name: string, rows: Query): this {
    this.lists.push({ name, rows, order: [], limit: 0, fields: [], values: [] });
    return this;
  }
  per(entity: string): this {
    this.perKind = entity;
    return this;
  }
  copy(name: string, field: string): this {
    this.copies.push([name, field]);
    return this;
  }
  count(name: string, rows: Query): this {
    this.counts.push([name, rows]);
    return this;
  }
  tally(name: string, list: string, tests: RowTest[] = []): this {
    this.tallies.push({ name, list, field: "", tests });
    return this;
  }
  total(name: string, list: string, field: string, tests: RowTest[] = []): this {
    this.tallies.push({ name, list, field, tests });
    return this;
  }
  order(keys: By | string | (By | string)[]): this {
    const all = Array.isArray(keys) ? keys : [keys];
    this.last("an order").order = all.map((k) => (typeof k === "string" ? by(k) : k));
    return this;
  }
  limit(rows: number): this {
    this.last("a limit").limit = rows;
    return this;
  }
  fields(names: string[]): this {
    this.last("fields").fields = names;
    return this;
  }
  value(name: string, from: Query, field: string): this {
    this.last("a value").values.push({ name, from, field });
    return this;
  }

  private last(what: string): Listing {
    const listing = this.lists[this.lists.length - 1];
    if (!listing) throw new Error(`view ${this.name} has ${what} before any each or list`);
    return listing;
  }
}

export function view(name: string): ViewDescription {
  return new ViewDescription(name);
}

// What a command's body returns: nothing when it's done, or why it refused.
export type Done = Failure | undefined | void;
export type Body = (c: Context, entity: Row) => Done;

export class CommandDescription {
  entity = "";
  doesWhat: "creates" | "updates" | "deletes" | "acts" = "acts";
  allowed: string[] | undefined; // the fields it may be sent
  inputs: string[] = []; // what it's sent besides them
  body: Body | undefined;
  standsApart = false; // apart from undo: never a step undo takes back

  constructor(readonly name: string) {}

  // x::create: makes one from its fields
  creates(on: string): this {
    return this.doing(on, "creates");
  }
  // x::update: changes the fields given
  updates(on: string): this {
    return this.doing(on, "updates");
  }
  // x::delete
  deletes(on: string): this {
    return this.doing(on, "deletes");
  }
  // any other: acts on the one its id names
  on(entity: string): this {
    return this.doing(entity, "acts");
  }
  fields(names: string[]): this {
    this.allowed = names;
    return this;
  }
  // input into stage
  takes(names: string[]): this {
    this.inputs = names;
    return this;
  }
  does(body: Body): this {
    this.body = body;
    return this;
  }
  // apart from undo: its steps kept, as if sent "one:history": "apart"
  apart(): this {
    this.standsApart = true;
    return this;
  }

  private doing(on: string, what: CommandDescription["doesWhat"]): this {
    this.entity = on;
    this.doesWhat = what;
    return this;
  }
}

export function command(name: string): CommandDescription {
  return new CommandDescription(name);
}

// One entity as a step leaves it: all its fields, or that it's gone.
export interface Change {
  entity: string;
  id: string;
  fields?: Row;
  removed?: boolean;
}

// What a command did, as the log keeps it.
export interface LocalEvent {
  command: string;
  at: string; // when, as RFC 3339 in UTC
  changes: Change[];
}

// The entities as the events so far have left them, each holding its id.
export class Rows {
  private readonly rows = new Map<string, Map<string, Row>>();

  find(entity: string, id: string): Row | undefined {
    return this.rows.get(entity)?.get(id);
  }

  // Each of an entity's rows, in the order of their ids.
  each(entity: string): Row[] {
    const kind = this.rows.get(entity);
    if (!kind) return [];
    return [...kind.keys()].sort(byText).map((id) => kind.get(id)!);
  }

  apply(e: LocalEvent): void {
    for (const c of e.changes) {
      if (!this.rows.has(c.entity)) this.rows.set(c.entity, new Map());
      const kind = this.rows.get(c.entity)!;
      if (c.removed) {
        kind.delete(c.id);
        continue;
      }
      kind.set(c.id, { ...copied(c.fields ?? {}), id: c.id });
    }
  }

  // Every file a row names, files/<sha-256>, in any of its fields.
  filesNamed(): Set<string> {
    const named = new Set<string>();
    const look = (v: Value | undefined) => {
      if (typeof v === "string" && v.startsWith("files/")) named.add(v);
      if (isArray(v)) v.forEach(look);
      if (isMap(v)) Object.values(v).forEach(look);
    };
    for (const kind of this.rows.values()) for (const r of kind.values()) look(r);
    return named;
  }
}

function byText(a: string, b: string): number {
  return a < b ? -1 : a > b ? 1 : 0;
}

// What a command changes, in one step: it reads the rows as they were, with its own
// changes on top, and nothing is kept until the step is done. What it reads are
// copies, so a command can change rows while it goes through them.
export class Step {
  readonly changes: Change[] = [];

  constructor(private readonly before: Rows) {}

  private changed(entity: string, id: string): Change | undefined {
    for (let i = this.changes.length - 1; i >= 0; i--) {
      const c = this.changes[i]!;
      if (c.entity === entity && c.id === id) return c;
    }
    return undefined;
  }

  private record(c: Change): void {
    const at = this.changes.findIndex((earlier) => earlier.entity === c.entity && earlier.id === c.id);
    if (at >= 0) this.changes.splice(at, 1);
    if (!c.removed) c.fields = { ...c.fields, id: c.id };
    this.changes.push(c);
  }

  find(entity: string, id: string): Row | undefined {
    const c = this.changed(entity, id);
    if (c) return c.removed ? undefined : copied(c.fields!);
    const kept = this.before.find(entity, id);
    return kept ? copied(kept) : undefined;
  }

  each(entity: string): Row[] {
    const rows = new Map<string, Row>();
    for (const r of this.before.each(entity)) rows.set(text(r["id"]), r);
    for (const c of this.changes) {
      if (c.entity !== entity) continue;
      if (c.removed) rows.delete(c.id);
      else rows.set(c.id, c.fields!);
    }
    return [...rows.keys()].sort(byText).map((id) => copied(rows.get(id)!));
  }

  // Makes an entity, under the id its fields hold, made from its keys, or one made
  // up when they hold none; its id.
  create(entity: string, fields: Row): string {
    const id = text(fields["id"]) || madeUpId();
    this.record({ entity, id, fields: copied(fields) });
    return id;
  }

  // Sets the fields given on an entity that's there, leaving the rest; false when
  // it isn't.
  update(entity: string, id: string, fields: Row): boolean {
    const r = this.find(entity, id);
    if (!r) return false;
    for (const [name, field] of Object.entries(fields)) if (name !== "id") r[name] = copied(field);
    this.record({ entity, id, fields: r });
    return true;
  }

  remove(entity: string, id: string): boolean {
    if (!this.find(entity, id)) return false;
    this.record({ entity, id, removed: true });
    return true;
  }

  // The serial an entity made now gets: one past the highest, counted within what
  // withinField points at when it's given.
  nextSerial(entity: string, field: string, withinField = "", within = ""): number {
    let highest = 0;
    for (const r of this.each(entity)) {
      if (withinField === "" || text(r[withinField]) === within) highest = Math.max(highest, number(r[field]));
    }
    return highest + 1;
  }
}

// What a query reads from: the rows, for a view, or a command's step, which sees
// its own changes.
export interface Reading {
  each(entity: string): Row[];
  find(entity: string, id: string): Row | undefined;
}

// What a command's body sees and does besides its own entity: when it runs, the
// rows it reads, what it was sent besides its entity's fields, and what else it
// changes in the same step, all kept with it or none of it.
export class Context {
  constructor(
    private readonly pkg: LocalPackage,
    private readonly step: Step,
    private readonly time: string,
    private readonly inputs: Row,
  ) {}

  now(): string {
    return this.time;
  }
  read(entity: string, id: string): Row | undefined {
    return this.step.find(entity, id);
  }
  // What a field pointing at an entity points at, or nothing: book.status reads
  // c.at("library::book", e["book"])["status"].
  at(entity: string, id: Value | undefined): Row {
    return this.read(entity, text(id)) ?? {};
  }
  each(rows: Query): Row[] {
    return this.pkg.picked(rows, this.step, undefined, "");
  }
  create(entity: string, fields: Row): { id: string } | Failure {
    const e = this.pkg.entity(entity);
    if (!e) return { error: `there's no entity ${entity}` };
    return this.pkg.make(e, fields, this.step, this.time);
  }
  update(entity: string, id: string, fields: Row): Failure | undefined {
    const e = this.pkg.entity(entity);
    if (!e) return { error: `there's no entity ${entity}` };
    const before = this.step.find(entity, id);
    if (!before) return { error: noSuch(entity) };
    const v: Row = { ...copied(before), ...copied(fields) };
    keepFixed(e, before, v);
    const checked = validate(e, v);
    if (checked) return checked;
    v["updated_at"] = this.time;
    this.step.update(entity, id, v);
    return undefined;
  }
  remove(entity: string, id: string): Failure | undefined {
    if (!this.step.remove(entity, id)) return { error: noSuch(entity) };
    return undefined;
  }
  input(name: string): Value {
    return this.inputs[name] ?? null;
  }
  fail(message: string): Failure {
    return { error: message };
  }
  done(): undefined {
    return undefined;
  }
}

function validate(e: EntityDescription, v: Row): Failure | undefined {
  for (const f of e.fields) {
    const held = v[f.name];
    if (f.required && nothing(held)) return { error: `${label(f.name)} is required` };
    if (f.choices?.length && typeof held === "string" && held !== "" && !f.choices.includes(held)) {
      return { error: `${label(f.name)} has to be one of ${f.choices.join(", ")}` };
    }
    if (f.range && typeof held === "number") {
      const { from, to, by: step } = f.range;
      if (held < from || held > to) return { error: `${label(f.name)} is from ${from} to ${to}` };
      const steps = (held - from) / step;
      if (Math.abs(steps - Math.round(steps)) > 1e-9) return { error: `${label(f.name)} goes in steps of ${step} from ${from}` };
    }
    if (f.after) {
      const before = v[f.after];
      if (typeof held === "string" && typeof before === "string" && held !== "" && before !== "" && !(held > before)) {
        return { error: `${label(f.name)} has to be after ${label(f.after).toLowerCase()}` };
      }
    }
  }
  return undefined;
}

// What's kept of an entity once made: its keys and serials never change.
function keepFixed(e: EntityDescription, before: Row, after: Row): void {
  for (const f of e.fields) if (f.key || f.serial !== undefined) after[f.name] = before[f.name] ?? null;
  after["id"] = before["id"] ?? null;
  after["created_at"] = before["created_at"] ?? null;
}

// A row's field, or one read through what it points at, like book.title: the title
// of the book its book field names, or of each in a list of them.
function fieldThrough(r: Row, name: string, kind: EntityDescription | undefined, reading: Reading): Value {
  const dot = name.indexOf(".");
  if (dot < 0) return r[name] ?? null;
  const through = name.slice(0, dot);
  const inner = name.slice(dot + 1);
  const pointer = fieldOf(kind, through);
  if (!pointer?.refers) return null;
  const read = (id: Value | undefined): Value => {
    if (typeof id !== "string") return null;
    return reading.find(pointer.refers!, id)?.[inner] ?? null;
  };
  const ids = r[through];
  return isArray(ids) ? ids.map(read) : read(ids);
}

function wantedOf(c: Compared, about: Row | undefined, r: string): Value {
  if (!(c instanceof Marker)) return c;
  switch (c.kind) {
    case "subject":
      return about ? (about["id"] ?? null) : null;
    case "subject_field":
      return about ? (about[c.field] ?? null) : null;
    case "row":
      return r;
    default:
      return null;
  }
}

// Rows in the order they were made, oldest first, ties by id.
function byAge(rows: Row[], oldestFirst: boolean): void {
  rows.sort((a, b) => {
    const c = compare(a["created_at"], b["created_at"]);
    if (c !== 0) return c < 0 === oldestFirst ? -1 : 1;
    return byText(text(a["id"]), text(b["id"]));
  });
}

// A namespace's entities, commands and views, given to a source.
export class LocalPackage {
  private readonly entities = new Map<string, EntityDescription>();

  constructor(
    readonly name: string,
    entities: EntityDescription[],
    readonly commands: CommandDescription[],
    readonly views: ViewDescription[],
  ) {
    for (const e of entities) this.entities.set(e.name, e);
  }

  // Gives the source each command and view.
  install(source: Installing): void {
    for (const c of this.commands) source.on(c.name, (sent, step) => this.run(c, sent, step), c.standsApart);
    for (const v of this.views) source.define(v.name, (rows, about) => this.build(v, rows, about));
  }

  entity(name: string): EntityDescription | undefined {
    return this.entities.get(name);
  }

  // A query's rows, for the subject a view is per and the row a value is for.
  picked(q: Query, reading: Reading, about: Row | undefined, forRow: string): Row[] {
    const out: Row[] = [];
    const seen = new Set<string>();
    for (const way of [q, ...q.others]) {
      const kind = this.entity(way.entity);
      for (const candidate of reading.each(way.entity)) {
        const kept = way.conditions.every((c) => {
          const got = fieldThrough(candidate, c.field, kind, reading);
          const wanted = wantedOf(c.wanted, about, forRow);
          if (c.test === "holds") return isArray(got) && got.some((item) => alike(item, wanted));
          const holds = nothing(wanted) ? nothing(got) : alike(got, wanted);
          return c.test === "is_not" ? !holds : holds;
        });
        const id = text(candidate["id"]);
        if (kept && !seen.has(id)) {
          seen.add(id);
          out.push(candidate);
        }
      }
    }
    return out;
  }

  // A view's document for its subject, or none when what it's per is gone.
  build(v: ViewDescription, reading: Reading, about: string): Row | undefined {
    let per: Row | undefined;
    if (v.perKind) {
      per = reading.find(v.perKind, about);
      if (!per) return undefined;
    }
    const document: Row = {};
    for (const [name, field] of v.copies) document[name] = per ? fieldThrough(per, field, this.entity(v.perKind), reading) : null;
    for (const [name, q] of v.counts) document[name] = this.picked(q, reading, per, "").length;
    for (const l of v.lists) {
      let found = this.picked(l.rows, reading, per, "");
      const kind = this.entity(l.rows.entity);
      // Newest first, unless the list has an order: then the ones that tie keep the
      // order they were made in, so a new one goes after the others.
      byAge(found, l.order.length > 0);
      if (l.order.length > 0) {
        const key = (r: Row, o: By) => {
          const v = fieldThrough(r, o.field, kind, reading);
          return o.through ? o.through(v) : v;
        };
        found.sort((a, b) => {
          for (const o of l.order) {
            const c = compare(key(a, o), key(b, o));
            if (c !== 0) return c < 0 !== o.descending ? -1 : 1;
          }
          return 0;
        });
      }
      if (l.limit > 0) found = found.slice(0, l.limit);
      document[l.name] = found.map((r) => {
        const out: Row = { id: r["id"] ?? null };
        for (const name of l.fields) out[name] = fieldThrough(r, name, kind, reading);
        for (const value of l.values) {
          const from = this.picked(value.from, reading, per, text(r["id"]));
          byAge(from, true); // the earliest made
          out[value.name] = from[0]?.[value.field] ?? null;
        }
        return out;
      });
    }
    for (const t of v.tallies) {
      const passing = ((document[t.list] as Row[] | undefined) ?? []).filter((row) =>
        t.tests.every(([field, op, other]) => {
          const c = compare(row[field], document[other]);
          return op === "==" ? c === 0 : op === "!=" ? c !== 0 : op === "<" ? c < 0 : op === "<=" ? c <= 0 : op === ">" ? c > 0 : c >= 0;
        }),
      );
      document[t.name] = t.field ? passing.reduce((sum, row) => sum + (typeof row[t.field] === "number" ? (row[t.field] as number) : 0), 0) : passing.length;
    }
    return document;
  }

  // An entity made as a create command makes it: defaults, serials, id, checks.
  make(e: EntityDescription, given: Row, step: Step, now: string, body?: (v: Row) => Done): { id: string } | Failure {
    const v: Row = {};
    for (const f of e.fields) {
      if (f.name in given) v[f.name] = copied(given[f.name]!);
      if (!nothing(v[f.name])) continue;
      if (f.initial !== undefined) v[f.name] = copied(f.initial);
      else if (f.now) v[f.name] = now;
    }
    for (const f of e.fields) {
      if (f.serial === undefined) continue;
      const counted = f.serial === "" ? "" : written(v[f.serial]);
      v[f.name] = step.nextSerial(e.name, f.name, f.serial, counted);
    }
    // The id is made from its keys, in order, when it has them all, before the body
    // runs, so the body can name it.
    const keys = e.fields.filter((f) => f.key).map((f) => written(v[f.name]));
    const id = (keys.length > 0 ? keyOf(keys) : undefined) ?? madeUpId();
    v["id"] = id;
    if (body) {
      const done = body(v);
      if (done) return done;
    }
    const checked = validate(e, v);
    if (checked) return checked;
    v["created_at"] = now;
    // The same keys are the same entity: made again, it keeps when it was first made
    // and the numbers it was given, unless a key is unique and refuses.
    const before = step.find(e.name, id);
    if (before) {
      for (const f of e.fields) if (f.key && f.unique) return { error: `${label(f.name)} is already taken` };
      keepFixed(e, before, v);
    }
    v["updated_at"] = now;
    step.create(e.name, v);
    return { id };
  }

  run(c: CommandDescription, sent: Row, step: Step): Answer {
    const e = this.entity(c.entity);
    if (!e) return { error: `there's no entity ${c.entity}` };
    const now = nowText();
    // What it's sent besides its entity's fields is kept apart, for its body.
    const inputs: Row = {};
    let given: Row = {};
    for (const [name, held] of Object.entries(sent)) (c.inputs.includes(name) ? inputs : given)[name] = held;
    const context = new Context(this, step, now, inputs);
    const body = (v: Row): Done => (c.body ? c.body(context, v) : undefined);

    if (c.doesWhat === "creates") {
      if (c.allowed) {
        const kept: Row = {};
        for (const name of c.allowed) if (name in given) kept[name] = given[name]!;
        given = kept;
      }
      return this.make(e, given, step, now, body);
    }

    const id = text(given["id"]);
    const before = step.find(e.name, id);
    if (!before) return { error: noSuch(e.name) };
    const v = copied(before);
    // An update may change the fields it names, or all but its keys; another command
    // only those it names; a delete none.
    let may: string[] = c.allowed ?? [];
    if (!c.allowed && c.doesWhat === "updates") may = e.fields.filter((f) => !f.key && f.serial === undefined).map((f) => f.name);
    if (c.doesWhat !== "deletes") for (const name of may) if (name in given) v[name] = copied(given[name]!);
    const done = body(v);
    if (done) return done;
    if (c.doesWhat === "deletes") {
      step.remove(e.name, id);
      return { id };
    }
    keepFixed(e, before, v);
    const checked = validate(e, v);
    if (checked) return checked;
    v["updated_at"] = now;
    step.update(e.name, id, v);
    return { id };
  }
}

// What a command answers: the id of what it changed, or why it didn't.
export type Answer = { id: string } | Failure;

// What a source is given by a package: how to run each command, in a step of its
// own, and how to build each view for its subject, or none when there's nothing to
// show.
export type CommandHandler = (sent: Row, step: Step) => Answer;
export type ViewBuilder = (rows: Rows, subject: string) => Row | undefined;
export interface Installing {
  on(command: string, run: CommandHandler, apart?: boolean): void;
  define(view: string, build: ViewBuilder): void;
}

// A namespace's entities, commands and views, as one package. (package is a word
// JavaScript keeps for itself, so it's localPackage.)
export function localPackage(name: string, entities: EntityDescription[], commands: CommandDescription[], views: ViewDescription[]): LocalPackage {
  return new LocalPackage(name, entities, commands, views);
}
