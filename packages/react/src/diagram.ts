// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A block diagram, signals flowing through blocks, written a line at a time and laid out
// by itself, its blocks' faces set by math.ts (math/README.md). The twin of one::math's
// diagram.hpp, both held to math/tests/diagrams.txt.

import { layOutMath, parseMath } from "./math.js";
import type { MathProblem } from "./math.js";

export type DiagramShapeKind = "port" | "sum" | "gain" | "box" | "constant" | "system";

// A block drawn, flipped when it's on a chain fed back, facing left.
export interface DiagramShape {
  type: DiagramShapeKind;
  x: number;
  y: number;
  width: number;
  height: number;
  flipped: boolean;
}

// A wire, with an arrow where it goes in; or the stub to where it branches, without one.
export interface DiagramWire {
  points: [number, number][];
  arrow: boolean;
}

// Something written, set by math.ts at a size: its left and its baseline.
export interface DiagramFace {
  written: string;
  size: number;
  x: number;
  y: number;
}

// Everything in ems from the top left.
export interface DiagramLaidOut {
  width: number;
  height: number;
  shapes: DiagramShape[];
  wires: DiagramWire[];
  dots: [number, number][];
  faces: DiagramFace[];
}

const columnGap = 1.8, rowGap = 1.0, label = 0.7, lead = 0.6;

class Failed {
  constructor(readonly problem: MathProblem) {}
}
const fail = (at: number, says: string): never => {
  throw new Failed({ at, says });
};

interface Measured {
  width: number;
  ascent: number;
  descent: number;
}
const measured = new Map<string, Measured>();
function measure(written: string, size: number): Measured {
  const key = `${size} ${written}`;
  let m = measured.get(key);
  if (!m) {
    const read = parseMath(written);
    if (!("node" in read)) m = { width: 0, ascent: 0, descent: 0 };
    else {
      const laid = layOutMath(read.node, size, false);
      m = { width: laid.width, ascent: laid.ascent, descent: laid.descent };
    }
    measured.set(key, m);
  }
  return m;
}
const words = (s: string) => `"${s}"`;

const nameCharacter = (c: string) => /[A-Za-z0-9_]/.test(c) || c.charCodeAt(0) >= 0x80;
const isName = (s: string) => s !== "" && !/[0-9]/.test(s[0]!) && [...s].every(nameCharacter);

// Some text and where it starts in what was written.
interface Piece {
  text: string;
  at: number;
}
function trimmed(p: Piece): Piece {
  let { text, at } = p;
  while (text !== "" && /\s/.test(text[0]!)) (text = text.slice(1)), ++at;
  return { text: text.trimEnd(), at };
}
// Split where `apart` is, but not within brackets or quotes.
function split(whole: Piece, apart: string): Piece[] {
  const out: Piece[] = [];
  let depth = 0,
    quoted = false,
    from = 0;
  for (let i = 0; i < whole.text.length; ++i) {
    const c = whole.text[i]!;
    if (c === '"') quoted = !quoted;
    if (quoted) continue;
    if (c === "(" || c === "[" || c === "{") ++depth;
    if (c === ")" || c === "]" || c === "}") --depth;
    if (depth === 0 && whole.text.startsWith(apart, i)) {
      out.push(trimmed({ text: whole.text.slice(from, i), at: whole.at + from }));
      from = i + apart.length;
      i = from - 1;
    }
  }
  out.push(trimmed({ text: whole.text.slice(from), at: whole.at + from }));
  return out;
}

interface Node {
  type: DiagramShapeKind;
  face: string;
  label: string;
  ins: string[]; // a block of its own's ports
  outs: string[];
  signs: number[]; // a sum's, one an input
  number: number; // a port's
  output: boolean; // a port out
  inputs: number;
  outputs: number;
  x: number;
  y: number;
  w: number;
  h: number;
  below: number;
  across: number; // as wide as it and what's written under it
  layer: number;
  feedback: boolean;
  above: boolean;
  flipped: boolean;
  sides: number[]; // a sum's inputs: 0 left, 1 above, 2 below
}
const node = (type: DiagramShapeKind): Node => ({
  type, face: "", label: "", ins: [], outs: [], signs: [], number: 0, output: false, inputs: 0, outputs: 0,
  x: 0, y: 0, w: 0, h: 0, below: 0, across: 0, layer: -1, feedback: false, above: false, flipped: false, sides: [],
});
interface Edge {
  from: number;
  fromPort: number;
  to: number;
  toPort: number;
  back: boolean;
}
interface Use {
  name: string;
  at: number;
  to: number;
  port: number;
}
type Made = [number, number];

class Builder {
  nodes: Node[] = [];
  edges: Edge[] = [];
  private makes = new Map<string, Made>();
  private uses: Use[] = [];
  private ins = 0;
  private outs = 0;

  statement(whole: Piece) {
    const line = trimmed(whole);
    if (line.text === "" || line.text.startsWith("//")) return;
    if (line.text.startsWith("in ")) return this.ports(trimmed({ text: line.text.slice(3), at: line.at + 3 }), false);
    if (line.text.startsWith("out ")) return this.ports(trimmed({ text: line.text.slice(4), at: line.at + 4 }), true);
    const chain = split(line, "->");
    if (chain.length === 1) return this.system(line);
    const first = chain[0]!;
    let from: Made = [0, 0];
    let waiting = ""; // a signal the chain starts with, until it's known what makes it
    if (isName(first.text)) waiting = first.text;
    else from = [this.sum(first), 0];
    for (let i = 1; i + 1 < chain.length; ++i) {
      const made = this.block(chain[i]!);
      this.connect(waiting, from, made, 0, chain[i - 1]!.at);
      waiting = "";
      from = [made, 0];
    }
    const last = chain[chain.length - 1]!;
    if (!isName(last.text)) fail(last.at, "what a chain makes is named at its end, like -> y");
    if (waiting !== "") fail(last.at, `nothing between ${waiting} and ${last.text}; put a block there, like ${waiting} -> *2 -> ${last.text}`);
    this.make(last.text, from, last.at);
  }

  // What's used, joined to what makes it; a name nothing makes, a constant.
  resolve() {
    const constants = new Map<string, number>();
    for (const u of this.uses) {
      const made = this.makes.get(u.name);
      if (made) {
        this.edges.push({ from: made[0], fromPort: made[1], to: u.to, toPort: u.port, back: false });
        continue;
      }
      if (this.nodes[u.to]!.type === "port") fail(u.at, `nothing makes ${u.name} for out to show`);
      let c = constants.get(u.name);
      if (c === undefined) {
        c = this.nodes.length;
        constants.set(u.name, c);
        const n = node("constant");
        n.face = u.name;
        n.outputs = 1;
        this.nodes.push(n);
      }
      this.edges.push({ from: c, fromPort: 0, to: u.to, toPort: u.port, back: false });
    }
    if (this.nodes.length === 0) fail(0, "something to draw, like in x and x -> *2 -> y");
  }

  private make(name: string, by: Made, at: number) {
    if (this.makes.has(name)) fail(at, `two things make ${name}`);
    this.makes.set(name, by);
  }
  private connect(waiting: string, from: Made, to: number, port: number, at: number) {
    if (waiting !== "") this.uses.push({ name: waiting, at, to, port });
    else this.edges.push({ from: from[0], fromPort: from[1], to, toPort: port, back: false });
  }

  private ports(list: Piece, out: boolean) {
    for (const item of split(list, ",")) {
      const named = split(item, " as ");
      const signal = named[0]!.text, shown = named.length > 1 ? named[1]!.text : named[0]!.text;
      if (!isName(signal) || !isName(shown) || named.length > 2) fail(item.at, out ? "an output is a name, like out y, or out y as speed" : "an input is a name, like in x, y");
      const p = node("port");
      p.output = out;
      p.number = out ? ++this.outs : ++this.ins;
      p.label = shown;
      p.inputs = out ? 1 : 0;
      p.outputs = out ? 0 : 1;
      this.nodes.push(p);
      if (out) this.uses.push({ name: signal, at: item.at, to: this.nodes.length - 1, port: 0 });
      else this.make(signal, [this.nodes.length - 1, 0], item.at);
    }
  }

  // a - b + c: a sum, its inputs' signs.
  private sum(written: Piece): number {
    const s = node("sum");
    const t = written.text;
    const terms: [string, number][] = [];
    let i = 0;
    const space = () => {
      while (i < t.length && t[i] === " ") ++i;
    };
    space();
    while (i < t.length) {
      let sign = 1;
      if (t[i] === "+" || t[i] === "-") (sign = t[i] === "-" ? -1 : 1), ++i;
      else if (terms.length > 0) fail(written.at + i, "a + or - between what's summed, like a - b");
      space();
      const from = i;
      while (i < t.length && nameCharacter(t[i]!)) ++i;
      if (!isName(t.slice(from, i))) fail(written.at + from, "a chain starts with a signal, or a sum of them, like a - b");
      terms.push([t.slice(from, i), written.at + from]);
      s.signs.push(sign);
      space();
    }
    if (terms.length === 0) fail(written.at, "a chain starts with a signal, or a sum of them, like a - b");
    s.inputs = terms.length;
    s.outputs = 1;
    this.nodes.push(s);
    terms.forEach(([name, at], k) => this.uses.push({ name, at, to: this.nodes.length - 1, port: k }));
    return this.nodes.length - 1;
  }

  // *k, a gain; anything else a box with it written in it; a name in quotes after either,
  // under it.
  private block(written: Piece): number {
    const b = node("box");
    let t = written.text;
    if (t.endsWith('"')) {
      const open = t.slice(0, -1).lastIndexOf('"');
      if (open < 0) fail(written.at, 'a " to close the name under the block');
      b.label = t.slice(open + 1, -1);
      t = t.slice(0, open).trim();
    }
    let at = written.at;
    if (t.startsWith("*")) {
      b.type = "gain";
      t = t.slice(1);
      ++at;
      while (t.startsWith(" ")) (t = t.slice(1)), ++at;
    }
    if (t === "") fail(written.at, "what the block does, like *k or 1/s");
    const read = parseMath(t);
    if (!("node" in read)) fail(at + read.problem.at, read.problem.says);
    b.face = t;
    b.inputs = b.outputs = 1;
    this.nodes.push(b);
    return this.nodes.length - 1;
  }

  // f(a, b -> c): a block of its own, its ports named.
  private system(line: Piece) {
    const t = line.text;
    const open = t.indexOf("(");
    if (open < 0 || !t.endsWith(")") || !isName(t.slice(0, open).trim())) {
      fail(line.at, "a chain, like a -> *k -> b; ports, like in a or out b; or a block of its own, like f(a -> b)");
    }
    const inside: Piece = { text: t.slice(open + 1, -1), at: line.at + open + 1 };
    const sides = split(inside, "->");
    if (sides.length !== 2) fail(inside.at, "a block of its own's inputs, then -> and its outputs, like f(a, b -> c)");
    const s = node("system");
    s.label = t.slice(0, open).trim();
    const ins = sides[0]!.text === "" ? [] : split(sides[0]!, ",");
    const outs = sides[1]!.text === "" ? [] : split(sides[1]!, ",");
    for (const p of [...ins, ...outs]) {
      if (!isName(p.text)) fail(p.at, "a port is a name, like f(a -> b)");
    }
    s.ins = ins.map((p) => p.text);
    s.outs = outs.map((p) => p.text);
    s.inputs = s.ins.length;
    s.outputs = s.outs.length;
    this.nodes.push(s);
    const me = this.nodes.length - 1;
    ins.forEach((p, k) => this.uses.push({ name: p.text, at: p.at, to: me, port: k }));
    outs.forEach((p, k) => this.make(p.text, [me, k], p.at));
  }
}

// Where a port is, from a block's top left, and which way a wire leaves or comes in.
interface PortPlace {
  x: number;
  y: number;
  dx: number;
  dy: number;
}

class Layout {
  private chains: [number, number[]][] = []; // what each chain fed back feeds, and its blocks, from where it branches
  private detour = 0;
  private widths: number[] = [];

  constructor(
    private n: Node[],
    private e: Edge[],
  ) {}

  run(): DiagramLaidOut {
    this.sizes();
    this.backs();
    this.feedbacks();
    this.layers();
    this.columns();
    this.rows();
    this.aboves();
    this.lanes();
    this.sides();
    const d: DiagramLaidOut = { width: 0, height: 0, shapes: [], wires: [], dots: [], faces: [] };
    for (const n of this.n) d.shapes.push({ type: n.type, x: n.x, y: n.y, width: n.w, height: n.h, flipped: n.flipped });
    this.wires(d);
    this.faces(d);
    this.fit(d);
    return d;
  }

  private into(v: number) {
    const out: number[] = [];
    this.e.forEach((e, i) => e.to === v && out.push(i));
    return out;
  }
  private from(v: number, port: number) {
    const out: number[] = [];
    this.e.forEach((e, i) => e.from === v && e.fromPort === port && out.push(i));
    return out;
  }

  private sizes() {
    for (const n of this.n) {
      let under = 0;
      if (n.label !== "") {
        const m = measure(words(n.label), label);
        n.below = 0.25 + m.ascent + m.descent;
        under = m.width;
      }
      if (n.type === "port") (n.w = 1.4), (n.h = 0.9);
      else if (n.type === "sum") n.w = n.h = 1.0;
      else if (n.type === "gain") {
        // Wide enough that what's written in it, from near its back, ends where it's still
        // tall enough to hold it.
        const m = measure(n.face, 1);
        n.w = Math.max(2.0, 2 * (m.width + 0.4) + 0.4);
        n.h = Math.max(1.6, (m.ascent + m.descent + 0.2) / (1 - (m.width + 0.4) / n.w));
      } else if (n.type === "box") {
        const m = measure(n.face, 1);
        n.h = Math.max(1.6, m.ascent + m.descent + 0.6);
        n.w = Math.max(1.8, m.width + 1.0);
      } else if (n.type === "constant") (n.w = measure(words(n.face), 1).width + 0.8), (n.h = 1.3);
      else {
        let ins = 0,
          outs = 0;
        for (const p of n.ins) ins = Math.max(ins, measure(words(p), label).width);
        for (const p of n.outs) outs = Math.max(outs, measure(words(p), label).width);
        n.w = Math.max(4.0, ins + outs + 2.0);
        n.h = Math.max(1.6, Math.max(n.ins.length, n.outs.length) * 1.1 + 0.5);
      }
      n.across = Math.max(n.w, under);
    }
  }

  // A wire to a block that's still being followed from it is one fed back.
  private backs() {
    const state = this.n.map(() => 0);
    const visit = (v: number) => {
      state[v] = 1;
      for (const e of this.e) {
        if (e.from !== v) continue;
        if (state[e.to] === 1) e.back = true;
        else if (state[e.to] === 0) visit(e.to);
      }
      state[v] = 2;
    };
    this.n.forEach((n, v) => n.type === "port" && !n.output && state[v] === 0 && visit(v));
    this.n.forEach((_, v) => state[v] === 0 && this.into(v).length === 0 && visit(v));
    this.n.forEach((_, v) => state[v] === 0 && visit(v));
  }

  // The blocks on a chain fed back: from the one that feeds back, back to where the
  // signal it's made from branches.
  private feedbacks() {
    const n = this.n;
    const passes = (v: number) => n[v]!.type === "gain" || n[v]!.type === "box";
    for (const e of this.e) {
      if (!e.back) continue;
      const chain: number[] = [];
      let at = e.from;
      while (passes(at) && !n[at]!.feedback && this.from(at, 0).length === 1) {
        chain.push(at);
        const fed = this.into(at);
        if (fed.length !== 1) break;
        const into = this.e[fed[0]!]!;
        if (this.from(into.from, into.fromPort).length > 1) break;
        if (!passes(into.from)) break;
        at = into.from;
      }
      if (chain.length === 0) continue;
      chain.reverse();
      for (const c of chain) n[c]!.feedback = n[c]!.flipped = true;
      this.chains.push([e.to, chain]);
    }
  }

  private main(v: number) {
    return !this.n[v]!.feedback && !this.n[v]!.above;
  }

  // Each block a column after everything that feeds it; outputs in the last; a constant
  // fed only into sums is over the sum it's fed into.
  private layers() {
    const n = this.n;
    n.forEach((c, v) => {
      if (c.type !== "constant") return;
      const fed = this.from(v, 0);
      c.above = fed.length > 0 && fed.every((i) => n[this.e[i]!.to]!.type === "sum" && this.e[i]!.toPort > 0);
    });
    for (const c of n) c.layer = 0;
    let moved = true;
    for (let round = 0; moved && round <= n.length; ++round) {
      moved = false;
      for (const e of this.e) {
        if (e.back || !this.main(e.from) || !this.main(e.to) || n[e.to]!.type === "port") continue;
        if (n[e.to]!.layer < n[e.from]!.layer + 1) (n[e.to]!.layer = n[e.from]!.layer + 1), (moved = true);
      }
    }
    let last = 0;
    for (const c of n) if (!(c.type === "port" && c.output)) last = Math.max(last, c.layer);
    for (const c of n) if (c.type === "port" && c.output) c.layer = last + 1;
    // A constant fed into blocks, in the column before the first of them.
    n.forEach((c, v) => {
      if (c.type !== "constant" || c.above) return;
      let fed = 1 << 20;
      for (const i of this.from(v, 0)) fed = Math.min(fed, n[this.e[i]!.to]!.layer);
      c.layer = Math.max(0, fed - 1);
    });
  }

  private columns() {
    let count = 0;
    this.n.forEach((c, v) => this.main(v) && (count = Math.max(count, c.layer + 1)));
    this.widths = new Array<number>(count).fill(0);
    this.n.forEach((c, v) => this.main(v) && (this.widths[c.layer] = Math.max(this.widths[c.layer]!, c.across)));
    const lefts: number[] = [];
    let x = 0;
    for (const w of this.widths) lefts.push(x), (x += w + columnGap);
    this.n.forEach((c, v) => this.main(v) && (c.x = lefts[c.layer]! + (this.widths[c.layer]! - c.w) / 2));
  }

  // Where a block's port is: its inputs, then its outputs.
  private inPort(n: Node, i: number): PortPlace {
    const left = n.flipped ? n.w : 0,
      way = n.flipped ? -1 : 1;
    if (n.type === "sum") {
      const side = n.sides[i] ?? 0;
      if (side === 1) return { x: n.w / 2, y: 0, dx: 0, dy: 1 };
      if (side === 2) return { x: n.w / 2, y: n.h, dx: 0, dy: -1 };
      return { x: left, y: n.h / 2, dx: way, dy: 0 };
    }
    const count = Math.max(n.inputs, 1);
    return { x: left, y: (n.h * (i + 1)) / (count + 1), dx: way, dy: 0 };
  }
  private outPort(n: Node, i: number): PortPlace {
    if (n.above) return { x: n.w / 2, y: n.h, dx: 0, dy: 1 };
    const right = n.flipped ? 0 : n.w,
      way = n.flipped ? -1 : 1;
    const count = Math.max(n.outputs, 1);
    return { x: right, y: (n.h * (i + 1)) / (count + 1), dx: way, dy: 0 };
  }

  // Each block level with what feeds it, and under the one before it in its column.
  private rows() {
    const n = this.n;
    for (let l = 0; l < this.widths.length; ++l) {
      let bottom = -1e9;
      n.forEach((c, v) => {
        if (!this.main(v) || c.layer !== l) return;
        let want: number | undefined;
        const fed = this.into(v).sort((a, b) => this.e[a]!.toPort - this.e[b]!.toPort);
        for (const i of fed) {
          const e = this.e[i]!;
          if (e.back || !this.main(e.from) || n[e.from]!.layer >= l) continue;
          want = n[e.from]!.y + this.outPort(n[e.from]!, e.fromPort).y - this.inPort(c, e.toPort).y;
          break;
        }
        let top = want ?? bottom + rowGap;
        if (bottom > -1e9) top = Math.max(top, bottom + rowGap);
        c.y = top;
        bottom = c.y + c.h + c.below;
      });
    }
  }

  // A constant over the sum it's fed into.
  private aboves() {
    this.n.forEach((c, v) => {
      if (!c.above) return;
      const s = this.n[this.e[this.from(v, 0)[0]!]!.to]!;
      c.x = s.x + s.w / 2 - c.w / 2;
      c.y = s.y - 0.9 - c.h;
    });
  }

  // How far down what's drawn between two places across reaches.
  private reach(left: number, right: number) {
    let bottom = -1e9;
    for (const c of this.n) {
      if (c.feedback && c.layer === -2) continue;
      if (c.x + c.across < left || c.x > right) continue;
      bottom = Math.max(bottom, c.y + c.h + c.below);
    }
    return bottom;
  }

  // How far up what's drawn between two places across reaches.
  private rise(left: number, right: number) {
    let top = 1e9;
    for (const c of this.n) {
      if (c.x + c.across < left || c.x > right) continue;
      top = Math.min(top, c.y);
    }
    return top;
  }

  // A chain fed back, under what's between where it branches and what it feeds, right to
  // left.
  private lanes() {
    const n = this.n;
    for (const c of n) if (c.feedback) c.layer = -2;
    for (const [target, chain] of this.chains) {
      const t = n[target]!;
      const fed = this.e[this.into(chain[0]!)[0]!]!;
      const source = n[fed.from]!;
      let branch = source.x + this.outPort(source, fed.fromPort).x + lead;
      const to = t.x + t.w / 2;
      let tall = 0,
        wide = columnGap * (chain.length - 1);
      for (const c of chain) (tall = Math.max(tall, n[c]!.h)), (wide += n[c]!.across);
      // Room for it between them, everything after what it feeds moved over if not.
      const shortBy = wide + 2 * columnGap - (branch - to);
      if (shortBy > 0) {
        for (const c of n) if (c.layer !== -2 && c.x > to) c.x += shortBy;
        branch += shortBy;
      }
      const top = this.reach(Math.min(to, branch) - 0.5, Math.max(to, branch) + 0.5) + rowGap * 1.2;
      let right = branch - Math.max(columnGap, (branch - to - wide) / 2);
      for (const v of chain) {
        const c = n[v]!;
        c.x = right - c.across / 2 - c.w / 2;
        c.y = top + (tall - c.h) / 2;
        c.layer = -1;
        right -= c.across + columnGap;
      }
    }
  }

  // A sum's inputs after its first: from above it or below, as what feeds each is.
  private sides() {
    this.n.forEach((c, v) => {
      if (c.type !== "sum") return;
      c.sides = new Array<number>(c.inputs).fill(0);
      const others: [number, number][] = [];
      for (const i of this.into(v)) {
        const e = this.e[i]!;
        if (e.toPort === 0) continue;
        const s = this.n[e.from]!;
        others.push([s.y + this.outPort(s, e.fromPort).y, e.toPort]);
      }
      others.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
      if (others.length === 1) c.sides[others[0]![1]] = others[0]![0] < c.y + c.h / 2 ? 1 : 2;
      else if (others.length > 0) (c.sides[others[0]![1]] = 1), (c.sides[others[others.length - 1]![1]] = 2);
    });
  }

  private wires(d: DiagramLaidOut) {
    const branched = new Set<string>();
    let low = -1e9;
    for (const c of this.n) low = Math.max(low, c.y + c.h + c.below);
    for (const e of this.e) {
      const a = this.n[e.from]!,
        b = this.n[e.to]!;
      const out = this.outPort(a, e.fromPort),
        into = this.inPort(b, e.toPort);
      let sx = a.x + out.x,
        sy = a.y + out.y;
      const qx = b.x + into.x,
        qy = b.y + into.y;
      const points: [number, number][] = [];
      const branch = this.from(e.from, e.fromPort).length > 1;
      if (branch) {
        const bx = sx + out.dx * lead,
          by = sy + out.dy * lead;
        const key = `${e.from} ${e.fromPort}`;
        if (!branched.has(key)) {
          branched.add(key);
          d.wires.push({ points: [[sx, sy], [bx, by]], arrow: false });
          d.dots.push([bx, by]);
        }
        (sx = bx), (sy = by);
      }
      points.push([sx, sy]);
      if (into.dy !== 0) {
        // Into a sum from above or below: across, then down or up to it; or, from the other
        // side of it, around its left first.
        const far = (sy - qy) * into.dy > 0;
        if (!far && out.dy !== 0 && Math.abs(sx - qx) > 1e-9) {
          const mid = (sy + qy) / 2;
          points.push([sx, mid], [qx, mid]);
        } else if (!far && Math.abs(sx - qx) > 1e-9) {
          points.push([qx, sy]);
        } else if (far && sx > qx + 1e-9 && e.back) {
          // Fed back from after it: under what's between them, or over it.
          const side = into.dy < 0 ? this.reach(qx, sx) + lead : this.rise(qx, sx) - lead;
          points.push([sx, side], [qx, side]);
        } else if (far) {
          const aside = b.x - lead,
            over = qy - into.dy * lead;
          points.push([aside, sy], [aside, over], [qx, over]);
        }
      } else {
        const ahead = into.dx * (qx - sx);
        const same = Math.abs(sy - qy) < 1e-9;
        if (ahead > 0 && (branch || into.dx === out.dx)) {
          if (!same && branch) points.push([sx, qy]);
          else if (!same) {
            const mid = (sx + qx) / 2;
            points.push([mid, sy], [mid, qy]);
          }
        } else {
          // Behind where it leaves: out, down under everything, back and in.
          this.detour += 0.6;
          const under = low + 0.2 + this.detour;
          const off = branch ? sx : sx + out.dx * lead;
          if (!branch) points.push([off, sy]);
          points.push([off, under], [qx - into.dx * lead, under], [qx - into.dx * lead, qy]);
        }
      }
      points.push([qx, qy]);
      d.wires.push({ points, arrow: true });
    }
  }

  // Words and faces, centered where they go.
  private centered(d: DiagramLaidOut, written: string, size: number, cx: number, cy: number) {
    const m = measure(written, size);
    d.faces.push({ written, size, x: cx - m.width / 2, y: cy + (m.ascent - m.descent) / 2 });
  }

  private faces(d: DiagramLaidOut) {
    for (const n of this.n) {
      const cx = n.x + n.w / 2,
        cy = n.y + n.h / 2;
      if (n.type === "port") this.centered(d, words(String(n.number)), 0.75, cx, cy);
      else if (n.type === "sum") {
        n.signs.forEach((sign, i) => {
          const p = this.inPort(n, i);
          const ox = p.dx !== 0 ? -p.dx * 0.24 : 0,
            oy = p.dy !== 0 ? -p.dy * 0.24 : 0;
          this.centered(d, words(sign < 0 ? "−" : "+"), 0.6, cx + ox, cy + oy);
        });
      } else if (n.type === "gain") {
        const half = measure(n.face, 1).width / 2 + 0.4;
        this.centered(d, n.face, 1, n.flipped ? n.x + n.w - half : n.x + half, cy);
      } else if (n.type === "box") this.centered(d, n.face, 1, cx, cy);
      else if (n.type === "constant") this.centered(d, words(n.face), 1, cx, cy);
      else {
        n.ins.forEach((p, i) => {
          const m = measure(words(p), label);
          const x = n.flipped ? n.x + n.w - 0.25 - m.width : n.x + 0.25;
          d.faces.push({ written: words(p), size: label, x, y: n.y + this.inPort(n, i).y + (m.ascent - m.descent) / 2 });
        });
        n.outs.forEach((p, i) => {
          const m = measure(words(p), label);
          const x = n.flipped ? n.x + 0.25 : n.x + n.w - 0.25 - m.width;
          d.faces.push({ written: words(p), size: label, x, y: n.y + this.outPort(n, i).y + (m.ascent - m.descent) / 2 });
        });
      }
      if (n.label !== "") {
        const m = measure(words(n.label), label);
        d.faces.push({ written: words(n.label), size: label, x: n.x + n.w / 2 - m.width / 2, y: n.y + n.h + 0.25 + m.ascent });
      }
    }
  }

  // Everything moved to start a little in from the top left, and as big as it is.
  private fit(d: DiagramLaidOut) {
    let left = 1e9,
      top = 1e9,
      right = -1e9,
      bottom = -1e9;
    const take = (x: number, y: number) => {
      (left = Math.min(left, x)), (top = Math.min(top, y)), (right = Math.max(right, x)), (bottom = Math.max(bottom, y));
    };
    for (const s of d.shapes) take(s.x, s.y), take(s.x + s.width, s.y + s.height);
    for (const w of d.wires) for (const [x, y] of w.points) take(x, y);
    for (const f of d.faces) {
      const m = measure(f.written, f.size);
      take(f.x, f.y - m.ascent), take(f.x + m.width, f.y + m.descent);
    }
    const dx = 0.3 - left,
      dy = 0.3 - top;
    for (const s of d.shapes) (s.x += dx), (s.y += dy);
    for (const w of d.wires) for (const p of w.points) (p[0] += dx), (p[1] += dy);
    for (const p of d.dots) (p[0] += dx), (p[1] += dy);
    for (const f of d.faces) (f.x += dx), (f.y += dy);
    d.width = right - left + 0.6;
    d.height = bottom - top + 0.6;
  }
}

// A diagram read and laid out, or why it can't be.
export function layOutDiagram(written: string): { diagram: DiagramLaidOut } | { problem: MathProblem } {
  try {
    const b = new Builder();
    for (let at = 0; at <= written.length; ) {
      let end = written.slice(at).search(/[\n;]/);
      end = end < 0 ? written.length : at + end;
      b.statement({ text: written.slice(at, end), at });
      at = end + 1;
    }
    b.resolve();
    return { diagram: new Layout(b.nodes, b.edges).run() };
  } catch (e) {
    if (e instanceof Failed) return { problem: e.problem };
    throw e;
  }
}

// What's drawn, a line each, to a hundredth of an em, as diagram.hpp's written() writes it.
export function writtenDiagram(d: DiagramLaidOut): string {
  const f = (v: number) => v.toFixed(2);
  let out = `${f(d.width)} ${f(d.height)}\n`;
  for (const s of d.shapes) out += `${s.type} ${f(s.x)} ${f(s.y)} ${f(s.width)} ${f(s.height)}${s.flipped ? " flipped" : ""}\n`;
  for (const w of d.wires) out += `${w.arrow ? "wire" : "stub"}${w.points.map(([x, y]) => ` ${f(x)},${f(y)}`).join("")}\n`;
  for (const [x, y] of d.dots) out += `dot ${f(x)},${f(y)}\n`;
  for (const t of d.faces) out += `face ${f(t.size)} ${f(t.x)} ${f(t.y)} ${t.written}\n`;
  return out;
}
