// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A surface: things placed anywhere on it, in its own units, panned and zoomed. A
// thing is dragged to move it, or by its corner to resize it, and told where it's
// gone once it's let go, snapped to the surface's grid when there's one. Empty space
// dragged pans; + and - zoom, 0 back to its size; the arrows move what's chosen a
// grid step, with Shift resizing it, and Enter opens it.

#include <algorithm>
#include <cmath>
#include <utility>

#include "one/desktop/ui.hpp"

namespace one::desktop {

	namespace {

		constexpr float corner = 12;  // the square at a thing's lower right corner that resizes it

		class surface_element : public element {
		public:
			std::vector<surface_item> items;  // their content is this element's children, in the same order
			surface_options options;

			extent measure_content(float width) override { return {width, options.height}; }

			void place_content(const rect& area) override {
				viewport_ = area;
				for (std::size_t i = 0; i < items.size() && i < children.size(); ++i) children[i]->place(on_screen(shown(i)));
				// Each region's title along its top, after the things' content.
				auto bands = drawn_regions();
				for (std::size_t r = 0; r < bands.size() && items.size() + r < children.size(); ++r) {
					float left = viewport_.x + pan_.x + bands[r].x * zoom_;
					children[items.size() + r]->place({left + 10, viewport_.y + 8, std::max(0.f, bands[r].width * zoom_ - 20), 20});
				}
			}

			void paint_content(canvas& c) override {
				auto clipped = c.clip(viewport_);
				const auto& t = current_theme();
				c.box(viewport_, {.fill = t.sunken, .radius = t.corners});
				// The grid's points, while they're far enough apart to read.
				if (options.grid > 0 && options.grid * zoom_ >= 24) {
					color dot = t.muted;
					dot.alpha = 0.25f;
					float step = options.grid * zoom_;
					float x0 = viewport_.x + std::fmod(options.origin_x * zoom_ + pan_.x, step), y0 = viewport_.y + std::fmod(options.origin_y * zoom_ + pan_.y, step);
					for (float x = x0 - step; x < viewport_.right(); x += step) {
						for (float y = y0 - step; y < viewport_.bottom(); y += step) c.box({x - 1, y - 1, 2, 2}, {.fill = dot, .radius = 1});
					}
				}
				// The regions: every other one faintly shaded, a line after each, solid while
				// it's being dragged, and its title.
				auto bands = drawn_regions();
				for (std::size_t r = 0; r < bands.size(); ++r) {
					float left = viewport_.x + pan_.x + bands[r].x * zoom_, right = left + bands[r].width * zoom_;
					if (r % 2 == 1) {
						color band = t.ink;
						band.alpha = 0.025f;
						c.box({left, viewport_.y, right - left, viewport_.height}, {.fill = band});
					}
					color edge = t.line;
					if (!widening_ || widening_->key != bands[r].key) edge.alpha = 0.8f;
					c.box({right - (widening_ && widening_->key == bands[r].key ? 1.f : 0.5f), viewport_.y, widening_ && widening_->key == bands[r].key ? 2.f : 1.f, viewport_.height}, {.fill = edge});
					if (items.size() + r < children.size()) children[items.size() + r]->paint(c);
				}
				for (std::size_t i = 0; i < items.size() && i < children.size(); ++i) {
					rect at = on_screen(shown(i));
					color shade = t.ink;
					shade.alpha = i == chosen_ ? 0.16f : 0.07f;
					c.shadow(at, std::min(t.corners, 8.f), i == chosen_ ? 12.f : 6.f, shade, {0, i == chosen_ ? 3.f : 1.f});
					c.box(at, {.fill = t.surface, .border = i == chosen_ ? t.accent : t.line, .border_width = i == chosen_ ? 2.f : 1.f, .radius = std::min(t.corners, 8.f)});
					auto inside = c.clip(at);
					children[i]->paint(c);
				}
				// The chosen thing's corner, to resize it by.
				if (chosen_ < items.size() && options.on_place && items[chosen_].movable) {
					rect at = on_screen(shown(chosen_));
					c.box({at.right() - corner / 2, at.bottom() - corner / 2, corner / 2, corner / 2}, {.fill = t.accent, .radius = 2});
				}
			}

			element* hit(point at) override {
				if (!viewport_.contains(at)) return nullptr;
				// Only within each thing's own place: what it holds is drawn clipped to it.
				for (std::size_t i = children.size(); i-- > 0;) {
					if (i < items.size() && on_screen(shown(i)).contains(at)) {
						if (auto* found = children[i]->hit(at)) return found;
						return this;
					}
				}
				return this;
			}

			bool focusable() const override { return true; }

			bool press(point at, int clicks) override {
				grabbed_ = std::nullopt;
				start_ = at;
				// The line after a region, grabbed to make it wider or narrower.
				if (options.on_resize) {
					for (const auto& band : drawn_regions()) {
						float right = viewport_.x + pan_.x + (band.x + band.width) * zoom_;
						if (std::abs(at.x - right) <= 5) {
							widening_ = widening{band.key, band.width, band.width};
							return true;
						}
					}
				}
				for (std::size_t i = items.size(); i-- > 0;) {
					rect r = on_screen(shown(i));
					if (!r.contains(at)) continue;
					chosen_ = i;
					if (clicks >= 2) {
						if (options.on_open) options.on_open(items[i].key);
						return true;
					}
					if (!options.on_place || !items[i].movable) return true;
					bool resizing = at.x >= r.right() - corner && at.y >= r.bottom() - corner;
					grabbed_ = grab{i, resizing, shown(i)};
					return true;
				}
				chosen_ = items.size();
				panned_from_ = pan_;
				panning_ = true;
				return true;
			}

			void drag(point at) override {
				float dx = at.x - start_.x, dy = at.y - start_.y;
				if (widening_) {
					widening_->width = std::max(options.smallest * 3, widening_->from + dx / zoom_);
					place_content(viewport_);
					return;
				}
				if (panning_) {
					pan_ = {panned_from_.x + dx, panned_from_.y + dy};
					place_content(viewport_);
					return;
				}
				if (!grabbed_) return;
				rect r = grabbed_->from;
				if (grabbed_->resizing) {
					r.width = std::max(options.smallest, r.width + dx / zoom_);
					r.height = std::max(options.smallest, r.height + dy / zoom_);
				} else {
					r.x += dx / zoom_;
					r.y += dy / zoom_;
				}
				live_ = std::pair{grabbed_->index, r};
				children[grabbed_->index]->place(on_screen(r));
			}

			void release(point, element*) override {
				if (auto done = std::exchange(widening_, std::nullopt)) {
					if (done->width != done->from) {
						resized_ = std::pair{done->key, done->width};
						options.on_resize(done->key, std::round(done->width));
					}
					place_content(viewport_);
					return;
				}
				panning_ = false;
				auto held = std::exchange(grabbed_, std::nullopt);
				if (!held || !live_ || live_->first != held->index) return;
				rect r = live_->second;
				// Snapped by where it was put, its size as it was made.
				if (!held->resizing) r.x = snapped(r.x, options.origin_x), r.y = snapped(r.y, options.origin_y);
				live_->second = r;
				place_content(viewport_);
				options.on_place(items[held->index].key, r.x, r.y, r.width, r.height);
			}

			// A press that moved something isn't a click on what's inside too.
			bool click(point at) override { return std::hypot(at.x - start_.x, at.y - start_.y) >= 4; }

			bool wheel(point, float dx, float dy) override {
				pan_.x -= dx;
				pan_.y -= dy;
				place_content(viewport_);
				return true;
			}

			bool handle_key(const key_event& e) override {
				if (!focused) return false;
				if (e.is('+') || e.is('=')) return zoom_by(1.25f);
				if (e.is('-')) return zoom_by(0.8f);
				if (e.is('0')) return zoom_by(1 / zoom_);
				if (chosen_ >= items.size()) {
					if (!items.empty() && (e.is(keys::tab) || e.is(keys::down) || e.is(keys::right))) {
						chosen_ = 0;
						return true;
					}
					return false;
				}
				if ((e.is(keys::enter) || e.is(keys::keypad_enter)) && options.on_open) {
					options.on_open(items[chosen_].key);
					return true;
				}
				if (e.is(keys::escape)) {
					chosen_ = items.size();
					return true;
				}
				if (e.is(keys::tab)) {
					chosen_ = (chosen_ + 1) % items.size();
					return true;
				}
				float step = options.grid > 0 ? options.grid : 10;
				float x = 0, y = 0;
				if (e.is(keys::left)) x = -step;
				else if (e.is(keys::right)) x = step;
				else if (e.is(keys::up)) y = -step;
				else if (e.is(keys::down)) y = step;
				else return false;
				if (!options.on_place || !items[chosen_].movable) return true;
				rect r = shown(chosen_);
				if (e.shift) {
					r.width = std::max(options.smallest, r.width + x);
					r.height = std::max(options.smallest, r.height + y);
				} else {
					r.x += x;
					r.y += y;
				}
				live_ = std::pair{chosen_, r};
				place_content(viewport_);
				options.on_place(items[chosen_].key, r.x, r.y, r.width, r.height);
				return true;
			}

			one::desktop::cursor cursor() const override {
				if (widening_) return cursor::resize_across;
				return grabbed_ || panning_ ? cursor::grabbing : cursor::grab;
			}

			void adopt(element& before) override {
				element::adopt(before);
				auto& was = static_cast<surface_element&>(before);
				pan_ = was.pan_, zoom_ = was.zoom_;
				// A region just resized stays so until what's given says so too.
				if (was.resized_) {
					for (const auto& band : options.regions) {
						if (band.key == was.resized_->first && std::abs(band.width - was.resized_->second) >= 0.5f) resized_ = was.resized_;
					}
				}
				// The same thing stays chosen, by its key, and one just let go stays where it
				// was put until what's given says so too.
				std::string chosen = was.chosen_ < was.items.size() ? was.items[was.chosen_].key : "";
				chosen_ = items.size();
				for (std::size_t i = 0; i < items.size(); ++i) {
					if (items[i].key == chosen) chosen_ = i;
				}
				if (was.live_ && was.live_->first < was.items.size()) {
					const std::string& moved = was.items[was.live_->first].key;
					for (std::size_t i = 0; i < items.size(); ++i) {
						if (items[i].key != moved) continue;
						rect r = was.live_->second;
						bool there = std::abs(items[i].x - r.x) < 0.5f && std::abs(items[i].y - r.y) < 0.5f && std::abs(items[i].width - r.width) < 0.5f &&
						             std::abs(items[i].height - r.height) < 0.5f;
						if (!there) live_ = std::pair{i, r};
					}
				}
			}

		private:
			struct grab {
				std::size_t index;
				bool resizing;
				rect from;
			};
			rect viewport_;
			point pan_{0, 0}, panned_from_{0, 0}, start_{0, 0};
			float zoom_ = 1;
			bool panning_ = false;
			std::optional<grab> grabbed_;
			std::optional<std::pair<std::size_t, rect>> live_;  // where one being moved is, in the surface's units
			std::size_t chosen_ = static_cast<std::size_t>(-1);
			struct widening {
				std::string key;
				float from, width;
			};
			std::optional<widening> widening_;
			std::optional<std::pair<std::string, float>> resized_;  // one just made wider, until it's given so

			// The regions as they're drawn: one being dragged, or just let go, as wide as
			// it's made, those after it along.
			std::vector<surface_region> drawn_regions() const {
				std::vector<surface_region> out;
				float shift = 0;
				for (auto band : options.regions) {
					float width = widening_ && widening_->key == band.key ? widening_->width : resized_ && resized_->first == band.key ? resized_->second : band.width;
					band.x += shift;
					shift += width - band.width;
					band.width = width;
					out.push_back(band);
				}
				return out;
			}

			// Where a thing is, in the surface's units: as it's being moved, or as given.
			rect shown(std::size_t i) const {
				if (live_ && live_->first == i) return live_->second;
				return {items[i].x, items[i].y, items[i].width, items[i].height};
			}
			rect on_screen(const rect& r) const {
				return {viewport_.x + pan_.x + r.x * zoom_, viewport_.y + pan_.y + r.y * zoom_, r.width * zoom_, r.height * zoom_};
			}
			float snapped(float at, float origin) const {
				if (options.grid <= 0) return at;
				return std::round((at - origin) / options.grid) * options.grid + origin;
			}
			// Zoomed about the middle of what's shown, from a tenth to four times.
			bool zoom_by(float by) {
				float to = std::clamp(zoom_ * by, 0.1f, 4.f);
				point middle{viewport_.width / 2, viewport_.height / 2};
				pan_ = {middle.x - (middle.x - pan_.x) * to / zoom_, middle.y - (middle.y - pan_.y) * to / zoom_};
				zoom_ = to;
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
		// Each region's title, drawn along its top.
		for (const auto& band : options.regions) {
			auto look = current_theme().quiet();
			look.weight = 600;
			auto title = text(band.title, look).take();
			title->parent = e.get();
			e->children.push_back(std::move(title));
		}
		e->items = std::move(items);
		e->options = std::move(options);
		e->grow = 1;
		return view(std::move(e));
	}

}  // namespace one::desktop
