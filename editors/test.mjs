// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Checks that Vim and VS Code highlight every .one file named in expected.tsv the
// same way, and the way expected.tsv says. Run: node editors/test.mjs
//
// Vim is run for real, headless. VS Code's grammar is run through the small
// TextMate tokenizer below, which covers only the rule shapes the grammar uses
// (match, begin/end, captures, include). Grammar features beyond those will not
// be understood here, so keep the grammar to them.

import { readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");

const specs = readFileSync(join(here, "expected.tsv"), "utf8")
  .split("\n")
  .filter((l) => l.trim() && !l.startsWith("#"))
  .map((l) => {
    // A piece writes a tab as \t, since the file itself is separated by tabs.
    const [file, written, token, want] = l.split("\t");
    const piece = written.replaceAll("\\t", "\t");
    const lines = readFileSync(join(root, file), "utf8").split("\n");
    const line = lines.findIndex((s) => s.includes(piece));
    const col = line < 0 ? -1 : lines[line].indexOf(token, lines[line].indexOf(piece));
    return { file, piece, token, want, line, col };
  });
const files = [...new Set(specs.map((s) => s.file))];

// Vim: the syntax group under each token.
const vimGroups = {
  uioneComment: "comment", uioneTodo: "comment", uioneDeclare: "declare",
  uioneName: "name", uioneFormatName: "name", uioneNamespaceName: "namespace",
  uioneQualifier: "namespace", uioneNamespace: "namespace", uioneScope: "punct",
  uionePattern: "pattern", uioneKeyword: "keyword", uioneStatement: "keyword",
  uioneModifier: "modifier", uioneType: "type", uioneBuiltin: "constant",
  uioneConstant: "constant", uioneLiteral: "string", uioneCall: "function",
  uioneNumber: "number", uioneOperator: "operator", uioneRoute: "route",
  uioneParam: "param", uioneString: "string", uioneInterpDelim: "interp",
  uioneInterp: "plain", uioneFieldName: "variable", uioneUserType: "type", uioneFieldWord: "keyword",
  uioneFields: "plain", uioneMember: "plain", uioneListOf: "type", uioneListType: "type", "": "plain",
  uioneSetting: "keyword", uioneAssigned: "variable", uioneDotted: "plain", uioneProject: "plain", uioneProjectBraces: "plain", uioneProjectBrace: "plain",
};

function vim(file, specs) {
  const dir = mkdtempSync(join(tmpdir(), "uione-"));
  const positions = join(dir, "positions");
  const out = join(dir, "groups");
  writeFileSync(positions, specs.map((s) => `${s.line + 1} ${s.col + 1}`).join("\n"));
  const script = join(dir, "probe.vim");
  writeFileSync(script, `
    let g:out = []
    for p in readfile('${positions}')
      let [l, c] = split(p)
      call add(g:out, synIDattr(synID(str2nr(l), str2nr(c), 1), 'name'))
    endfor
    call writefile(g:out, '${out}')
  `);
  execFileSync("vim", [
    "-Nu", "NONE", "-i", "NONE", "-es",
    "--cmd", `set rtp^=${join(here, "vim")}`,
    "-c", "filetype on", "-c", "syntax on",
    "-c", `edit ${join(root, file)}`, "-c", `source ${script}`, "-c", "qa!",
  ]);
  return readFileSync(out, "utf8").split("\n").map((g) => vimGroups[g] ?? `?${g}`);
}

// VS Code: the innermost TextMate scope under each token.
const scopeCategories = [
  ["comment", "comment"], ["keyword.other.todo", "comment"],
  ["punctuation.section.embedded", "interp"], ["meta.embedded", "plain"],
  ["punctuation.separator.namespace", "punct"], ["entity.name.namespace", "namespace"],
  ["storage.type", "declare"], ["keyword.control", "keyword"], ["keyword.other", "keyword"],
  ["keyword.operator", "operator"], ["storage.modifier", "modifier"], ["support.type", "type"],
  ["entity.name.function.call", "function"], ["entity.name", "name"],
  ["string.regexp", "pattern"], ["variable.parameter", "param"],
  ["string.other.path", "route"], ["string", "string"], ["constant.numeric", "number"],
  ["constant", "constant"], ["variable.other", "variable"], ["source", "plain"],
];

function tokenize(grammar, lines) {
  const repo = grammar.repository;
  const expand = (patterns) =>
    patterns.flatMap((p) => {
      if (!p.include) return [p];
      const r = p.include === "$self" ? { patterns: grammar.patterns } : repo[p.include.slice(1)];
      return r.patterns && !r.begin ? expand(r.patterns) : [r];
    });
  const re = (src) => new RegExp(src, "dg");
  const find = (src, s, pos) => {
    const r = re(src);
    r.lastIndex = pos;
    return r.exec(s);
  };

  const stack = [{ rule: { patterns: grammar.patterns }, scopes: [grammar.scopeName] }];
  return lines.map((s) => {
    const scopes = new Array(s.length).fill(null);
    const paint = (from, to, sc) => { for (let i = from; i < to; i++) scopes[i] = sc; };
    const capture = (m, caps, base) => {
      for (const [k, v] of Object.entries(caps ?? {})) {
        const at = m.indices[+k];
        if (at) paint(at[0], at[1], [...base, v.name]);
      }
    };
    let pos = 0;
    for (let guard = 0; pos <= s.length && guard < 10000; guard++) {
      const top = stack.at(-1);
      let best = null;
      if (top.rule.end) {
        const m = find(top.rule.end, s, pos);
        if (m) best = { kind: "end", m };
      }
      for (const rule of expand(top.rule.patterns ?? [])) {
        const m = find(rule.match ?? rule.begin, s, pos);
        if (m && (!best || m.index < best.m.index)) best = { kind: rule.match ? "match" : "begin", rule, m };
      }
      if (!best) { paint(pos, s.length, top.scopes); break; }
      const { kind, rule, m } = best;
      paint(pos, m.index, top.scopes);
      const end = m.index + m[0].length;
      if (kind === "end") {
        paint(m.index, end, top.scopes);
        capture(m, top.rule.endCaptures, top.scopes);
        stack.pop();
      } else {
        const base = rule.name ? [...top.scopes, rule.name] : top.scopes;
        paint(m.index, end, base);
        capture(m, kind === "match" ? rule.captures : rule.beginCaptures, base);
        if (kind === "begin") stack.push({ rule, scopes: base });
      }
      if (end === pos && kind === "match") pos++;
      else pos = end;
      if (pos >= s.length && kind === "match") { paint(pos, s.length, top.scopes); break; }
    }
    return scopes;
  });
}

const grammar = JSON.parse(readFileSync(join(here, "vscode/syntaxes/uione.tmlanguage.json"), "utf8"));

function vscode(file, specs) {
  const scopes = tokenize(grammar, readFileSync(join(root, file), "utf8").split("\n"));
  return specs.map(({ line, col }) => {
    const inner = scopes[line]?.[col]?.at(-1) ?? "";
    return scopeCategories.find(([prefix]) => inner.startsWith(prefix))?.[1] ?? `?${inner}`;
  });
}

let failures = 0;
for (const file of files) {
  const mine = specs.filter((s) => s.file === file);
  const fromVim = vim(file, mine);
  const fromCode = vscode(file, mine);
  mine.forEach((s, i) => {
    const ok = s.line >= 0 && s.col >= 0 && fromVim[i] === s.want && fromCode[i] === s.want;
    if (!ok) {
      failures++;
      const where = s.line < 0 ? "piece not found" : `${file}:${s.line + 1}`;
      console.log(`FAIL  ${JSON.stringify(s.token)} in ${JSON.stringify(s.piece)} (${where}): want ${s.want}, vim ${fromVim[i]}, vscode ${fromCode[i]}`);
    }
  });
}
console.log(`${specs.length - failures}/${specs.length} tokens in ${files.length} files highlighted as expected in both editors`);

// The words each editor grammar picks out are exactly the words of the language,
// as docs/grammar.ebnf names them, so a word added to the language can't be
// forgotten in an editor, or an editor keep one the language dropped.
const grammarWords = new Set(
  [...readFileSync(join(root, "docs/grammar.ebnf"), "utf8").replace(/\/\*[\s\S]*?\*\//g, "").matchAll(/"([a-z_]+)"/g)].map((m) => m[1]),
);
const vimSource = readFileSync(join(here, "vim/syntax/uione.vim"), "utf8");
const vimWords = new Set();
for (const line of vimSource.split("\n")) {
  const keyword = line.match(/^syn keyword (uione\w+)\s+(?:contained\s+)?(.*)$/);
  if (keyword && keyword[1] !== "uioneTodo") {
    const options = new Set(["skipwhite", "skipnl", "skipempty", "transparent", "contained", "display"]);
    for (const w of keyword[2].split(/\s+/)) if (/^[a-z_]+$/.test(w) && !options.has(w)) vimWords.add(w);
  }
  for (const group of line.matchAll(/\\%\(((?:[a-z_]+\\\|)*[a-z_]+)\\\)/g)) for (const w of group[1].split("\\|")) vimWords.add(w);
  for (const word of line.matchAll(/\\<([a-z_]+)\\>/g)) vimWords.add(word[1]);
}
const codeWords = new Set();
const walk = (x) => {
  if (Array.isArray(x)) return x.forEach(walk);
  if (x && typeof x === "object") {
    for (const [k, v] of Object.entries(x)) {
      if ((k === "match" || k === "begin") && typeof v === "string") {
        for (const group of v.matchAll(/\((?:\?:)?((?:[a-z_]+\|)*[a-z_]+)\)/g)) for (const w of group[1].split("|")) codeWords.add(w);
      } else walk(v);
    }
  }
};
walk(JSON.parse(readFileSync(join(here, "vscode/syntaxes/uione.tmlanguage.json"), "utf8")));
for (const [editor, words] of [["vim", vimWords], ["vscode", codeWords]]) {
  const missing = [...grammarWords].filter((w) => !words.has(w)).sort();
  const extra = [...words].filter((w) => !grammarWords.has(w)).sort();
  if (missing.length) { failures++; console.log(`FAIL  ${editor} doesn't know ${missing.join(", ")}`); }
  if (extra.length) { failures++; console.log(`FAIL  ${editor} knows ${extra.join(", ")}, which the grammar doesn't`); }
}
console.log(`${grammarWords.size} words of the grammar, known to both editors`);
process.exit(failures ? 1 : 0);
