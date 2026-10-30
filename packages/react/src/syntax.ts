// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::syntax: code colored as it's read, the same on every target. Each language is a
// line of syntax/tests/languages.tsv, kept here as it's written there: its comments, its
// quotes and its words. What's read is told as spans, each a kind of thing and where it
// is, and a kind is colored alike everywhere, light or dark. syntax.hpp is the same, held
// to the same files.

export type SyntaxKind = "keyword" | "type" | "constant" | "string" | "number" | "comment" | "directive" | "function";

export interface SyntaxSpan {
  kind: SyntaxKind;
  from: number;
  to: number; // in UTF-16 code units, as a string's indices are
}

export interface Language {
  name: string;
  title: string;
  aliases: string[];
  lineComments: string[];
  quotes: string[];
  blockComments: [string, string][];
  longStrings: [string, string][];
  // What it does besides: # starting a line is a directive, @name is one, a name before (
  // is a function's, a capital starts a type, 'a is a lifetime, R"(…)" and r#"…"# are raw
  // strings, f"…" and its like are strings, and its words are the same in any case.
  directives: boolean;
  decorators: boolean;
  functions: boolean;
  capitals: boolean;
  lifetimes: boolean;
  raw: boolean;
  prefixes: boolean;
  nocase: boolean;
  keywords: Set<string>;
  types: Set<string>;
  constants: Set<string>;
}

// syntax/tests/languages.tsv as it's written, but its comments.
export const syntaxTable = `c	C	h	//	/* */	" '	-	directives functions	auto break case const continue default do else enum extern for goto if inline register restrict return sizeof static struct switch typedef union volatile while alignas alignof constexpr static_assert thread_local typeof _Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local	char double float int long short signed unsigned void bool _Bool _Complex size_t ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t FILE	true false NULL nullptr
cpp	C++	c++ cc cxx hpp hh h++	//	/* */	" '	-	directives functions raw	auto break case const continue default do else enum extern for goto if inline register restrict return sizeof static struct switch typedef union volatile while alignas alignof constexpr static_assert thread_local and asm catch class co_await co_return co_yield concept const_cast consteval constinit decltype delete dynamic_cast explicit export final friend import module mutable namespace new noexcept not operator or override private protected public reinterpret_cast requires static_cast template this throw try typeid typename using virtual xor	char char8_t char16_t char32_t wchar_t double float int long short signed unsigned void bool size_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t	true false nullptr NULL
csharp	C#	cs c#	//	/* */	" '	-	directives functions capitals	abstract as async await base break case catch checked class const continue default delegate do else enum event explicit extern finally fixed for foreach get goto if implicit in init interface internal is lock namespace new operator out override params private protected public readonly record ref return sealed set sizeof stackalloc static struct switch this throw try typeof unchecked unsafe using var virtual volatile when where while yield	bool byte char decimal double dynamic float int long nint nuint object sbyte short string uint ulong ushort void	true false null
java	Java	-	//	/* */	" '	""" """	decorators functions capitals	abstract assert break case catch class const continue default do else enum extends final finally for goto if implements import instanceof interface native new non-sealed package permits private protected public record return sealed static strictfp super switch synchronized this throw throws transient try var volatile while yield	boolean byte char double float int long short void	true false null
javascript	JavaScript	js jsx mjs cjs	//	/* */	" ' \`	-	decorators functions capitals	as async await break case catch class const continue debugger default delete do else export extends finally for from function get if import in instanceof let new of return set static super switch this throw try typeof var void while with yield	-	true false null undefined NaN Infinity
typescript	TypeScript	ts tsx mts cts	//	/* */	" ' \`	-	decorators functions capitals	as async await break case catch class const continue debugger default delete do else export extends finally for from function get if import in instanceof let new of return set static super switch this throw try typeof var void while with yield abstract asserts declare enum implements infer interface is keyof module namespace override private protected public readonly satisfies type	any bigint boolean never number object string symbol unknown	true false null undefined NaN Infinity
rust	Rust	rs	//	/* */	" '	-	functions capitals lifetimes raw	as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod move mut pub ref return self static struct super trait type unsafe use where while macro_rules	bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize Self	true false
go	Go	golang	//	/* */	" ' \`	-	functions	break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var	any bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr	true false nil iota
swift	Swift	-	//	/* */	"	""" """	decorators functions capitals	actor any as associatedtype async await break case catch class continue default defer deinit do else enum extension fallthrough fileprivate final for func guard if import in indirect init inout internal is lazy let mutating nonisolated open operator override private protocol public repeat rethrows return self some static struct subscript super switch throw throws try typealias var weak where while	Any Bool Character Double Float Int Int8 Int16 Int32 Int64 String UInt UInt8 UInt16 UInt32 UInt64 Void Self	true false nil
kotlin	Kotlin	kt kts	//	/* */	" '	""" """	decorators functions capitals	abstract as break by catch class companion constructor continue data do else enum finally for fun get if import in init inline interface internal is lateinit object open operator out override package private protected public return sealed set super suspend this throw try typealias val var vararg when where while	Any Boolean Byte Char Double Float Int Long Nothing Short String Unit	true false null
python	Python	py	#	-	" '	""" """, ''' '''	decorators functions prefixes	and as assert async await break case class continue def del elif else except finally for from global if import in is lambda match nonlocal not or pass raise return try while with yield	bool bytes dict float int list object set str tuple	True False None
ruby	Ruby	rb	#	-	" '	-	functions capitals	alias and begin break case class def defined? do else elsif end ensure for if in module next not or redo rescue retry return self super then undef unless until when while yield	-	true false nil
shell	Shell	sh bash zsh	#	-	" '	-	-	case do done elif else esac export fi for function if in local readonly return select then until while	-	true false
sql	SQL	-	--	/* */	" '	-	nocase	add all alter and as asc begin between by case check commit constraint create default delete desc distinct drop else end exists foreign from full group having if in index inner insert into is join key left like limit not offset on or order outer primary references returning right rollback select set table then transaction union unique update using values view when where with	bigint blob boolean char date datetime decimal double float int integer numeric real serial smallint text time timestamp varchar	true false null
json	JSON	-	-	-	"	-	-	-	-	true false null
yaml	YAML	yml	#	-	" '	-	-	-	-	true false null yes no on off
lua	Lua	-	--	--[[ ]]	" '	[[ ]]	functions	and break do else elseif end for function goto if in local not or repeat return then until while	-	true false nil
`;

const words = (s: string, apart = " "): string[] => (s === "-" ? [] : s.split(apart).map((w) => w.trim()).filter((w) => w !== ""));
const pairs = (s: string): [string, string][] => words(s, ",").map((p) => words(p)).filter((b) => b.length === 2).map((b) => [b[0]!, b[1]!]);

function read(line: string): Language {
  const f = line.split("\t");
  while (f.length < 11) f.push("-");
  const flags = new Set(words(f[7]!));
  return {
    name: f[0]!,
    title: f[1]!,
    aliases: words(f[2]!),
    lineComments: words(f[3]!),
    blockComments: pairs(f[4]!),
    quotes: words(f[5]!),
    longStrings: pairs(f[6]!),
    directives: flags.has("directives"),
    decorators: flags.has("decorators"),
    functions: flags.has("functions"),
    capitals: flags.has("capitals"),
    lifetimes: flags.has("lifetimes"),
    raw: flags.has("raw"),
    prefixes: flags.has("prefixes"),
    nocase: flags.has("nocase"),
    keywords: new Set(words(f[8]!)),
    types: new Set(words(f[9]!)),
    constants: new Set(words(f[10]!)),
  };
}

// Every language, in the order the table has them.
export const languages: Language[] = syntaxTable.split("\n").filter((l) => l !== "").map(read);

// The language a name gives, its own or another it goes by, in any case; none when there's
// none.
export function findLanguage(name: string): Language | undefined {
  const wanted = name.toLowerCase();
  return languages.find((l) => l.name === wanted || l.aliases.includes(wanted));
}

const letter = (c: string | undefined) => c !== undefined && (/[A-Za-z_$]/.test(c) || c.charCodeAt(0) >= 0x80);
const digit = (c: string | undefined) => c !== undefined && c >= "0" && c <= "9";
const alnum = (c: string | undefined) => c !== undefined && /[A-Za-z0-9]/.test(c);
const upper = (c: string | undefined) => c !== undefined && c >= "A" && c <= "Z";

// Where each thing that's colored is, in order, read as the language reads it.
export function highlight(s: string, l: Language): SyntaxSpan[] {
  const out: SyntaxSpan[] = [];
  const n = s.length;
  let i = 0;
  let lineStart = true;
  const add = (kind: SyntaxKind, from: number, to: number) => {
    if (to > from) out.push({ kind, from, to });
  };
  const at = (p: number, w: string) => s.startsWith(w, p);
  const until = (from: number, close: string) => {
    const e = s.indexOf(close, from);
    return e < 0 ? n : e + close.length;
  };
  const lineEnd = (from: number) => {
    const e = s.indexOf("\n", from);
    return e < 0 ? n : e;
  };
  const nameEnd = (from: number) => {
    while (from < n && (letter(s[from]) || digit(s[from]))) from++;
    return from;
  };
  // A quoted string: to its closing quote, past any escaped, or the line's end but for a
  // backtick's, which runs on.
  const quoted = (from: number) => {
    const q = s[from];
    let e = from + 1;
    while (e < n && s[e] !== q) {
      if (s[e] === "\\") e++;
      else if (s[e] === "\n" && q !== "`") return e;
      e++;
    }
    return Math.min(n, e + 1);
  };
  const longString = (from: number) => {
    for (const [open, close] of l.longStrings) {
      if (at(from, open)) return until(from + open.length, close);
    }
    return 0;
  };
  while (i < n) {
    const c = s[i]!;
    if (c === "\n") {
      lineStart = true;
      i++;
      continue;
    }
    if (c === " " || c === "\t" || c === "\r") {
      i++;
      continue;
    }
    const first = lineStart;
    lineStart = false;
    const block = l.blockComments.find(([open]) => at(i, open));
    if (block) {
      const e = until(i + block[0].length, block[1]);
      add("comment", i, e);
      i = e;
      continue;
    }
    if (l.lineComments.some((open) => at(i, open))) {
      const e = lineEnd(i);
      add("comment", i, e);
      i = e;
      continue;
    }
    const long = longString(i);
    if (long) {
      add("string", i, long);
      i = long;
      continue;
    }
    // A directive runs to its line's end, and on past one that ends in \.
    if (l.directives && c === "#" && first) {
      let e = lineEnd(i);
      while (e < n && e > 0 && s[e - 1] === "\\") e = lineEnd(e + 1);
      add("directive", i, e);
      i = e;
      continue;
    }
    if (l.quotes.includes(c)) {
      // 'a, a lifetime, isn't a character.
      if (l.lifetimes && c === "'") {
        const e = nameEnd(i + 1);
        if (e > i + 1 && (e >= n || s[e] !== "'")) {
          i = e;
          continue;
        }
      }
      const e = quoted(i);
      add("string", i, e);
      i = e;
      continue;
    }
    if (l.decorators && c === "@" && letter(s[i + 1])) {
      let e = nameEnd(i + 1);
      while (e + 1 < n && s[e] === "." && letter(s[e + 1])) e = nameEnd(e + 1);
      add("directive", i, e);
      i = e;
      continue;
    }
    if (digit(c) || (c === "." && digit(s[i + 1]))) {
      let e = i + 1;
      const hex = c === "0" && (s[i + 1] === "x" || s[i + 1] === "X");
      while (e < n) {
        const d = s[e]!;
        const sign = (d === "+" || d === "-") && "eEpP".includes(s[e - 1]!) && !hex;
        const apart = d === "'" && l.directives && alnum(s[e + 1]);
        if (alnum(d) || d === "_" || sign || apart || (d === "." && digit(s[e + 1]))) e++;
        else break;
      }
      add("number", i, e);
      i = e;
      continue;
    }
    if (letter(c)) {
      let e = nameEnd(i);
      const word = s.slice(i, e);
      // R"x(…)x", C++'s raw string, and r#"…"#, Rust's.
      if (l.raw && s[e] === '"' && word.endsWith("R")) {
        const open = s.indexOf("(", e);
        if (open >= 0) {
          const end = until(open + 1, ")" + s.slice(e + 1, open) + '"');
          add("string", i, end);
          i = end;
          continue;
        }
      }
      if (l.raw && (s[e] === '"' || s[e] === "#") && (word === "r" || word === "br")) {
        let q = e;
        while (q < n && s[q] === "#") q++;
        if (s[q] === '"') {
          const end = until(q + 1, '"' + "#".repeat(q - e));
          add("string", i, end);
          i = end;
          continue;
        }
      }
      // f"…" and its like: the letters before the quote are the string's.
      if (l.prefixes && e < n && word.length <= 2 && [...word].every((p) => "rbfuRBFU".includes(p))) {
        const long = longString(e);
        if (long) {
          add("string", i, long);
          i = long;
          continue;
        }
        if (l.quotes.includes(s[e]!)) {
          const end = quoted(e);
          add("string", i, end);
          i = end;
          continue;
        }
      }
      let key = l.nocase ? word.toLowerCase() : word;
      // A word that ends in ?, as Ruby's defined? does.
      if (s[e] === "?" && l.keywords.has(key + "?")) {
        key += "?";
        e++;
      }
      let next = e;
      while (next < n && (s[next] === " " || s[next] === "\t")) next++;
      const caps = word.length > 1 && /^[A-Z0-9_]+$/.test(word);
      if (l.keywords.has(key)) add("keyword", i, e);
      else if (l.types.has(key)) add("type", i, e);
      else if (l.constants.has(key)) add("constant", i, e);
      else if (l.capitals && caps) add("constant", i, e);
      else if (l.capitals && upper(word[0])) add("type", i, e);
      else if (l.functions && s[next] === "(") add("function", i, e);
      i = e;
      continue;
    }
    // Anything else, a whole character at a time.
    i += c.codePointAt(0)! > 0xffff ? 2 : 1;
  }
  return out;
}

// Each kind's color, on a light page or a dark one: GitHub's.
export function syntaxColor(kind: SyntaxKind, dark: boolean): string {
  switch (kind) {
    case "keyword":
    case "directive":
      return dark ? "#ff7b72" : "#cf222e";
    case "type":
      return dark ? "#ffa657" : "#953800";
    case "constant":
    case "number":
      return dark ? "#79c0ff" : "#0550ae";
    case "string":
      return dark ? "#a5d6ff" : "#0a3069";
    case "comment":
      return dark ? "#8b949e" : "#6e7781";
    case "function":
      return dark ? "#d2a8ff" : "#8250df";
  }
}
