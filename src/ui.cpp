// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cmath>
#include <map>
#include <typeinfo>

#include "loom/ui.hpp"
#include "ui_internal.hpp"

namespace loom {

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

	void element::adopt(element& before) { focused = before.focused; }

	void element::reveal(const rect& area) {
		for (element* e = parent; e; e = e->parent) {
			if (e->reveal_here(area)) return;
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
					extent e = content.measure(area.width);
					length_ = e.height;
					offset_ = std::clamp(offset_, 0.f, std::max(0.f, length_ - area.height));
					content.place({area.x, area.y - offset_, area.width, std::max(length_, area.height)});
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
			loom::cursor cursor() const override { return open_ ? cursor::pointer : cursor::normal; }

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
					if (i == chosen) {
						color tint = t.accent;
						tint.alpha = focused ? 0.12f : 0.07f;
						c.box(r, {.fill = tint, .radius = 6});
						if (focused) c.box({r.x, r.y + 4, 3, r.height - 8}, {.fill = t.accent, .radius = 1.5f});
					} else if (keys_[i] == hovered_) {
						c.box(r, {.fill = t.sunken, .radius = 6});
					}
				}
				element::paint_content(c);
			}

			bool handle_key(const key_event& e) override {
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
			button_element(std::string label, std::function<void()> pressed, bool primary)
			    : label_(std::move(label)), pressed_(std::move(pressed)), primary_(primary) {
				padding = edges(8, 16);
			}

			bool focusable() const override { return true; }
			loom::cursor cursor() const override { return cursor::pointer; }

			extent measure_content(float) override {
				const auto& laid = laid_out();
				return {laid.width(), laid.height()};
			}
			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				color fill = primary_ ? t.accent : t.surface;
				if (hovered) fill = primary_ ? mixed(t.accent, t.ink, 0.15f) : t.sunken;
				c.box(frame, {.fill = fill, .border = primary_ ? fill : t.line, .border_width = 1, .radius = std::min(t.corners, 8.f)});
				if (focused) c.box(frame.inset(-3), {.border = mixed(t.accent, t.page, 0.4f), .border_width = 2, .radius = std::min(t.corners, 8.f) + 3});
				rect area = inner();
				const auto& laid = laid_out();
				c.text(laid, {area.x + (area.width - laid.width()) / 2, area.y + (area.height - laid.height()) / 2});
			}
			bool handle_key(const key_event& e) override {
				if (e.is(keys::enter) || e.is(keys::space) || e.is(keys::keypad_enter)) {
					if (pressed_) pressed_();
					return true;
				}
				return false;
			}
			bool click(point) override {
				if (pressed_) pressed_();
				return true;
			}

		private:
			static color mixed(color a, color b, float by) {
				return {a.red + (b.red - a.red) * by, a.green + (b.green - a.green) * by, a.blue + (b.blue - a.blue) * by, a.alpha};
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
			std::optional<text_layout> laid_;
			double scale_ = 0;
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

	view list(std::vector<list_row> rows, std::function<void(const std::string&)> on_open) {
		std::vector<std::string> keys;
		std::vector<std::unique_ptr<element>> shown;
		for (auto& r : rows) {
			if (!r.shown.get()) continue;
			keys.push_back(r.key);
			auto e = std::move(r.shown).take();
			e->key = r.key;
			e->padding.top += 7, e->padding.bottom += 7, e->padding.left += 12, e->padding.right += 12;
			shown.push_back(std::move(e));
		}
		auto e = std::make_unique<list_element>(std::move(keys), std::move(on_open));
		e->children = std::move(shown);
		adopt_children(*e);
		return view(std::move(e));
	}

	view button(std::string label, std::function<void()> pressed, bool primary) {
		return made<button_element>(std::move(label), std::move(pressed), primary);
	}
	view edge(color c, float width) { return made<edge_element>(c, width); }
	view avatar(const std::string& name, float size) { return made<avatar_element>(name, size); }
	view divider() { return made<rule_element>(); }
	view spacer() { return std::move(made<element>()).grow(1); }

	// For a list under the pointer: which row it's over.
	void point_at_list(element& e, std::optional<point> at) {
		if (auto* l = dynamic_cast<list_element*>(&e)) l->pointing(at);
	}

}  // namespace loom
