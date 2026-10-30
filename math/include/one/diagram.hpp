// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::diagram: a block diagram, signals flowing through blocks, written a line at a
// time and laid out by itself, its blocks' faces set by one::math.
//
//     in p_mechanical, p_electrical
//     p_mechanical - p_electrical - damping -> *k_inertia -> 1/s -> w
//     w -> num(s)/den(s) "HPF" -> *k_damp -> damping
//     w + nominalFrequency -> f
//     oscillator_3ph(f -> osc)
//     out osc, f as f_rotor
//
// `in` and `out` are the numbered ports. A chain runs left to right: it starts with a
// signal, or a sum of them with their signs, and each block after is what it does, `*k`
// a gain and anything else, like 1/s, a box with that written in it, a name in quotes
// under it. The name at its end is the signal it makes, and a name is how chains join, so
// a signal used before it's made is fed back. A name nothing makes is a constant, and
// `name(a, b -> c)` is a block of its own with its ports named. A ; ends a line as a line
// break does.
//
// Signals run left to right, a column at a time, each block level with what feeds it; a
// chain that feeds back is drawn under what it feeds, flowing right to left; the wires
// turn at right angles and branch at a dot. Everything is placed in ems, the same in a
// window and on a web page: @uione/react's diagram.ts is the same, held to the same files.

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "one/math.hpp"
#include "one/math_layout.hpp"

namespace one::diagram {

	using math::problem;

	// A block drawn: a port's rounded box, a sum's circle, a gain's triangle, a box, a
	// constant's box or a block of its own; flipped when it's on a chain fed back, facing
	// left.
	struct shape {
		enum class kind { port, sum, gain, box, constant, system } type = kind::box;
		double x = 0, y = 0, width = 0, height = 0;
		bool flipped = false;
	};

	// A wire, from where a signal leaves to where it goes in, with an arrow there; or the
	// stub to where it branches, without one.
	struct wire {
		std::vector<std::pair<double, double>> points;
		bool arrow = true;
	};

	// Something written, set by one::math at a size: its left and its baseline.
	struct face {
		std::string written;
		double size = 1, x = 0, y = 0;
	};

	struct drawing {
		double width = 0, height = 0;
		std::vector<shape> shapes;
		std::vector<wire> wires;
		std::vector<std::pair<double, double>> dots;  // where a wire branches
		std::vector<face> faces;
	};

	namespace detail {

		constexpr double column_gap = 1.8, row_gap = 1.0, label = 0.7, lead = 0.6;

		struct failed {
			problem p;
		};
		[[noreturn]] inline void fail(std::size_t at, std::string says) { throw failed{{at, std::move(says)}}; }

		struct measured {
			double width = 0, ascent = 0, descent = 0;
		};
		inline measured measure(const std::string& written, double size) {
			auto read = math::parse(written);
			if (!read) return {};
			auto laid = math::lay_out(*read, size, false);
			return {laid.width, laid.ascent, laid.descent};
		}
		inline std::string words(std::string_view s) { return "\"" + std::string(s) + "\""; }

		inline bool name_character(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
		inline bool is_name(std::string_view s) {
			return !s.empty() && !std::isdigit(static_cast<unsigned char>(s[0])) && std::ranges::all_of(s, name_character);
		}

		// Some text and where it starts in what was written.
		struct piece {
			std::string_view text;
			std::size_t at = 0;
		};
		inline piece trimmed(piece p) {
			while (!p.text.empty() && std::isspace(static_cast<unsigned char>(p.text.front()))) p.text.remove_prefix(1), ++p.at;
			while (!p.text.empty() && std::isspace(static_cast<unsigned char>(p.text.back()))) p.text.remove_suffix(1);
			return p;
		}
		// Split where `apart` is, but not within brackets or quotes.
		inline std::vector<piece> split(piece whole, std::string_view apart) {
			std::vector<piece> out;
			int depth = 0;
			bool quoted = false;
			std::size_t from = 0;
			for (std::size_t i = 0; i < whole.text.size(); ++i) {
				char c = whole.text[i];
				if (c == '"') quoted = !quoted;
				if (quoted) continue;
				if (c == '(' || c == '[' || c == '{') ++depth;
				if (c == ')' || c == ']' || c == '}') --depth;
				if (depth == 0 && whole.text.substr(i, apart.size()) == apart) {
					out.push_back(trimmed({whole.text.substr(from, i - from), whole.at + from}));
					from = i + apart.size();
					i = from - 1;
				}
			}
			out.push_back(trimmed({whole.text.substr(from), whole.at + from}));
			return out;
		}

		struct node {
			shape::kind type = shape::kind::box;
			std::string face, label;
			std::vector<std::string> ins, outs;  // a block of its own's ports
			std::vector<int> signs;               // a sum's, one an input
			int number = 0;                       // a port's
			bool output = false;                  // a port out
			std::size_t inputs = 0, outputs = 0;
			double x = 0, y = 0, w = 0, h = 0, below = 0, across = 0;  // across: as wide as it and what's written under it
			int layer = -1;
			bool feedback = false, above = false, flipped = false;
			std::vector<int> sides;  // a sum's inputs: 0 left, 1 above, 2 below
		};
		struct edge {
			std::size_t from = 0, from_port = 0, to = 0, to_port = 0;
			bool back = false;
		};
		struct use {
			std::string name;
			std::size_t at = 0, to = 0, port = 0;
		};

		class builder {
		public:
			std::vector<node> nodes;
			std::vector<edge> edges;

			void statement(piece line) {
				line = trimmed(line);
				if (line.text.empty() || line.text.starts_with("//")) return;
				if (line.text.starts_with("in ")) return ports(trimmed({line.text.substr(3), line.at + 3}), false);
				if (line.text.starts_with("out ")) return ports(trimmed({line.text.substr(4), line.at + 4}), true);
				auto chain = split(line, "->");
				if (chain.size() == 1) return system(line);
				auto& first = chain.front();
				std::pair<std::size_t, std::size_t> from{};
				std::string waiting;  // a signal the chain starts with, until it's known what makes it
				if (is_name(first.text)) waiting = std::string(first.text);
				else from = {sum(first), 0};
				for (std::size_t i = 1; i + 1 < chain.size(); ++i) {
					std::size_t made = block(chain[i]);
					connect(waiting, from, made, 0, chain[i - 1].at);
					waiting.clear();
					from = {made, 0};
				}
				auto& last = chain.back();
				if (!is_name(last.text)) fail(last.at, "what a chain makes is named at its end, like -> y");
				if (!waiting.empty()) fail(last.at, "nothing between " + waiting + " and " + std::string(last.text) + "; put a block there, like " + waiting + " -> *2 -> " + std::string(last.text));
				make(std::string(last.text), from, last.at);
			}

			// What's used, joined to what makes it; a name nothing makes, a constant.
			void resolve() {
				std::map<std::string, std::size_t, std::less<>> constants;
				for (const auto& u : uses) {
					auto made = makes.find(u.name);
					if (made != makes.end()) {
						edges.push_back({made->second.first, made->second.second, u.to, u.port});
						continue;
					}
					if (nodes[u.to].type == shape::kind::port) fail(u.at, "nothing makes " + u.name + " for out to show");
					auto [it, fresh] = constants.try_emplace(u.name, nodes.size());
					if (fresh) {
						node c;
						c.type = shape::kind::constant;
						c.face = u.name;
						c.outputs = 1;
						nodes.push_back(std::move(c));
					}
					edges.push_back({it->second, 0, u.to, u.port});
				}
				if (nodes.empty()) fail(0, "something to draw, like in x and x -> *2 -> y");
			}

		private:
			std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> makes;
			std::vector<use> uses;
			int ins_ = 0, outs_ = 0;

			void make(const std::string& name, std::pair<std::size_t, std::size_t> by, std::size_t at) {
				if (!makes.try_emplace(name, by).second) fail(at, "two things make " + name);
			}
			void connect(const std::string& waiting, std::pair<std::size_t, std::size_t> from, std::size_t to, std::size_t port, std::size_t at) {
				if (!waiting.empty()) uses.push_back({waiting, at, to, port});
				else edges.push_back({from.first, from.second, to, port});
			}

			void ports(piece list, bool out) {
				for (auto item : split(list, ",")) {
					auto named = split(item, " as ");
					std::string_view signal = named[0].text, shown = named.size() > 1 ? named[1].text : named[0].text;
					if (!is_name(signal) || !is_name(shown) || named.size() > 2) fail(item.at, out ? "an output is a name, like out y, or out y as speed" : "an input is a name, like in x, y");
					node p;
					p.type = shape::kind::port;
					p.output = out;
					p.number = out ? ++outs_ : ++ins_;
					p.label = std::string(shown);
					p.inputs = out ? 1 : 0;
					p.outputs = out ? 0 : 1;
					nodes.push_back(std::move(p));
					if (out) uses.push_back({std::string(signal), item.at, nodes.size() - 1, 0});
					else make(std::string(signal), {nodes.size() - 1, 0}, item.at);
				}
			}

			// a - b + c: a sum, its inputs' signs.
			std::size_t sum(piece written) {
				node s;
				s.type = shape::kind::sum;
				std::size_t i = 0;
				std::string_view t = written.text;
				std::vector<std::pair<std::string, std::size_t>> terms;
				auto space = [&] {
					while (i < t.size() && t[i] == ' ') ++i;
				};
				space();
				while (i < t.size()) {
					int sign = 1;
					if (t[i] == '+' || t[i] == '-') sign = t[i] == '-' ? -1 : 1, ++i;
					else if (!terms.empty()) fail(written.at + i, "a + or - between what's summed, like a - b");
					space();
					std::size_t from = i;
					while (i < t.size() && name_character(t[i])) ++i;
					if (!is_name(t.substr(from, i - from))) fail(written.at + from, "a chain starts with a signal, or a sum of them, like a - b");
					terms.emplace_back(std::string(t.substr(from, i - from)), written.at + from);
					s.signs.push_back(sign);
					space();
				}
				if (terms.empty()) fail(written.at, "a chain starts with a signal, or a sum of them, like a - b");
				s.inputs = terms.size();
				s.outputs = 1;
				nodes.push_back(std::move(s));
				for (std::size_t k = 0; k < terms.size(); ++k) uses.push_back({terms[k].first, terms[k].second, nodes.size() - 1, k});
				return nodes.size() - 1;
			}

			// *k, a gain; anything else a box with it written in it; a name in quotes after
			// either, under it.
			std::size_t block(piece written) {
				node b;
				std::string_view t = written.text;
				if (t.ends_with('"')) {
					std::size_t open = t.substr(0, t.size() - 1).rfind('"');
					if (open == std::string_view::npos) fail(written.at, "a \" to close the name under the block");
					b.label = std::string(t.substr(open + 1, t.size() - open - 2));
					t = trimmed({t.substr(0, open), 0}).text;
				}
				std::size_t at = written.at;
				b.type = shape::kind::box;
				if (t.starts_with('*')) {
					b.type = shape::kind::gain;
					t.remove_prefix(1);
					++at;
					while (!t.empty() && t.front() == ' ') t.remove_prefix(1), ++at;
				}
				if (t.empty()) fail(written.at, "what the block does, like *k or 1/s");
				if (auto read = math::parse(t); !read) fail(at + read.error().at, read.error().says);
				b.face = std::string(t);
				b.inputs = b.outputs = 1;
				nodes.push_back(std::move(b));
				return nodes.size() - 1;
			}

			// f(a, b -> c): a block of its own, its ports named.
			void system(piece line) {
				std::string_view t = line.text;
				std::size_t open = t.find('(');
				if (open == std::string_view::npos || !t.ends_with(')') || !is_name(trimmed({t.substr(0, open), 0}).text)) {
					fail(line.at, "a chain, like a -> *k -> b; ports, like in a or out b; or a block of its own, like f(a -> b)");
				}
				piece inside{t.substr(open + 1, t.size() - open - 2), line.at + open + 1};
				auto sides = split(inside, "->");
				if (sides.size() != 2) fail(inside.at, "a block of its own's inputs, then -> and its outputs, like f(a, b -> c)");
				node s;
				s.type = shape::kind::system;
				s.label = std::string(trimmed({t.substr(0, open), 0}).text);
				std::vector<piece> ins, outs;
				if (!sides[0].text.empty()) ins = split(sides[0], ",");
				if (!sides[1].text.empty()) outs = split(sides[1], ",");
				for (auto& p : ins) {
					if (!is_name(p.text)) fail(p.at, "a port is a name, like f(a -> b)");
					s.ins.emplace_back(p.text);
				}
				for (auto& p : outs) {
					if (!is_name(p.text)) fail(p.at, "a port is a name, like f(a -> b)");
					s.outs.emplace_back(p.text);
				}
				s.inputs = s.ins.size();
				s.outputs = s.outs.size();
				nodes.push_back(std::move(s));
				std::size_t me = nodes.size() - 1;
				for (std::size_t k = 0; k < ins.size(); ++k) uses.push_back({std::string(ins[k].text), ins[k].at, me, k});
				for (std::size_t k = 0; k < outs.size(); ++k) make(std::string(outs[k].text), {me, k}, outs[k].at);
			}
		};

		// Where a port is, from a block's top left, and which way a wire leaves or comes in:
		// right, left, down or up.
		struct port_place {
			double x = 0, y = 0, dx = 0, dy = 0;
		};

		class layout {
		public:
			explicit layout(builder& b) : n_(b.nodes), e_(b.edges) {}

			drawing run() {
				sizes();
				backs();
				feedbacks();
				layers();
				columns();
				rows();
				aboves();
				lanes();
				sides();
				drawing d;
				for (const auto& n : n_) d.shapes.push_back({n.type, n.x, n.y, n.w, n.h, n.flipped});
				wires(d);
				faces(d);
				fit(d);
				return d;
			}

		private:
			std::vector<node>& n_;
			std::vector<edge>& e_;
			std::vector<std::pair<std::size_t, std::vector<std::size_t>>> chains_;  // what each chain fed back feeds, and its blocks, from where it branches
			double detour_ = 0;

			std::vector<std::size_t> into(std::size_t v) const {
				std::vector<std::size_t> out;
				for (std::size_t i = 0; i < e_.size(); ++i) {
					if (e_[i].to == v) out.push_back(i);
				}
				return out;
			}
			std::vector<std::size_t> from(std::size_t v, std::size_t port) const {
				std::vector<std::size_t> out;
				for (std::size_t i = 0; i < e_.size(); ++i) {
					if (e_[i].from == v && e_[i].from_port == port) out.push_back(i);
				}
				return out;
			}

			void sizes() {
				for (auto& n : n_) {
					double under = 0;
					if (!n.label.empty()) {
						auto m = measure(words(n.label), label);
						n.below = 0.25 + m.ascent + m.descent;
						under = m.width;
					}
					switch (n.type) {
						case shape::kind::port: n.w = 1.4, n.h = 0.9; break;
						case shape::kind::sum: n.w = n.h = 1.0; break;
						case shape::kind::gain: {
							// Wide enough that what's written in it, from near its back, ends where
							// it's still tall enough to hold it.
							auto m = measure(n.face, 1);
							n.w = std::max(2.0, 2 * (m.width + 0.4) + 0.4);
							n.h = std::max(1.6, (m.ascent + m.descent + 0.2) / (1 - (m.width + 0.4) / n.w));
							break;
						}
						case shape::kind::box: {
							auto m = measure(n.face, 1);
							n.h = std::max(1.6, m.ascent + m.descent + 0.6);
							n.w = std::max(1.8, m.width + 1.0);
							break;
						}
						case shape::kind::constant: n.w = measure(words(n.face), 1).width + 0.8, n.h = 1.3; break;
						case shape::kind::system: {
							double in = 0, out = 0;
							for (const auto& p : n.ins) in = std::max(in, measure(words(p), label).width);
							for (const auto& p : n.outs) out = std::max(out, measure(words(p), label).width);
							n.w = std::max(4.0, in + out + 2.0);
							n.h = std::max(1.6, static_cast<double>(std::max(n.ins.size(), n.outs.size())) * 1.1 + 0.5);
							break;
						}
					}
					n.across = std::max(n.w, under);
				}
			}

			// A wire to a block that's still being followed from it is one fed back.
			void backs() {
				std::vector<int> state(n_.size(), 0);
				auto visit = [&](auto& self, std::size_t v) -> void {
					state[v] = 1;
					for (std::size_t i = 0; i < e_.size(); ++i) {
						if (e_[i].from != v) continue;
						if (state[e_[i].to] == 1) e_[i].back = true;
						else if (state[e_[i].to] == 0) self(self, e_[i].to);
					}
					state[v] = 2;
				};
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (n_[v].type == shape::kind::port && !n_[v].output && state[v] == 0) visit(visit, v);
				}
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (state[v] == 0 && into(v).empty()) visit(visit, v);
				}
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (state[v] == 0) visit(visit, v);
				}
			}

			// The blocks on a chain fed back: from the one that feeds back, back to where
			// the signal it's made from branches.
			void feedbacks() {
				for (const auto& e : e_) {
					if (!e.back) continue;
					std::vector<std::size_t> chain;
					std::size_t at = e.from;
					while ((n_[at].type == shape::kind::gain || n_[at].type == shape::kind::box) && !n_[at].feedback && from(at, 0).size() == 1) {
						chain.push_back(at);
						auto fed = into(at);
						if (fed.size() != 1) break;
						const auto& in = e_[fed[0]];
						if (from(in.from, in.from_port).size() > 1) break;
						if (n_[in.from].type != shape::kind::gain && n_[in.from].type != shape::kind::box) break;
						at = in.from;
					}
					if (chain.empty()) continue;
					std::ranges::reverse(chain);
					for (auto c : chain) n_[c].feedback = n_[c].flipped = true;
					chains_.push_back({e.to, chain});
				}
			}

			bool main(std::size_t v) const { return !n_[v].feedback && !n_[v].above; }

			// Each block a column after everything that feeds it; outputs in the last; a
			// constant fed only into sums is over the sum it's fed into.
			void layers() {
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (n_[v].type != shape::kind::constant) continue;
					bool sums = true;
					for (auto i : from(v, 0)) sums = sums && n_[e_[i].to].type == shape::kind::sum && e_[i].to_port > 0;
					n_[v].above = sums && !from(v, 0).empty();
				}
				bool moved = true;
				for (auto& n : n_) n.layer = 0;
				for (std::size_t round = 0; moved && round <= n_.size(); ++round) {
					moved = false;
					for (const auto& e : e_) {
						if (e.back || !main(e.from) || !main(e.to) || (n_[e.to].type == shape::kind::port)) continue;
						if (n_[e.to].layer < n_[e.from].layer + 1) n_[e.to].layer = n_[e.from].layer + 1, moved = true;
					}
				}
				int last = 0;
				for (const auto& n : n_) {
					if (!(n.type == shape::kind::port && n.output)) last = std::max(last, n.layer);
				}
				for (auto& n : n_) {
					if (n.type == shape::kind::port && n.output) n.layer = last + 1;
				}
				// A constant fed into blocks, in the column before the first of them.
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (n_[v].type != shape::kind::constant || n_[v].above) continue;
					int fed = 1 << 20;
					for (auto i : from(v, 0)) fed = std::min(fed, n_[e_[i].to].layer);
					n_[v].layer = std::max(0, fed - 1);
				}
			}

			std::vector<double> lefts_, widths_;
			void columns() {
				int count = 0;
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (main(v)) count = std::max(count, n_[v].layer + 1);
				}
				widths_.assign(static_cast<std::size_t>(count), 0);
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (main(v)) widths_[static_cast<std::size_t>(n_[v].layer)] = std::max(widths_[static_cast<std::size_t>(n_[v].layer)], n_[v].across);
				}
				double x = 0;
				for (auto w : widths_) lefts_.push_back(x), x += w + column_gap;
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (!main(v)) continue;
					auto l = static_cast<std::size_t>(n_[v].layer);
					n_[v].x = lefts_[l] + (widths_[l] - n_[v].w) / 2;
				}
			}

		public:
			// Where a block's port is: its inputs, then its outputs.
			port_place in_port(const node& n, std::size_t i) const {
				double left = n.flipped ? n.w : 0, way = n.flipped ? -1 : 1;
				if (n.type == shape::kind::sum) {
					int side = i < n.sides.size() ? n.sides[i] : 0;
					if (side == 1) return {n.w / 2, 0, 0, 1};
					if (side == 2) return {n.w / 2, n.h, 0, -1};
					return {left, n.h / 2, way, 0};
				}
				double count = static_cast<double>(std::max<std::size_t>(n.inputs, 1));
				return {left, n.h * (static_cast<double>(i) + 1) / (count + 1), way, 0};
			}
			port_place out_port(const node& n, std::size_t i) const {
				if (n.above) return {n.w / 2, n.h, 0, 1};
				double right = n.flipped ? 0 : n.w, way = n.flipped ? -1 : 1;
				double count = static_cast<double>(std::max<std::size_t>(n.outputs, 1));
				return {right, n.h * (static_cast<double>(i) + 1) / (count + 1), way, 0};
			}

		private:
			// Each block level with what feeds it, and under the one before it in its column.
			void rows() {
				int count = static_cast<int>(widths_.size());
				for (int l = 0; l < count; ++l) {
					double bottom = -1e9;
					for (std::size_t v = 0; v < n_.size(); ++v) {
						auto& n = n_[v];
						if (!main(v) || n.layer != l) continue;
						std::optional<double> want;
						auto fed = into(v);
						std::ranges::stable_sort(fed, {}, [&](std::size_t i) { return e_[i].to_port; });
						for (auto i : fed) {
							const auto& e = e_[i];
							if (e.back || !main(e.from) || n_[e.from].layer >= l) continue;
							want = n_[e.from].y + out_port(n_[e.from], e.from_port).y - in_port(n, e.to_port).y;
							break;
						}
						double top = want.value_or(bottom + row_gap);
						if (bottom > -1e9) top = std::max(top, bottom + row_gap);
						n.y = top;
						bottom = n.y + n.h + n.below;
					}
				}
			}

			// A constant over the sum it's fed into.
			void aboves() {
				for (std::size_t v = 0; v < n_.size(); ++v) {
					if (!n_[v].above) continue;
					const auto& s = n_[e_[from(v, 0).front()].to];
					n_[v].x = s.x + s.w / 2 - n_[v].w / 2;
					n_[v].y = s.y - 0.9 - n_[v].h;
				}
			}

			// How far down what's drawn between two places across reaches.
			double reach(double left, double right) const {
				double bottom = -1e9;
				for (std::size_t v = 0; v < n_.size(); ++v) {
					const auto& n = n_[v];
					if (n.feedback && n.layer == -2) continue;
					if (n.x + n.across < left || n.x > right) continue;
					bottom = std::max(bottom, n.y + n.h + n.below);
				}
				return bottom;
			}

			// How far up what's drawn between two places across reaches.
			double rise(double left, double right) const {
				double top = 1e9;
				for (const auto& n : n_) {
					if (n.x + n.across < left || n.x > right) continue;
					top = std::min(top, n.y);
				}
				return top;
			}

			// A chain fed back, under what's between where it branches and what it feeds,
			// right to left.
			void lanes() {
				for (auto& n : n_) {
					if (n.feedback) n.layer = -2;
				}
				for (const auto& [target, chain] : chains_) {
					const auto& t = n_[target];
					auto fed = into(chain.front());
					const auto& source = n_[e_[fed.front()].from];
					auto out = out_port(source, e_[fed.front()].from_port);
					double branch = source.x + out.x + lead;
					double to = t.x + t.w / 2;
					double tall = 0, wide = column_gap * static_cast<double>(chain.size() - 1);
					for (auto c : chain) tall = std::max(tall, n_[c].h), wide += n_[c].across;
					// Room for it between them, everything after what it feeds moved over if not.
					if (double short_by = wide + 2 * column_gap - (branch - to); short_by > 0) {
						for (auto& n : n_) {
							if (n.layer != -2 && n.x > to) n.x += short_by;
						}
						branch += short_by;
					}
					double top = reach(std::min(to, branch) - 0.5, std::max(to, branch) + 0.5) + row_gap * 1.2;
					double right = branch - std::max(column_gap, (branch - to - wide) / 2);
					for (auto c : chain) {
						auto& n = n_[c];
						n.x = right - n.across / 2 - n.w / 2;
						n.y = top + (tall - n.h) / 2;
						n.layer = -1;
						right -= n.across + column_gap;
					}
				}
			}

			// A sum's inputs after its first: from above it or below, as what feeds each is.
			void sides() {
				for (std::size_t v = 0; v < n_.size(); ++v) {
					auto& n = n_[v];
					if (n.type != shape::kind::sum) continue;
					n.sides.assign(n.inputs, 0);
					std::vector<std::pair<double, std::size_t>> others;
					for (auto i : into(v)) {
						const auto& e = e_[i];
						if (e.to_port == 0) continue;
						const auto& s = n_[e.from];
						others.emplace_back(s.y + out_port(s, e.from_port).y, e.to_port);
					}
					std::ranges::sort(others);
					if (others.size() == 1) n.sides[others[0].second] = others[0].first < n.y + n.h / 2 ? 1 : 2;
					else if (!others.empty()) n.sides[others.front().second] = 1, n.sides[others.back().second] = 2;
				}
			}

			void wires(drawing& d) {
				std::set<std::pair<std::size_t, std::size_t>> branched;
				double low = -1e9;
				for (const auto& n : n_) low = std::max(low, n.y + n.h + n.below);
				for (const auto& e : e_) {
					const auto& a = n_[e.from];
					const auto& b = n_[e.to];
					auto out = out_port(a, e.from_port);
					auto in = in_port(b, e.to_port);
					double sx = a.x + out.x, sy = a.y + out.y, qx = b.x + in.x, qy = b.y + in.y;
					wire w;
					bool branch = from(e.from, e.from_port).size() > 1;
					if (branch) {
						double bx = sx + out.dx * lead, by = sy + out.dy * lead;
						if (branched.insert({e.from, e.from_port}).second) {
							d.wires.push_back({{{sx, sy}, {bx, by}}, false});
							d.dots.emplace_back(bx, by);
						}
						sx = bx, sy = by;
					}
					w.points.emplace_back(sx, sy);
					if (in.dy != 0) {
						// Into a sum from above or below: across, then down or up to it; or, from
						// the other side of it, around its left first.
						bool far = (sy - qy) * in.dy > 0;
						if (!far && out.dy != 0 && std::abs(sx - qx) > 1e-9) {
							double mid = (sy + qy) / 2;
							w.points.emplace_back(sx, mid);
							w.points.emplace_back(qx, mid);
						} else if (!far && std::abs(sx - qx) > 1e-9) {
							w.points.emplace_back(qx, sy);
						} else if (far && sx > qx + 1e-9 && e.back) {
							// Fed back from after it: under what's between them, or over it.
							double side = in.dy < 0 ? reach(qx, sx) + lead : rise(qx, sx) - lead;
							w.points.emplace_back(sx, side);
							w.points.emplace_back(qx, side);
						} else if (far) {
							double aside = b.x - lead, over = qy - in.dy * lead;
							w.points.emplace_back(aside, sy);
							w.points.emplace_back(aside, over);
							w.points.emplace_back(qx, over);
						}
					} else {
						double ahead = in.dx * (qx - sx);
						bool same = std::abs(sy - qy) < 1e-9;
						if (ahead > 0 && (branch || in.dx == out.dx)) {
							if (!same && branch) w.points.emplace_back(sx, qy);
							else if (!same) {
								double mid = (sx + qx) / 2;
								w.points.emplace_back(mid, sy);
								w.points.emplace_back(mid, qy);
							}
						} else {
							// Behind where it leaves: out, down under everything, back and in.
							detour_ += 0.6;
							double under = low + 0.2 + detour_;
							double off = branch ? sx : sx + out.dx * lead;
							if (!branch) w.points.emplace_back(off, sy);
							w.points.emplace_back(off, under);
							w.points.emplace_back(qx - in.dx * lead, under);
							w.points.emplace_back(qx - in.dx * lead, qy);
						}
					}
					w.points.emplace_back(qx, qy);
					d.wires.push_back(std::move(w));
				}
			}

			// Words and faces, centered where they go.
			void centered(drawing& d, const std::string& written, double size, double cx, double cy) {
				auto m = measure(written, size);
				d.faces.push_back({written, size, cx - m.width / 2, cy + (m.ascent - m.descent) / 2});
			}
			void under(drawing& d, const node& n) {
				if (n.label.empty()) return;
				auto m = measure(words(n.label), label);
				d.faces.push_back({words(n.label), label, n.x + n.w / 2 - m.width / 2, n.y + n.h + 0.25 + m.ascent});
			}

			void faces(drawing& d) {
				for (const auto& n : n_) {
					double cx = n.x + n.w / 2, cy = n.y + n.h / 2;
					switch (n.type) {
						case shape::kind::port: centered(d, words(std::to_string(n.number)), 0.75, cx, cy); break;
						case shape::kind::sum:
							for (std::size_t i = 0; i < n.signs.size(); ++i) {
								auto p = in_port(n, i);
								double ox = p.dx != 0 ? -p.dx * 0.24 : 0, oy = p.dy != 0 ? -p.dy * 0.24 : 0;
								centered(d, words(n.signs[i] < 0 ? "−" : "+"), 0.6, cx + ox, cy + oy);
							}
							break;
						case shape::kind::gain: {
							double half = measure(n.face, 1).width / 2 + 0.4;
							centered(d, n.face, 1, n.flipped ? n.x + n.w - half : n.x + half, cy);
							break;
						}
						case shape::kind::box: centered(d, n.face, 1, cx, cy); break;
						case shape::kind::constant: centered(d, words(n.face), 1, cx, cy); break;
						case shape::kind::system:
							for (std::size_t i = 0; i < n.ins.size(); ++i) {
								auto m = measure(words(n.ins[i]), label);
								double x = n.flipped ? n.x + n.w - 0.25 - m.width : n.x + 0.25;
								d.faces.push_back({words(n.ins[i]), label, x, n.y + in_port(n, i).y + (m.ascent - m.descent) / 2});
							}
							for (std::size_t i = 0; i < n.outs.size(); ++i) {
								auto m = measure(words(n.outs[i]), label);
								double x = n.flipped ? n.x + 0.25 : n.x + n.w - 0.25 - m.width;
								d.faces.push_back({words(n.outs[i]), label, x, n.y + out_port(n, i).y + (m.ascent - m.descent) / 2});
							}
							break;
					}
					under(d, n);
				}
			}

			// Everything moved to start a little in from the top left, and as big as it is.
			void fit(drawing& d) {
				double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
				auto take = [&](double x, double y) {
					left = std::min(left, x), top = std::min(top, y), right = std::max(right, x), bottom = std::max(bottom, y);
				};
				for (const auto& s : d.shapes) take(s.x, s.y), take(s.x + s.width, s.y + s.height);
				for (const auto& w : d.wires) {
					for (auto [x, y] : w.points) take(x, y);
				}
				for (const auto& f : d.faces) {
					auto m = measure(f.written, f.size);
					take(f.x, f.y - m.ascent), take(f.x + m.width, f.y + m.descent);
				}
				double dx = 0.3 - left, dy = 0.3 - top;
				for (auto& s : d.shapes) s.x += dx, s.y += dy;
				for (auto& w : d.wires) {
					for (auto& [x, y] : w.points) x += dx, y += dy;
				}
				for (auto& [x, y] : d.dots) x += dx, y += dy;
				for (auto& f : d.faces) f.x += dx, f.y += dy;
				d.width = right - left + 0.6;
				d.height = bottom - top + 0.6;
			}
		};

	}  // namespace detail

	// A diagram read and laid out, or why it can't be.
	inline std::expected<drawing, problem> lay_out(std::string_view written) {
		try {
			detail::builder b;
			std::size_t at = 0;
			while (at <= written.size()) {
				std::size_t end = written.find_first_of("\n;", at);
				if (end == std::string_view::npos) end = written.size();
				b.statement({written.substr(at, end - at), at});
				at = end + 1;
			}
			b.resolve();
			return detail::layout(b).run();
		} catch (const detail::failed& f) {
			return std::unexpected(f.p);
		}
	}

	// What's drawn, a line each, to a hundredth of an em, as tests/diagrams.txt keeps it.
	inline std::string written(const drawing& d) {
		static constexpr const char* kinds[] = {"port", "sum", "gain", "box", "constant", "system"};
		std::string out = std::format("{:.2f} {:.2f}\n", d.width, d.height);
		for (const auto& s : d.shapes) out += std::format("{} {:.2f} {:.2f} {:.2f} {:.2f}{}\n", kinds[static_cast<int>(s.type)], s.x, s.y, s.width, s.height, s.flipped ? " flipped" : "");
		for (const auto& w : d.wires) {
			out += w.arrow ? "wire" : "stub";
			for (auto [x, y] : w.points) out += std::format(" {:.2f},{:.2f}", x, y);
			out += "\n";
		}
		for (auto [x, y] : d.dots) out += std::format("dot {:.2f},{:.2f}\n", x, y);
		for (const auto& f : d.faces) out += std::format("face {:.2f} {:.2f} {:.2f} {}\n", f.size, f.x, f.y, f.written);
		return out;
	}

}  // namespace one::diagram
