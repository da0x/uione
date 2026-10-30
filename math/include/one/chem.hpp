// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Chemical equations written as they'd be typed, 2H2 + O2 -> 2H2O, read as a reaction
// that's set as a textbook sets one, checked to balance, and weighed (math/README.md).
// Set through one::math, as ce(...) inside an equation. The twin of @uione/react's
// chem.ts, both held to math/tests/chem.tsv and, set, to math/tests/cases.tsv.

#pragma once

#include <algorithm>
#include <cmath>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "one/math.hpp"

namespace one::math::chem {

	// Each element by its symbol, and its standard atomic weight; for one with no stable
	// isotope, its longest-lived isotope's mass number.
	inline const std::map<std::string, double, std::less<>>& weights() {
		static const std::map<std::string, double, std::less<>> all{
		    {"H", 1.008},   {"He", 4.0026}, {"Li", 6.94},   {"Be", 9.0122}, {"B", 10.81},   {"C", 12.011},  {"N", 14.007},  {"O", 15.999},  {"F", 18.998},
		    {"Ne", 20.18},  {"Na", 22.99},  {"Mg", 24.305}, {"Al", 26.982}, {"Si", 28.085}, {"P", 30.974},  {"S", 32.06},   {"Cl", 35.45},  {"Ar", 39.948},
		    {"K", 39.098},  {"Ca", 40.078}, {"Sc", 44.956}, {"Ti", 47.867}, {"V", 50.942},  {"Cr", 51.996}, {"Mn", 54.938}, {"Fe", 55.845}, {"Co", 58.933},
		    {"Ni", 58.693}, {"Cu", 63.546}, {"Zn", 65.38},  {"Ga", 69.723}, {"Ge", 72.63},  {"As", 74.922}, {"Se", 78.971}, {"Br", 79.904}, {"Kr", 83.798},
		    {"Rb", 85.468}, {"Sr", 87.62},  {"Y", 88.906},  {"Zr", 91.224}, {"Nb", 92.906}, {"Mo", 95.95},  {"Tc", 98},     {"Ru", 101.07}, {"Rh", 102.91},
		    {"Pd", 106.42}, {"Ag", 107.87}, {"Cd", 112.41}, {"In", 114.82}, {"Sn", 118.71}, {"Sb", 121.76}, {"Te", 127.6},  {"I", 126.9},   {"Xe", 131.29},
		    {"Cs", 132.91}, {"Ba", 137.33}, {"La", 138.91}, {"Ce", 140.12}, {"Pr", 140.91}, {"Nd", 144.24}, {"Pm", 145},    {"Sm", 150.36}, {"Eu", 151.96},
		    {"Gd", 157.25}, {"Tb", 158.93}, {"Dy", 162.5},  {"Ho", 164.93}, {"Er", 167.26}, {"Tm", 168.93}, {"Yb", 173.05}, {"Lu", 174.97}, {"Hf", 178.49},
		    {"Ta", 180.95}, {"W", 183.84},  {"Re", 186.21}, {"Os", 190.23}, {"Ir", 192.22}, {"Pt", 195.08}, {"Au", 196.97}, {"Hg", 200.59}, {"Tl", 204.38},
		    {"Pb", 207.2},  {"Bi", 208.98}, {"Po", 209},    {"At", 210},    {"Rn", 222},    {"Fr", 223},    {"Ra", 226},    {"Ac", 227},    {"Th", 232.04},
		    {"Pa", 231.04}, {"U", 238.03},  {"Np", 237},    {"Pu", 244},    {"Am", 243},    {"Cm", 247},    {"Bk", 247},    {"Cf", 251},    {"Es", 252},
		    {"Fm", 257},    {"Md", 258},    {"No", 259},    {"Lr", 266},    {"Rf", 267},    {"Db", 268},    {"Sg", 269},    {"Bh", 270},    {"Hs", 269},
		    {"Mt", 278},    {"Ds", 281},    {"Rg", 282},    {"Cn", 285},    {"Nh", 286},    {"Fl", 289},    {"Mc", 290},    {"Lv", 293},    {"Ts", 294},
		    {"Og", 294},
		};
		return all;
	}

	// The elements, in the order of their atomic numbers.
	inline const std::vector<std::string>& elements() {
		static const std::vector<std::string> all{
		    "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca",
		    "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr",
		    "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd",
		    "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg",
		    "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm",
		    "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"};
		return all;
	}

	// What a formula is made of: an element, or a group of them in parentheses or
	// brackets, each as many times as its count says.
	struct unit {
		std::string element;
		std::vector<unit> inside;
		char bracket = 0;   // '(' or '[' for a group
		std::string count;  // "" for one
	};

	// A formula, as many as its number before it says, with its charge; or an electron.
	struct formula {
		std::string number;  // "" for one; 5 in CuSO4*5H2O
		std::vector<unit> units;
		std::string charge;  // "", "+", "2-"
		bool electron = false;
	};

	// One taking part, as many as its number says, of one formula or several joined, as
	// a hydrate's are, and the state it's in.
	struct term {
		std::string number;
		std::vector<formula> formulas;
		std::string state;  // "", "s", "l", "g" or "aq"
	};

	// Sides, each its terms, with an arrow between each and the next, and what's over
	// and under it.
	struct arrow {
		std::string kind;  // ->, <-, <->, <=> or =
		std::string over, under;
	};
	struct reaction {
		std::vector<std::vector<term>> sides;
		std::vector<arrow> arrows;
	};

	namespace detail {

		inline bool digit(char c) { return c >= '0' && c <= '9'; }
		inline bool upper(char c) { return c >= 'A' && c <= 'Z'; }
		inline bool lower(char c) { return c >= 'a' && c <= 'z'; }
		inline bool state(std::string_view s) { return s == "aq" || s == "s" || s == "l" || s == "g"; }

		struct failed {
			problem said;
		};

		class reader {
		public:
			explicit reader(std::string_view written) : s_(written) {}

			reaction whole() {
				space();
				if (at_ >= s_.size()) fail("something to react, like 2H2 + O2 -> 2H2O");
				reaction made;
				made.sides.push_back(side());
				for (;;) {
					space();
					std::string kind;
					for (std::string_view a : {"<=>", "<->", "->", "<-", "="}) {
						if (s_.substr(at_).starts_with(a)) {
							kind = a;
							break;
						}
					}
					if (kind.empty()) break;
					at_ += kind.size();
					std::string over = said_over(), under = over.empty() ? "" : said_over();
					made.arrows.push_back({kind, over, under});
					space();
					if (at_ >= s_.size()) fail("what it makes after the arrow, like " + kind + " 2H2O");
					made.sides.push_back(side());
				}
				space();
				if (at_ < s_.size()) fail("a + or an arrow between what reacts, not \"" + std::string(1, s_[at_]) + "\"");
				return made;
			}

		private:
			std::string_view s_;
			std::size_t at_ = 0;

			[[noreturn]] void fail(std::string says) const { throw failed{problem{at_, std::move(says)}}; }
			char at(std::size_t i) const { return i < s_.size() ? s_[i] : '\0'; }
			void space() {
				while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t')) ++at_;
			}
			std::string digits() {
				std::size_t from = at_;
				while (digit(at(at_))) ++at_;
				return std::string(s_.substr(from, at_ - from));
			}

			// What's over or under an arrow, in brackets right after it: ->[heat][Pt].
			std::string said_over() {
				if (at(at_) != '[') return "";
				auto close = s_.find(']', at_);
				if (close == std::string_view::npos) fail("a ] after what's over the arrow, like ->[heat]");
				std::string_view said = s_.substr(at_ + 1, close - at_ - 1);
				while (!said.empty() && said.front() == ' ') said.remove_prefix(1);
				while (!said.empty() && said.back() == ' ') said.remove_suffix(1);
				at_ = close + 1;
				return std::string(said);
			}

			std::vector<term> side() {
				std::vector<term> terms{one_term()};
				for (;;) {
					std::size_t keep = at_;
					space();
					if (at(at_) != '+') {
						at_ = keep;
						return terms;
					}
					++at_;
					space();
					terms.push_back(one_term());
				}
			}

			// 2H2O(l): its number, what it is, and its state.
			term one_term() {
				space();
				term t;
				t.number = number();
				if (!t.number.empty()) space();
				t.formulas.push_back(one_formula());
				// A hydrate's parts joined by a dot: CuSO4*5H2O.
				for (;;) {
					std::size_t dot = s_.substr(at_).starts_with("·") ? 2 : (at(at_) == '*' || at(at_) == '.') ? 1 : 0;
					if (dot == 0) break;
					at_ += dot;
					std::string part = number();
					formula f = one_formula();
					f.number = part;
					t.formulas.push_back(std::move(f));
				}
				if (at(at_) == '(') {
					auto close = s_.find(')', at_);
					std::string_view inside = close == std::string_view::npos ? "" : s_.substr(at_ + 1, close - at_ - 1);
					if (state(inside)) t.state = inside, at_ = close + 1;
				}
				return t;
			}

			// A number before a formula: 2, or a fraction, 1/2.
			std::string number() {
				std::string n = digits();
				if (!n.empty() && at(at_) == '/' && digit(at(at_ + 1))) {
					++at_;
					n += "/" + digits();
				}
				return n;
			}

			formula one_formula() {
				// An electron: e, with its charge or without.
				if (at(at_) == 'e' && !lower(at(at_ + 1))) {
					++at_;
					std::string c = charge();
					if (!c.empty() && c != "-") fail("an electron's charge is -, as e^-");
					return formula{"", {}, "-", true};
				}
				auto made = units();
				if (made.empty()) {
					if (lower(at(at_))) fail("an element starts with a capital, like Na or H");
					fail(at_ >= s_.size() ? "something more to finish the equation" : "an element, like H or Na, not \"" + std::string(1, s_[at_]) + "\"");
				}
				return formula{"", std::move(made), charge(), false};
			}

			std::vector<unit> units() {
				std::vector<unit> out;
				for (;;) {
					char c = at(at_);
					if (upper(c)) {
						std::string symbol(1, c);
						if (lower(at(at_ + 1))) {
							std::string two = symbol + at(at_ + 1);
							if (!weights().contains(two)) fail("there's no element " + two);
							symbol = two;
						} else if (!weights().contains(symbol)) {
							fail("there's no element " + symbol);
						}
						at_ += symbol.size();
						out.push_back(unit{symbol, {}, 0, digits()});
					} else if (c == '(' || c == '[') {
						// A state after a formula isn't a group in it.
						auto close = s_.find(')', at_);
						if (c == '(' && !out.empty() && close != std::string_view::npos && state(s_.substr(at_ + 1, close - at_ - 1))) return out;
						++at_;
						auto inside = units();
						char shut = c == '(' ? ')' : ']';
						if (inside.empty()) fail(c == '(' ? "an element in the parentheses, like (OH)2" : "an element in the brackets, like [Cu(NH3)4]^2+");
						if (at(at_) != shut) fail(c == '(' ? "a ) to close the group, like (OH)2" : "a ] to close the brackets, like [Cu(NH3)4]^2+");
						++at_;
						out.push_back(unit{"", std::move(inside), c, digits()});
					} else {
						return out;
					}
				}
			}

			// ^2-, ^+, ^3+, or in braces, ^{2-}.
			std::string charge() {
				if (at(at_) != '^') return "";
				++at_;
				bool braced = at(at_) == '{';
				if (braced) ++at_;
				std::string size = digits();
				char sign = at(at_);
				if (sign != '+' && sign != '-') fail("a charge after ^, like ^2- or ^+");
				++at_;
				if (size.empty()) size = digits();
				if (braced) {
					if (at(at_) != '}') fail("a } to close the charge, like ^{2-}");
					++at_;
				}
				return (size == "1" ? "" : size) + sign;
			}
		};

		inline node made(node::kind k, std::string text = "", std::vector<node> parts = {}) { return node{k, std::move(text), std::move(parts)}; }
		inline node row(std::vector<node> parts) { return parts.size() == 1 ? std::move(parts[0]) : made(node::kind::row, "", std::move(parts)); }
		inline node amount(const std::string& n) {
			auto slash = n.find('/');
			if (slash == std::string::npos) return made(node::kind::number, n);
			return made(node::kind::fraction, "", {made(node::kind::number, n.substr(0, slash)), made(node::kind::number, n.substr(slash + 1))});
		}
		inline node charged(std::string charge) {
			if (charge.ends_with('-')) charge = charge.substr(0, charge.size() - 1) + "−";
			return made(node::kind::words, charge);
		}

		inline node unit_as_math(const unit& u) {
			node shown;
			if (!u.element.empty()) {
				shown = made(node::kind::words, u.element);
			} else {
				std::vector<node> inside;
				for (const auto& i : u.inside) inside.push_back(unit_as_math(i));
				shown = u.bracket == '[' ? made(node::kind::bracket, "square", {row(std::move(inside))}) : made(node::kind::group, "", {row(std::move(inside))});
			}
			return u.count.empty() ? shown : made(node::kind::subscript, "", {std::move(shown), made(node::kind::number, u.count)});
		}

		inline node formula_as_math(const formula& f) {
			std::vector<node> parts;
			if (f.electron) parts.push_back(made(node::kind::words, "e"));
			for (const auto& u : f.units) parts.push_back(unit_as_math(u));
			// The charge over the last unit's count, both stacked on it.
			if (!f.charge.empty()) parts.back() = made(node::kind::power, "", {std::move(parts.back()), charged(f.charge)});
			if (!f.number.empty()) parts.insert(parts.begin(), amount(f.number));
			return row(std::move(parts));
		}

		inline node term_as_math(const term& t) {
			std::vector<node> parts;
			if (!t.number.empty()) parts.push_back(amount(t.number));
			for (std::size_t i = 0; i < t.formulas.size(); ++i) {
				if (i > 0) parts.push_back(made(node::kind::symbol, "cdot"));
				node f = formula_as_math(t.formulas[i]);
				if (f.type == node::kind::row) {
					for (auto& p : f.parts) parts.push_back(std::move(p));
				} else {
					parts.push_back(std::move(f));
				}
			}
			if (!t.state.empty()) parts.push_back(made(node::kind::group, "", {made(node::kind::words, t.state)}));
			return row(std::move(parts));
		}

		inline std::expected<reaction, problem> read(std::string_view written) {
			try {
				return reader(written).whole();
			} catch (const failed& f) {
				return std::unexpected(f.said);
			}
		}

		// What's over an arrow: a formula as one, a Greek letter as its letter, or words.
		inline node over_as_math(const std::string& said) {
			if (said.empty()) return made(node::kind::row);
			if (math::detail::greek_names().contains(said)) return made(node::kind::greek, said);
			auto r = read(said);
			if (r && r->arrows.empty() && r->sides[0].size() == 1) return term_as_math(r->sides[0][0]);
			return made(node::kind::words, said);
		}

		// How many of each element, and the charge, in what's taking part, the elements
		// in the order they're first met.
		struct counted {
			std::vector<std::pair<std::string, double>> atoms;
			double charge = 0;
			void add(const std::string& e, double n) {
				for (auto& [k, v] : atoms) {
					if (k == e) {
						v += n;
						return;
					}
				}
				atoms.emplace_back(e, n);
			}
			double of(const std::string& e) const {
				for (const auto& [k, v] : atoms) {
					if (k == e) return v;
				}
				return 0;
			}
		};
		inline double amount_of(const std::string& n) {
			if (n.empty()) return 1;
			auto slash = n.find('/');
			if (slash == std::string::npos) return std::stod(n);
			return std::stod(n.substr(0, slash)) / std::stod(n.substr(slash + 1));
		}
		inline void count_units(const std::vector<unit>& units, double times, counted& into) {
			for (const auto& u : units) {
				double n = times * amount_of(u.count);
				if (!u.element.empty()) into.add(u.element, n);
				else count_units(u.inside, n, into);
			}
		}
		inline counted count(const std::vector<term>& terms) {
			counted out;
			for (const auto& t : terms) {
				for (const auto& f : t.formulas) {
					double times = amount_of(t.number) * amount_of(f.number);
					count_units(f.units, times, out);
					if (!f.charge.empty()) out.charge += times * (f.charge.ends_with('-') ? -1 : 1) * amount_of(f.charge.substr(0, f.charge.size() - 1));
				}
			}
			return out;
		}
		inline std::string said(double n) { return std::format("{}", std::round(n * 1000) / 1000); }
		inline std::string charge_said(double n) {
			if (n == 0) return "0";
			std::string size = said(std::abs(n));
			return (size == "1" ? "" : size) + (n > 0 ? "+" : "−");
		}

	}  // namespace detail

	// A chemical equation read, or where and why it can't be.
	inline std::expected<reaction, problem> parse(std::string_view written) { return detail::read(written); }

	// A reaction as one::math's tree, to be set as an equation is.
	inline node as_math(const reaction& r) {
		using detail::made;
		std::vector<node> sides;
		for (const auto& terms : r.sides) {
			node side = detail::term_as_math(terms[0]);
			for (std::size_t i = 1; i < terms.size(); ++i) side = made(node::kind::infix, "+", {std::move(side), detail::term_as_math(terms[i])});
			sides.push_back(std::move(side));
		}
		node out = std::move(sides[0]);
		for (std::size_t i = 1; i < sides.size(); ++i) {
			const arrow& a = r.arrows[i - 1];
			if (a.kind == "=") out = made(node::kind::infix, "=", {std::move(out), std::move(sides[i])});
			else out = made(node::kind::arrow, a.kind, {std::move(out), std::move(sides[i]), detail::over_as_math(a.over), detail::over_as_math(a.under)});
		}
		return out;
	}

	// Whether each arrow's two sides hold as many of each element, and as much charge;
	// what differs, said, when they don't.
	inline std::optional<std::string> unbalanced(const reaction& r) {
		for (std::size_t i = 0; i < r.arrows.size(); ++i) {
			auto left = detail::count(r.sides[i]), right = detail::count(r.sides[i + 1]);
			std::vector<std::string> order;
			for (const auto& [e, n] : left.atoms) order.push_back(e);
			for (const auto& [e, n] : right.atoms) {
				if (std::find(order.begin(), order.end(), e) == order.end()) order.push_back(e);
			}
			std::string differ;
			for (const auto& e : order) {
				double l = left.of(e), rr = right.of(e);
				if (std::abs(l - rr) > 1e-9) differ += (differ.empty() ? "" : "; ") + detail::said(l) + " " + e + " on the left, " + detail::said(rr) + " on the right";
			}
			if (std::abs(left.charge - right.charge) > 1e-9) {
				differ += (differ.empty() ? "" : "; ") + std::string("a charge of ") + detail::charge_said(left.charge) + " on the left, " + detail::charge_said(right.charge) + " on the right";
			}
			if (!differ.empty()) return "not balanced: " + differ;
		}
		return std::nullopt;
	}

	// What one mole of it weighs, in grams, when it's one thing alone, like H2O or
	// CuSO4*5H2O: its number before it counted too.
	inline std::optional<double> molar_mass(const reaction& r) {
		if (!r.arrows.empty() || r.sides[0].size() != 1) return std::nullopt;
		auto c = detail::count(r.sides[0]);
		if (c.atoms.empty()) return std::nullopt;
		double grams = 0;
		for (const auto& [e, n] : c.atoms) grams += weights().find(e)->second * n;
		return grams;
	}

	// A reaction written back as its tree, as math/tests/chem.tsv writes it.
	inline std::string written(const reaction& r) {
		std::function<std::string(const unit&)> unit_said = [&](const unit& u) {
			std::string out = u.element;
			if (out.empty()) {
				std::string inside;
				for (const auto& i : u.inside) inside += (inside.empty() ? "" : " ") + unit_said(i);
				out = u.bracket == '[' ? "[" + inside + "]" : "(" + inside + ")";
			}
			return u.count.empty() ? out : out + "_" + u.count;
		};
		auto formula_said = [&](const formula& f) {
			std::string out = f.number.empty() ? "" : f.number + " ";
			if (f.electron) out += "e";
			for (std::size_t i = 0; i < f.units.size(); ++i) out += (i > 0 ? " " : "") + unit_said(f.units[i]);
			return f.charge.empty() ? out : out + "^" + f.charge;
		};
		auto term_said = [&](const term& t) {
			std::string out = "{" + (t.number.empty() ? "" : t.number + " ");
			for (std::size_t i = 0; i < t.formulas.size(); ++i) out += (i > 0 ? " * " : "") + formula_said(t.formulas[i]);
			return out + (t.state.empty() ? "" : " (" + t.state + ")") + "}";
		};
		auto side_said = [&](const std::vector<term>& terms) {
			std::string out;
			for (std::size_t i = 0; i < terms.size(); ++i) out += (i > 0 ? " + " : "") + term_said(terms[i]);
			return out;
		};
		std::string out = side_said(r.sides[0]);
		for (std::size_t i = 0; i < r.arrows.size(); ++i) {
			const arrow& a = r.arrows[i];
			out += " " + a.kind + (a.over.empty() ? "" : "[" + a.over + "]") + (a.under.empty() ? "" : "[" + a.under + "]") + " " + side_said(r.sides[i + 1]);
		}
		return out;
	}

}  // namespace one::math::chem

namespace one::math::detail {

	// What's in ce( ) read as a chemical equation, set as math; or why it can't be, where
	// in it.
	inline std::expected<node, problem> chemical(std::string_view written) {
		auto r = chem::parse(written);
		if (!r) return std::unexpected(r.error());
		return chem::as_math(*r);
	}

}  // namespace one::math::detail
