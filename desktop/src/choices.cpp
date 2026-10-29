// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Choosing: a box ticked or not, one of several choices picked from a list that
// opens under it, one of a few with round buttons, and a number along a slider.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <set>

#include "one/desktop/ui.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		class toggle_element : public element {
		public:
			toggle_element(state<bool> on, std::string label) : on_(std::move(on)), label_(std::move(label)) { padding = edges(4, 2); }

			bool focusable() const override { return true; }
			one::desktop::cursor cursor() const override { return cursor::pointer; }

			extent measure_content(float) override {
				const auto& laid = laid_out();
				return {box + 8 + laid.width(), std::max(box, laid.height())};
			}
			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				rect area = inner();
				rect square{area.x, area.y + (area.height - box) / 2, box, box};
				bool ticked = on_.peek();
				c.box(square, {.fill = ticked ? t.accent : t.surface, .border = ticked ? t.accent : (hovered ? t.muted : t.line), .border_width = 1.5f, .radius = 4});
				if (ticked) {
					text_style mark = t.strong();
					mark.color = color::hex(0xffffff);
					mark.size = 12;
					auto laid = lay_out({"✓", mark}, 0, detail::layout_scale());
					c.text(laid, {square.x + (box - laid.width()) / 2, square.y + (box - laid.height()) / 2});
				}
				if (focused) c.box(square.inset(-3), {.border = t.accent, .border_width = 1.5f, .radius = 6});
				const auto& laid = laid_out();
				c.text(laid, {area.x + box + 8, area.y + (area.height - laid.height()) / 2});
			}
			bool handle_key(const key_event& e) override {
				if (!e.is(keys::space) && !e.is(keys::enter) && !e.is(keys::keypad_enter)) return false;
				on_.set(!on_.peek());
				return true;
			}
			bool click(point) override {
				on_.set(!on_.peek());
				return true;
			}

		private:
			static constexpr float box = 16;
			const text_layout& laid_out() {
				double scale = detail::layout_scale();
				if (!laid_ || scale != scale_) {
					laid_ = lay_out({label_, current_theme().body()}, 0, scale);
					scale_ = scale;
				}
				return *laid_;
			}
			state<bool> on_;
			std::string label_;
			std::optional<text_layout> laid_;
			double scale_ = 0;
		};

		// One of a few choices, side by side: one place for the keyboard, the arrows
		// moving what's chosen.
		class radio_element : public element {
		public:
			radio_element(state<std::string> chosen, std::vector<option> options, std::function<void(const std::string&)> on_pick)
			    : chosen_(std::move(chosen)), options_(std::move(options)), on_pick_(std::move(on_pick)) { padding = edges(4, 2); }

			bool focusable() const override { return true; }
			one::desktop::cursor cursor() const override { return cursor::pointer; }

			extent measure_content(float) override {
				float width = 0, height = dot;
				for (std::size_t i = 0; i < options_.size(); ++i) {
					const auto& laid = laid_out(i);
					width += (i ? gap : 0) + dot + 8 + laid.width();
					height = std::max(height, laid.height());
				}
				return {width, height};
			}
			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				rect area = inner();
				float x = area.x;
				std::string now = chosen_.peek();
				starts_.clear();
				for (std::size_t i = 0; i < options_.size(); ++i) {
					if (i) x += gap;
					starts_.push_back(x);
					bool picked = options_[i].value == now;
					rect circle{x, area.y + (area.height - dot) / 2, dot, dot};
					c.box(circle, {.fill = t.surface, .border = picked ? t.accent : (hovered ? t.muted : t.line), .border_width = 1.5f, .radius = dot / 2});
					if (picked) c.box(circle.inset(4), {.fill = t.accent, .radius = dot / 2 - 4});
					if (shows_focus() && (picked || (now.empty() && i == 0))) {
						c.box(circle.inset(-3), {.border = t.accent, .border_width = 1.5f, .radius = dot / 2 + 3});
					}
					const auto& laid = laid_out(i);
					c.text(laid, {x + dot + 8, area.y + (area.height - laid.height()) / 2});
					x += dot + 8 + laid.width();
				}
			}
			bool handle_key(const key_event& e) override {
				if (options_.empty()) return false;
				int at = picked_at();
				if (e.is(keys::right) || e.is(keys::down)) {
					pick(options_[at < 0 ? 0 : (at + 1) % options_.size()].value);
				} else if (e.is(keys::left) || e.is(keys::up)) {
					pick(options_[at <= 0 ? options_.size() - 1 : at - 1].value);
				} else if ((e.is(keys::space) || e.is(keys::enter) || e.is(keys::keypad_enter)) && at < 0) {
					pick(options_[0].value);
				} else {
					return false;
				}
				return true;
			}
			bool click(point p) override {
				for (std::size_t i = starts_.size(); i-- > 0;) {
					if (p.x >= starts_[i]) {
						pick(options_[i].value);
						return true;
					}
				}
				return false;
			}

		private:
			static constexpr float dot = 16, gap = 20;
			int picked_at() const {
				std::string now = chosen_.peek();
				for (std::size_t i = 0; i < options_.size(); ++i) {
					if (options_[i].value == now) return static_cast<int>(i);
				}
				return -1;
			}
			const text_layout& laid_out(std::size_t i) {
				double scale = detail::layout_scale();
				if (laid_.size() != options_.size() || scale != scale_) {
					laid_.clear();
					for (const auto& o : options_) laid_.push_back(lay_out({o.label, current_theme().body()}, 0, scale));
					scale_ = scale;
				}
				return laid_[i];
			}
			void pick(const std::string& value) {
				chosen_.set(value);
				if (on_pick_) on_pick_(value);
			}
			state<std::string> chosen_;
			std::vector<option> options_;
			std::function<void(const std::string&)> on_pick_;
			std::vector<text_layout> laid_;
			std::vector<float> starts_;  // where each choice was drawn, for a click
			double scale_ = 0;
		};

		// A number as it's written: whole when it is, and otherwise to as many places as its step has.
		std::string written(double n, double by) {
			int places = 0;
			while (places < 6 && std::abs(by * std::pow(10, places) - std::round(by * std::pow(10, places))) > 1e-9) ++places;
			std::string out = std::format("{:.{}f}", n, places);
			return out == "-0" ? "0" : out;
		}

		class slider_element : public element {
		public:
			slider_element(state<std::string> value, number_range range, std::function<void(const std::string&)> settled)
			    : value_(std::move(value)), range_(range), settled_(std::move(settled)) {
				padding = edges(4, 2);
			}

			bool focusable() const override { return true; }
			one::desktop::cursor cursor() const override { return cursor::pointer; }

			extent measure_content(float width) override {
				const auto& laid = number_laid();
				return {std::max(160.f, std::min(width, 320.f)), std::max(knob, laid.height())};
			}
			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				rect area = inner();
				const auto& laid = number_laid();
				rect track = track_of(area);
				float along = range_.to > range_.from ? static_cast<float>((now() - range_.from) / (range_.to - range_.from)) : 0;
				float x = track.x + along * track.width;
				c.box(track, {.fill = t.sunken, .radius = 2});
				c.box({track.x, track.y, x - track.x, track.height}, {.fill = t.accent, .radius = 2});
				rect handle{x - knob / 2, area.y + (area.height - knob) / 2, knob, knob};
				c.box(handle, {.fill = t.surface, .border = t.accent, .border_width = 2, .radius = knob / 2});
				if (shows_focus()) c.box(handle.inset(-3), {.border = t.accent, .border_width = 1.5f, .radius = knob / 2 + 3});
				c.text(laid, {area.right() - laid.width(), area.y + (area.height - laid.height()) / 2});
			}
			bool handle_key(const key_event& e) override {
				double steps = std::round((range_.to - range_.from) / range_.by), tenth = std::max(1.0, std::round(steps / 10));
				if (e.is(keys::right) || e.is(keys::up)) set(now() + range_.by);
				else if (e.is(keys::left) || e.is(keys::down)) set(now() - range_.by);
				else if (e.is(keys::page_up)) set(now() + tenth * range_.by);
				else if (e.is(keys::page_down)) set(now() - tenth * range_.by);
				else if (e.is(keys::home)) set(range_.from);
				else if (e.is(keys::end)) set(range_.to);
				else return false;
				if (settled_) settled_(value_.peek());
				return true;
			}
			bool press(point at, int) override {
				pressed_ = value_.peek();
				drag(at);
				return true;
			}
			void release(point, element*) override {
				if (settled_ && value_.peek() != pressed_) settled_(value_.peek());
			}
			void drag(point at) override {
				rect track = track_of(inner());
				if (track.width <= 0) return;
				float along = std::clamp((at.x - track.x) / track.width, 0.f, 1.f);
				set(range_.from + along * (range_.to - range_.from));
			}

		private:
			static constexpr float knob = 18;
			// The bar the knob goes along, in what it was given, the number after it.
			rect track_of(const rect& area) { return {area.x + knob / 2, area.y + (area.height - 4) / 2, std::max(0.f, area.width - widest() - 12 - knob), 4}; }
			// What it holds, or its least while that's not a number in its range.
			double now() const {
				const std::string& text = value_.peek();
				char* end = nullptr;
				double n = std::strtod(text.c_str(), &end);
				if (text.empty() || end == text.c_str()) return range_.from;
				return std::clamp(n, range_.from, range_.to);
			}
			// To the nearest step, within the range.
			void set(double n) {
				double steps = std::round((std::clamp(n, range_.from, range_.to) - range_.from) / range_.by);
				std::string text = written(std::min(range_.to, range_.from + steps * range_.by), range_.by);
				if (text != value_.peek()) value_.set(text);
			}
			// As wide as the widest number it shows, so the bar doesn't move as it changes.
			float widest() {
				double scale = detail::layout_scale();
				if (widest_ <= 0 || scale != widest_scale_) {
					widest_ = 0;
					for (double end : {range_.from, range_.to}) widest_ = std::max(widest_, lay_out({shown(end), current_theme().body()}, 0, scale).width());
					widest_scale_ = scale;
				}
				return widest_;
			}
			const text_layout& number_laid() {
				double scale = detail::layout_scale();
				std::string said = shown(now());
				if (!laid_ || scale != scale_ || said != shown_) {
					laid_ = lay_out({said, current_theme().body()}, 0, scale);
					scale_ = scale;
					shown_ = said;
				}
				return *laid_;
			}
			// As it's said beside the bar: written to its step, or as a percent of the way.
			std::string shown(double n) const {
				if (!range_.percent || range_.to <= range_.from) return written(n, range_.by);
				double of = range_.to - range_.from;
				return written((n - range_.from) / of * 100, range_.by / of * 100) + "%";
			}
			state<std::string> value_;
			number_range range_;
			std::function<void(const std::string&)> settled_;
			std::string pressed_;  // what it held as it was pressed
			std::optional<text_layout> laid_;
			std::string shown_;
			double scale_ = 0, widest_scale_ = 0;
			float widest_ = 0;
		};

		// What a picker shows while it's closed: the choice, in a box like a field's.
		class chosen_element : public element {
		public:
			chosen_element(std::string shown, bool placeholder, state<bool> open) : shown_(std::move(shown)), placeholder_(placeholder), open_(std::move(open)) {
				padding = edges(7, 10);
			}
			bool focusable() const override { return true; }
			one::desktop::cursor cursor() const override { return cursor::pointer; }
			extent measure_content(float width) override {
				const auto& laid = laid_out();
				const text_style body = current_theme().body();  // as tall as a field, whatever it says
				return {std::min(width, std::max(160.f, laid.width() + 24)), std::max(laid.height(), body.size * body.line_height)};
			}
			void paint_content(canvas& c) override {
				const theme& t = current_theme();
				c.box(frame, {.fill = hovered ? t.sunken : t.surface, .border = focused ? t.accent : t.line, .border_width = focused ? 1.5f : 1, .radius = std::min(t.corners, 6.f)});
				rect area = inner();
				const auto& laid = laid_out();
				c.text(laid, {area.x, area.y + (area.height - laid.height()) / 2});
				text_style s = t.quiet();
				auto mark = lay_out({"▾", s}, 0, detail::layout_scale());
				c.text(mark, {area.right() - mark.width(), area.y + (area.height - mark.height()) / 2});
			}
			bool handle_key(const key_event& e) override {
				if (!e.is(keys::space) && !e.is(keys::enter) && !e.is(keys::keypad_enter) && !e.is(keys::down)) return false;
				open_.set(true);
				return true;
			}
			bool click(point) override {
				open_.set(true);
				return true;
			}

		private:
			const text_layout& laid_out() {
				double scale = detail::layout_scale();
				if (!laid_ || scale != scale_) {
					laid_ = lay_out({shown_, placeholder_ ? current_theme().quiet() : current_theme().body()}, 0, scale);
					scale_ = scale;
				}
				return *laid_;
			}
			std::string shown_;
			bool placeholder_;
			state<bool> open_;
			std::optional<text_layout> laid_;
			double scale_ = 0;
		};

		// The popovers open as the window was last laid out, by their open state: where
		// each is, its trigger and its panel.
		struct open_popover {
			state<bool> open;
			std::vector<rect> on;      // where it is: its trigger and its panel
			element* panel = nullptr;  // as last laid out
		};
		std::map<const void*, open_popover>& open_popovers() {
			thread_local std::map<const void*, open_popover> open;
			return open;
		}

		// A trigger, and under it while it's open a panel drawn over everything else.
		class popover_element : public element {
		public:
			explicit popover_element(state<bool> open) : open_(std::move(open)) {}
			// Gone, it's no longer open anywhere, unless a newer one's taken its place.
			~popover_element() override {
				auto& open = open_popovers();
				auto it = open.find(&open_.peek());
				if (it != open.end() && children.size() > 1 && it->second.panel == children[1].get()) open.erase(it);
			}
			popover_element(const popover_element&) = delete;
			popover_element& operator=(const popover_element&) = delete;

			extent measure_content(float width) override { return children.empty() ? extent{} : children[0]->measure(width); }
			void place_content(const rect& area) override {
				if (children.empty()) return;
				children[0]->place(area);
				if (children.size() < 2) return open_popovers().erase(&open_.peek()), void();
				extent e = children[1]->measure(320);
				children[1]->place({frame.x, frame.bottom() + 4, std::max(e.width, 220.f), e.height});
				open_popovers()[&open_.peek()] = {open_, {frame, children[1]->frame}, children[1].get()};
			}
			void paint_content(canvas& c) override {
				if (children.empty()) return;
				children[0]->paint(c);
				if (children.size() < 2) return;
				// Its panel last, over everything, with a shadow under it.
				element* panel = children[1].get();
				c.later([panel](canvas& over) {
					const theme& t = current_theme();
					color shade = color::hex(0x000000);
					shade.alpha = 0.18f;
					over.shadow(panel->frame, 10, 16, shade, {0, 4});
					over.box(panel->frame, {.fill = t.surface, .border = t.line, .border_width = 1, .radius = 10});
					panel->paint(over);
				});
			}
			element* hit(point at) override {
				if (children.size() > 1) {
					if (element* found = children[1]->hit(at)) return found;
				}
				return element::hit(at);
			}

		private:
			state<bool> open_;
		};

		// Pickers whose list just closed, by their open state, to be given the keyboard back.
		std::set<const void*>& closing() {
			thread_local std::set<const void*> pickers;
			return pickers;
		}

	}  // namespace

	namespace detail {
		void close_popovers_outside(point at) {
			auto& open = open_popovers();
			for (auto it = open.begin(); it != open.end();) {
				bool on = std::ranges::any_of(it->second.on, [&](const rect& r) { return r.contains(at); });
				if (!on && it->second.open.peek()) it->second.open.set(false);
				it = on ? std::next(it) : open.erase(it);
			}
		}

		element* popover_hit(point at) {
			for (auto& [key, o] : open_popovers()) {
				if (o.open.peek() && o.panel && o.on.size() > 1 && o.on[1].contains(at)) return o.panel->hit(at);
			}
			return nullptr;
		}
	}  // namespace detail

	view popover(view trigger, view panel, state<bool> open) {
		auto e = std::make_unique<popover_element>(open);
		e->children.push_back(std::move(trigger).take());
		if (open.get()) {
			e->children.push_back(std::move(panel)
			                          .padding(6)
			                          .on_key([open](const key_event& k) {
				                          if (!k.is(keys::escape)) return false;
				                          open.set(false);
				                          return true;
			                          })
			                          .take());
		}
		for (auto& child : e->children) child->parent = e.get();
		return view(std::move(e));
	}

	view toggle(state<bool> on, std::string label) { return view(std::make_unique<toggle_element>(std::move(on), std::move(label))); }

	view radio_buttons(state<std::string> chosen, std::vector<option> options, std::function<void(const std::string&)> on_pick) {
		return view(std::make_unique<radio_element>(std::move(chosen), std::move(options), std::move(on_pick)));
	}

	view slider(state<std::string> value, number_range range, std::function<void(const std::string&)> settled) {
		return view(std::make_unique<slider_element>(std::move(value), range, std::move(settled)));
	}

	view picker(state<std::string> chosen, std::vector<option> options, state<bool> open, std::string placeholder) {
		std::string now = chosen.peek();
		const option* current = nullptr;
		for (const auto& o : options) {
			if (o.value == now) current = &o;
		}
		// Read, so the view is built again as it opens and closes.
		const void* id = &open.peek();
		if (!open.get()) {
			view closed(std::make_unique<chosen_element>(current ? current->label : placeholder, !current, open));
			if (closing().erase(id)) return std::move(closed).autofocus();
			return closed;
		}
		std::vector<list_row> rows;
		for (const auto& o : options) {
			const theme& t = current_theme();
			rows.push_back({o.value, row(text(o.value == now ? "✓" : "", t.strong()).width(16), text(o.label, t.body())).spacing(6)});
		}
		auto choosing = list(std::move(rows), [chosen, open, id](const std::string& value) {
			chosen.set(value);
			closing().insert(id);
			open.set(false);
		}, now);
		const theme& t = current_theme();
		return std::move(choosing)
		    .autofocus()
		    .padding(4)
		    .background({.fill = t.surface, .border = t.accent, .border_width = 1.5f, .radius = std::min(t.corners, 6.f)})
		    .on_key([open, id](const key_event& e) {
			    if (!e.is(keys::escape)) return false;
			    closing().insert(id);
			    open.set(false);
			    return true;
		    });
	}

}  // namespace one::desktop
