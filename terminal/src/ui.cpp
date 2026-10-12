// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Views in a terminal: cells, elements laid out in them, focus and keys, the views
// themselves, and the app running them.

#include "one/terminal/ui.hpp"

#include <one/screens/editing.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <map>
#include <set>
#include <typeinfo>

#include "curses_internal.hpp"

// curses makes scroll a macro; here it's a view.
#undef scroll

namespace one::terminal {

	using namespace detail;

	// areas and cells

	area area::intersect(const area& other) const {
		int left = std::max(x, other.x), top = std::max(y, other.y);
		int r = std::min(right(), other.right()), b = std::min(bottom(), other.bottom());
		return {left, top, std::max(0, r - left), std::max(0, b - top)};
	}

	canvas::canvas(int width, int height) : width_(std::max(0, width)), height_(std::max(0, height)), cells_(static_cast<std::size_t>(width_ * height_)) {}

	canvas::clip_scope canvas::clip(const area& a) {
		clips_.push_back(clips_.empty() ? a : clips_.back().intersect(a));
		return clip_scope(*this);
	}

	int canvas::put(int y, int x, std::string_view text, style look, int limit) {
		area within = clips_.empty() ? area{0, 0, width_, height_} : clips_.back();
		within = within.intersect({0, 0, width_, height_});
		if (y < within.y || y >= within.bottom()) {
			// Still counts its width, so what follows it on the row is placed right.
			return std::min(limit < 0 ? display_width(text) : limit, display_width(text));
		}
		int used = 0;
		for (std::size_t i = 0; i < text.size();) {
			unsigned char lead = static_cast<unsigned char>(text[i]);
			std::size_t length = length_of(lead);
			std::string_view character = text.substr(i, length);
			i += length;
			if (lead < 0x20) continue;
			int wide = width_of(character);
			if (wide == 0) continue;
			if (limit >= 0 && used + wide > limit) break;
			int at = x + used;
			if (at >= within.x && at + wide <= within.right()) {
				auto& c = cells_[static_cast<std::size_t>(y * width_ + at)];
				c.glyph = std::string(character);
				c.look = look;
				c.covered = false;
				if (wide == 2) {
					auto& second = cells_[static_cast<std::size_t>(y * width_ + at + 1)];
					second.glyph.clear();
					second.look = look;
					second.covered = true;
				}
			}
			used += wide;
		}
		return used;
	}

	void canvas::fill(const area& a, style look) {
		area within = (clips_.empty() ? area{0, 0, width_, height_} : clips_.back()).intersect(a).intersect({0, 0, width_, height_});
		for (int y = within.y; y < within.bottom(); ++y) {
			for (int x = within.x; x < within.right(); ++x) cells_[static_cast<std::size_t>(y * width_ + x)] = {" ", look, false};
		}
	}

	void canvas::reverse(const area& a) {
		area within = (clips_.empty() ? area{0, 0, width_, height_} : clips_.back()).intersect(a).intersect({0, 0, width_, height_});
		for (int y = within.y; y < within.bottom(); ++y) {
			for (int x = within.x; x < within.right(); ++x) cells_[static_cast<std::size_t>(y * width_ + x)].look.reverse = true;
		}
	}

	std::vector<std::string> canvas::rows() const {
		std::vector<std::string> out;
		for (int y = 0; y < height_; ++y) {
			std::string line;
			for (int x = 0; x < width_; ++x) {
				const auto& c = cells_[static_cast<std::size_t>(y * width_ + x)];
				if (!c.covered) line += c.glyph;
			}
			while (!line.empty() && line.back() == ' ') line.pop_back();
			out.push_back(line);
		}
		return out;
	}

	std::string canvas::text() const {
		std::string out;
		for (const auto& line : rows()) out += line + "\n";
		return out;
	}

	void canvas::show() const {
		for (int y = 0; y < height_ && y < LINES; ++y) {
			for (int x = 0; x < width_ && x < COLS; ++x) {
				const auto& c = cells_[static_cast<std::size_t>(y * width_ + x)];
				if (c.covered) continue;
				attr_t a = attributes_of(c.look);
				attron(a);
				mvaddstr(y, x, c.glyph.c_str());
				attroff(a);
			}
		}
	}

	// element

	area element::inner() const {
		int edge = boxed ? 1 : 0;  // the line around it, then its padding
		return {frame.x + pad_left + edge, frame.y + pad_top + edge, std::max(0, frame.width - pad_left - pad_right - 2 * edge),
		        std::max(0, frame.height - pad_top - pad_bottom - 2 * edge)};
	}

	extent element::measure(int width) {
		int across = pad_left + pad_right + frame_width(), down = pad_top + pad_bottom + frame_width();
		int given = fixed_width.value_or(width);
		extent content = measure_content(std::max(0, given - across));
		return {fixed_width.value_or(content.width + across), fixed_height.value_or(content.height + down)};
	}

	void element::place(const area& a) {
		frame = a;
		place_content(inner());
	}

	void element::paint(canvas& c) {
		// The line goes around its edge, its padding inside the line.
		if (boxed && frame.width >= 2 && frame.height >= 2) {
			int left = frame.x, top = frame.y;
			int w = frame.width, h = frame.height;
			style edge_style = focused ? style{.bold = true, .foreground = color::blue} : style{.dim = true};
			std::string across;
			for (int i = 0; i < w - 2; ++i) across += "─";
			c.put(top, left, "┌" + across + "┐", edge_style);
			c.put(top + h - 1, left, "└" + across + "┘", edge_style);
			for (int y = top + 1; y < top + h - 1; ++y) {
				c.put(y, left, "│", edge_style);
				c.put(y, left + w - 1, "│", edge_style);
			}
			if (!box_title.empty()) c.put(top, left + 2, " " + box_title + " ", {.bold = true}, w - 4);
		}
		paint_content(c);
	}

	extent element::measure_content(int width) {
		extent out;
		for (auto& child : children) {
			extent e = child->measure(width);
			out.width = std::max(out.width, e.width);
			out.height = std::max(out.height, e.height);
		}
		return out;
	}

	void element::place_content(const area& a) {
		for (auto& child : children) child->place(a);
	}

	void element::paint_content(canvas& c) {
		for (auto& child : children) child->paint(c);
	}

	bool element::handle_key(const key_press&) { return false; }

	void element::adopt(element& before) {
		carried = true;
		focused = before.focused;
		focused_by_default = before.focused_by_default;
		brought = before.brought;
	}

	void element::reveal(const area& a) {
		for (element* e = parent; e; e = e->parent) {
			if (e->reveal_here(a)) return;
		}
	}

	void element::bring_to_top() {
		for (element* e = parent; e; e = e->parent) {
			if (e->top_here(frame)) return;
		}
	}

	namespace {

		void adopt_children(element& e) {
			for (auto& child : e.children) child->parent = &e;
		}

		std::vector<std::unique_ptr<element>> taken(std::vector<view>& views) {
			std::vector<std::unique_ptr<element>> out;
			for (auto& v : views) {
				if (v.get()) out.push_back(std::move(v).take());
			}
			return out;
		}

		template <class E, class... A>
		view made(A&&... arguments) {
			return view(std::make_unique<E>(std::forward<A>(arguments)...));
		}

		void reconcile(element& now, element& before) {
			now.adopt(before);
			std::map<std::string, element*> keyed;
			std::vector<element*> plain;
			for (auto& child : before.children) {
				if (!child->key.empty()) keyed.emplace(child->key, child.get());
				else plain.push_back(child.get());
			}
			std::size_t next_plain = 0;
			for (auto& child : now.children) {
				element* match = nullptr;
				if (!child->key.empty()) {
					if (auto it = keyed.find(child->key); it != keyed.end()) match = it->second;
				} else if (next_plain < plain.size()) {
					match = plain[next_plain++];
				}
				if (match && typeid(*match) == typeid(*child)) reconcile(*child, *match);
			}
		}

		void focusables(element& root, std::vector<element*>& into) {
			if (root.focusable()) into.push_back(&root);
			for (auto& child : root.children) focusables(*child, into);
		}

		element* focused_in(element& root) {
			if (root.focused) return &root;
			for (auto& child : root.children) {
				if (auto* f = focused_in(*child)) return f;
			}
			return nullptr;
		}

		template <class F>
		void each_element(element& e, F&& visit) {
			visit(e);
			for (auto& child : e.children) each_element(*child, visit);
		}

		// Words wrapped to a width, each run's style kept, a newline starting a row.
		std::vector<line> wrapped(const line& runs, int width) {
			std::vector<line> out(1);
			int used = 0;
			auto add = [&](const std::string& piece, const style& look) {
				int w = display_width(piece);
				if (width > 0 && used + w > width && used > 0) {
					out.emplace_back();
					used = 0;
					if (piece == " ") return;
				}
				if (!out.back().empty() && out.back().back().look.bold == look.bold && out.back().back().look.dim == look.dim &&
				    out.back().back().look.underline == look.underline && out.back().back().look.reverse == look.reverse &&
				    out.back().back().look.foreground == look.foreground && out.back().back().look.background == look.background && out.back().back().look.italic == look.italic)
					out.back().back().text += piece;
				else out.back().push_back({piece, look});
				used += w;
			};
			for (const auto& r : runs) {
				std::size_t at = 0;
				while (at < r.text.size()) {
					if (r.text[at] == '\n') {
						out.emplace_back();
						used = 0;
						++at;
						continue;
					}
					std::size_t end = r.text.find_first_of(" \n", at);
					if (end == std::string::npos) end = r.text.size();
					std::string word = r.text.substr(at, end - at);
					// A word longer than a row is cut across rows.
					while (width > 0 && display_width(word) > width) {
						std::string fits;
						for (std::size_t i = 0; i < word.size();) {
							std::size_t length = length_of(static_cast<unsigned char>(word[i]));
							if (display_width(fits + word.substr(i, length)) > width - used && !fits.empty()) break;
							fits += word.substr(i, length);
							i += length;
						}
						if (fits.empty()) break;
						add(fits, r.look);
						word = word.substr(fits.size());
						out.emplace_back();
						used = 0;
					}
					if (!word.empty()) add(word, r.look);
					at = end;
					if (at < r.text.size() && r.text[at] == ' ') {
						add(" ", r.look);
						++at;
					}
				}
			}
			for (auto& line : out) {
				while (!line.empty() && !line.back().text.empty() && line.back().text.back() == ' ') {
					line.back().text.pop_back();
					if (line.back().text.empty()) line.pop_back();
				}
			}
			return out;
		}

		// text

		class text_element : public element {
		public:
			explicit text_element(line runs) : runs_(std::move(runs)) {}
			extent measure_content(int width) override {
				auto lines = wrapped(runs_, width);
				int widest = 0;
				for (const auto& l : lines) widest = std::max(widest, display_width(l));
				return {widest, static_cast<int>(lines.size())};
			}
			void paint_content(canvas& c) override {
				area a = inner();
				auto lines = wrapped(runs_, a.width);
				for (std::size_t i = 0; i < lines.size(); ++i) {
					int x = a.x;
					for (const auto& r : lines[i]) x += c.put(a.y + static_cast<int>(i), x, r.text, r.look, a.right() - x);
				}
			}

		private:
			line runs_;
		};

		class rows_element : public element {
		public:
			explicit rows_element(std::vector<line> rows) : rows_(std::move(rows)) {}
			extent measure_content(int) override {
				int widest = 0;
				for (const auto& r : rows_) widest = std::max(widest, display_width(r));
				return {widest, static_cast<int>(rows_.size())};
			}
			void paint_content(canvas& c) override {
				area a = inner();
				for (std::size_t i = 0; i < rows_.size(); ++i) {
					int x = a.x;
					for (const auto& r : rows_[i]) x += c.put(a.y + static_cast<int>(i), x, r.text, r.look, a.right() - x);
				}
			}

		private:
			std::vector<line> rows_;
		};

		class laid_element : public element {
		public:
			explicit laid_element(std::function<std::vector<line>(int)> lay_out) : lay_out_(std::move(lay_out)) {}
			extent measure_content(int width) override {
				const auto& rows = laid(width);
				int widest = 0;
				for (const auto& r : rows) widest = std::max(widest, display_width(r));
				return {widest, static_cast<int>(rows.size())};
			}
			void paint_content(canvas& c) override {
				area a = inner();
				const auto& rows = laid(a.width);
				for (std::size_t i = 0; i < rows.size(); ++i) {
					int x = a.x;
					for (const auto& r : rows[i]) x += c.put(a.y + static_cast<int>(i), x, r.text, r.look, a.right() - x);
				}
			}

		private:
			const std::vector<line>& laid(int width) {
				if (width != width_) {
					rows_ = lay_out_(width);
					width_ = width;
				}
				return rows_;
			}
			std::function<std::vector<line>(int)> lay_out_;
			std::vector<line> rows_;
			int width_ = -1;
		};

		// rows and columns

		class stack_element : public element {
		public:
			explicit stack_element(bool across) : across_(across) {}

			extent measure_content(int width) override {
				extent out;
				if (!across_) {
					for (std::size_t i = 0; i < children.size(); ++i) {
						extent e = children[i]->measure(width);
						out.width = std::max(out.width, e.width);
						out.height += e.height + (i ? spacing : 0);
					}
					return out;
				}
				auto widths = widths_within(width);
				for (std::size_t i = 0; i < children.size(); ++i) {
					extent e = children[i]->measure(widths[i]);
					out.height = std::max(out.height, e.height);
					out.width += widths[i] + (i ? spacing : 0);
				}
				return out;
			}

			void place_content(const area& a) override {
				if (across_) {
					auto widths = widths_within(a.width);
					int x = a.x;
					for (std::size_t i = 0; i < children.size(); ++i) {
						auto& child = *children[i];
						int height = cross == align::stretch ? a.height : std::min(a.height, child.measure(widths[i]).height);
						int y = cross == align::center ? a.y + (a.height - height) / 2 : cross == align::end ? a.bottom() - height : a.y;
						child.place({x, y, widths[i], height});
						x += widths[i] + spacing;
					}
					return;
				}
				std::vector<int> heights;
				int used = 0;
				float shares = 0;
				for (std::size_t i = 0; i < children.size(); ++i) {
					heights.push_back(children[i]->measure(a.width).height);
					used += heights.back() + (i ? spacing : 0);
					shares += children[i]->grow;
				}
				int left = a.height - used;
				if (shares > 0 && left > 0) {
					for (std::size_t i = 0; i < children.size(); ++i) heights[i] += static_cast<int>(static_cast<float>(left) * children[i]->grow / shares);
				}
				int y = a.y;
				for (std::size_t i = 0; i < children.size(); ++i) {
					auto& child = *children[i];
					int width = a.width;
					if (cross != align::stretch) width = std::min(a.width, child.measure(a.width).width);
					int x = cross == align::center ? a.x + (a.width - width) / 2 : cross == align::end ? a.right() - width : a.x;
					child.place({x, y, width, heights[i]});
					y += heights[i] + spacing;
				}
			}

		private:
			std::vector<int> widths_within(int width) {
				std::vector<int> widths(children.size(), 0);
				int left = width - spacing * static_cast<int>(children.empty() ? 0 : children.size() - 1);
				float shares = 0;
				for (std::size_t i = 0; i < children.size(); ++i) {
					if (children[i]->grow > 0) {
						shares += children[i]->grow;
						continue;
					}
					widths[i] = std::min(std::max(0, left), children[i]->measure(std::max(0, left)).width);
					left -= widths[i];
				}
				int handed = 0, last = -1;
				for (std::size_t i = 0; i < children.size(); ++i) {
					if (children[i]->grow <= 0) continue;
					widths[i] = static_cast<int>(static_cast<float>(std::max(0, left)) * children[i]->grow / shares);
					handed += widths[i];
					last = static_cast<int>(i);
				}
				// What rounding left over goes to the last that grows.
				if (last >= 0) widths[static_cast<std::size_t>(last)] += std::max(0, left) - handed;
				return widths;
			}
			bool across_;
		};

		// scrolling

		class scroll_element : public element {
		public:
			extent measure_content(int width) override {
				extent e = children.front()->measure(width);
				return {e.width, std::min(e.height, 1)};
			}
			void place_content(const area& a) override {
				content_height_ = children.front()->measure(a.width).height;
				offset_ = std::clamp(offset_, 0, std::max(0, content_height_ - a.height));
				children.front()->place({a.x, a.y - offset_, a.width, std::max(content_height_, a.height)});
			}
			void paint_content(canvas& c) override {
				area a = inner();
				auto clipped = c.clip(a);
				children.front()->paint(c);
			}
			bool handle_key(const key_press& e) override {
				area a = inner();
				int before = offset_;
				if (e.is(keys::page_down) || e.is(' ')) offset_ += std::max(1, a.height - 2);
				else if (e.is(keys::page_up) || e.is('b')) offset_ -= std::max(1, a.height - 2);
				else if (e.is('g')) offset_ = 0;
				else if (e.is('G')) offset_ = content_height_;
				else return false;
				offset_ = std::clamp(offset_, 0, std::max(0, content_height_ - a.height));
				if (offset_ != before) place(frame);
				return true;
			}

			bool top_here(const area& wanted) override {
				area a = inner();
				offset_ = std::clamp(wanted.y - (a.y - offset_), 0, std::max(0, content_height_ - a.height));
				place(frame);
				return true;
			}
			void adopt(element& before) override {
				element::adopt(before);
				offset_ = static_cast<scroll_element&>(before).offset_;
			}

		protected:
			bool reveal_here(const area& wanted) override {
				area a = inner();
				int before = offset_;
				// Where it is within the content, kept in sight.
				int top = wanted.y - (a.y - offset_);
				if (top < offset_) offset_ = top;
				if (top + wanted.height > offset_ + a.height) offset_ = top + wanted.height - a.height;
				offset_ = std::clamp(offset_, 0, std::max(0, content_height_ - a.height));
				if (offset_ != before) place(frame);
				return true;
			}

		private:
			int offset_ = 0, content_height_ = 0;
		};

		// lists

		class list_element : public element {
		public:
			list_element(std::vector<std::string> keys, list_options options) : keys_(std::move(keys)), options_(std::move(options)) {
				if (std::ranges::contains(keys_, options_.start)) chosen_ = options_.start;
			}

			bool focusable() const override { return !keys_.empty(); }
			bool prefers_focus() const override { return true; }

			extent measure_content(int width) override {
				extent out;
				for (auto& child : children) {
					extent e = child->measure(std::max(0, width - 2));
					out.width = std::max(out.width, e.width + 2);
					out.height += e.height;
				}
				return out;
			}
			void place_content(const area& a) override {
				int y = a.y;
				for (auto& child : children) {
					int h = child->measure(std::max(0, a.width - 2)).height;
					child->place({a.x + 2, y, std::max(0, a.width - 2), h});
					y += h;
				}
			}
			void paint_content(canvas& c) override {
				std::size_t chosen = index_of(chosen_);
				area a = inner();
				for (std::size_t i = 0; i < children.size(); ++i) {
					auto& child = *children[i];
					if (i == chosen && focused) {
						c.fill({a.x, child.frame.y, a.width, child.frame.height}, {.reverse = true});
						c.put(child.frame.y, a.x, "▸", {.bold = true, .reverse = true});
					} else if (i == chosen) {
						c.put(child.frame.y, a.x, "›", {.dim = true});
					}
					child.paint(c);
					// The chosen row is drawn reversed while the list has the keyboard.
					if (i == chosen && focused) reverse(c, {a.x, child.frame.y, a.width, child.frame.height});
				}
			}
			bool handle_key(const key_press& e) override {
				if (e.alt || e.control) return false;
				if (keys_.empty()) return false;
				std::size_t at = index_of(chosen_), last = keys_.size() - 1;
				std::size_t to = at;
				if (e.is(keys::down) || e.is('j')) to = std::min(last, at + 1);
				else if (e.is(keys::up) || e.is('k')) {
					if (at == 0) return false;
					to = at - 1;
				} else if (e.is(keys::home)) to = 0;
				else if (e.is(keys::end)) to = last;
				else if (e.is(keys::page_down)) to = std::min(last, at + 10);
				else if (e.is(keys::page_up)) to = at > 10 ? at - 10 : 0;
				else if (e.is(keys::newline) || e.is(keys::enter) || e.is('l') || e.is(keys::right)) {
					if (!options_.on_open) return false;
					options_.on_open(keys_[at]);
					return true;
				} else
					return false;
				if (to == at && (e.is(keys::down) || e.is('j'))) return false;  // past the last: for whatever's below
				chosen_ = keys_[to];
				children[to]->reveal(children[to]->frame);
				return true;
			}
			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<list_element&>(before);
				if (std::ranges::contains(keys_, was.chosen_)) chosen_ = was.chosen_;
				else if (!keys_.empty() && !was.chosen_.empty()) chosen_ = keys_[std::min(was.index_of(was.chosen_), keys_.size() - 1)];
			}
			std::string shown_chosen() const { return keys_.empty() ? "" : keys_[index_of(chosen_)]; }

		private:
			static void reverse(canvas& c, const area& a) { c.reverse(a); }
			std::size_t index_of(const std::string& key) const {
				auto it = std::ranges::find(keys_, key);
				return it == keys_.end() ? 0 : static_cast<std::size_t>(it - keys_.begin());
			}
			std::vector<std::string> keys_;
			list_options options_;
			std::string chosen_;
		};

		// small things

		class button_element : public element {
		public:
			button_element(std::string label, std::function<void()> pressed, bool primary) : label_(std::move(label)), pressed_(std::move(pressed)), primary_(primary) {}
			bool focusable() const override { return true; }
			extent measure_content(int) override { return {display_width(label_) + 4, 1}; }
			void paint_content(canvas& c) override {
				area a = inner();
				style look{.bold = primary_ || focused, .reverse = focused, .foreground = primary_ && !focused ? color::blue : color::none};
				c.put(a.y, a.x, "[ " + label_ + " ]", look, a.width);
			}
			bool handle_key(const key_press& e) override {
				if (!e.is(keys::newline) && !e.is(keys::enter) && !e.is(' ')) return false;
				if (pressed_) pressed_();
				return true;
			}

		private:
			std::string label_;
			std::function<void()> pressed_;
			bool primary_;
		};

		class link_element : public element {
		public:
			link_element(std::string words, std::function<void()> go, style look) : words_(std::move(words)), go_(std::move(go)), look_(look) {}
			bool focusable() const override { return true; }
			extent measure_content(int) override { return {display_width(words_), 1}; }
			void paint_content(canvas& c) override {
				area a = inner();
				style look = look_;
				if (look.foreground == color::none && !look.bold) look.foreground = color::blue;
				look.underline = true;
				look.reverse = focused;
				c.put(a.y, a.x, words_, look, a.width);
			}
			bool handle_key(const key_press& e) override {
				if (!e.is(keys::newline) && !e.is(keys::enter)) return false;
				if (go_) go_();
				return true;
			}

		private:
			std::string words_;
			std::function<void()> go_;
			style look_;
		};

		class rule_element : public element {
		public:
			rule_element() { fixed_height = 1; }
			extent measure_content(int) override { return {0, 1}; }
			void paint_content(canvas& c) override { c.put(frame.y, frame.x, rule(frame.width), {.dim = true}, frame.width); }
		};

		// fields

		class field_element : public element {
		public:
			field_element(state<std::string> value, field_options options) : value_(std::move(value)), options_(std::move(options)), edit_(value_.peek(), options_.multiline) {}

			bool focusable() const override { return true; }
			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<field_element&>(before);
				edit_ = std::move(was.edit_);
				scroll_ = was.scroll_;
				edit_.replace_all(value_.peek());
			}
			extent measure_content(int width) override {
				if (!options_.multiline) return {std::min(width, 30), 1};
				return {width, std::max(options_.lines, static_cast<int>(lines(width).size()))};
			}
			void paint_content(canvas& c) override {
				area a = inner();
				style look{.underline = !options_.multiline, .reverse = false};
				c.fill(a, {.underline = !options_.multiline});
				if (edit_.text().empty()) {
					c.put(a.y, a.x, options_.placeholder, {.underline = !options_.multiline, .dim = true}, a.width);
				} else if (!options_.multiline) {
					auto [col, _] = caret_place(a.width);
					if (col - scroll_ >= a.width) scroll_ = col - a.width + 1;
					if (col < scroll_) scroll_ = col;
					std::string shown = cut_from(edit_.text(), scroll_);
					c.put(a.y, a.x, shown, look, a.width);
				} else {
					auto all = lines(a.width);
					for (std::size_t i = 0; i < all.size() && static_cast<int>(i) < a.height; ++i) c.put(a.y + static_cast<int>(i), a.x, all[i].second, {}, a.width);
				}
				if (focused) {
					auto [col, line] = caret_place(a.width);
					cursor = {a.y + line, a.x + col - (options_.multiline ? 0 : scroll_)};
				}
			}
			bool handle_key(const key_press& e) override {
				bool word = e.alt, extend = false;
				if (e.is(keys::newline) || e.is(keys::enter)) {
					if (options_.multiline) return changed([&] { edit_.type("\n"); });
					if (!options_.on_submit) return false;
					options_.on_submit();
					return true;
				}
				if (e.is(keys::backspace) || (e.control && e.text == "h")) return changed([&] { edit_.erase_back(word); });
				if (e.is(keys::delete_key)) return changed([&] { edit_.erase_forward(word); });
				if (e.is(keys::left)) return moved([&] { edit_.left(word, extend); });
				if (e.is(keys::right)) return moved([&] { edit_.right(word, extend); });
				if (e.is(keys::home)) return moved([&] { options_.multiline ? edit_.line_start(false) : edit_.start(false); });
				if (e.is(keys::end)) return moved([&] { options_.multiline ? edit_.line_end(false) : edit_.end(false); });
				if (e.alt && e.text == "b") return moved([&] { edit_.left(true, false); });
				if (e.alt && e.text == "f") return moved([&] { edit_.right(true, false); });
				if (e.control) {
					if (e.text == "a") return moved([&] { edit_.line_start(false); });
					if (e.text == "e") return moved([&] { edit_.line_end(false); });
					if (e.text == "k") return changed([&] {
						edit_.line_end(true);
						edit_.erase_forward(false);
					});
					if (e.text == "u") return changed([&] {
						edit_.line_start(true);
						edit_.erase_back(false);
					});
					if (e.text == "w") return changed([&] { edit_.erase_back(true); });
					if (e.text == "z") return changed([&] { edit_.undo(); });
					if (e.text == "y") return changed([&] { edit_.redo(); });
					return false;
				}
				if (!e.alt && !e.text.empty() && static_cast<unsigned char>(e.text[0]) >= 0x20) return changed([&] { edit_.type(e.text); });
				return false;
			}
			std::optional<std::pair<int, int>> cursor;

		private:
			// The text cut into rows of a width, each with where it starts.
			std::vector<std::pair<std::size_t, std::string>> lines(int width) const {
				std::vector<std::pair<std::size_t, std::string>> out;
				const std::string& t = edit_.text();
				std::size_t start = 0;
				std::string current;
				int used = 0;
				for (std::size_t i = 0; i <= t.size();) {
					if (i == t.size() || t[i] == '\n') {
						out.emplace_back(start, current);
						current.clear();
						used = 0;
						start = i + 1;
						if (i == t.size()) break;
						++i;
						continue;
					}
					std::size_t length = length_of(static_cast<unsigned char>(t[i]));
					std::string ch = t.substr(i, length);
					int w = display_width(ch);
					if (width > 0 && used + w > width) {
						out.emplace_back(start, current);
						current.clear();
						used = 0;
						start = i;
					}
					current += ch;
					used += w;
					i += length;
				}
				return out;
			}
			// The caret's column and row.
			std::pair<int, int> caret_place(int width) const {
				std::size_t at = edit_.caret();
				if (!options_.multiline) return {display_width(std::string_view(edit_.text()).substr(0, at)), 0};
				auto all = lines(width);
				for (std::size_t i = all.size(); i-- > 0;) {
					if (all[i].first <= at) return {display_width(std::string_view(edit_.text()).substr(all[i].first, at - all[i].first)), static_cast<int>(i)};
				}
				return {0, 0};
			}
			static std::string cut_from(const std::string& text, int columns) {
				std::size_t i = 0;
				int used = 0;
				while (i < text.size() && used < columns) {
					std::size_t length = length_of(static_cast<unsigned char>(text[i]));
					used += display_width(std::string_view(text).substr(i, length));
					i += length;
				}
				return text.substr(i);
			}
			template <class F>
			bool moved(F&& how) {
				how();
				return true;
			}
			template <class F>
			bool changed(F&& how) {
				std::string before = edit_.text();
				how();
				if (edit_.text() != before) value_.set(edit_.text());
				return true;
			}
			state<std::string> value_;
			field_options options_;
			screens::editing edit_;
			int scroll_ = 0;
		};

		class toggle_element : public element {
		public:
			toggle_element(state<bool> on, std::string label) : on_(std::move(on)), label_(std::move(label)) {}
			bool focusable() const override { return true; }
			extent measure_content(int) override { return {display_width(label_) + 4, 1}; }
			void paint_content(canvas& c) override {
				area a = inner();
				c.put(a.y, a.x, on_.peek() ? "[x] " : "[ ] ", {.bold = focused, .reverse = focused});
				c.put(a.y, a.x + 4, label_, {}, a.width - 4);
			}
			bool handle_key(const key_press& e) override {
				if (!e.is(' ') && !e.is(keys::newline) && !e.is(keys::enter)) return false;
				on_.set(!on_.peek());
				return true;
			}

		private:
			state<bool> on_;
			std::string label_;
		};

		class chosen_element : public element {
		public:
			chosen_element(std::string shown, bool placeholder, state<bool> open) : shown_(std::move(shown)), placeholder_(placeholder), open_(std::move(open)) {}
			bool focusable() const override { return true; }
			extent measure_content(int width) override { return {std::min(width, std::max(20, display_width(shown_) + 4)), 1}; }
			void paint_content(canvas& c) override {
				area a = inner();
				c.fill(a, {.underline = true, .reverse = focused});
				c.put(a.y, a.x, shown_, {.underline = true, .dim = placeholder_ && !focused, .reverse = focused}, a.width - 2);
				c.put(a.y, a.right() - 1, "▾", {.underline = true, .reverse = focused});
			}
			bool handle_key(const key_press& e) override {
				if (!e.is(' ') && !e.is(keys::newline) && !e.is(keys::enter) && !e.is(keys::down)) return false;
				open_.set(true);
				return true;
			}

		private:
			std::string shown_;
			bool placeholder_;
			state<bool> open_;
		};

		std::set<const void*>& closing() {
			thread_local std::set<const void*> pickers;
			return pickers;
		}

	}  // namespace

	view text(std::string words, style look) { return made<text_element>(line{run{std::move(words), look}}); }
	view text(terminal::line runs) { return made<text_element>(std::move(runs)); }
	view text_rows(std::vector<terminal::line> rows) { return made<rows_element>(std::move(rows)); }
	view text_laid(std::function<std::vector<terminal::line>(int)> lay_out) { return made<laid_element>(std::move(lay_out)); }

	view column(std::vector<view> children) {
		auto e = std::make_unique<stack_element>(false);
		e->children = taken(children);
		adopt_children(*e);
		return view(std::move(e));
	}

	view row(std::vector<view> children) {
		auto e = std::make_unique<stack_element>(true);
		e->children = taken(children);
		adopt_children(*e);
		return view(std::move(e));
	}

	view scroll(view content) {
		auto e = std::make_unique<scroll_element>();
		e->children.push_back(content.get() ? std::move(content).take() : std::make_unique<element>());
		adopt_children(*e);
		return view(std::move(e));
	}

	view list(std::vector<list_row> rows, list_options options) {
		std::vector<std::string> keys_of;
		std::vector<std::unique_ptr<element>> shown;
		for (auto& r : rows) {
			if (!r.shown.get()) continue;
			keys_of.push_back(r.key);
			auto e = std::move(r.shown).take();
			e->key = r.key;
			shown.push_back(std::move(e));
		}
		auto heard = options.on_row_key;
		auto e = std::make_unique<list_element>(std::move(keys_of), std::move(options));
		e->children = std::move(shown);
		adopt_children(*e);
		if (heard) {
			list_element* self = e.get();
			e->on_key = [self, heard](const key_press& k) { return !self->shown_chosen().empty() && heard(self->shown_chosen(), k); };
		}
		return view(std::move(e));
	}

	view list(std::vector<list_row> rows, std::function<void(const std::string&)> on_open) { return list(std::move(rows), list_options{.on_open = std::move(on_open)}); }

	view button(std::string label, std::function<void()> pressed, bool primary) { return made<button_element>(std::move(label), std::move(pressed), primary); }
	view link(std::string words, std::function<void()> go, style look) { return made<link_element>(std::move(words), std::move(go), look); }
	view divider() { return made<rule_element>(); }
	view spacer() { return std::move(made<element>()).grow(1); }
	view field(state<std::string> value, field_options options) { return made<field_element>(std::move(value), std::move(options)); }
	view toggle(state<bool> on, std::string label) { return made<toggle_element>(std::move(on), std::move(label)); }

	view picker(state<std::string> chosen, std::vector<option> options, state<bool> open, std::string placeholder) {
		std::string now = chosen.peek();
		const option* current = nullptr;
		for (const auto& o : options) {
			if (o.value == now) current = &o;
		}
		const void* id = &open.peek();
		if (!open.get()) {
			view closed = made<chosen_element>(current ? current->label : placeholder, !current, open);
			if (closing().erase(id)) return std::move(closed).autofocus();
			return closed;
		}
		std::vector<list_row> rows;
		for (const auto& o : options) rows.push_back({o.value, text(o.label, {.bold = o.value == now})});
		return list(std::move(rows), list_options{.on_open = [chosen, open, id](const std::string& value) {
			                                          chosen.set(value);
			                                          closing().insert(id);
			                                          open.set(false);
		                                          },
		                                          .start = now})
		    .autofocus()
		    .boxed()
		    .on_key([open, id](const key_press& e) {
			    if (!e.is(keys::escape)) return false;
			    closing().insert(id);
			    open.set(false);
			    return true;
		    });
	}

	// The host: building, focus and keys, as a window's are on the desktop

	void view_host::bring_up_to_date(int width, int height) {
		if (needs_building_ && build_) {
			auto reader = std::make_shared<screens::detail::reader>();
			view_host* self = this;
			reader->changed = [self] {
				self->needs_building_ = true;
				self->dirty_ = true;
			};
			std::unique_ptr<element> built;
			{
				screens::detail::reading noting(reader);
				built = build_().take();
			}
			reading_ = std::move(reader);
			if (built && root_) reconcile(*built, *root_);
			root_ = std::move(built);
			needs_building_ = false;
			dirty_ = true;
			element* had = root_ ? focused_in(*root_) : nullptr;
			if (root_ && (!had || had->focused_by_default)) {
				std::vector<element*> all;
				focusables(*root_, all);
				auto first = std::ranges::find_if(all, [](element* e) { return e->wants_focus && !e->carried; });
				if (first == all.end()) first = std::ranges::find_if(all, [](element* e) { return e->wants_focus; });
				if (first == all.end()) first = std::ranges::find_if(all, [](element* e) { return e->prefers_focus(); });
				bool better = first != all.end();
				if (!better) first = all.begin();
				if (first != all.end() && (!had || better)) {
					if (had) had->focused = had->focused_by_default = false;
					(*first)->focused = true;
					(*first)->focused_by_default = !better;
				}
			}
			width_ = 0;
		}
		if (root_ && (width != width_ || height != height_ || dirty_)) {
			root_->place({0, 0, width, height});
			each_element(*root_, [](element& e) {
				if (!e.to_top || e.brought) return;
				e.brought = true;
				e.bring_to_top();
			});
			width_ = width;
			height_ = height;
		}
	}

	element* view_host::focused() { return root_ ? focused_in(*root_) : nullptr; }

	bool view_host::deliver(const key_press& e) {
		if (!root_) return false;
		dirty_ = true;
		auto focus = [&](element* chosen) {
			each_element(*root_, [](element& el) { el.focused = el.focused_by_default = false; });
			chosen->focused = true;
			chosen->reveal(chosen->frame);
		};
		if (e.is(keys::tab) || e.is(keys::left_tab)) {
			std::vector<element*> all;
			focusables(*root_, all);
			if (all.empty()) return false;
			auto it = std::ranges::find(all, focused());
			std::size_t at = it == all.end() ? 0 : static_cast<std::size_t>(it - all.begin());
			std::size_t next = it == all.end() ? 0 : e.is(keys::left_tab) ? (at + all.size() - 1) % all.size() : (at + 1) % all.size();
			focus(all[next]);
			return true;
		}
		for (element* target = focused(); target; target = target->parent) {
			if (target->handle_key(e) || (target->on_key && target->on_key(e))) return true;
		}
		// The nearest that takes keys in a direction, when what has them doesn't take it.
		int dx = 0, dy = 0;
		if (e.is(keys::left) || e.is('h')) dx = -1;
		else if (e.is(keys::right) || e.is('l')) dx = 1;
		else if (e.is(keys::up) || e.is('k')) dy = -1;
		else if (e.is(keys::down) || e.is('j')) dy = 1;
		element* from = focused();
		if ((dx || dy) && from) {
			std::vector<element*> all;
			focusables(*root_, all);
			auto middle = [](const area& a) { return std::pair<float, float>{a.x + a.width / 2.f, a.y + a.height / 2.f}; };
			auto here = middle(from->frame);
			element* best = nullptr;
			float best_score = 0;
			for (element* candidate : all) {
				if (candidate == from) continue;
				const area& r = candidate->frame;
				float ahead = dx > 0 ? r.x - from->frame.right() : dx < 0 ? from->frame.x - r.right() : dy > 0 ? r.y - from->frame.bottom() : from->frame.y - r.bottom();
				if (ahead < 0) continue;
				auto there = middle(r);
				float aside = dx ? std::abs(there.second - here.second) : std::abs(there.first - here.first);
				float score = ahead + 2 * aside;
				if (!best || score < best_score) best = candidate, best_score = score;
			}
			if (best) {
				focus(best);
				return true;
			}
		}
		return false;
	}

	void view_host::draw(canvas& c) {
		dirty_ = false;
		cursor.reset();
		if (!root_) return;
		root_->paint(c);
		if (auto* f = dynamic_cast<field_element*>(focused())) cursor = f->cursor;
	}

	// The app

	namespace {

		// A key as curses reads it: a key's code, a character's text, Alt as an Escape
		// just before, and Control as the character it makes.
		std::optional<key_press> read_key() {
			wint_t got = 0;
			int kind = get_wch(&got);
			if (kind == ERR) return std::nullopt;
			if (kind == KEY_CODE_YES) return key_press{.code = static_cast<int>(got)};
			auto plain = [](wint_t c) -> key_press {
				if (c == '\n' || c == '\r') return {.code = keys::newline};
				if (c == '\t') return {.code = keys::tab};
				if (c == 127 || c == 8) return {.code = keys::backspace};
				if (c == 27) return {.code = keys::escape};
				if (c < 32) return {.text = std::string(1, static_cast<char>('a' + c - 1)), .control = true};
				std::mbstate_t state{};
				char bytes[MB_LEN_MAX];
				std::size_t length = std::wcrtomb(bytes, static_cast<wchar_t>(c), &state);
				return {.text = length == static_cast<std::size_t>(-1) ? std::string() : std::string(bytes, length)};
			};
			if (got == 27) {
				// Alt: Escape, then the key, at once.
				timeout(0);
				wint_t next = 0;
				int then = get_wch(&next);
				timeout(100);
				if (then == OK) {
					key_press e = plain(next);
					e.alt = true;
					return e;
				}
				if (then == KEY_CODE_YES) return key_press{.code = static_cast<int>(next), .alt = true};
			}
			return plain(got);
		}

	}  // namespace

	void view_app::post(std::function<void()> task) {
		std::lock_guard hold(lock_);
		posted_.push_back(std::move(task));
	}

	int view_app::run(std::function<view()> build, std::function<bool(const key_press&)> on_key) {
		session terminal;
		raw();  // Control's keys are the app's, as a field takes them
		view_host host(std::move(build));
		auto last_draw = std::chrono::steady_clock::now();
		bool dirty = true;
		while (!quitting_) {
			if (auto pressed = read_key()) {
				if (pressed->control && pressed->text == "c") break;
				if (pressed->code == KEY_RESIZE) {
					dirty = true;
				} else if (!host.deliver(*pressed) && on_key && !on_key(*pressed) && pressed->is('q')) {
					break;
				}
				dirty = true;
			}
			std::vector<std::function<void()>> tasks;
			{
				std::lock_guard hold(lock_);
				tasks.swap(posted_);
			}
			for (auto& task : tasks) task();
			if (!tasks.empty()) dirty = true;
			// What fades, and what says how long ago, moves on without a change to hear of.
			if (dirty || host.needs_drawing() || std::chrono::steady_clock::now() - last_draw > std::chrono::seconds(1)) {
				host.bring_up_to_date(COLS, LINES);
				canvas c(COLS, LINES);
				host.draw(c);
				// Every cell is drawn each frame, so the screen is written whole, as it is.
				clearok(stdscr, TRUE);
				c.show();
				if (host.cursor) {
					curs_set(1);
					move(host.cursor->first, host.cursor->second);
				} else {
					curs_set(0);
				}
				refresh();
				dirty = false;
				last_draw = std::chrono::steady_clock::now();
			}
		}
		return 0;
	}

}  // namespace one::terminal
