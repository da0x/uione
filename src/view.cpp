// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// A window's view: built again when what it read changes, matched with what it
// replaces, placed in the window, and given the keyboard and the pointer.

#include <algorithm>

#include "app_internal.hpp"
#include "ui_internal.hpp"

namespace loom {

	namespace {

		// While a window's view is built, placed or handled: its theme and scale.
		class in_window {
		public:
			explicit in_window(window::state& w) {
				detail::theme_in_use() = &w.look;
				detail::layout_scale() = w.scale;
			}
			~in_window() { detail::theme_in_use() = nullptr; }
		};

		template <class F>
		void each_element(element& e, F&& visit) {
			visit(e);
			for (auto& child : e.children) each_element(*child, visit);
		}

		void focus(window::state& w, element* chosen) {
			if (!w.root) return;
			each_element(*w.root, [](element& e) { e.focused = false; });
			if (chosen) chosen->focused = true;
			w.redraw();
		}

		// Which element the pointer is over, and those it's within, marked as hovered.
		void point_to(window::state& w, std::optional<loom::point> at) {
			if (!w.root) return;
			element* over = at ? w.root->hit(*at) : nullptr;
			each_element(*w.root, [](element& e) {
				e.hovered = false;
				point_at_list(e, std::nullopt);
			});
			for (element* e = over; e; e = e->parent) {
				e->hovered = true;
				point_at_list(*e, at);
			}
		}

	}  // namespace

	void bring_up_to_date(window::state& w) {
		in_window context(w);
		if (w.needs_building && w.build) {
			auto reader = std::make_shared<detail::reader>();
			window::state* self = &w;
			reader->changed = [self] {
				self->needs_building = true;
				self->redraw();
			};
			std::unique_ptr<element> built;
			{
				detail::reading noting(reader);
				built = w.build().take();
			}
			w.reading = std::move(reader);
			if (built && w.root) detail::reconcile(*built, *w.root);
			w.root = std::move(built);
			w.pressed = nullptr;
			w.needs_building = false;
			w.needs_placing = true;
			// Something to type into from the start: the first that takes keys.
			if (w.root && !detail::focused_in(*w.root)) {
				std::vector<element*> all;
				detail::focusables(*w.root, all);
				auto first = std::ranges::find_if(all, [](element* e) { return e->wants_focus; });
				if (first == all.end()) first = std::ranges::find_if(all, [](element* e) { return e->prefers_focus(); });
				if (first == all.end()) first = all.begin();
				if (first != all.end()) (*first)->focused = true;
			}
		}
		if (w.root && (w.needs_placing || w.placed_at != w.size || w.placed_scale != w.scale)) {
			w.root->place({0, 0, static_cast<float>(w.size.width), static_cast<float>(w.size.height)});
			w.placed_at = w.size;
			w.placed_scale = w.scale;
			w.needs_placing = false;
			auto& input = w.owner->input;
			if (input && input->pointer_over == &w) point_to(w, input->pointer_at);
		}
	}

	void deliver_key(window::state& w, const key_event& e) {
		bring_up_to_date(w);
		in_window context(w);
		if (w.root) {
			if (e.is(keys::tab) || e.is(keys::left_tab)) {
				std::vector<element*> all;
				detail::focusables(*w.root, all);
				if (!all.empty()) {
					auto it = std::ranges::find(all, detail::focused_in(*w.root));
					std::size_t at = it == all.end() ? 0 : static_cast<std::size_t>(it - all.begin());
					bool back = e.shift || e.is(keys::left_tab);
					std::size_t next = it == all.end() ? 0 : back ? (at + all.size() - 1) % all.size() : (at + 1) % all.size();
					focus(w, all[next]);
					return;
				}
			}
			for (element* target = detail::focused_in(*w.root); target; target = target->parent) {
				if (target->handle_key(e) || (target->on_key && target->on_key(e))) {
					w.redraw();
					return;
				}
			}
			if (!detail::focused_in(*w.root) && w.root->on_key && w.root->on_key(e)) {
				w.redraw();
				return;
			}
		}
		if (w.outside->on_key && w.outside->on_key(e)) w.redraw();
	}

	void deliver_pointer(window::state& w, loom::point at) {
		bring_up_to_date(w);
		in_window context(w);
		point_to(w, at);
		w.redraw();
	}

	void deliver_leave(window::state& w) {
		in_window context(w);
		point_to(w, std::nullopt);
		w.pressed = nullptr;
		w.redraw();
	}

	void deliver_press(window::state& w, loom::point at) {
		bring_up_to_date(w);
		in_window context(w);
		if (!w.root) return;
		element* target = w.root->hit(at);
		w.pressed = target;
		for (element* e = target; e; e = e->parent) {
			if (e->focusable()) {
				focus(w, e);
				break;
			}
		}
		w.redraw();
	}

	void deliver_release(window::state& w, loom::point at) {
		bring_up_to_date(w);
		in_window context(w);
		element* pressed = std::exchange(w.pressed, nullptr);
		if (!pressed || !w.root) return;
		// A click is a press and a release over the same thing.
		for (element* e = pressed; e; e = e->parent) {
			if (!e->frame.contains(at)) continue;
			if (e->click(at)) break;
		}
		w.redraw();
	}

	void deliver_wheel(window::state& w, loom::point at, float dx, float dy) {
		bring_up_to_date(w);
		in_window context(w);
		if (!w.root) return;
		for (element* e = w.root->hit(at); e; e = e->parent) {
			if (e->wheel(at, dx, dy)) {
				point_to(w, at);
				w.redraw();
				return;
			}
		}
	}

	loom::cursor cursor_at(window::state& w, loom::point at) {
		if (!w.root) return cursor::normal;
		for (element* e = w.root->hit(at); e; e = e->parent) {
			if (e->cursor() != cursor::normal) return e->cursor();
		}
		return cursor::normal;
	}

}  // namespace loom
