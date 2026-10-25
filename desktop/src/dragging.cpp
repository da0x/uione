// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Dragging something from one place to another, like a card to a column: a source
// carries its payload once it's moved a few pixels, and a target under the pointer
// when it's let go takes it, if it accepts it.

#include <cmath>
#include <utility>

#include "one/desktop/ui.hpp"
#include "ui_internal.hpp"

namespace one::desktop {

	namespace {

		// What's carried now, on the one thread views run on.
		std::optional<one::desktop::carried>& now_carried() {
			static std::optional<one::desktop::carried> it;
			return it;
		}

		class drop_target_element : public element {
		public:
			std::function<bool(const std::string&)> accepts;
			std::function<void(const std::string&)> on_drop;
			box_style over;
			color dim;

			void paint_content(canvas& c) override {
				element::paint_content(c);
				const auto& held = now_carried();
				if (!held) return;
				if (!accepts(held->payload)) {
					c.box(frame, {.fill = dim, .radius = background.radius});
				} else if (frame.contains(held->at)) {
					c.box(frame, over);
				}
			}
		};

		// Where files from another program may be let go.
		class file_target_element : public element {
		public:
			std::function<void(dropped, point)> on_drop;
			box_style over;

			void paint_content(canvas& c) override {
				element::paint_content(c);
				if (const auto& at = detail::files_carried(); at && frame.contains(*at)) c.box(frame, over);
			}
		};

		class drag_source_element : public element {
		public:
			std::string payload;
			std::optional<point> start;
			bool dragged = false;

			bool press(point at, int) override {
				start = at;
				dragged = false;
				return true;
			}
			void drag(point at) override {
				if (!start) return;
				if (!dragged && std::hypot(at.x - start->x, at.y - start->y) < 4) return;
				dragged = true;
				now_carried() = one::desktop::carried{payload, at};
			}
			void release(point at, element* over) override {
				start.reset();
				if (!std::exchange(now_carried(), std::nullopt) || !dragged) return;
				for (element* e = over; e; e = e->parent) {
					if (auto* target = dynamic_cast<drop_target_element*>(e)) {
						if (target->accepts(payload) && target->on_drop) target->on_drop(payload);
						break;
					}
				}
				(void)at;
			}
			// A press that became a drag isn't a click on what's inside too.
			bool click(point) override { return std::exchange(dragged, false); }
			one::desktop::cursor cursor() const override { return now_carried() ? cursor::grabbing : cursor::grab; }
			void paint_content(canvas& c) override {
				element::paint_content(c);
				// What's being carried stays where it was, faded.
				if (dragged && now_carried() && now_carried()->payload == payload) c.box(frame, {.fill = {1, 1, 1, 0.55f}, .radius = background.radius});
			}
			void adopt(element& before) override {
				element::adopt(before);
				if (auto* was = dynamic_cast<drag_source_element*>(&before)) {
					start = was->start;
					dragged = was->dragged;
				}
			}
		};

	}  // namespace

	const std::optional<carried>& carrying() { return now_carried(); }

	view drag_source(view content, std::string payload) {
		auto e = std::make_unique<drag_source_element>();
		e->payload = std::move(payload);
		e->children.push_back(std::move(content).take());
		e->children.back()->parent = e.get();
		return view(std::move(e));
	}

	view drop_target(view content, std::function<bool(const std::string&)> accepts, std::function<void(const std::string&)> on_drop, box_style over, color dim) {
		auto e = std::make_unique<drop_target_element>();
		e->accepts = std::move(accepts);
		e->on_drop = std::move(on_drop);
		e->over = over;
		e->dim = dim;
		e->children.push_back(std::move(content).take());
		e->children.back()->parent = e.get();
		return view(std::move(e));
	}

	view file_target(view content, std::function<void(dropped, point)> on_drop, box_style over) {
		auto e = std::make_unique<file_target_element>();
		e->on_drop = std::move(on_drop);
		e->over = over;
		e->children.push_back(std::move(content).take());
		e->children.back()->parent = e.get();
		return view(std::move(e));
	}

	namespace detail {

		std::optional<point>& files_carried() {
			static std::optional<point> at;
			return at;
		}

		void drop_files(element& root, point at, dropped what) {
			for (element* e = root.hit(at); e; e = e->parent) {
				if (auto* target = dynamic_cast<file_target_element*>(e)) {
					if (target->on_drop) target->on_drop(std::move(what), at);
					return;
				}
			}
		}

	}  // namespace detail

}  // namespace one::desktop
