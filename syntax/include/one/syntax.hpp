// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::syntax: code colored as it's read, the same on every target. Each language is a
// line of tests/languages.tsv, kept here as it's written there: its comments, its quotes
// and its words. What's read is told as spans, each a kind of thing and where it is,
// and a kind is colored alike everywhere, light or dark. @uione/react's syntax.ts is the
// same, held to the same files.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace one::syntax {

	enum class kind { keyword, type, constant, string, number, comment, directive, function };

	struct span {
		syntax::kind kind;
		std::size_t from = 0, to = 0;  // in bytes
	};

	struct language {
		std::string name, title;
		std::vector<std::string> aliases, line_comments, quotes;
		std::vector<std::pair<std::string, std::string>> block_comments, long_strings;
		// What it does besides: # starting a line is a directive, @name is one, a name
		// before ( is a function's, a capital starts a type, 'a is a lifetime, R"(…)" and
		// r#"…"# are raw strings, f"…" and its like are strings, and its words are the
		// same in any case.
		bool directives = false, decorators = false, functions = false, capitals = false, lifetimes = false, raw = false, prefixes = false, nocase = false;
		std::set<std::string, std::less<>> keywords, types, constants;
	};

	// tests/languages.tsv as it's written, but its comments.
	inline constexpr std::string_view table = R"tsv(c	C	h	//	/* */	" '	-	directives functions	auto break case const continue default do else enum extern for goto if inline register restrict return sizeof static struct switch typedef union volatile while alignas alignof constexpr static_assert thread_local typeof _Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local	char double float int long short signed unsigned void bool _Bool _Complex size_t ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t FILE	true false NULL nullptr
cpp	C++	c++ cc cxx hpp hh h++	//	/* */	" '	-	directives functions raw	auto break case const continue default do else enum extern for goto if inline register restrict return sizeof static struct switch typedef union volatile while alignas alignof constexpr static_assert thread_local and asm catch class co_await co_return co_yield concept const_cast consteval constinit decltype delete dynamic_cast explicit export final friend import module mutable namespace new noexcept not operator or override private protected public reinterpret_cast requires static_cast template this throw try typeid typename using virtual xor	char char8_t char16_t char32_t wchar_t double float int long short signed unsigned void bool size_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t	true false nullptr NULL
csharp	C#	cs c#	//	/* */	" '	-	directives functions capitals	abstract as async await base break case catch checked class const continue default delegate do else enum event explicit extern finally fixed for foreach get goto if implicit in init interface internal is lock namespace new operator out override params private protected public readonly record ref return sealed set sizeof stackalloc static struct switch this throw try typeof unchecked unsafe using var virtual volatile when where while yield	bool byte char decimal double dynamic float int long nint nuint object sbyte short string uint ulong ushort void	true false null
java	Java	-	//	/* */	" '	""" """	decorators functions capitals	abstract assert break case catch class const continue default do else enum extends final finally for goto if implements import instanceof interface native new non-sealed package permits private protected public record return sealed static strictfp super switch synchronized this throw throws transient try var volatile while yield	boolean byte char double float int long short void	true false null
javascript	JavaScript	js jsx mjs cjs	//	/* */	" ' `	-	decorators functions capitals	as async await break case catch class const continue debugger default delete do else export extends finally for from function get if import in instanceof let new of return set static super switch this throw try typeof var void while with yield	-	true false null undefined NaN Infinity
typescript	TypeScript	ts tsx mts cts	//	/* */	" ' `	-	decorators functions capitals	as async await break case catch class const continue debugger default delete do else export extends finally for from function get if import in instanceof let new of return set static super switch this throw try typeof var void while with yield abstract asserts declare enum implements infer interface is keyof module namespace override private protected public readonly satisfies type	any bigint boolean never number object string symbol unknown	true false null undefined NaN Infinity
rust	Rust	rs	//	/* */	" '	-	functions capitals lifetimes raw	as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod move mut pub ref return self static struct super trait type unsafe use where while macro_rules	bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize Self	true false
go	Go	golang	//	/* */	" ' `	-	functions	break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var	any bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr	true false nil iota
swift	Swift	-	//	/* */	"	""" """	decorators functions capitals	actor any as associatedtype async await break case catch class continue default defer deinit do else enum extension fallthrough fileprivate final for func guard if import in indirect init inout internal is lazy let mutating nonisolated open operator override private protocol public repeat rethrows return self some static struct subscript super switch throw throws try typealias var weak where while	Any Bool Character Double Float Int Int8 Int16 Int32 Int64 String UInt UInt8 UInt16 UInt32 UInt64 Void Self	true false nil
kotlin	Kotlin	kt kts	//	/* */	" '	""" """	decorators functions capitals	abstract as break by catch class companion constructor continue data do else enum finally for fun get if import in init inline interface internal is lateinit object open operator out override package private protected public return sealed set super suspend this throw try typealias val var vararg when where while	Any Boolean Byte Char Double Float Int Long Nothing Short String Unit	true false null
python	Python	py	#	-	" '	""" """, ''' '''	decorators functions prefixes	and as assert async await break case class continue def del elif else except finally for from global if import in is lambda match nonlocal not or pass raise return try while with yield	bool bytes dict float int list object set str tuple	True False None
ruby	Ruby	rb	#	-	" '	-	functions capitals	alias and begin break case class def defined? do else elsif end ensure for if in module next not or redo rescue retry return self super then undef unless until when while yield	-	true false nil
shell	Shell	sh bash zsh	#	-	" '	-	-	case do done elif else esac export fi for function if in local readonly return select then until while	-	true false
sql	SQL	-	--	/* */	" '	-	nocase	add all alter and as asc begin between by case check commit constraint create default delete desc distinct drop else end exists foreign from full group having if in index inner insert into is join key left like limit not offset on or order outer primary references returning right rollback select set table then transaction union unique update using values view when where with	bigint blob boolean char date datetime decimal double float int integer numeric real serial smallint text time timestamp varchar	true false null
json	JSON	-	-	-	"	-	-	-	-	true false null
yaml	YAML	yml	#	-	" '	-	-	-	-	true false null yes no on off
lua	Lua	-	--	--[[ ]]	" '	[[ ]]	functions	and break do else elseif end for function goto if in local not or repeat return then until while	-	true false nil
)tsv";

	namespace detail {

		inline std::vector<std::string> words(std::string_view s, char apart = ' ') {
			std::vector<std::string> out;
			if (s == "-") return out;
			std::size_t at = 0;
			while (at <= s.size()) {
				std::size_t end = s.find(apart, at);
				if (end == std::string_view::npos) end = s.size();
				std::string_view w = s.substr(at, end - at);
				while (!w.empty() && w.front() == ' ') w.remove_prefix(1);
				while (!w.empty() && w.back() == ' ') w.remove_suffix(1);
				if (!w.empty()) out.emplace_back(w);
				at = end + 1;
			}
			return out;
		}

		inline std::vector<std::pair<std::string, std::string>> pairs(std::string_view s) {
			std::vector<std::pair<std::string, std::string>> out;
			for (const auto& pair : words(s, ',')) {
				auto both = words(pair);
				if (both.size() == 2) out.emplace_back(both[0], both[1]);
			}
			return out;
		}

		inline language read(std::string_view line) {
			std::vector<std::string_view> f;
			std::size_t at = 0;
			while (true) {
				std::size_t end = line.find('\t', at);
				f.push_back(line.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
				if (end == std::string_view::npos) break;
				at = end + 1;
			}
			f.resize(11, "-");
			language l;
			l.name = std::string(f[0]);
			l.title = std::string(f[1]);
			l.aliases = words(f[2]);
			l.line_comments = words(f[3]);
			l.block_comments = pairs(f[4]);
			l.quotes = words(f[5]);
			l.long_strings = pairs(f[6]);
			for (const auto& flag : words(f[7])) {
				if (flag == "directives") l.directives = true;
				if (flag == "decorators") l.decorators = true;
				if (flag == "functions") l.functions = true;
				if (flag == "capitals") l.capitals = true;
				if (flag == "lifetimes") l.lifetimes = true;
				if (flag == "raw") l.raw = true;
				if (flag == "prefixes") l.prefixes = true;
				if (flag == "nocase") l.nocase = true;
			}
			for (auto& w : words(f[8])) l.keywords.insert(std::move(w));
			for (auto& w : words(f[9])) l.types.insert(std::move(w));
			for (auto& w : words(f[10])) l.constants.insert(std::move(w));
			return l;
		}

		inline std::string lower(std::string_view s) {
			std::string out(s);
			for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return out;
		}

		inline bool letter(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80; }
		inline bool digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

	}  // namespace detail

	// Every language, in the order the table has them.
	inline const std::vector<language>& languages() {
		static const std::vector<language> all = [] {
			std::vector<language> out;
			std::size_t at = 0;
			while (at < table.size()) {
				std::size_t end = table.find('\n', at);
				if (end == std::string_view::npos) end = table.size();
				if (end > at) out.push_back(detail::read(table.substr(at, end - at)));
				at = end + 1;
			}
			return out;
		}();
		return all;
	}

	// The language a name gives, its own or another it goes by, in any case; none when
	// there's none.
	inline const language* find(std::string_view name) {
		std::string wanted = detail::lower(name);
		for (const auto& l : languages()) {
			if (l.name == wanted || std::ranges::find(l.aliases, wanted) != l.aliases.end()) return &l;
		}
		return nullptr;
	}

	// Where each thing that's colored is, in order, read as the language reads it.
	inline std::vector<span> highlight(std::string_view s, const language& l) {
		using detail::digit;
		using detail::letter;
		std::vector<span> out;
		std::size_t i = 0, n = s.size();
		bool line_start = true;
		auto add = [&](kind k, std::size_t from, std::size_t to) {
			if (to > from) out.push_back({k, from, to});
		};
		auto at = [&](std::size_t p, std::string_view w) { return s.substr(p, w.size()) == w; };
		auto until = [&](std::size_t from, std::string_view close) {
			std::size_t e = s.find(close, from);
			return e == std::string_view::npos ? n : e + close.size();
		};
		auto line_end = [&](std::size_t from) {
			std::size_t e = s.find('\n', from);
			return e == std::string_view::npos ? n : e;
		};
		auto name_end = [&](std::size_t from) {
			while (from < n && (letter(s[from]) || digit(s[from]))) ++from;
			return from;
		};
		// A quoted string: to its closing quote, past any escaped, or the line's end but
		// for a backtick's, which runs on.
		auto quoted = [&](std::size_t from) {
			char q = s[from];
			std::size_t e = from + 1;
			while (e < n && s[e] != q) {
				if (s[e] == '\\') ++e;
				else if (s[e] == '\n' && q != '`') return e;
				++e;
			}
			return std::min(n, e + 1);
		};
		auto long_string = [&](std::size_t from) -> std::size_t {
			for (const auto& [open, close] : l.long_strings) {
				if (at(from, open)) return until(from + open.size(), close);
			}
			return 0;
		};
		while (i < n) {
			char c = s[i];
			if (c == '\n') {
				line_start = true;
				++i;
				continue;
			}
			if (c == ' ' || c == '\t' || c == '\r') {
				++i;
				continue;
			}
			bool first = std::exchange(line_start, false);
			bool done = false;
			for (const auto& [open, close] : l.block_comments) {
				if (!done && at(i, open)) {
					std::size_t e = until(i + open.size(), close);
					add(kind::comment, i, e);
					i = e;
					done = true;
				}
			}
			for (const auto& open : l.line_comments) {
				if (!done && at(i, open)) {
					std::size_t e = line_end(i);
					add(kind::comment, i, e);
					i = e;
					done = true;
				}
			}
			if (done) continue;
			if (std::size_t e = long_string(i)) {
				add(kind::string, i, e);
				i = e;
				continue;
			}
			// A directive runs to its line's end, and on past one that ends in \.
			if (l.directives && c == '#' && first) {
				std::size_t e = line_end(i);
				while (e < n && e > 0 && s[e - 1] == '\\') e = line_end(e + 1);
				add(kind::directive, i, e);
				i = e;
				continue;
			}
			if (std::ranges::find(l.quotes, std::string_view(&s[i], 1)) != l.quotes.end()) {
				// 'a, a lifetime, isn't a character.
				if (l.lifetimes && c == '\'') {
					std::size_t e = name_end(i + 1);
					if (e > i + 1 && (e >= n || s[e] != '\'')) {
						i = e;
						continue;
					}
				}
				std::size_t e = quoted(i);
				add(kind::string, i, e);
				i = e;
				continue;
			}
			if (l.decorators && c == '@' && i + 1 < n && letter(s[i + 1])) {
				std::size_t e = name_end(i + 1);
				while (e + 1 < n && s[e] == '.' && letter(s[e + 1])) e = name_end(e + 1);
				add(kind::directive, i, e);
				i = e;
				continue;
			}
			if (digit(c) || (c == '.' && i + 1 < n && digit(s[i + 1]))) {
				std::size_t e = i + 1;
				while (e < n) {
					char d = s[e];
					bool sign = (d == '+' || d == '-') && std::string_view("eEpP").find(s[e - 1]) != std::string_view::npos && !(s[i] == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X'));
					bool apart = d == '\'' && l.directives && e + 1 < n && std::isalnum(static_cast<unsigned char>(s[e + 1]));
					if (std::isalnum(static_cast<unsigned char>(d)) || d == '_' || sign || apart || (d == '.' && e + 1 < n && digit(s[e + 1]))) ++e;
					else break;
				}
				add(kind::number, i, e);
				i = e;
				continue;
			}
			if (letter(c)) {
				std::size_t e = name_end(i);
				std::string_view word = s.substr(i, e - i);
				// R"x(…)x", C++'s raw string, and r#"…"#, Rust's.
				if (l.raw && e < n && s[e] == '"' && word.ends_with('R')) {
					std::size_t open = s.find('(', e);
					if (open != std::string_view::npos) {
						std::string close = ")" + std::string(s.substr(e + 1, open - e - 1)) + "\"";
						std::size_t end = until(open + 1, close);
						add(kind::string, i, end);
						i = end;
						continue;
					}
				}
				if (l.raw && e < n && (s[e] == '"' || s[e] == '#') && (word == "r" || word == "br")) {
					std::size_t q = e;
					while (q < n && s[q] == '#') ++q;
					if (q < n && s[q] == '"') {
						std::size_t end = until(q + 1, "\"" + std::string(q - e, '#'));
						add(kind::string, i, end);
						i = end;
						continue;
					}
				}
				// f"…" and its like: the letters before the quote are the string's.
				if (l.prefixes && e < n && word.size() <= 2 && std::ranges::all_of(word, [](char p) { return std::string_view("rbfuRBFU").find(p) != std::string_view::npos; })) {
					if (std::size_t end = long_string(e)) {
						add(kind::string, i, end);
						i = end;
						continue;
					}
					if (std::ranges::find(l.quotes, std::string_view(&s[e], 1)) != l.quotes.end()) {
						std::size_t end = quoted(e);
						add(kind::string, i, end);
						i = end;
						continue;
					}
				}
				std::string key = l.nocase ? detail::lower(word) : std::string(word);
				// A word that ends in ?, as Ruby's defined? does.
				if (e < n && s[e] == '?' && l.keywords.contains(key + "?")) key += "?", ++e;
				std::size_t next = e;
				while (next < n && (s[next] == ' ' || s[next] == '\t')) ++next;
				bool caps = word.size() > 1 && std::ranges::all_of(word, [](char p) { return std::isupper(static_cast<unsigned char>(p)) || std::isdigit(static_cast<unsigned char>(p)) || p == '_'; });
				if (l.keywords.contains(key)) add(kind::keyword, i, e);
				else if (l.types.contains(key)) add(kind::type, i, e);
				else if (l.constants.contains(key)) add(kind::constant, i, e);
				else if (l.capitals && caps) add(kind::constant, i, e);
				else if (l.capitals && std::isupper(static_cast<unsigned char>(word[0]))) add(kind::type, i, e);
				else if (l.functions && next < n && s[next] == '(') add(kind::function, i, e);
				i = e;
				continue;
			}
			// Anything else, a whole character at a time.
			++i;
			while (i < n && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) ++i;
		}
		return out;
	}

	// What each kind is called, as tests/cases.tsv writes it.
	inline std::string_view name(kind k) {
		switch (k) {
			case kind::keyword: return "keyword";
			case kind::type: return "type";
			case kind::constant: return "constant";
			case kind::string: return "string";
			case kind::number: return "number";
			case kind::comment: return "comment";
			case kind::directive: return "directive";
			case kind::function: return "function";
		}
		return "";
	}

	// Each kind's color, as 0xrrggbb, on a light page or a dark one: GitHub's.
	inline std::uint32_t color(kind k, bool dark) {
		switch (k) {
			case kind::keyword:
			case kind::directive: return dark ? 0xff7b72 : 0xcf222e;
			case kind::type: return dark ? 0xffa657 : 0x953800;
			case kind::constant:
			case kind::number: return dark ? 0x79c0ff : 0x0550ae;
			case kind::string: return dark ? 0xa5d6ff : 0x0a3069;
			case kind::comment: return dark ? 0x8b949e : 0x6e7781;
			case kind::function: return dark ? 0xd2a8ff : 0x8250df;
		}
		return 0;
	}

}  // namespace one::syntax
