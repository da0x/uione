// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An equation: its words in one::math's notation, set as math. While the caret's in it,
// it's its words, in a well; otherwise its equation, aligned as the block says. It keeps
// the room of the larger, so the page doesn't move as the caret comes and goes. A
// chemical equation is the same block, its words read as ce( ) reads them.

#include <format>

#include "kinds.hpp"
#include "one/chem.hpp"

namespace one::desktop::runtime::paper_kinds {

	namespace {

		key_tab greek(std::string name, std::vector<std::string> letters) {
			key_tab t{std::move(name), {}};
			for (const auto& n : letters) {
				bool capital = std::isupper(static_cast<unsigned char>(n[0]));
				std::string lower = n;
				lower[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[0])));
				t.keys.push_back({capital ? "Capital " + lower : n, n, n});
			}
			return t;
		}

		key_tab applied(std::string name, std::vector<std::string> functions) {
			key_tab t{std::move(name), {}};
			for (const auto& n : functions) t.keys.push_back({n, n + "(x)", n + "(‸)", n + "(§)‸"});
			return t;
		}

		key_tab named(std::string name, const std::vector<std::string>& symbols) {
			key_tab t{std::move(name), {}};
			for (const auto& e : symbols) t.keys.push_back({e, "ce(" + e + ")", e});
			return t;
		}

		// Everything the notation writes, by what it's for, as the web's sheet has it.
		const written_apart& equation() {
			static const written_apart apart{
			    "Equation",
			    {
			        {"Basic",
			         {{"Plus", "a + b", " + "},
			          {"Minus", "a - b", " - "},
			          {"Plus or minus", "a +- b", " +- "},
			          {"Times", "a * b", " * "},
			          {"Fraction", "a / b", "‸ / ", "(§) / ‸"},
			          {"Parentheses", "(x)", "(‸)", "(§)‸"},
			          {"Equals", "a = b", " = "},
			          {"Not equal", "a != b", " != "},
			          {"Less than", "a < b", " < "},
			          {"Greater than", "a > b", " > "},
			          {"At most", "a <= b", " <= "},
			          {"At least", "a >= b", " >= "},
			          {"About", "a ~= b", " ~= "},
			          {"Percent", "5%", "%"}}},
			        {"Powers",
			         {{"Squared", "x^2", "^2", "(§)^2‸"},
			          {"Power", "x^n", "^(‸)", "(§)^(‸)"},
			          {"Subscript", "x_i", "_‸"},
			          {"Square root", "sqrt(x)", "sqrt(‸)", "sqrt(§)‸"},
			          {"Cube root", "root(3, x)", "root(3, ‸)", "root(3, §)‸"},
			          {"Root", "root(n, x)", "root(‸, )", "root(‸, §)"},
			          {"Absolute value", "|x|", "|‸|", "|§|‸"},
			          {"Factorial", "n!", "!"},
			          {"Prime", "f'", "'"},
			          {"Exponential", "e^x", "e^(‸)", "e^(§)‸"}}},
			        greek("Greek", {"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "lambda", "mu", "pi", "rho", "sigma", "tau", "phi", "omega", "Gamma",
			                        "Delta", "Theta", "Lambda", "Pi", "Sigma", "Phi", "Omega"}),
			        {"Calculus",
			         {{"Sum", "sum(k = 1 to n) a_k", "sum(k = 1 to n) ‸"},
			          {"Product", "prod(i = 1 to n) x_i", "prod(i = 1 to n) ‸"},
			          {"Integral", "int(a to b) f dx", "int(0 to 1) ‸ dx", "int(0 to 1) § dx‸"},
			          {"Integral without bounds", "int f dx", "int ‸ dx", "int § dx‸"},
			          {"Limit", "lim(x -> 0) f", "lim(x -> 0) ‸"},
			          {"Derivative", "d / dx f", "d / dx ‸"},
			          {"dy over dx", "dy / dx", "dy / dx"},
			          {"Partial", "partial f", "partial "},
			          {"Infinity", "inf", "inf"},
			          {"Tends to", "x -> a", " -> "}}},
			        applied("Functions", {"sin", "cos", "tan", "ln", "log", "exp", "min", "max", "floor", "ceil", "round"}),
			        {"Layout",
			         {{"Matrix", "[a, b; c, d]", "[‸, ; , ]"},
			          {"Row", "[a, b, c]", "[‸, , ]"},
			          {"Cases", "cases(a if x > 0; b otherwise)", "cases(‸ if ; otherwise)"},
			          {"Hat", "hat(x)", "hat(‸)", "hat(§)‸"},
			          {"Bar", "bar(x)", "bar(‸)", "bar(§)‸"},
			          {"Vector", "vec(v)", "vec(‸)", "vec(§)‸"},
			          {"Dot", "dot(x)", "dot(‸)", "dot(§)‸"},
			          {"Two dots", "ddot(x)", "ddot(‸)", "ddot(§)‸"},
			          {"Words", "\"text\"", "\"‸\"", "\"§\"‸"},
			          {"Implies", "a => b", " => "},
			          {"If and only if", "a <=> b", " <=> "},
			          {"In", "x in A", " in "}}},
			    },
			    [](const std::string& words) { return words; },
			    {},
			    true,
			};
			return apart;
		}

		// What a reaction's written with, by what it's for; and, as it's typed, whether it
		// balances, or, one thing alone, what a mole of it weighs.
		const written_apart& chemical() {
			static const written_apart apart{
			    "Chemistry",
			    {
			        named("Common", {"H", "C", "N", "O", "F", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "K", "Ca", "Fe", "Cu", "Zn", "Ag", "Br", "I", "Ba", "Pt", "Au", "Pb"}),
			        {"Arrows",
			         {{"Plus", "ce(H2 + O2)", " + "},
			          {"Reacts to make", "ce(H2 -> 2H)", " -> "},
			          {"Equilibrium", "ce(H2 <=> 2H)", " <=> "},
			          {"Both ways", "ce(H2 <-> 2H)", " <-> "},
			          {"Made from", "ce(H2 <- 2H)", " <- "},
			          {"With heat", "ce(H2 ->[Delta] 2H)", " ->[Delta] "},
			          {"With something over the arrow", "ce(H2 ->[Pt] 2H)", " ->[‸] "},
			          {"Balanced as written", "ce(H2 = 2H)", " = "}}},
			        {"States",
			         {{"Solid", "ce(Fe(s))", "(s)"},
			          {"Liquid", "ce(H2O(l))", "(l)"},
			          {"Gas", "ce(O2(g))", "(g)"},
			          {"Dissolved", "ce(Na^+(aq))", "(aq)"},
			          {"Hydrate", "ce(CuSO4*5H2O)", "*"},
			          {"Group", "ce((OH)2)", "(‸)", "(§)‸"},
			          {"Brackets", "ce([Cu(NH3)4])", "[‸]", "[§]‸"}}},
			        {"Charges",
			         {{"Plus one", "ce(Na^+)", "^+"},
			          {"Minus one", "ce(Cl^-)", "^-"},
			          {"Plus two", "ce(Ca^2+)", "^2+"},
			          {"Minus two", "ce(O^2-)", "^2-"},
			          {"Plus three", "ce(Al^3+)", "^3+"},
			          {"Minus three", "ce(N^3-)", "^3-"},
			          {"Electron", "ce(e^-)", "e^-"}}},
			        named("Elements", one::math::chem::elements()),
			    },
			    [](const std::string& words) { return "ce(" + words + ")"; },
			    [](const std::string& words) -> std::optional<std::string> {
				    auto read = one::math::chem::parse(words);
				    if (!read) return std::nullopt;
				    if (auto weighed = one::math::chem::molar_mass(*read)) return std::format("{:.2f} g/mol", *weighed);
				    if (read->arrows.empty()) return std::nullopt;
				    return one::math::chem::unbalanced(*read).value_or("Balanced");
			    },
			};
			return apart;
		}

		struct set {
			set_equation equation;
			float down = 0;  // from the block's top
		};

		class math final : public kind {
		public:
			explicit math(bool chemical) : chemical_(chemical) {}

			text_style words_look(const setting& at) const override {
				text_style s = at.look.code();
				s.line_height = 1.5f;
				return s;
			}
			std::pair<float, float> room() const override { return {10, 18}; }
			bool joins() const override { return false; }
			const written_apart* apart() const override { return chemical_ ? &chemical() : &equation(); }

			void lay_out(laid& l, const setting& at, bool here) const override {
				if (l.b.words.empty()) return;
				set made{equation_set(chemical_ ? "ce(" + l.b.words + ")" : l.b.words, at.look.text_size * 1.25f * at.zoom, true, at.look.ink), 0};
				if (!made.equation.read) return;
				float tall = made.equation.ascent + made.equation.descent;
				made.down = std::max(0.f, l.height - tall) / 2;
				l.words_y = std::max(0.f, tall - l.height) / 2;
				l.height = std::max(l.height, tall);
				l.raw = here;
				l.own = made;
			}

			void paint_behind(canvas& c, const laid& l, point at, const setting& s) const override {
				if (l.raw) c.box({at.x - 8, at.y - 4, l.width + 16, l.height + 8}, {.fill = s.look.sunken, .radius = 4});
			}

			void paint_front(canvas& c, const laid& l, point at, const setting&) const override {
				const auto* made = std::any_cast<set>(&l.own);
				if (l.raw || !made) return;
				float w = made->equation.width;
				float x = l.b.align == "left" ? at.x : l.b.align == "right" ? at.x + l.width - w : at.x + (l.width - w) / 2;
				made->equation.draw(c, {x, at.y + made->down});
			}

		private:
			bool chemical_;
		};

	}  // namespace

	const kind& math_kind() {
		static const math k(false);
		return k;
	}

	const kind& chem_kind() {
		static const math k(true);
		return k;
	}

}  // namespace one::desktop::runtime::paper_kinds
