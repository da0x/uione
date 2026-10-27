// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <cmath>
#include <map>
#include <typeinfo>

#include "one/desktop/ui.hpp"
#include "one/desktop/icons.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace detail {

		double& layout_scale() {
			thread_local double scale = 1;
			return scale;
		}

		const theme*& theme_in_use() {
			thread_local const theme* current = nullptr;
			return current;
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

	}  // namespace detail

	const theme& current_theme() {
		static const theme harbor = theme::harbor();
		const theme* t = detail::theme_in_use();
		return t ? *t : harbor;
	}

	// element

	rect element::inner() const {
		return {frame.x + padding.left, frame.y + padding.top, std::max(0.f, frame.width - padding.left - padding.right),
		        std::max(0.f, frame.height - padding.top - padding.bottom)};
	}

	extent element::measure(float width) {
		float across = padding.left + padding.right, down = padding.top + padding.bottom;
		float given = fixed_width.value_or(width);
		extent content = measure_content(std::max(0.f, given - across));
		return {fixed_width.value_or(content.width + across), fixed_height.value_or(content.height + down)};
	}

	void element::place(const rect& area) {
		frame = area;
		place_content(inner());
	}

	void element::paint(canvas& c) {
		if (background.fill.alpha > 0 || background.border_width > 0) c.box(frame, background);
		paint_content(c);
	}

	extent element::measure_content(float width) {
		extent out;
		for (auto& child : children) {
			extent e = child->measure(width);
			out.width = std::max(out.width, e.width);
			out.height = std::max(out.height, e.height);
		}
		return out;
	}

	void element::place_content(const rect& inner_area) {
		for (auto& child : children) child->place(inner_area);
	}

	void element::paint_content(canvas& c) {
		for (auto& child : children) child->paint(c);
	}

	bool element::handle_key(const key_event&) { return false; }

	bool element::click(point) {
		if (!on_click) return false;
		on_click();
		return true;
	}

	bool element::wheel(point, float, float) { return false; }

	element* element::hit(point at) {
		if (!frame.contains(at)) return nullptr;
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			if (auto* found = (*it)->hit(at)) return found;
		}
		return this;
	}

	void element::adopt(element& before) {
		carried = true;
		focused = before.focused;
		focused_by_default = before.focused_by_default;
		focused_by_pointer = before.focused_by_pointer;
		brought = before.brought;
	}

	void element::reveal(const rect& area) {
		for (element* e = parent; e; e = e->parent) {
			if (e->reveal_here(area)) return;
		}
	}

	void element::bring_to_top() {
		for (element* e = parent; e; e = e->parent) {
			if (e->top_here(frame)) return;
		}
	}

	namespace {

		// Each child's parent, once a tree is put together.
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

		// text

		class text_element : public element {
		public:
			explicit text_element(std::vector<text_span> spans) : spans_(std::move(spans)) {}

			extent measure_content(float width) override {
				const text_layout& laid = laid_out(width);
				return {laid.width(), laid.height()};
			}
			void place_content(const rect& area) override { laid_out(area.width); }
			void paint_content(canvas& c) override {
				rect area = inner();
				c.text(laid_out(area.width), {area.x, area.y});
			}

		private:
			const text_layout& laid_out(float width) {
				double scale = detail::layout_scale();
				if (!laid_ || width != width_ || scale != scale_) {
					laid_ = lay_out(spans_, width, scale);
					width_ = width;
					scale_ = scale;
				}
				return *laid_;
			}
			std::vector<text_span> spans_;
			std::optional<text_layout> laid_;
			float width_ = -1;
			double scale_ = 0;
		};

		// rows and columns

		class stack_element : public element {
		public:
			explicit stack_element(bool across) : across_(across) {}

			extent measure_content(float width) override {
				if (!across_) {
					extent out;
					for (std::size_t i = 0; i < children.size(); ++i) {
						extent e = children[i]->measure(width);
						out.width = std::max(out.width, e.width);
						out.height += e.height + (i ? spacing : 0);
					}
					return out;
				}
				auto widths = widths_within(width);
				extent out;
				for (std::size_t i = 0; i < children.size(); ++i) {
					extent e = children[i]->measure(widths[i]);
					out.height = std::max(out.height, e.height);
					out.width += widths[i] + (i ? spacing : 0);
				}
				return out;
			}

			void place_content(const rect& area) override {
				if (across_) {
					auto widths = widths_within(area.width);
					float x = area.x;
					for (std::size_t i = 0; i < children.size(); ++i) {
						auto& child = *children[i];
						bool fills = cross == align::stretch || child.fills_across;
						float height = fills ? area.height : std::min(area.height, child.measure(widths[i]).height);
						float y = fills ? area.y : cross == align::center ? area.y + (area.height - height) / 2 : cross == align::end ? area.bottom() - height : area.y;
						child.place({x, y, widths[i], height});
						x += widths[i] + spacing;
					}
					return;
				}
				std::vector<float> heights;
				float used = 0, shares = 0;
				for (std::size_t i = 0; i < children.size(); ++i) {
					heights.push_back(children[i]->measure(area.width).height);
					used += heights.back() + (i ? spacing : 0);
					shares += children[i]->grow;
				}
				float left = area.height - used;
				if (shares > 0 && left > 0) {
					for (std::size_t i = 0; i < children.size(); ++i) heights[i] += left * children[i]->grow / shares;
				}
				float y = area.y;
				for (std::size_t i = 0; i < children.size(); ++i) {
					auto& child = *children[i];
					float width = area.width;
					if (cross != align::stretch && !child.fills_across) width = std::min(area.width, child.measure(area.width).width);
					float x = cross == align::center ? area.x + (area.width - width) / 2 : cross == align::end ? area.right() - width : area.x;
					child.place({x, y, width, heights[i]});
					y += heights[i] + spacing;
				}
			}

		private:
			// Across: each child that doesn't grow at the width it wants, in what's left,
			// and those that grow sharing the rest.
			std::vector<float> widths_within(float width) {
				std::vector<float> widths(children.size(), 0);
				float left = width - spacing * static_cast<float>(children.empty() ? 0 : children.size() - 1), shares = 0;
				for (std::size_t i = 0; i < children.size(); ++i) {
					if (children[i]->grow > 0) {
						shares += children[i]->grow;
						continue;
					}
					widths[i] = std::min(std::max(0.f, left), children[i]->measure(std::max(0.f, left)).width);
					left -= widths[i];
				}
				for (std::size_t i = 0; i < children.size(); ++i) {
					if (children[i]->grow > 0) widths[i] = std::max(0.f, left) * children[i]->grow / shares;
				}
				return widths;
			}
			bool across_;
		};

		// Children left to right at the widths they want, going on to a new line, as
		// words do, when the next doesn't fit; each line as tall as its tallest.
		class flow_element : public element {
		public:
			extent measure_content(float width) override {
				extent out;
				for (const auto& line : lines(width)) {
					out.width = std::max(out.width, line.width);
					out.height += line.height + (out.height > 0 ? spacing : 0);
				}
				return out;
			}

			void place_content(const rect& area) override {
				float y = area.y;
				for (const auto& line : lines(area.width)) {
					float x = area.x;
					for (std::size_t i = line.first; i < line.last; ++i) {
						extent e = children[i]->measure(area.width);
						float width = std::min(e.width, area.width);
						children[i]->place({x, y + (line.height - e.height) / 2, width, e.height});
						x += width + spacing;
					}
					y += line.height + spacing;
				}
			}

		private:
			struct line {
				std::size_t first = 0, last = 0;
				float width = 0, height = 0;
			};
			std::vector<line> lines(float width) {
				std::vector<line> out;
				line now;
				for (std::size_t i = 0; i < children.size(); ++i) {
					extent e = children[i]->measure(width);
					float w = std::min(e.width, width);
					if (now.last > now.first && now.width + spacing + w > width) {
						out.push_back(now);
						now = {i, i, 0, 0};
					}
					now.width += (now.last > now.first ? spacing : 0) + w;
					now.height = std::max(now.height, e.height);
					now.last = i + 1;
				}
				if (now.last > now.first) out.push_back(now);
				return out;
			}
		};

		// scrolling

		class scroll_element : public element {
		public:
			explicit scroll_element(bool across) : across_(across) { grow = 1; }

			extent measure_content(float width) override {
				if (children.empty()) return {};
				extent e = children[0]->measure(across_ ? 1e7f : width);
				return across_ ? extent{std::min(width, e.width), e.height} : extent{e.width, 0};
			}

			void place_content(const rect& area) override {
				viewport_ = area;
				if (children.empty()) return;
				auto& content = *children[0];
				if (across_) {
					extent e = content.measure(1e7f);
					length_ = e.width;
					offset_ = std::clamp(offset_, 0.f, std::max(0.f, length_ - area.width));
					content.place({area.x - offset_, area.y, std::max(length_, area.width), area.height});
				} else {
					// Content of a fixed width narrower than the scroll is centered in it, as a
					// page of text is, when the scroll aligns its content to the center.
					float width = content.fixed_width ? std::min(*content.fixed_width, area.width) : area.width;
					float x = cross == align::center ? area.x + (area.width - width) / 2 : area.x;
					extent e = content.measure(width);
					length_ = e.height;
					offset_ = std::clamp(offset_, 0.f, std::max(0.f, length_ - area.height));
					content.place({x, area.y - offset_, width, std::max(length_, area.height)});
				}
			}

			void paint_content(canvas& c) override {
				auto clipped = c.clip(viewport_);
				element::paint_content(c);
				// Where it's scrolled to, while there's more than fits.
				float visible = across_ ? viewport_.width : viewport_.height;
				if (length_ <= visible + 0.5f) return;
				float bar = std::max(24.f, visible * visible / length_);
				float at = (visible - bar) * offset_ / (length_ - visible);
				color tint = current_theme().muted;
				tint.alpha = 0.35f;
				if (across_) c.box({viewport_.x + at, viewport_.bottom() - 5, bar, 3}, {.fill = tint, .radius = 1.5f});
				else c.box({viewport_.right() - 5, viewport_.y + at, 3, bar}, {.fill = tint, .radius = 1.5f});
			}

			element* hit(point at) override { return viewport_.contains(at) ? element::hit(at) : nullptr; }

			// A scroll given the keyboard, as a page of text is, scrolls with it.
			bool focusable() const override { return wants_focus; }
			bool handle_key(const key_event& e) override {
				// Only its own keys, not those of what's inside it, like a list's.
				if (!focused) return false;
				float visible = across_ ? viewport_.width : viewport_.height;
				float by = 0;
				bool forward = across_ ? (e.is(keys::right) || e.is('l')) : (e.is(keys::down) || e.is('j'));
				bool backward = across_ ? (e.is(keys::left) || e.is('h')) : (e.is(keys::up) || e.is('k'));
				if (forward) by = 48;
				else if (backward) by = -48;
				else if (e.is(keys::page_down) || e.is(keys::space)) by = visible - 48;
				else if (e.is(keys::page_up)) by = -(visible - 48);
				else if (e.is(keys::home) || e.is('g')) by = -offset_;
				else if (e.is(keys::end) || e.is('G')) by = length_;
				else return false;
				return across_ ? wheel({}, by, 0) : wheel({}, 0, by);
			}

			bool wheel(point, float dx, float dy) override {
				float delta = across_ ? (dx != 0 ? dx : dy) : dy;
				float limit = std::max(0.f, length_ - (across_ ? viewport_.width : viewport_.height));
				float moved = std::clamp(offset_ + delta, 0.f, limit);
				if (moved == offset_) return false;
				offset_ = moved;
				place_content(viewport_);
				return true;
			}

			void adopt(element& before) override {
				element::adopt(before);
				offset_ = static_cast<scroll_element&>(before).offset_;
			}

		protected:
			bool reveal_here(const rect& area) override {
				float start = across_ ? area.x - viewport_.x : area.y - viewport_.y;
				float size = across_ ? area.width : area.height, visible = across_ ? viewport_.width : viewport_.height;
				float before = offset_;
				if (start < 0) offset_ += start - 8;
				else if (start + size > visible) offset_ += start + size - visible + 8;
				if (offset_ != before) place_content(viewport_);
				return true;
			}

			bool top_here(const rect& area) override {
				float start = across_ ? area.x - viewport_.x : area.y - viewport_.y;
				float visible = across_ ? viewport_.width : viewport_.height;
				offset_ = std::clamp(offset_ + start - 8, 0.f, std::max(0.f, length_ - visible));
				place_content(viewport_);
				return true;
			}

		private:
			bool across_;
			rect viewport_;
			float offset_ = 0, length_ = 0;
		};

		// lists

		class list_element : public element {
		public:
			list_element(std::vector<std::string> keys, std::function<void(const std::string&)> on_open) : keys_(std::move(keys)), open_(std::move(on_open)) {}

			bool focusable() const override { return !keys_.empty(); }
			bool prefers_focus() const override { return true; }
			one::desktop::cursor cursor() const override { return open_ ? cursor::pointer : cursor::normal; }

			extent measure_content(float width) override {
				extent out;
				for (auto& child : children) {
					extent e = child->measure(width);
					out.width = std::max(out.width, e.width);
					out.height += e.height;
				}
				return out;
			}
			void place_content(const rect& area) override {
				float y = area.y;
				for (auto& child : children) {
					float h = child->measure(area.width).height;
					child->place({area.x, y, area.width, h});
					y += h;
				}
				// The chosen row moved, as when the list is turned around: it's brought
				// back into view.
				if (std::exchange(moved_, false) && !children.empty()) {
					auto& shown = *children[index_of(chosen_)];
					shown.reveal(shown.frame);
				}
			}

			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				std::size_t chosen = index_of(chosen_);
				for (std::size_t i = 0; i < children.size(); ++i) {
					const rect& r = children[i]->frame;
					// The chosen row is shown while the list has the keyboard; otherwise only
					// the row under the pointer is.
					if (i == chosen && focused) {
						color tint = t.accent;
						tint.alpha = 0.12f;
						c.box(r, {.fill = tint, .radius = 6});
						c.box({r.x, r.y + 4, 3, r.height - 8}, {.fill = t.accent, .radius = 1.5f});
					} else if (keys_[i] == hovered_) {
						c.box(r, {.fill = t.sunken, .radius = 6});
					}
				}
				element::paint_content(c);
				// A chosen row that's a card of its own, with a background, is outlined, as
				// the tint behind it is mostly hidden.
				if (focused && chosen < children.size() && !children[chosen]->children.empty()) {
					const element& card = *children[chosen]->children.front();
					if (card.background.fill.alpha > 0)
						c.box(card.frame.inset(-2), {.border = t.accent, .border_width = 2, .radius = card.background.radius + 2});
				}
			}

			bool handle_key(const key_event& e) override {
				if (e.alt || e.control || e.super) return false;  // for what the list is given to do with them, like moving a row
				if (keys_.empty()) return false;
				std::size_t at = index_of(chosen_), last = keys_.size() - 1;
				std::size_t to = at;
				// Past either end, the key is left for what's beyond, like the next list.
				if (e.is(keys::down) || e.is('j')) {
					if (at == last) return false;
					to = at + 1;
				} else if (e.is(keys::up) || e.is('k')) {
					if (at == 0) return false;
					to = at - 1;
				}
				else if (e.is(keys::home) || e.is('g')) to = 0;
				else if (e.is(keys::end) || e.is('G')) to = last;
				else if (e.is(keys::page_down)) to = std::min(at + 10, last);
				else if (e.is(keys::page_up)) to = at < 10 ? 0 : at - 10;
				else if ((e.is(keys::enter) || e.is(keys::keypad_enter)) && open_) {
					open_(keys_[at]);
					return true;
				} else {
					return false;
				}
				choose(to);
				return true;
			}

			bool click(point at) override {
				for (std::size_t i = 0; i < children.size(); ++i) {
					if (children[i]->frame.contains(at)) {
						choose(i);
						if (open_) open_(keys_[i]);
						return true;
					}
				}
				return false;
			}

			// Which row the pointer is over, for the hover tint.
			void pointing(std::optional<point> at) {
				hovered_.clear();
				if (!at) return;
				for (std::size_t i = 0; i < children.size(); ++i) {
					if (children[i]->frame.contains(*at)) hovered_ = keys_[i];
				}
			}

			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<list_element&>(before);
				// The same row, if it's still here; otherwise the one now where it was.
				if (std::ranges::contains(keys_, was.chosen_)) chosen_ = was.chosen_;
				else if (!keys_.empty()) chosen_ = keys_[std::min(was.index_of(was.chosen_), keys_.size() - 1)];
				moved_ = !keys_.empty() && index_of(chosen_) != was.index_of(was.chosen_);
				hovered_ = was.hovered_;
			}

			const std::string& chosen() const { return chosen_; }
			// The row shown as chosen: the first, until one's chosen.
			std::string shown_chosen() const { return keys_.empty() ? "" : keys_[index_of(chosen_)]; }
			void start_at(const std::string& key) {
				if (std::ranges::contains(keys_, key)) chosen_ = key;
			}

		private:
			std::size_t index_of(const std::string& key) const {
				auto it = std::ranges::find(keys_, key);
				return it == keys_.end() ? 0 : static_cast<std::size_t>(it - keys_.begin());
			}
			void choose(std::size_t i) {
				chosen_ = keys_[i];
				children[i]->reveal(children[i]->frame);
			}

			std::vector<std::string> keys_;
			std::function<void(const std::string&)> open_;
			std::string chosen_, hovered_;
			bool moved_ = false;
		};

		// buttons and small things

		class button_element : public element {
		public:
			button_element(std::string label, std::function<void()> pressed, bool primary, std::string icon = "")
			    : label_(std::move(label)), pressed_(std::move(pressed)), primary_(primary), icon_(has_icon(icon) ? std::move(icon) : std::string()) {
				padding = icon_.empty() ? edges(8, 16) : edges(8, 9);
			}
			// A toolbar's, flat, large or small.
			button_element(std::string label, std::function<void()> pressed, std::string icon, tool_options tool)
			    : button_element(std::move(label), std::move(pressed), false, std::move(icon)) {
				tool_ = tool;
				flat_ = true;
				padding = tool.large ? edges(6, 8) : edges(7, 7);
			}

			bool focusable() const override { return !tool_.off; }
			one::desktop::cursor cursor() const override { return tool_.off ? cursor::normal : cursor::pointer; }

			extent measure_content(float) override {
				const auto& laid = laid_out();
				if (flat_ && tool_.large) return {std::max({44.f, small_words().width(), large_icon()}), large_icon() + 4 + small_words().height()};
				// A small one with no icon is its words, as a scale like 1.5× is; with one or
				// without, as tall as its words would be, so every small one is.
				if (flat_ && tool_.swatch) return {18, small_height()};
				if (flat_ && icon_.empty()) return {std::max(18.f, small_words().width()), small_height()};
				if (flat_) return {18, small_height()};
				// Drawn as an icon, as tall as its words would be, so it lines up with the buttons beside it.
				if (!icon_.empty()) return {16, laid.height()};
				return {laid.width(), laid.height()};
			}
			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				if (flat_) {
					paint_tool(c, t);
					return;
				}
				if (!icon_.empty()) {
					paint_icon(c, t);
					return;
				}
				color fill = primary_ ? t.accent : t.surface;
				if (hovered) fill = primary_ ? mixed(t.accent, t.ink, 0.15f) : t.sunken;
				c.box(frame, {.fill = fill, .border = primary_ ? fill : t.line, .border_width = 1, .radius = std::min(t.corners, 8.f)});
				// The ring once the person moves to it, not where the window put the keyboard first.
				if (shows_focus()) c.box(frame.inset(-3), {.border = mixed(t.accent, t.page, 0.4f), .border_width = 2, .radius = std::min(t.corners, 8.f) + 3});
				rect area = inner();
				const auto& laid = laid_out();
				c.text(laid, {area.x + (area.width - laid.width()) / 2, area.y + (area.height - laid.height()) / 2});
			}
			bool handle_key(const key_event& e) override {
				if (e.is(keys::enter) || e.is(keys::space) || e.is(keys::keypad_enter)) {
					if (pressed_ && !tool_.off) pressed_();
					return true;
				}
				return false;
			}
			bool click(point) override {
				if (pressed_ && !tool_.off) pressed_();
				return true;
			}

		private:
			static color mixed(color a, color b, float by) {
				return {a.red + (b.red - a.red) * by, a.green + (b.green - a.green) * by, a.blue + (b.blue - a.blue) * by, a.alpha};
			}
			// Flat, tinted while it's on and shaded while it's pointed at: a large one's icon
			// over its words, a small one's alone, its words under it while pointed at.
			void paint_tool(canvas& c, const theme& t) {
				float corners = std::min(t.corners, 6.f);
				color tint = mixed(t.surface, t.accent, 0.16f);
				if (tool_.on) c.box(frame, {.fill = hovered ? mixed(tint, t.ink, 0.06f) : tint, .border = mixed(t.surface, t.accent, 0.45f), .border_width = 1, .radius = corners});
				else if (hovered && !tool_.off) c.box(frame, {.fill = t.sunken, .radius = corners});
				if (shows_focus()) c.box(frame.inset(-2), {.border = mixed(t.accent, t.page, 0.4f), .border_width = 2, .radius = corners + 2});
				rect area = inner();
				// One that doesn't apply now is faint, as a disabled button is.
				color ink = tool_.on ? t.accent : t.ink;
				if (tool_.off) ink = mixed(t.surface, ink, 0.35f);
				if (tool_.large) {
					float size = large_icon();
					draw_icon(c, icon_, {area.x + (area.width - size) / 2, area.y, size, size}, ink);
					const auto& words = small_words();
					c.text(words, {area.x + (area.width - words.width()) / 2, area.y + size + 4});
					return;
				}
				if (tool_.swatch) {
					color fill = *tool_.swatch;
					if (tool_.off) fill.alpha *= 0.35f;
					c.box({area.x + (area.width - 16) / 2, area.y + (area.height - 16) / 2, 16, 16}, {.fill = fill, .border = t.line, .border_width = 1, .radius = 8});
					if (hovered) tooltip(c, t);
					return;
				}
				if (icon_.empty()) {
					text_style s = t.quiet();
					s.color = ink;
					s.weight = tool_.on ? 600 : 400;
					auto words = lay_out({label_, s}, 0, detail::layout_scale());
					c.text(words, {area.x + (area.width - words.width()) / 2, area.y + (area.height - words.height()) / 2});
					return;
				}
				draw_icon(c, icon_, {area.x + (area.width - 18) / 2, area.y + (area.height - 18) / 2, 18, 18}, ink);
				if (hovered) tooltip(c, t);
			}
			// Its words in a dark tag under it, over whatever's there.
			void tooltip(canvas& c, const theme& t) {
				text_style s = t.quiet();
				s.color = t.page;
				auto named = lay_out({label_, s}, 0, detail::layout_scale());
				rect under{frame.x + (frame.width - named.width()) / 2 - 8, frame.bottom() + 6, named.width() + 16, named.height() + 8};
				color behind = t.ink;
				c.later([named = std::move(named), under, behind](canvas& on) {
					on.box(under, {.fill = behind, .radius = 6});
					on.text(named, {under.x + 8, under.y + 4});
				});
			}
			// A small tool's height inside its padding, and a large one's icon: as big as
			// makes a large tool as tall as two small ones stacked, as a group of them is,
			// so a toolbar stays as tall whichever groups it shows.
			float small_height() { return std::max(18.f, small_words().height()); }
			float large_icon() { return 2 * (small_height() + 14) + 2 - 12 - 4 - small_words().height(); }

			const text_layout& small_words() {
				double scale = detail::layout_scale();
				if (!small_ || scale != small_scale_) {
					text_style s = current_theme().quiet();
					s.color = tool_.off ? mixed(current_theme().surface, current_theme().ink, 0.35f) : current_theme().ink;
					small_ = lay_out({label_, s}, 0, scale);
					small_scale_ = scale;
				}
				return *small_;
			}
			// Its icon in place of its words, which show under it while it's pointed at.
			void paint_icon(canvas& c, const theme& t) {
				color fill = primary_ ? t.accent : t.surface;
				if (hovered) fill = primary_ ? mixed(t.accent, t.ink, 0.15f) : t.sunken;
				c.box(frame, {.fill = fill, .border = primary_ ? fill : t.line, .border_width = 1, .radius = std::min(t.corners, 8.f)});
				if (shows_focus()) c.box(frame.inset(-3), {.border = mixed(t.accent, t.page, 0.4f), .border_width = 2, .radius = std::min(t.corners, 8.f) + 3});
				rect area = inner();
				draw_icon(c, icon_, {area.x + (area.width - 16) / 2, area.y + (area.height - 16) / 2, 16, 16}, primary_ ? color::hex(0xffffff) : t.ink);
				if (hovered || (shows_focus())) tooltip(c, t);
			}
			const text_layout& laid_out() {
				double scale = detail::layout_scale();
				if (!laid_ || scale != scale_) {
					text_style s = current_theme().strong();
					if (primary_) s.color = color::hex(0xffffff);
					laid_ = lay_out({label_, s}, 0, scale);
					scale_ = scale;
				}
				return *laid_;
			}
			std::string label_;
			std::function<void()> pressed_;
			bool primary_;
			std::string icon_;
			std::optional<text_layout> laid_;
			double scale_ = 0;
			bool flat_ = false;
			tool_options tool_;
			std::optional<text_layout> small_;
			double small_scale_ = 0;
		};

		class link_element : public element {
		public:
			link_element(std::string words, std::function<void()> go, std::optional<text_style> style)
			    : words_(std::move(words)), go_(std::move(go)), style_(std::move(style)) {}

			bool focusable() const override { return true; }
			one::desktop::cursor cursor() const override { return cursor::pointer; }
			extent measure_content(float width) override {
				const auto& laid = laid_out(width);
				return {laid.width(), laid.height()};
			}
			void place_content(const rect& area) override { laid_out(area.width); }
			void paint_content(canvas& c) override {
				rect area = inner();
				if (shows_focus()) c.box(frame.inset(-2), {.border = current_theme().accent, .border_width = 1.5f, .radius = 3});
				c.text(laid_out(area.width), {area.x, area.y});
			}
			bool handle_key(const key_event& e) override {
				if (!e.is(keys::enter) && !e.is(keys::keypad_enter)) return false;
				if (go_) go_();
				return true;
			}
			bool click(point) override {
				if (go_) go_();
				return true;
			}

		private:
			const text_layout& laid_out(float width) {
				double scale = detail::layout_scale();
				bool underlined = hovered || focused;
				if (!laid_ || width != width_ || scale != scale_ || underlined != underlined_) {
					text_style s = style_.value_or(current_theme().body());
					if (!style_) s.color = current_theme().accent;
					s.underline = underlined;
					laid_ = lay_out({words_, s}, width, scale);
					width_ = width;
					scale_ = scale;
					underlined_ = underlined;
				}
				return *laid_;
			}
			std::string words_;
			std::function<void()> go_;
			std::optional<text_style> style_;
			std::optional<text_layout> laid_;
			float width_ = -1;
			double scale_ = 0;
			bool underlined_ = false;
		};

		class edge_element : public element {
		public:
			edge_element(color c, float width) : color_(c) {
				fixed_width = width;
				fills_across = true;
			}
			extent measure_content(float) override { return {0, 0}; }
			void paint_content(canvas& c) override { c.box(frame, {.fill = color_, .radius = frame.width / 2}); }

		private:
			color color_;
		};

		// One of the icons, alone, in a color: beside a front page's section's title.
		class icon_element : public element {
		public:
			icon_element(std::string name, color tint, float size) : name_(std::move(name)), tint_(tint) { fixed_width = fixed_height = size; }
			extent measure_content(float) override { return {0, 0}; }
			void paint_content(canvas& c) override { draw_icon(c, name_, frame, tint_); }

		private:
			std::string name_;
			color tint_;
		};

		class avatar_element : public element {
		public:
			avatar_element(const std::string& name, float size) {
				fixed_width = fixed_height = size;
				// A color of theirs, from their name, among ones white text reads on.
				static constexpr std::uint32_t tones[] = {0x2457d6, 0x1a7f37, 0x8250df, 0xbf3989, 0x9a6700, 0x0a7ea4, 0xc2312d, 0x57606a};
				std::size_t hash = std::hash<std::string>{}(name);
				tone_ = color::hex(tones[hash % std::size(tones)]);
				std::size_t i = 0;
				while (i < name.size() && name[i] == ' ') ++i;
				std::size_t length = i < name.size() ? (static_cast<unsigned char>(name[i]) < 0x80 ? 1 : static_cast<unsigned char>(name[i]) < 0xe0 ? 2 : static_cast<unsigned char>(name[i]) < 0xf0 ? 3 : 4) : 0;
				initial_ = name.substr(i, length);
				if (initial_.size() == 1) initial_[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(initial_[0])));
			}
			extent measure_content(float) override { return {0, 0}; }
			void paint_content(canvas& c) override {
				c.box(frame, {.fill = tone_, .radius = frame.width / 2});
				text_style s = current_theme().strong();
				s.size = frame.height * 0.48f;
				s.color = color::hex(0xffffff);
				auto laid = lay_out({initial_, s}, 0, detail::layout_scale());
				c.text(laid, {frame.x + (frame.width - laid.width()) / 2, frame.y + (frame.height - laid.height()) / 2});
			}

		private:
			color tone_;
			std::string initial_;
		};

		// A run of numbers over time, newest at the right, as btop draws one: a soft fill
		// under a line in the accent, over a quiet floor and, when large, a grid.
		class drawing_element : public element {
		public:
			drawing_element(std::function<void(canvas&, rect)> paint, drawing_pointer pointer) : paint_(std::move(paint)), pointer_(std::move(pointer)) {
				fills_across = true;
			}
			extent measure_content(float) override { return {0, 0}; }
			void paint_content(canvas& c) override {
				if (paint_) paint_(c, frame);
			}
			// One that hears presses takes the keyboard when it's pressed, as a drawing
			// app's canvas does, so keys go to it, not to a button pressed before.
			bool focusable() const override { return static_cast<bool>(pointer_.on_press); }
			bool press(point at, int clicks) override { return pointer_.on_press && pointer_.on_press(at, clicks); }
			void drag(point at) override {
				if (pointer_.on_drag) pointer_.on_drag(at);
			}
			void release(point at, element*) override {
				if (pointer_.on_release) pointer_.on_release(at);
			}
			// Where the pointer is, as it's found over this, for the cursor it shows there.
			element* hit(point at) override {
				element* found = element::hit(at);
				if (found == this) over_ = at;
				return found;
			}
			one::desktop::cursor cursor() const override { return pointer_.cursor ? pointer_.cursor(over_) : cursor::normal; }

		private:
			std::function<void(canvas&, rect)> paint_;
			drawing_pointer pointer_;
			point over_;
		};

		class graph_element : public element {
		public:
			graph_element(std::vector<double> values, double top, float height, bool large) : values_(std::move(values)), top_(top > 0 ? top : 1), large_(large) {
				fixed_height = height;
				fills_across = true;
			}
			extent measure_content(float) override { return {0, 0}; }
			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				rect a = frame;
				color quiet = t.line;
				if (large_) {
					for (float at : {0.25f, 0.5f, 0.75f}) c.box({a.x, a.y + a.height * at, a.width, 1}, {.fill = {quiet.red, quiet.green, quiet.blue, quiet.alpha * 0.6f}});
				}
				c.box({a.x, a.bottom() - 1, a.width, 1}, {.fill = quiet});
				if (values_.empty()) return;
				// At least sixty places, so a run just begun grows leftward from the right.
				std::size_t places = std::max<std::size_t>(values_.size(), 60);
				auto x_of = [&](std::size_t i) { return a.x + static_cast<float>(places - values_.size() + i) / static_cast<float>(places - 1) * a.width; };
				auto y_of = [&](double v) { return a.bottom() - 1 - static_cast<float>(std::clamp(v, 0.0, top_) / top_) * (a.height - 2); };
				std::vector<point> line;
				for (std::size_t i = 0; i < values_.size(); ++i) line.push_back({x_of(i), y_of(values_[i])});
				if (line.size() > 1) {
					std::vector<point> area = line;
					area.push_back({line.back().x, a.bottom() - 1});
					area.push_back({line.front().x, a.bottom() - 1});
					color soft = t.accent;
					soft.alpha *= 0.16f;
					c.fill(area, soft);
				}
				for (std::size_t i = 0; i + 1 < line.size(); ++i) c.line(line[i], line[i + 1], large_ ? 2.f : 1.5f, t.accent);
				if (line.size() == 1) c.box({line[0].x - 1.5f, line[0].y - 1.5f, 3, 3}, {.fill = t.accent, .radius = 1.5f});
			}

		private:
			std::vector<double> values_;
			double top_;
			bool large_;
		};

		class rule_element : public element {
		public:
			rule_element() {
				fixed_height = 1;
				fills_across = true;
			}
			extent measure_content(float) override { return {0, 0}; }
			void paint_content(canvas& c) override { c.box(frame, {.fill = current_theme().line}); }
		};

		template <class E, class... A>
		view made(A&&... arguments) {
			return view(std::make_unique<E>(std::forward<A>(arguments)...));
		}

	}  // namespace

	view text(std::string words, std::optional<text_style> style) {
		return made<text_element>(std::vector<text_span>{{std::move(words), style.value_or(current_theme().body())}});
	}

	view text(std::vector<text_span> spans) { return made<text_element>(std::move(spans)); }

	view column(std::vector<view> children) {
		auto e = std::make_unique<stack_element>(false);
		e->children = taken(children);
		adopt_children(*e);
		return view(std::move(e));
	}

	view row(std::vector<view> children) {
		auto e = std::make_unique<stack_element>(true);
		e->cross = align::center;
		e->children = taken(children);
		adopt_children(*e);
		return view(std::move(e));
	}

	view flow(std::vector<view> children) {
		auto e = std::make_unique<flow_element>();
		e->children = taken(children);
		adopt_children(*e);
		return view(std::move(e));
	}

	view scroll(view content) {
		auto e = std::make_unique<scroll_element>(false);
		if (content.get()) e->children.push_back(std::move(content).take());
		adopt_children(*e);
		return view(std::move(e));
	}

	view scroll_across(view content) {
		auto e = std::make_unique<scroll_element>(true);
		if (content.get()) e->children.push_back(std::move(content).take());
		adopt_children(*e);
		return view(std::move(e));
	}

	view list(std::vector<list_row> rows, std::function<void(const std::string&)> on_open, std::string start) {
		return list(std::move(rows), list_options{std::move(on_open), std::move(start), {}});
	}

	view list(std::vector<list_row> rows, list_options options) {
		auto on_open = std::move(options.on_open);
		auto start = std::move(options.start);
		std::vector<std::string> keys;
		std::vector<std::unique_ptr<element>> shown;
		for (auto& r : rows) {
			if (!r.shown.get()) continue;
			keys.push_back(r.key);
			// Each row in a cell of its own, the space around it the list's, so a row
			// with a background, like a card, keeps a gap to the next.
			auto cell = std::make_unique<element>();
			cell->key = r.key;
			cell->padding = edges(5, 8);
			cell->children.push_back(std::move(r.shown).take());
			adopt_children(*cell);
			shown.push_back(std::move(cell));
		}
		auto e = std::make_unique<list_element>(std::move(keys), std::move(on_open));
		e->start_at(start);
		if (options.on_row_key) {
			list_element* self = e.get();
			e->on_key = [self, heard = std::move(options.on_row_key)](const key_event& k) { return !self->shown_chosen().empty() && heard(self->shown_chosen(), k); };
		}
		e->children = std::move(shown);
		adopt_children(*e);
		return view(std::move(e));
	}

	view button(std::string label, std::function<void()> pressed, bool primary) {
		return made<button_element>(std::move(label), std::move(pressed), primary);
	}
	view icon_button(std::string icon, std::string label, std::function<void()> pressed, bool primary) {
		return made<button_element>(std::move(label), std::move(pressed), primary, std::move(icon));
	}
	view tool_button(std::string icon, std::string label, std::function<void()> pressed, tool_options options) {
		return made<button_element>(std::move(label), std::move(pressed), std::move(icon), options);
	}
	view link(std::string words, std::function<void()> go, std::optional<text_style> style) {
		return made<link_element>(std::move(words), std::move(go), std::move(style));
	}
	view edge(color c, float width) { return made<edge_element>(c, width); }
	view icon(std::string name, color tint, float size) { return made<icon_element>(std::move(name), tint, size); }
	view avatar(const std::string& name, float size) { return made<avatar_element>(name, size); }
	view graph(std::vector<double> values, double top, float height, bool large) { return made<graph_element>(std::move(values), top, height, large); }
	view drawing(std::function<void(canvas&, rect)> paint, drawing_pointer pointer) { return made<drawing_element>(std::move(paint), std::move(pointer)); }
	view divider() { return made<rule_element>(); }
	view spacer() { return std::move(made<element>()).grow(1); }

	// For a list under the pointer: which row it's over.
	void point_at_list(element& e, std::optional<point> at) {
		if (auto* l = dynamic_cast<list_element*>(&e)) l->pointing(at);
	}

}  // namespace one::desktop
