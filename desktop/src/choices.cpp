// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Choosing: a box ticked or not, and one of several choices picked from a list that
// opens under it.

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

		// Pickers whose list just closed, by their open state, to be given the keyboard back.
		std::set<const void*>& closing() {
			thread_local std::set<const void*> pickers;
			return pickers;
		}

	}  // namespace

	view toggle(state<bool> on, std::string label) { return view(std::make_unique<toggle_element>(std::move(on), std::move(label))); }

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
