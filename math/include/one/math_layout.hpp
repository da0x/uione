// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An equation set as a book sets it: its tree laid out as glyphs, rules and lines at
// places in pixels, from the math font's own measures (math_measures.hpp), so a window
// and a web page, which draw what this says, place every glyph alike. The twin of
// @uione/react's math.ts layOutMath, both held to math/tests/layouts.txt.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "one/math.hpp"
#include "one/math_measures.hpp"

namespace one::math {

	// One thing drawn: a glyph of the math font, its origin on its baseline; a filled
	// rule, its top left; or a line through points, `size` its thickness. In pixels from
	// the equation's left end on its baseline, down being more.
	struct placed {
		enum class kind { glyph, rule, line } type = kind::glyph;
		std::uint32_t code = 0;
		double x = 0, y = 0, size = 0;  // a glyph's em, or a line's thickness
		double width = 0, height = 0;   // a rule's
		std::vector<std::pair<double, double>> points;
	};

	// An equation laid out: how wide it is, how far it rises above its baseline and
	// falls below, and what's drawn.
	struct laid_out {
		double width = 0, ascent = 0, descent = 0;
		std::vector<placed> drawn;
	};

	namespace detail {

		inline const measures::glyph* measured(std::uint32_t code) {
			auto at = std::lower_bound(measures::glyphs.begin(), measures::glyphs.end(), code, [](const measures::glyph& g, std::uint32_t c) { return g.code < c; });
			return at != measures::glyphs.end() && at->code == code ? &*at : nullptr;
		}

		// The size an equation's parts are set at: as displayed, in a line of text, as a
		// script, or a script's script.
		enum class style { display, text, script, scripts };
		inline style smaller(style s) { return s == style::display ? style::text : s == style::text ? style::script : style::scripts; }
		inline style lowered(style s) { return s == style::display || s == style::text ? style::script : style::scripts; }

		class setter {
		public:
			explicit setter(double em) : em_(em) {}

			laid_out set(const node& n, style s) {
				switch (n.type) {
					case node::kind::number: return number(n.text, s);
					case node::kind::name: return name(n.text, s);
					case node::kind::greek: return glyph(greek(n.text), s);
					case node::kind::symbol: return glyph(n.text == "inf" ? 0x221E : n.text == "cdot" ? 0x22C5 : 0x2202, s);
					case node::kind::words: return word(n.text, s, false);
					case node::kind::row: return row(n.parts, s);
					case node::kind::infix: return infix(n, s);
					case node::kind::negative: return beside({glyph(0x2212, s), set(n.parts[0], s)});
					case node::kind::fraction: return fraction(n, s);
					case node::kind::power: return scripts(n.parts[0], nullptr, &n.parts[1], s);
					case node::kind::subscript: return scripts(n.parts[0], &n.parts[1], nullptr, s);
					case node::kind::prime: return beside({set(n.parts[0], s), glyph(n.text.size() > 1 ? 0x2033 : 0x2032, s)});
					case node::kind::factorial: return beside({set(n.parts[0], s), glyph('!', s)});
					case node::kind::percent: return beside({set(n.parts[0], s), glyph('%', s)});
					case node::kind::group: return around(set(n.parts[0], s), '(', ')', s);
					case node::kind::call: return call(n, s);
					case node::kind::root: return root(n, s);
					case node::kind::bracket: return around(set(n.parts[0], s), n.text == "abs" ? '|' : n.text == "floor" ? 0x230A : n.text == "square" ? '[' : 0x2308,
					                                        n.text == "abs" ? '|' : n.text == "floor" ? 0x230B : n.text == "square" ? ']' : 0x2309, s);
					case node::kind::accent: return accent(n, s);
					case node::kind::big: return big(n, s);
					case node::kind::differential: return beside({word("d", s, false), set(n.parts[0], s)});
					case node::kind::matrix: return matrix(n, s);
					case node::kind::cases: return cases(n, s);
					case node::kind::arrow: return arrow(n, s);
				}
				return {};
			}

		private:
			double em_;

			double scale(style s) const {
				double base = em_ / measures::units;
				if (s == style::script) return base * measures::script_percent_scale_down / 100.0;
				if (s == style::scripts) return base * measures::script_script_percent_scale_down / 100.0;
				return base;
			}
			double unit(style s) const { return scale(s) * measures::units; }  // an em in pixels, at this size
			double axis(style s) const { return measures::axis_height * scale(s); }

			// Things side by side, each placed after the one before.
			static laid_out beside(std::vector<laid_out> parts, double gap = 0) {
				laid_out out;
				bool first = true;
				for (auto& p : parts) {
					if (!first) out.width += gap;
					first = false;
					move(p, out.width, 0);
					out.ascent = std::max(out.ascent, p.ascent), out.descent = std::max(out.descent, p.descent);
					out.width += p.width;
					out.drawn.insert(out.drawn.end(), std::make_move_iterator(p.drawn.begin()), std::make_move_iterator(p.drawn.end()));
				}
				return out;
			}
			static void move(laid_out& l, double dx, double dy) {
				for (auto& d : l.drawn) {
					d.x += dx, d.y += dy;
					for (auto& [px, py] : d.points) px += dx, py += dy;
				}
			}
			laid_out space(double ems, style s) const { return laid_out{ems * unit(s), 0, 0, {}}; }

			laid_out glyph(std::uint32_t code, style s) const {
				laid_out out;
				const measures::glyph* g = measured(code);
				double k = scale(s);
				if (!g) {
					out.width = 0.5 * unit(s);
					return out;
				}
				out.width = g->advance * k;
				out.ascent = std::max(0.0, g->y_max * k);
				out.descent = std::max(0.0, -g->y_min * k);
				out.drawn.push_back(placed{placed::kind::glyph, code, 0, 0, unit(s), 0, 0, {}});
				return out;
			}

			// Characters upright, as numbers and words and longer names are.
			laid_out word(const std::string& text, style s, bool spaced_underscores = false) const {
				std::vector<laid_out> parts;
				// Each character, as UTF-8 writes it: one byte, or a lead byte and those that follow.
				for (std::size_t i = 0; i < text.size();) {
					auto c = static_cast<unsigned char>(text[i]);
					std::size_t bytes = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
					std::uint32_t code = bytes == 1 ? c : c & (0xFF >> (bytes + 1));
					for (std::size_t b = 1; b < bytes && i + b < text.size(); ++b) code = (code << 6) | (static_cast<unsigned char>(text[i + b]) & 0x3F);
					i += bytes;
					parts.push_back(glyph(spaced_underscores && code == '_' ? ' ' : code, s));
				}
				return beside(std::move(parts));
			}

			// A number as written, 6.02e23 as 6.02 × 10 raised to 23.
			laid_out number(const std::string& text, style s) {
				auto e = text.find_first_of("eE");
				if (e == std::string::npos) return word(text, s, false);
				laid_out ten = word("10", s);
				laid_out power = word(text.substr(e + 1), lowered(s), false);
				double k = scale(s);
				move(power, ten.width, -measures::superscript_shift_up * k);
				ten.ascent = std::max(ten.ascent, measures::superscript_shift_up * k + power.ascent);
				ten.drawn.insert(ten.drawn.end(), power.drawn.begin(), power.drawn.end());
				ten.width += power.width;
				return beside({word(text.substr(0, e), s, false), space(2.0 / 18, s), glyph(0x00D7, s), space(2.0 / 18, s), ten});
			}

			// A letter in math italic; a longer name upright, its underscores spaces.
			laid_out name(const std::string& text, style s) const {
				if (text.size() != 1 || !std::isalpha(static_cast<unsigned char>(text[0]))) return word(text, s, true);
				char c = text[0];
				std::uint32_t code = c >= 'a' ? (c == 'h' ? 0x210E : 0x1D44E + static_cast<std::uint32_t>(c - 'a')) : 0x1D434 + static_cast<std::uint32_t>(c - 'A');
				return glyph(code, s);
			}

			static std::uint32_t greek(const std::string& n) {
				static const std::vector<std::string> lower{"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa", "lambda", "mu", "nu", "xi",
				                                            "omicron", "pi", "rho", "", "sigma", "tau", "upsilon", "phi", "chi", "psi", "omega"};
				for (std::size_t i = 0; i < lower.size(); ++i) {
					if (lower[i].empty()) continue;
					if (lower[i] == n) return 0x3B1 + static_cast<std::uint32_t>(i);
					std::string upper = lower[i];
					upper[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(upper[0])));
					if (upper == n) return 0x391 + static_cast<std::uint32_t>(i);
				}
				return '?';
			}

			// What a part holds, without parentheses that only group it.
			static const node& ungrouped(const node& n) { return n.type == node::kind::group ? n.parts[0] : n; }

			static bool starts_with_digit(const node& n) {
				if (n.type == node::kind::number) return true;
				if (n.type == node::kind::row || n.type == node::kind::infix || n.type == node::kind::power || n.type == node::kind::subscript) return !n.parts.empty() && starts_with_digit(n.parts[0]);
				return false;
			}
			static bool long_name(const node& n) { return n.type == node::kind::name && n.text.size() > 1; }
			static bool ends_with_digit(const node& n) {
				if (n.type == node::kind::number) return true;
				if (n.type == node::kind::row || n.type == node::kind::infix) return !n.parts.empty() && ends_with_digit(n.parts.back());
				return false;
			}

			laid_out row(const std::vector<node>& parts, style s) {
				std::vector<laid_out> out;
				for (std::size_t i = 0; i < parts.size(); ++i) {
					// A function or a big operator after something has a thin space before it.
					if (i > 0 && (parts[i].type == node::kind::call || parts[i].type == node::kind::big) && s != style::script && s != style::scripts) out.push_back(space(3.0 / 18, s));
					out.push_back(set(parts[i], s));
				}
				return beside(std::move(out));
			}

			laid_out infix(const node& n, style s) {
				static const std::vector<std::pair<std::string, std::uint32_t>> drawn_as{
				    {"=", '='}, {"!=", 0x2260}, {"<", '<'}, {">", '>'}, {"<=", 0x2264}, {">=", 0x2265}, {"~=", 0x2248}, {"->", 0x2192}, {"=>", 0x21D2}, {"<=>", 0x21D4},
				    {"in", 0x2208}, {"+", '+'}, {"-", 0x2212}, {"+-", 0x00B1}, {"-+", 0x2213}, {"*", 0x00D7}};
				bool relation = n.text != "+" && n.text != "-" && n.text != "+-" && n.text != "-+" && n.text != "*";
				bool small = s == style::script || s == style::scripts;
				// a * b side by side; 2 × 3 between numbers; and a · beside a number or a word,
				// so rate · 12 doesn't read as one name.
				if (n.text == "*") {
					bool numbers = ends_with_digit(n.parts[0]) && starts_with_digit(n.parts[1]);
					bool worded = ends_with_digit(n.parts[0]) || starts_with_digit(n.parts[1]) || long_name(n.parts[0]) || long_name(n.parts[1]);
					if (!numbers && !worded) return beside({set(n.parts[0], s), space(3.0 / 18, s), set(n.parts[1], s)});
					double gap = small ? 0 : 4.0 / 18;
					return beside({set(n.parts[0], s), space(gap, s), glyph(numbers ? 0x00D7 : 0x22C5, s), space(gap, s), set(n.parts[1], s)});
				}
				std::uint32_t code = '?';
				for (const auto& [t, c] : drawn_as) {
					if (t == n.text) code = c;
				}
				double gap = small ? 0 : (relation ? 5.0 / 18 : 4.0 / 18);
				return beside({set(n.parts[0], s), space(gap, s), glyph(code, s), space(gap, s), set(n.parts[1], s)});
			}

			laid_out fraction(const node& n, style s) {
				style inner = smaller(s);
				laid_out top = set(ungrouped(n.parts[0]), inner), bottom = set(ungrouped(n.parts[1]), inner);
				double k = scale(s), rule = measures::fraction_rule_thickness * k;
				bool shown = s == style::display;
				double up = (shown ? measures::fraction_numerator_display_style_shift_up : measures::fraction_numerator_shift_up) * k;
				double down = (shown ? measures::fraction_denominator_display_style_shift_down : measures::fraction_denominator_shift_down) * k;
				double gap_up = (shown ? measures::fraction_num_display_style_gap_min : measures::fraction_numerator_gap_min) * k;
				double gap_down = (shown ? measures::fraction_denom_display_style_gap_min : measures::fraction_denominator_gap_min) * k;
				double a = axis(s);
				up = std::max(up, a + rule / 2 + gap_up + top.descent);
				down = std::max(down, rule / 2 + gap_down + bottom.ascent - a);
				double pad = 0.1 * unit(s), width = std::max(top.width, bottom.width) + 2 * pad;
				laid_out out;
				out.width = width;
				move(top, (width - top.width) / 2, -up);
				move(bottom, (width - bottom.width) / 2, down);
				out.ascent = up + top.ascent;
				out.descent = down + bottom.descent;
				out.drawn = std::move(top.drawn);
				out.drawn.insert(out.drawn.end(), bottom.drawn.begin(), bottom.drawn.end());
				out.drawn.push_back(placed{placed::kind::rule, 0, 0, -a - rule / 2, 0, width, rule, {}});
				// A little room either side, so it doesn't touch what's beside it.
				laid_out spaced = beside({space(0.1, s), out, space(0.1, s)});
				return spaced;
			}

			// A base with a subscript, a superscript, or both stacked.
			laid_out scripts(const node& base_node, const node* below, const node* above, style s) {
				// x_1^2: the subscript and the superscript stacked on x.
				if (above && base_node.type == node::kind::subscript) return scripts(base_node.parts[0], &base_node.parts[1], above, s);
				laid_out base = set(base_node, s);
				double k = scale(s);
				style small = lowered(s);
				laid_out out = base;
				double right = 0;
				if (above) {
					laid_out sup = set(ungrouped(*above), small);
					double up = std::max<double>(measures::superscript_shift_up * k, base.ascent - measures::superscript_baseline_drop_max * k);
					up = std::max(up, measures::superscript_bottom_min * k + sup.descent);
					move(sup, base.width, -up);
					out.ascent = std::max(out.ascent, up + sup.ascent);
					right = std::max(right, sup.width);
					out.drawn.insert(out.drawn.end(), sup.drawn.begin(), sup.drawn.end());
				}
				if (below) {
					laid_out sub = set(ungrouped(*below), small);
					double down = std::max<double>(measures::subscript_shift_down * k, base.descent + measures::subscript_baseline_drop_min * k);
					down = std::max(down, sub.ascent - measures::subscript_top_max * k);
					move(sub, base.width, down);
					out.descent = std::max(out.descent, down + sub.descent);
					right = std::max(right, sub.width);
					out.drawn.insert(out.drawn.end(), sub.drawn.begin(), sub.drawn.end());
				}
				out.width = base.width + right + measures::space_after_script * k;
				return out;
			}

			laid_out call(const node& n, style s) {
				std::vector<laid_out> inside;
				for (std::size_t i = 0; i < n.parts.size(); ++i) {
					if (i > 0) inside.push_back(beside({glyph(',', s), space(3.0 / 18, s)}));
					inside.push_back(set(n.parts[i], s));
				}
				laid_out args = beside(std::move(inside));
				laid_out named = name(n.text, s);
				// One thing after it: in the parentheses it was written in, sin(x), or after
				// a thin space, sin x.
				if (n.parts.size() == 1) return n.parts[0].type == node::kind::group ? beside({named, args}) : beside({named, space(3.0 / 18, s), args});
				return beside({named, around(args, '(', ')', s)});
			}

			// Something between two delimiters as tall as it, drawn as glyphs while it's no
			// taller than a line, and as lines past that.
			laid_out around(laid_out inner, std::uint32_t open, std::uint32_t close, style s) {
				double em = unit(s), a = axis(s);
				double half = std::max(inner.ascent - a, inner.descent + a);
				if (half <= 0.62 * em) return beside({glyph(open, s), inner, glyph(close, s)});
				double top = -a - half - 0.05 * em, bottom = -a + half + 0.05 * em, width = 0.32 * em, line = 0.06 * em;
				laid_out left = delimiter(open, top, bottom, width, line), right = delimiter(close, top, bottom, width, line);
				return beside({left, inner, right});
			}

			// A tall delimiter drawn as a line from its top to its bottom.
			static laid_out delimiter(std::uint32_t code, double top, double bottom, double width, double line) {
				laid_out out;
				out.width = width;
				out.ascent = -top;
				out.descent = bottom;
				placed p{placed::kind::line, 0, 0, 0, line, 0, 0, {}};
				double inset = width * 0.25, far = width * 0.75, middle = (top + bottom) / 2;
				if (code == '(' || code == ')') {
					// A curve, as a parenthesis bows away from what it holds.
					bool left = code == '(';
					for (int i = 0; i <= 16; ++i) {
						double t = i / 16.0, y = top + (bottom - top) * t, bow = std::sin(t * 3.14159265358979) * (far - inset);
						p.points.push_back({left ? far - bow : inset + bow, y});
					}
				} else if (code == '[' || code == ']') {
					bool left = code == '[';
					double edge = left ? inset : far, tip = left ? far : inset;
					p.points = {{tip, top}, {edge, top}, {edge, bottom}, {tip, bottom}};
				} else if (code == '{' || code == '}') {
					bool left = code == '{';
					double edge = left ? far : inset, mid = left ? inset : far;
					double h = bottom - top;
					p.points = {{edge, top}, {(edge + mid) / 2, top + h * 0.05}, {(edge + mid) / 2, middle - h * 0.05}, {mid, middle},
					            {(edge + mid) / 2, middle + h * 0.05}, {(edge + mid) / 2, bottom - h * 0.05}, {edge, bottom}};
				} else if (code == 0x230A || code == 0x230B) {
					bool left = code == 0x230A;
					double edge = left ? inset : far, tip = left ? far : inset;
					p.points = {{edge, top}, {edge, bottom}, {tip, bottom}};
				} else if (code == 0x2308 || code == 0x2309) {
					bool left = code == 0x2308;
					double edge = left ? inset : far, tip = left ? far : inset;
					p.points = {{tip, top}, {edge, top}, {edge, bottom}};
				} else {
					p.points = {{width / 2, top}, {width / 2, bottom}};
				}
				out.drawn.push_back(std::move(p));
				return out;
			}

			laid_out root(const node& n, style s) {
				laid_out inner = set(ungrouped(n.parts.back()), s);
				double k = scale(s), em = unit(s);
				double rule = measures::radical_rule_thickness * k;
				double gap = (s == style::display ? measures::radical_display_style_vertical_gap : measures::radical_vertical_gap) * k;
				double top = -(inner.ascent + gap + rule), bottom = inner.descent + 0.05 * em;
				double sign_width = 0.5 * em;
				// The radical's tick, its stroke down and its long stroke up, then the bar over
				// what's under it.
				placed sign{placed::kind::line, 0, 0, 0, rule, 0, 0, {}};
				sign.points = {{0, (top + bottom) * 0.55 + 0.1 * em}, {sign_width * 0.25, (top + bottom) * 0.5}, {sign_width * 0.55, bottom}, {sign_width, top + rule / 2},
				               {sign_width + inner.width + 0.1 * em, top + rule / 2}};
				laid_out out;
				move(inner, sign_width + 0.05 * em, 0);
				out.width = sign_width + inner.width + 0.15 * em;
				out.ascent = -top + measures::radical_extra_ascender * k;
				out.descent = bottom;
				out.drawn = std::move(inner.drawn);
				out.drawn.push_back(std::move(sign));
				if (n.parts.size() == 2) {
					// root(3, x): its index small, over the tick.
					laid_out index = set(n.parts[0], style::scripts);
					double raise = (top + bottom) * 0.55 - index.descent - 0.05 * em;
					move(index, 0, raise);
					move(out, std::max(0.0, index.width - sign_width * 0.3), 0);
					out.width += std::max(0.0, index.width - sign_width * 0.3);
					out.ascent = std::max(out.ascent, -raise + index.ascent);
					out.drawn.insert(out.drawn.end(), index.drawn.begin(), index.drawn.end());
				}
				return out;
			}

			laid_out accent(const node& n, style s) {
				laid_out inner = set(n.parts[0], s);
				double em = unit(s), k = scale(s);
				double over = -(std::max<double>(inner.ascent, measures::accent_base_height * k) + 0.08 * em);
				laid_out out = inner;
				if (n.text == "bar") {
					double thick = measures::overbar_rule_thickness * k;
					out.drawn.push_back(placed{placed::kind::rule, 0, 0, over - thick, 0, inner.width, thick, {}});
					out.ascent = -over + thick;
					return out;
				}
				if (n.text == "vec") {
					placed arrow{placed::kind::line, 0, 0, 0, 0.05 * em, 0, 0, {}};
					double y = over - 0.08 * em, head = 0.12 * em;
					arrow.points = {{0, y}, {inner.width, y}, {inner.width - head, y - head}, {inner.width, y}, {inner.width - head, y + head}};
					out.drawn.push_back(std::move(arrow));
					out.ascent = -y + head;
					return out;
				}
				// A hat, or a dot or two, drawn as lines, the font having no marks of its own.
				double middle = inner.width / 2, line = 0.06 * em;
				if (n.text == "hat") {
					double y = over - 0.04 * em, half = 0.18 * em, rise = 0.14 * em;
					placed hat{placed::kind::line, 0, 0, 0, line, 0, 0, {{middle - half, y}, {middle, y - rise}, {middle + half, y}}};
					out.drawn.push_back(std::move(hat));
					out.ascent = std::max(out.ascent, -(y - rise) + line);
					return out;
				}
				double y = over - 0.06 * em, dot = 0.1 * em;
				std::vector<double> at = n.text == "dot" ? std::vector<double>{middle} : std::vector<double>{middle - 0.12 * em, middle + 0.12 * em};
				for (double x : at) out.drawn.push_back(placed{placed::kind::line, 0, 0, 0, dot, 0, 0, {{x, y}, {x, y}}});
				out.ascent = std::max(out.ascent, -y + dot);
				(void)k;
				return out;
			}

			laid_out big(const node& n, style s) {
				double em = unit(s), k = scale(s);
				bool shown = s == style::display;
				laid_out sign;
				if (n.text == "lim") sign = word("lim", s, false);
				else {
					std::uint32_t code = n.text == "sum" ? 0x2211 : n.text == "prod" ? 0x220F : 0x222B;
					// Drawn larger when shown on its own, centered on the axis.
					double grow = shown ? 1.6 : 1.15;
					sign = glyph(code, s);
					for (auto& d : sign.drawn) d.size *= grow;
					sign.width *= grow;
					const measures::glyph* g = measured(code);
					double gk = k * grow;
					double middle = g ? (g->y_max + g->y_min) / 2.0 * gk : 0;
					double shift = middle - axis(s);
					move(sign, 0, shift);
					sign.ascent = g ? g->y_max * gk - shift : sign.ascent;
					sign.descent = g ? -g->y_min * gk + shift : sign.descent;
				}
				const node& below = n.parts[0];
				const node& above = n.parts[1];
				bool has_below = !(below.type == node::kind::row && below.parts.empty());
				bool has_above = !(above.type == node::kind::row && above.parts.empty());
				laid_out body = set(n.parts[2], s);
				laid_out op = sign;
				if (n.text == "int") {
					// An integral's bounds beside it, at its top and bottom.
					double right = 0;
					if (has_above) {
						laid_out a = set(above, lowered(s));
						move(a, sign.width * 0.85, -sign.ascent + a.ascent * 0.6);
						right = std::max(right, a.width);
						op.drawn.insert(op.drawn.end(), a.drawn.begin(), a.drawn.end());
					}
					if (has_below) {
						laid_out b = set(below, lowered(s));
						move(b, sign.width * 0.45, sign.descent);
						right = std::max(right, b.width - sign.width * 0.4);
						op.descent = std::max(op.descent, sign.descent + b.descent);
						op.drawn.insert(op.drawn.end(), b.drawn.begin(), b.drawn.end());
					}
					op.width = sign.width + right + 0.1 * em;
				} else {
					// Limits centered under and over it.
					laid_out b = has_below ? set(below, lowered(s)) : laid_out{};
					laid_out a = has_above ? set(above, lowered(s)) : laid_out{};
					double width = std::max({sign.width, a.width, b.width});
					op = laid_out{};
					op.width = width;
					move(sign, (width - sign.width) / 2, 0);
					op.ascent = sign.ascent, op.descent = sign.descent;
					op.drawn = sign.drawn;
					if (has_above) {
						double rise = sign.ascent + measures::upper_limit_gap_min * k + a.descent;
						move(a, (width - a.width) / 2, -rise);
						op.ascent = rise + a.ascent;
						op.drawn.insert(op.drawn.end(), a.drawn.begin(), a.drawn.end());
					}
					if (has_below) {
						double drop = sign.descent + measures::lower_limit_gap_min * k + b.ascent;
						move(b, (width - b.width) / 2, drop);
						op.descent = drop + b.descent;
						op.drawn.insert(op.drawn.end(), b.drawn.begin(), b.drawn.end());
					}
				}
				return beside({op, space(3.0 / 18, s), body});
			}

			// A reaction's arrow, drawn as lines as long as what's set small over or under it
			// needs; ⇌ as two half arrows, one each way.
			laid_out arrow(const node& n, style s) {
				style small = lowered(s);
				double em = unit(s), a = axis(s);
				double line = measures::fraction_rule_thickness * scale(s);
				auto none = [](const node& p) { return p.type == node::kind::row && p.parts.empty(); };
				std::optional<laid_out> over, under;
				if (!none(n.parts[2])) over = set(n.parts[2], small);
				if (!none(n.parts[3])) under = set(n.parts[3], small);
				double pad = 0.3 * em, head = 0.28 * em, apart = n.text == "<=>" ? 0.12 * em : 0;
				double width = std::max({1.6 * em, (over ? over->width : 0) + 2 * pad, (under ? under->width : 0) + 2 * pad});
				using points = std::vector<std::pair<double, double>>;
				std::vector<points> lines;
				double top = -a - apart, bottom = -a + apart;
				lines.push_back({{0, top}, {width, top}});
				if (n.text == "<=>") {
					lines.push_back({{0, bottom}, {width, bottom}});
					lines.push_back({{width - head, top - head / 2}, {width, top}});
					lines.push_back({{head, bottom + head / 2}, {0, bottom}});
				} else {
					if (n.text != "<-") lines.push_back({{width - head, top - head / 2}, {width, top}, {width - head, top + head / 2}});
					if (n.text != "->") lines.push_back({{head, top - head / 2}, {0, top}, {head, top + head / 2}});
				}
				laid_out drawn{width, a + apart + head / 2, std::max(0.0, -a + apart + head / 2), {}};
				for (auto& p : lines) drawn.drawn.push_back(placed{placed::kind::line, 0, 0, 0, line, 0, 0, std::move(p)});
				double room = 0.1 * em;
				if (over) {
					double rise = drawn.ascent + room + over->descent;
					move(*over, (width - over->width) / 2, -rise);
					drawn.ascent = rise + over->ascent;
					drawn.drawn.insert(drawn.drawn.end(), over->drawn.begin(), over->drawn.end());
				}
				if (under) {
					double drop = drawn.descent + room + under->ascent;
					move(*under, (width - under->width) / 2, drop);
					drawn.descent = drop + under->descent;
					drawn.drawn.insert(drawn.drawn.end(), under->drawn.begin(), under->drawn.end());
				}
				double gap = s == style::script || s == style::scripts ? 0 : 5.0 / 18;
				return beside({set(n.parts[0], s), space(gap, s), std::move(drawn), space(gap, s), set(n.parts[1], s)});
			}

			laid_out matrix(const node& n, style s) {
				style inner = s == style::display ? style::text : s;
				std::vector<std::vector<laid_out>> cells;
				std::size_t columns = 0;
				for (const auto& r : n.parts) {
					cells.emplace_back();
					for (const auto& c : r.parts) cells.back().push_back(set(c, inner));
					columns = std::max(columns, r.parts.size());
				}
				return grid(std::move(cells), columns, s, '[', ']', false);
			}

			laid_out cases(const node& n, style s) {
				style inner = s == style::display ? style::text : s;
				std::vector<std::vector<laid_out>> cells;
				for (const auto& r : n.parts) {
					cells.emplace_back();
					cells.back().push_back(set(r.parts[0], inner));
					const node& condition = r.parts[1];
					bool none = condition.type == node::kind::row && condition.parts.empty();
					if (none) cells.back().push_back(laid_out{});
					else if (condition.type == node::kind::words) cells.back().push_back(set(condition, inner));
					else cells.back().push_back(beside({word("if", inner, false), space(0.3, inner), set(condition, inner)}));
				}
				return grid(std::move(cells), 2, s, '{', 0, true);
			}

			// Cells in rows and columns, centered on the axis, between delimiters; a cases'
			// columns start at their left, a matrix's are centered.
			laid_out grid(std::vector<std::vector<laid_out>> cells, std::size_t columns, style s, std::uint32_t open, std::uint32_t close, bool left_aligned) {
				double em = unit(s);
				std::vector<double> widths(columns, 0), ascents(cells.size(), 0), descents(cells.size(), 0);
				for (std::size_t r = 0; r < cells.size(); ++r) {
					for (std::size_t c = 0; c < cells[r].size(); ++c) {
						widths[c] = std::max(widths[c], cells[r][c].width);
						ascents[r] = std::max(ascents[r], std::max(cells[r][c].ascent, 0.7 * em));
						descents[r] = std::max(descents[r], std::max(cells[r][c].descent, 0.25 * em));
					}
				}
				double gap_x = left_aligned ? 1.0 * em : 0.8 * em, gap_y = 0.3 * em;
				double height = 0;
				for (std::size_t r = 0; r < cells.size(); ++r) height += ascents[r] + descents[r] + (r ? gap_y : 0);
				double top = -axis(s) - height / 2;
				laid_out body;
				double y = top;
				for (std::size_t r = 0; r < cells.size(); ++r) {
					if (r) y += gap_y;
					y += ascents[r];
					double x = 0;
					for (std::size_t c = 0; c < columns; ++c) {
						if (c < cells[r].size()) {
							laid_out& cell = cells[r][c];
							move(cell, x + (left_aligned ? 0 : (widths[c] - cell.width) / 2), y);
							body.drawn.insert(body.drawn.end(), cell.drawn.begin(), cell.drawn.end());
						}
						x += widths[c] + (c + 1 < columns ? gap_x : 0);
					}
					body.width = std::max(body.width, x);
					y += descents[r];
				}
				body.ascent = -top;
				body.descent = top + height;
				double line = 0.06 * em, width = 0.36 * em;
				std::vector<laid_out> parts;
				parts.push_back(delimiter(open, top, top + height, width, line));
				parts.push_back(space(0.15, s));
				parts.push_back(std::move(body));
				if (close) {
					parts.push_back(space(0.15, s));
					parts.push_back(delimiter(close, top, top + height, width, line));
				}
				return beside(std::move(parts));
			}
		};

	}  // namespace detail

	// An equation set `em` pixels to the em, as it's shown on its own lines, or as it's set
	// in a line of text.
	inline laid_out lay_out(const node& n, double em, bool displayed) {
		return detail::setter(em).set(n, displayed ? detail::style::display : detail::style::text);
	}

	// What's drawn written out a line each, to two decimals, as the layout cases are.
	inline std::string written(const laid_out& l) {
		std::string out = std::format("{:.2f} {:.2f} {:.2f}\n", l.width, l.ascent, l.descent);
		for (const auto& d : l.drawn) {
			if (d.type == placed::kind::glyph) out += std::format("glyph {:x} {:.2f} {:.2f} {:.2f}\n", d.code, d.x, d.y, d.size);
			else if (d.type == placed::kind::rule) out += std::format("rule {:.2f} {:.2f} {:.2f} {:.2f}\n", d.x, d.y, d.width, d.height);
			else {
				out += std::format("line {:.2f}", d.size);
				for (const auto& [x, y] : d.points) out += std::format(" {:.2f},{:.2f}", x, y);
				out += "\n";
			}
		}
		return out;
	}

}  // namespace one::math
