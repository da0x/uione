// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A window's view: built again when what it read changes, matched with what it
// replaces, placed in the window, and given the keyboard and the pointer.

#include <algorithm>
#include <optional>
#include <typeinfo>
#include <vector>

#include "app_internal.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

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

		// Where an element is under the root: which child it is at each level.
		std::optional<std::vector<std::size_t>> path_of(const element* root, const element* e) {
			std::vector<std::size_t> path;
			for (; e && e != root && e->parent; e = e->parent) {
				const auto& siblings = e->parent->children;
				auto at = std::ranges::find_if(siblings, [e](const auto& child) { return child.get() == e; });
				if (at == siblings.end()) return std::nullopt;
				path.push_back(static_cast<std::size_t>(at - siblings.begin()));
			}
			if (!e || e != root) return std::nullopt;
			std::ranges::reverse(path);
			return path;
		}

		// The element at that place in a view built again, when it's the same kind.
		element* at_path(element* root, const std::vector<std::size_t>& path, const std::type_info& kind) {
			element* e = root;
			for (std::size_t i : path) {
				if (!e || i >= e->children.size()) return nullptr;
				e = e->children[i].get();
			}
			return e && typeid(*e) == kind ? e : nullptr;
		}

		template <class F>
		void each_element(element& e, F&& visit) {
			visit(e);
			for (auto& child : e.children) each_element(*child, visit);
		}

		// What's given the keyboard is brought into view, unless it was pressed, when it's
		// in sight already.
		void focus(window::state& w, element* chosen, bool reveal = true) {
			if (!w.root) return;
			each_element(*w.root, [](element& e) { e.focused = e.focused_by_default = e.focused_by_pointer = false; });
			if (chosen) {
				chosen->focused = true;
				if (reveal) chosen->reveal(chosen->frame);
			}
			w.redraw();
		}

		// The focusable element nearest in a direction from the focused one: what h, j,
		// k, l and the arrows move to when what's focused doesn't take them, like the
		// next column of a board.
		bool move_focus(window::state& w, const key_event& e) {
			int dx = 0, dy = 0;
			if (e.is(keys::left) || e.is('h')) dx = -1;
			else if (e.is(keys::right) || e.is('l')) dx = 1;
			else if (e.is(keys::up) || e.is('k')) dy = -1;
			else if (e.is(keys::down) || e.is('j')) dy = 1;
			else return false;
			element* from = detail::focused_in(*w.root);
			if (!from) return false;
			std::vector<element*> all;
			detail::focusables(*w.root, all);
			auto middle = [](const rect& r) { return one::desktop::point{r.x + r.width / 2, r.y + r.height / 2}; };
			one::desktop::point here = middle(from->frame);
			element* best = nullptr;
			float best_score = 0;
			for (element* candidate : all) {
				if (candidate == from) continue;
				const rect& r = candidate->frame;
				// Ahead in that direction, by its near edge.
				float ahead = dx > 0 ? r.x - from->frame.right() : dx < 0 ? from->frame.x - r.right() : dy > 0 ? r.y - from->frame.bottom() : from->frame.y - r.bottom();
				if (ahead < -1) continue;
				one::desktop::point there = middle(r);
				float aside = dx ? std::abs(there.y - here.y) : std::abs(there.x - here.x);
				float score = std::max(0.f, ahead) + 2 * aside;
				if (!best || score < best_score) best = candidate, best_score = score;
			}
			if (!best) return false;
			focus(w, best);
			return true;
		}

		// Which element the pointer is over, and those it's within, marked as hovered.
		void point_to(window::state& w, std::optional<one::desktop::point> at) {
			if (!w.root) return;
			element* over = at ? detail::popover_hit(*at) : nullptr;
			if (at && !over) over = w.root->hit(*at);
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
			// Built now; something the build changes, like a screen opening another in its
			// place as it opens, builds it again.
			w.needs_building = false;
			std::unique_ptr<element> built;
			{
				detail::reading noting(reader);
				built = w.build().take();
			}
			w.reading = std::move(reader);
			// What's being dragged, like a slider's knob, goes on being dragged as what it
			// changes builds the view again.
			auto dragged = path_of(w.root.get(), w.dragging);
			const std::type_info* dragged_kind = w.dragging ? &typeid(*w.dragging) : nullptr;
			if (built && w.root) detail::reconcile(*built, *w.root);
			w.root = std::move(built);
			w.pressed = w.dragging = nullptr;
			if (dragged && dragged_kind) w.pressed = w.dragging = at_path(w.root.get(), *dragged, *dragged_kind);
			w.needs_placing = true;
			// Something to type into from the start: what asks for it, or a list, or the
			// first that takes keys. Focus given that way moves on when something better
			// arrives, as a screen's list does once its data has.
			element* had = w.root ? detail::focused_in(*w.root) : nullptr;
			if (w.root && (!had || had->focused_by_default)) {
				std::vector<element*> all;
				detail::focusables(*w.root, all);
				// What's just appeared asking for the keyboard, like a list a picker opened,
				// before what asked for it in the view before.
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
		}
		if (w.root && (w.needs_placing || w.placed_at != w.size || w.placed_scale != w.scale)) {
			w.root->place({0, 0, static_cast<float>(w.size.width), static_cast<float>(w.size.height)});
			each_element(*w.root, [](element& e) {
				if ((!e.to_top && !e.in_view) || e.brought) return;
				e.brought = true;
				if (e.to_top) e.bring_to_top();
				else e.reveal(e.frame);
			});
			w.placed_at = w.size;
			w.placed_scale = w.scale;
			w.needs_placing = false;
			auto& input = w.owner->input;
			if (input && input->pointer_over == &w) {
				point_to(w, input->pointer_at);
				// What it's over now may look otherwise, as a picture's pointer hides once
				// it's been still.
				point_again(w);
			}
		}
	}

	void deliver_key(window::state& w, const key_event& e) {
		bring_up_to_date(w);
		in_window context(w);
		if (w.outside->on_key_first && w.outside->on_key_first(e)) {
			w.redraw();
			return;
		}
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
			// A key the window's own lines take, like left for the previous picture, is
			// theirs before it moves the keyboard to what's beside.
			if (w.outside->on_key && w.outside->on_key(e)) {
				w.redraw();
				return;
			}
			if (!e.control && !e.alt && !e.super) move_focus(w, e);
			return;
		}
		if (w.outside->on_key && w.outside->on_key(e)) w.redraw();
	}

	void deliver_pointer(window::state& w, one::desktop::point at) {
		bring_up_to_date(w);
		in_window context(w);
		if (w.dragging) w.dragging->drag(at);
		point_to(w, at);
		if (w.outside && w.outside->on_pointer) w.outside->on_pointer();
		w.redraw();
	}

	void deliver_leave(window::state& w) {
		in_window context(w);
		point_to(w, std::nullopt);
		w.pressed = w.dragging = nullptr;
		w.redraw();
	}

	void deliver_press(window::state& w, one::desktop::point at, int clicks) {
		bring_up_to_date(w);
		in_window context(w);
		if (!w.root) return;
		detail::close_popovers_outside(at);
		element* target = detail::popover_hit(at);
		if (!target) target = w.root->hit(at);
		w.pressed = target;
		w.dragging = nullptr;
		for (element* e = target; e; e = e->parent) {
			if (e->focusable()) {
				if (!e->focused_by_press()) break;
				focus(w, e, false);
				e->focused_by_pointer = true;
				break;
			}
		}
		for (element* e = target; e; e = e->parent) {
			if (e->press(at, clicks)) {
				w.dragging = e;
				break;
			}
		}
		w.redraw();
	}

	void deliver_release(window::state& w, one::desktop::point at) {
		bring_up_to_date(w);
		in_window context(w);
		element* pressed = std::exchange(w.pressed, nullptr);
		if (element* dragged = std::exchange(w.dragging, nullptr); dragged && w.root) dragged->release(at, w.root->hit(at));
		if (!pressed || !w.root) return;
		// A click is a press and a release over the same thing.
		for (element* e = pressed; e; e = e->parent) {
			if (!e->frame.contains(at)) continue;
			if (e->click(at)) break;
		}
		w.redraw();
	}

	void deliver_wheel(window::state& w, one::desktop::point at, float dx, float dy) {
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

	void deliver_pinch(window::state& w, one::desktop::point at, float by) {
		bring_up_to_date(w);
		in_window context(w);
		if (!w.root) return;
		for (element* e = w.root->hit(at); e; e = e->parent) {
			if (e->pinch(at, by)) {
				w.redraw();
				return;
			}
		}
	}

	one::desktop::cursor cursor_at(window::state& w, one::desktop::point at) {
		if (!w.root) return cursor::normal;
		for (element* e = w.root->hit(at); e; e = e->parent) {
			if (e->cursor() != cursor::normal) return e->cursor();
		}
		return cursor::normal;
	}

}  // namespace one::desktop
