// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::math: equations written as they'd be typed in a message, read as a tree that's
// set as a book sets them and, where it can be, worked out (math/README.md). One
// header. Its twin in @uione/react's math.ts reads the same notation the same way, both
// held to math/tests/cases.tsv.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <expected>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace one::math {

	// What an equation is made of. A row is things side by side, multiplied when
	// they're values; an operator or relation is said between the parts of its row.
	struct node {
		enum class kind {
			row,         // parts side by side
			number,      // 6.02e23, as written
			name,        // x, rate, monthly_rate, a letter or a word
			greek,       // alpha, set as the letter
			symbol,      // inf, partial, and the like, set as their sign
			words,       // "for every n", set upright
			infix,       // + - +- * and relations, `text` the one written, between parts[0] and parts[1]
			negative,    // -x
			fraction,    // parts[0] over parts[1]
			power,       // parts[0] raised to parts[1]
			subscript,   // parts[0] lowered by parts[1]
			prime,       // parts[0] with `text` primes, like f''
			factorial,   // parts[0]!
			percent,     // parts[0]%
			group,       // (parts[0]), its parentheses drawn
			call,        // a function, `text` its name, on parts, like sin(x) or f(x, y)
			root,        // sqrt(parts[0]), or root(parts[0], parts[1]) for an index
			bracket,     // `text` abs, floor or ceil, around parts[0]
			accent,      // `text` hat, bar, vec, dot or ddot, over parts[0]
			big,         // `text` sum, prod, int or lim; parts: below, above, and what it acts on
			differential,  // d and a letter, like dx: parts[0] the letter
			matrix,      // rows of columns: parts are rows, each a row of cells
			cases,       // parts are rows: value, then condition, or nothing for otherwise
		};
		kind type = kind::row;
		std::string text;
		std::vector<node> parts;
	};

	struct problem {
		std::size_t at = 0;  // where, in bytes into what was written
		std::string says;
	};

	namespace detail {

		inline const std::set<std::string, std::less<>>& greek_names() {
			static const std::set<std::string, std::less<>> names{
			    "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau",
			    "upsilon", "phi", "chi", "psi", "omega", "Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma", "Upsilon", "Phi", "Psi", "Omega"};
			return names;
		}
		// Functions set upright, taking parentheses or the side-by-side term after them.
		inline const std::set<std::string, std::less<>>& function_names() {
			static const std::set<std::string, std::less<>> names{"sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
			                                                      "ln", "log", "exp", "min", "max", "det", "gcd", "lcm", "sgn", "round", "arg"};
			return names;
		}
		inline const std::set<std::string, std::less<>>& bracket_names() {
			static const std::set<std::string, std::less<>> names{"abs", "floor", "ceil"};
			return names;
		}
		inline const std::set<std::string, std::less<>>& accent_names() {
			static const std::set<std::string, std::less<>> names{"hat", "bar", "vec", "dot", "ddot"};
			return names;
		}
		inline const std::set<std::string, std::less<>>& big_names() {
			static const std::set<std::string, std::less<>> names{"sum", "prod", "int", "lim"};
			return names;
		}

		struct token {
			enum class kind { end, number, name, words, sign } type = kind::end;
			std::string text;
			std::size_t at = 0;
		};

		// The signs written as more than one character, longest first, and the same typed
		// as themselves.
		inline const std::vector<std::pair<std::string, std::string>>& signs() {
			static const std::vector<std::pair<std::string, std::string>> all{
			    {"<=>", "<=>"}, {"<=", "<="}, {">=", ">="}, {"!=", "!="}, {"~=", "~="}, {"->", "->"}, {"=>", "=>"}, {"+-", "+-"}, {"-+", "-+"},
			    {"≤", "<="}, {"≥", ">="}, {"≠", "!="}, {"≈", "~="}, {"→", "->"}, {"⇒", "=>"}, {"⇔", "<=>"}, {"±", "+-"},
			    {"∓", "-+"}, {"×", "*"}, {"·", "*"}, {"−", "-"}, {"∈", "in"}, {"∞", "inf"}, {"∂", "partial"}, {"√", "sqrt"},
			    {"∑", "sum"}, {"∏", "prod"}, {"∫", "int"}, {"π", "pi"}};
			return all;
		}

		class reader {
		public:
			explicit reader(std::string_view written) : s_(written) { next(); }

			std::expected<node, problem> whole() {
				auto made = relation();
				if (!made) return made;
				if (now_.type != token::kind::end) return fail("there's more after the equation here, " + quote(now_.text));
				return made;
			}

		private:
			std::string_view s_;
			std::size_t at_ = 0;
			token now_;
			int bars_ = 0;  // inside how many absolute values, where a | closes one

			static std::string quote(const std::string& t) { return "\"" + t + "\""; }
			std::unexpected<problem> fail(std::string says) const { return std::unexpected(problem{now_.at, std::move(says)}); }

			void skip_spaces() {
				while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t' || s_[at_] == '\n' || s_[at_] == '\r')) ++at_;
			}

			void next() {
				skip_spaces();
				now_ = {token::kind::end, "", at_};
				if (at_ >= s_.size()) return;
				char c = s_[at_];
				if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && at_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[at_ + 1])))) {
					std::size_t from = at_;
					while (at_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[at_])) || s_[at_] == '.')) ++at_;
					// Its thousands set apart by commas, 299,792,458, while it's a whole number: a
					// comma with three digits after it and no fourth.
					auto digit = [&](std::size_t i) { return i < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i])); };
					while (at_ < s_.size() && s_[at_] == ',' && s_.substr(from, at_ - from).find('.') == std::string_view::npos && digit(at_ + 1) && digit(at_ + 2) && digit(at_ + 3) && !digit(at_ + 4)) at_ += 4;
					if (at_ < s_.size() && s_[at_] == '.' && digit(at_ + 1))
						for (++at_; digit(at_);) ++at_;
					// An exponent, 6.02e23, only when digits follow the e.
					if (at_ + 1 < s_.size() && (s_[at_] == 'e' || s_[at_] == 'E')) {
						std::size_t e = at_ + 1;
						if (e < s_.size() && (s_[e] == '+' || s_[e] == '-')) ++e;
						if (e < s_.size() && std::isdigit(static_cast<unsigned char>(s_[e]))) {
							at_ = e;
							while (at_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[at_]))) ++at_;
						}
					}
					now_ = {token::kind::number, std::string(s_.substr(from, at_ - from)), from};
					return;
				}
				if (std::isalpha(static_cast<unsigned char>(c))) {
					std::size_t from = at_;
					while (at_ < s_.size() && std::isalnum(static_cast<unsigned char>(s_[at_]))) ++at_;
					now_ = {token::kind::name, std::string(s_.substr(from, at_ - from)), from};
					return;
				}
				if (c == '"') {
					std::size_t from = at_++;
					std::size_t close = s_.find('"', at_);
					std::size_t end = close == std::string_view::npos ? s_.size() : close;
					now_ = {token::kind::words, std::string(s_.substr(at_, end - at_)), from};
					at_ = close == std::string_view::npos ? s_.size() : close + 1;
					return;
				}
				for (const auto& [written, means] : signs()) {
					if (s_.substr(at_).starts_with(written)) {
						now_ = {std::isalpha(static_cast<unsigned char>(means[0])) ? token::kind::name : token::kind::sign, means, at_};
						at_ += written.size();
						return;
					}
				}
				// One byte, or one character of several bytes, as itself.
				std::size_t length = 1;
				auto lead = static_cast<unsigned char>(c);
				if (lead >= 0xf0) length = 4;
				else if (lead >= 0xe0) length = 3;
				else if (lead >= 0xc0) length = 2;
				now_ = {token::kind::sign, std::string(s_.substr(at_, length)), at_};
				at_ += length;
			}

			bool sign(std::string_view t) const { return now_.type == token::kind::sign && now_.text == t; }
			bool word(std::string_view t) const { return now_.type == token::kind::name && now_.text == t; }

			static bool relation_sign(const token& t) {
				static const std::set<std::string, std::less<>> all{"=", "!=", "<", ">", "<=", ">=", "~=", "->", "=>", "<=>"};
				return (t.type == token::kind::sign && all.contains(t.text)) || (t.type == token::kind::name && t.text == "in");
			}

			static node infix(std::string op, node left, node right) { return node{node::kind::infix, std::move(op), {std::move(left), std::move(right)}}; }

			std::expected<node, problem> relation() {
				auto left = sum();
				if (!left) return left;
				while (relation_sign(now_)) {
					std::string op = now_.text;
					next();
					auto right = sum();
					if (!right) return right;
					left = infix(op, std::move(*left), std::move(*right));
				}
				return left;
			}

			std::expected<node, problem> sum() {
				auto left = product();
				if (!left) return left;
				while (sign("+") || sign("-") || sign("+-") || sign("-+")) {
					std::string op = now_.text;
					next();
					auto right = product();
					if (!right) return right;
					left = infix(op, std::move(*left), std::move(*right));
				}
				return left;
			}

			// * and /, a / stacking what's side by side on either side of it.
			std::expected<node, problem> product() {
				auto left = run();
				if (!left) return left;
				while (sign("*") || sign("/")) {
					bool over = sign("/");
					next();
					auto right = run();
					if (!right) return right;
					left = over ? node{node::kind::fraction, "", {std::move(*left), std::move(*right)}} : infix("*", std::move(*left), std::move(*right));
				}
				return left;
			}

			// Whether what's next starts a term side by side with the one before.
			bool starts_term() const {
				if (now_.type == token::kind::number || now_.type == token::kind::words) return true;
				if (now_.type == token::kind::name) return now_.text != "to" && now_.text != "if" && now_.text != "otherwise" && now_.text != "in";
				return sign("(") || sign("[") || (sign("|") && bars_ == 0);
			}

			// Terms side by side, multiplied: 2a, 2 pi r, n(n + 1), sin 2x.
			std::expected<node, problem> run() {
				if (sign("-")) {
					next();
					auto inner = run();
					if (!inner) return inner;
					return node{node::kind::negative, "", {std::move(*inner)}};
				}
				std::vector<node> parts;
				do {
					// A function or a big operator without parentheses, or with bounds, takes
					// the rest of the run as what it acts on, up to another of them.
					auto made = term();
					if (!made) return made;
					parts.push_back(std::move(*made));
				} while (starts_term());
				if (parts.size() == 1) return std::move(parts.front());
				return node{node::kind::row, "", std::move(parts)};
			}

			// The side-by-side terms after a function or a big operator, up to the next one.
			std::expected<node, problem> acted_on() {
				std::vector<node> parts;
				while (starts_term()) {
					if (now_.type == token::kind::name && (function_names().contains(now_.text) || big_names().contains(now_.text)) && !parts.empty()) break;
					auto made = term();
					if (!made) return made;
					parts.push_back(std::move(*made));
				}
				if (parts.empty()) return fail("a function or a sum needs what it acts on after it");
				if (parts.size() == 1) return std::move(parts.front());
				return node{node::kind::row, "", std::move(parts)};
			}

			// One term with what follows it: ^, _, ', ! and %.
			std::expected<node, problem> term() {
				auto base = atom();
				if (!base) return base;
				while (true) {
					if (sign("^")) {
						next();
						auto raised = script();
						if (!raised) return raised;
						base = node{node::kind::power, "", {std::move(*base), std::move(*raised)}};
					} else if (sign("_")) {
						next();
						auto lowered = script();
						if (!lowered) return lowered;
						base = node{node::kind::subscript, "", {std::move(*base), std::move(*lowered)}};
					} else if (sign("'")) {
						std::string primes;
						while (sign("'")) primes += "'", next();
						base = node{node::kind::prime, primes, {std::move(*base)}};
					} else if (sign("!")) {
						next();
						base = node{node::kind::factorial, "", {std::move(*base)}};
					} else if (sign("%")) {
						next();
						base = node{node::kind::percent, "", {std::move(*base)}};
					} else {
						break;
					}
				}
				return base;
			}

			// What a power or a subscript holds: one atom, a minus before it, and its own
			// power, read right to left: 2^3^2 is 2^(3^2).
			std::expected<node, problem> script() {
				if (sign("-")) {
					next();
					auto inner = script();
					if (!inner) return inner;
					return node{node::kind::negative, "", {std::move(*inner)}};
				}
				// A word here is a name, as x_max's is, unless parentheses follow it.
				std::expected<node, problem> base = std::unexpected(problem{});
				if (now_.type == token::kind::name && !greek_names().contains(now_.text) && now_.text != "inf" && now_.text != "partial") {
					std::size_t keep_at = at_;
					token keep = now_;
					next();
					bool called = sign("(");
					at_ = keep_at, now_ = keep;
					if (called) base = atom();
					else {
						base = node{node::kind::name, now_.text, {}};
						next();
					}
				} else {
					base = atom();
				}
				if (!base) return base;
				if (sign("^")) {
					next();
					auto raised = script();
					if (!raised) return raised;
					return node{node::kind::power, "", {std::move(*base), std::move(*raised)}};
				}
				return base;
			}

			// What's inside parentheses: an equation, or one stripped of the parentheses
			// that only group, as a script's or a fraction's are drawn without them.
			std::expected<std::vector<node>, problem> arguments() {
				std::vector<node> out;
				next();  // (
				if (sign(")")) {
					next();
					return out;
				}
				while (true) {
					auto one = relation();
					if (!one) return std::unexpected(one.error());
					out.push_back(std::move(*one));
					if (sign(",")) {
						next();
						continue;
					}
					if (!sign(")")) return fail("a ) to close what's in parentheses");
					next();
					return out;
				}
			}

			std::expected<node, problem> atom() {
				if (now_.type == token::kind::number) {
					node n{node::kind::number, now_.text, {}};
					next();
					return n;
				}
				if (now_.type == token::kind::words) {
					node n{node::kind::words, now_.text, {}};
					next();
					return n;
				}
				if (sign("(")) {
					auto inside = arguments();
					if (!inside) return std::unexpected(inside.error());
					if (inside->size() != 1) return fail("one thing in parentheses, or a function before them, like f(x, y)");
					return node{node::kind::group, "", {std::move(inside->front())}};
				}
				if (sign("|")) {
					next();
					++bars_;
					auto inside = sum();
					--bars_;
					if (!inside) return inside;
					if (!sign("|")) return fail("a | to close the absolute value");
					next();
					return node{node::kind::bracket, "abs", {std::move(*inside)}};
				}
				if (sign("[")) return matrix();
				if (now_.type == token::kind::name) return named();
				return fail(now_.type == token::kind::end ? "something more to finish the equation" : "an equation, not " + quote(now_.text));
			}

			// [1, 2; 3, 4]: commas between columns, semicolons between rows.
			std::expected<node, problem> matrix() {
				next();  // [
				node out{node::kind::matrix, "", {}};
				node row{node::kind::row, "", {}};
				while (true) {
					auto cell = relation();
					if (!cell) return cell;
					row.parts.push_back(std::move(*cell));
					if (sign(",")) {
						next();
					} else if (sign(";")) {
						next();
						out.parts.push_back(std::move(row));
						row = node{node::kind::row, "", {}};
					} else if (sign("]")) {
						next();
						out.parts.push_back(std::move(row));
						return out;
					} else {
						return fail("a , ; or ] in the matrix");
					}
				}
			}

			std::expected<node, problem> named() {
				std::string name = now_.text;
				std::size_t was = at_;
				next();
				bool parenthesized = sign("(");
				// A word that names a part of the notation, as long as what it needs follows.
				if (name == "sqrt" || name == "root") {
					if (!parenthesized) return fail(name + " takes parentheses, like sqrt(x)");
					auto inside = arguments();
					if (!inside) return std::unexpected(inside.error());
					std::size_t wanted = name == "sqrt" ? 1 : 2;
					if (inside->size() != wanted) return fail(name == "sqrt" ? "sqrt takes one thing, like sqrt(x)" : "root takes its index and what's under it, like root(3, x)");
					return node{node::kind::root, "", std::move(*inside)};
				}
				if (bracket_names().contains(name) || accent_names().contains(name)) {
					if (!parenthesized) return fail(name + " takes parentheses, like " + name + "(x)");
					auto inside = arguments();
					if (!inside) return std::unexpected(inside.error());
					if (inside->size() != 1) return fail(name + " takes one thing, like " + name + "(x)");
					return node{bracket_names().contains(name) ? node::kind::bracket : node::kind::accent, name, std::move(*inside)};
				}
				if (name == "cases") {
					if (!parenthesized) return fail("cases takes parentheses, like cases(x if x >= 0; -x otherwise)");
					return cases();
				}
				if (big_names().contains(name)) return big(name);
				if (function_names().contains(name)) {
					if (parenthesized) return called(name);
					auto acts = acted_on();
					if (!acts) return acts;
					return node{node::kind::call, name, {std::move(*acts)}};
				}
				if (greek_names().contains(name)) return with_subscript_word(node{node::kind::greek, name, {}}, true);
				if (name == "inf" || name == "partial") return node{node::kind::symbol, name, {}};
				// dx, dy, dt: a d and a letter is a differential.
				if (name.size() == 2 && name[0] == 'd') return node{node::kind::differential, "", {node{node::kind::name, name.substr(1), {}}}};
				(void)was;
				// A longer name followed by parentheses is a function as written, area(x); a
				// letter is multiplied by what's in them, n(n + 1), and set the same as f(x).
				if (parenthesized && name.size() > 1) return called(name);
				return with_subscript_word(node{node::kind::name, name, {}}, name.size() == 1);
			}

			// A function and what's in the parentheses after it, one thing kept in them as
			// it was written, sin(x), so they're drawn.
			std::expected<node, problem> called(const std::string& name) {
				auto inside = arguments();
				if (!inside) return std::unexpected(inside.error());
				if (inside->size() == 1) return node{node::kind::call, name, {node{node::kind::group, "", std::move(*inside)}}};
				return node{node::kind::call, name, std::move(*inside)};
			}

			// A longer name's _ joins the next word to it, monthly_rate; after one letter or
			// a Greek one it's a subscript, read as the term goes on.
			std::expected<node, problem> with_subscript_word(node n, bool short_name) {
				while (!short_name && sign("_")) {
					std::size_t keep_at = at_;
					token keep = now_;
					next();
					if (now_.type != token::kind::name) {
						at_ = keep_at, now_ = keep;
						break;
					}
					n.text += "_" + now_.text;
					next();
				}
				return n;
			}

			// sum(k = 1 to n) k^2, int(0 to 1) x dx, int x dx, lim(x -> 0) f(x).
			std::expected<node, problem> big(const std::string& name) {
				node below{node::kind::row, "", {}}, above{node::kind::row, "", {}};
				if (sign("(")) {
					next();
					auto low = relation();
					if (!low) return low;
					below = std::move(*low);
					if (word("to")) {
						next();
						auto high = relation();
						if (!high) return high;
						above = std::move(*high);
					}
					if (!sign(")")) return fail("a ) to close the bounds, like sum(k = 1 to n)");
					next();
				}
				auto acts = acted_on();
				if (!acts) return acts;
				return node{node::kind::big, name, {std::move(below), std::move(above), std::move(*acts)}};
			}

			// cases(x if x >= 0; -x otherwise)
			std::expected<node, problem> cases() {
				next();  // (
				node out{node::kind::cases, "", {}};
				while (true) {
					auto value = sum();
					if (!value) return value;
					node condition{node::kind::row, "", {}};
					if (word("if")) {
						next();
						auto c = relation();
						if (!c) return c;
						condition = std::move(*c);
					} else if (word("otherwise")) {
						next();
						condition = node{node::kind::words, "otherwise", {}};
					}
					out.parts.push_back(node{node::kind::row, "", {std::move(*value), std::move(condition)}});
					if (sign(";")) {
						next();
						continue;
					}
					if (!sign(")")) return fail("a ; between cases, or a ) to close them");
					next();
					return out;
				}
			}
		};

	}  // namespace detail

	// An equation read, or where and why it can't be.
	inline std::expected<node, problem> parse(std::string_view written) { return detail::reader(written).whole(); }

	// A tree written out one way, its kinds and parts in parentheses, as the cases are.
	inline std::string written(const node& n) {
		using k = node::kind;
		auto all = [&](std::string head) {
			for (const auto& p : n.parts) head += " " + written(p);
			return "(" + head + ")";
		};
		switch (n.type) {
			case k::row: return all("row");
			case k::number: return n.text;
			case k::name: return n.text;
			case k::greek: return "greek:" + n.text;
			case k::symbol: return "symbol:" + n.text;
			case k::words: return "\"" + n.text + "\"";
			case k::infix: return all(n.text);
			case k::negative: return all("neg");
			case k::fraction: return all("frac");
			case k::power: return all("^");
			case k::subscript: return all("_");
			case k::prime: return all(n.text);
			case k::factorial: return all("!");
			case k::percent: return all("%");
			case k::group: return all("group");
			case k::call: return all("call:" + n.text);
			case k::root: return all("root");
			case k::bracket: return all(n.text);
			case k::accent: return all(n.text);
			case k::big: return all(n.text);
			case k::differential: return all("d");
			case k::matrix: return all("matrix");
			case k::cases: return all("cases");
		}
		return "";
	}

}  // namespace one::math
