// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A surface: things placed anywhere on it, in its own units, drawn as boxes in the
// terminal's cells, a cell across and a row down each a number of units. n chooses a
// thing and p the one before; the arrows, or h j k l, move it a grid step, and with
// Shift, or H J K L, resize it; it's told where it's gone at once. With nothing chosen
// the arrows look around. + and - zoom, 0 back to its size; Enter opens what's
// chosen, Escape lets go of it.

#include <algorithm>
#include <cmath>

#include "one/terminal/ui.hpp"

namespace one::terminal {

	namespace {

		// Shift with an arrow, as curses says it.
		constexpr int shift_left = 0611, shift_right = 0622, shift_up = 0520, shift_down = 0517;

		class surface_element : public element {
		public:
			std::vector<surface_item> items;
			surface_options options;

			extent measure_content(int width) override { return {width, options.height}; }

			void place_content(const area& a) override {
				viewport_ = a;
				for (std::size_t i = 0; i < items.size() && i < children.size(); ++i) {
					area box = on_screen(i);
					children[i]->place({box.x + 1, box.y + 1, std::max(0, box.width - 2), std::max(0, box.height - 2)});
				}
			}

			void paint_content(canvas& c) override {
				auto clipped = c.clip(viewport_);
				for (std::size_t i = 0; i < items.size() && i < children.size(); ++i) {
					area box = on_screen(i);
					if (box.width < 2 || box.height < 2) continue;
					bool is_chosen = i == chosen_;
					style edge = is_chosen ? style{.bold = true, .foreground = color::blue} : style{.foreground = color::bright_black};
					c.fill(box, {});
					std::string across;
					for (int x = 0; x < box.width - 2; ++x) across += "─";
					c.put(box.y, box.x, "╭" + across + "╮", edge);
					c.put(box.bottom() - 1, box.x, "╰" + across + (is_chosen && options.on_place && items[i].movable ? "◢" : "╯"), edge);
					for (int y = box.y + 1; y < box.bottom() - 1; ++y) {
						c.put(y, box.x, "│", edge);
						c.put(y, box.right() - 1, "│", edge);
					}
					auto inside = c.clip({box.x + 1, box.y + 1, std::max(0, box.width - 2), std::max(0, box.height - 2)});
					children[i]->paint(c);
				}
				if (items.empty()) c.put(viewport_.y, viewport_.x, "Nothing here yet", {.dim = true});
			}

			bool focusable() const override { return true; }

			bool handle_key(const key_press& e) override {
				if (!focused) return false;
				if (e.is('+') || e.is('=')) return zoom_by(1.25);
				if (e.is('-')) return zoom_by(0.8);
				if (e.is('0')) return zoom_by(1 / zoom_);
				// n and p choose the next thing and the one before, as Tab moves between views.
				if ((e.is('n') || e.is('p')) && !items.empty()) {
					if (chosen_ >= items.size()) chosen_ = e.is('n') ? 0 : items.size() - 1;
					else chosen_ = e.is('n') ? (chosen_ + 1) % items.size() : (chosen_ + items.size() - 1) % items.size();
					reveal_chosen();
					return true;
				}
				int dx = 0, dy = 0;
				bool resize = false;
				if (e.is(keys::left) || e.is('h')) dx = -1;
				else if (e.is(keys::right) || e.is('l')) dx = 1;
				else if (e.is(keys::up) || e.is('k')) dy = -1;
				else if (e.is(keys::down) || e.is('j')) dy = 1;
				else if (e.is(shift_left) || e.is('H')) dx = -1, resize = true;
				else if (e.is(shift_right) || e.is('L')) dx = 1, resize = true;
				else if (e.is(shift_up) || e.is('K')) dy = -1, resize = true;
				else if (e.is(shift_down) || e.is('J')) dy = 1, resize = true;
				if (chosen_ >= items.size()) {
					if (dx == 0 && dy == 0) return false;
					// Nothing chosen: looking around, a few cells at a time.
					pan_x_ += dx * 4 * units_across();
					pan_y_ += dy * 2 * units_down();
					place_content(viewport_);
					return true;
				}
				if ((e.is(keys::enter) || e.is(keys::newline)) && options.on_open) {
					options.on_open(items[chosen_].key);
					return true;
				}
				if (e.is(keys::escape)) {
					chosen_ = items.size();
					return true;
				}
				if (dx == 0 && dy == 0) return false;
				if (!options.on_place || !items[chosen_].movable) return true;
				double step = options.grid > 0 ? options.grid : 10;
				auto& it = items[chosen_];
				if (resize) {
					it.width = std::max(options.smallest, it.width + dx * step);
					it.height = std::max(options.smallest, it.height + dy * step);
				} else {
					it.x += dx * step;
					it.y += dy * step;
				}
				place_content(viewport_);
				reveal_chosen();
				options.on_place(it.key, it.x, it.y, it.width, it.height);
				return true;
			}

			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<surface_element&>(before);
				pan_x_ = was.pan_x_, pan_y_ = was.pan_y_, zoom_ = was.zoom_;
				std::string chosen = was.chosen_ < was.items.size() ? was.items[was.chosen_].key : "";
				chosen_ = items.size();
				for (std::size_t i = 0; i < items.size(); ++i) {
					if (items[i].key == chosen) chosen_ = i;
				}
			}

		private:
			area viewport_;
			double pan_x_ = 0, pan_y_ = 0, zoom_ = 1;
			std::size_t chosen_ = static_cast<std::size_t>(-1);

			double units_across() const { return options.units_across / zoom_; }
			double units_down() const { return options.units_down / zoom_; }
			// Where a thing is drawn, in cells: its corners rounded to the cells they fall in.
			area on_screen(std::size_t i) const {
				const auto& it = items[i];
				int x = static_cast<int>(std::floor((it.x - pan_x_) / units_across())), y = static_cast<int>(std::floor((it.y - pan_y_) / units_down()));
				int right = static_cast<int>(std::floor((it.x + it.width - pan_x_) / units_across())), bottom = static_cast<int>(std::floor((it.y + it.height - pan_y_) / units_down()));
				return {viewport_.x + x, viewport_.y + y, std::max(2, right - x), std::max(2, bottom - y)};
			}
			// What's chosen brought within what's shown.
			void reveal_chosen() {
				if (chosen_ >= items.size()) return;
				area box = on_screen(chosen_);
				if (box.x < viewport_.x) pan_x_ -= (viewport_.x - box.x + 2) * units_across();
				else if (box.right() > viewport_.right()) pan_x_ += (box.right() - viewport_.right() + 2) * units_across();
				if (box.y < viewport_.y) pan_y_ -= (viewport_.y - box.y + 1) * units_down();
				else if (box.bottom() > viewport_.bottom()) pan_y_ += (box.bottom() - viewport_.bottom() + 1) * units_down();
				place_content(viewport_);
			}
			bool zoom_by(double by) {
				zoom_ = std::clamp(zoom_ * by, 0.25, 4.0);
				place_content(viewport_);
				return true;
			}
		};

	}  // namespace

	view surface(std::vector<surface_item> items, surface_options options) {
		auto e = std::make_unique<surface_element>();
		for (auto& item : items) {
			auto content = std::move(item.content).take();
			content->parent = e.get();
			e->children.push_back(std::move(content));
		}
		e->items = std::move(items);
		e->options = std::move(options);
		e->grow = 1;
		return view(std::move(e));
	}

}  // namespace one::terminal
