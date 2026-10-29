// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a window draws with, in logical pixels: boxes with rounded corners, borders
// and soft shadows, laid-out text, and pictures. Everything is drawn in the order it's asked
// for, each over what came before, and within the clip in force.

#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "one/desktop/app.hpp"
#include "one/desktop/picture.hpp"
#include "one/desktop/text.hpp"

namespace one::desktop {

	struct point {
		float x = 0;
		float y = 0;
	};

	struct rect {
		float x = 0, y = 0, width = 0, height = 0;

		float right() const { return x + width; }
		float bottom() const { return y + height; }
		bool contains(point p) const { return p.x >= x && p.y >= y && p.x < right() && p.y < bottom(); }
		rect inset(float by) const { return {x + by, y + by, width - 2 * by, height - 2 * by}; }
		rect intersect(const rect& other) const;
	};

	// How a box is drawn: its fill, a border inside its edge, and its corners.
	struct box_style {
		one::desktop::color fill{0, 0, 0, 0};
		one::desktop::color border{0, 0, 0, 0};
		float border_width = 0;
		float radius = 0;
	};

	class renderer;

	class canvas {
	public:
		one::desktop::size size() const;
		double scale() const;

		void box(const rect& area, const box_style& style);
		// A shadow under a box of the same area and corners, softened by blur and
		// moved by offset.
		void shadow(const rect& area, float radius, float blur, one::desktop::color tint, point offset = {});
		// A line from a to b, its ends round, as an icon's strokes are drawn.
		void line(point a, point b, float width, one::desktop::color tint);
		// The inside of an outline, by the even-odd rule, as an icon's filled shapes are.
		void fill(const std::vector<point>& outline, one::desktop::color tint);
		// Text laid out by one::desktop::lay_out, its first line's top at `at`.
		void text(const text_layout& laid, point at);
		// A picture in an area, stretched to it: all of it, or the part `from` names
		// in fractions of its width and height, as a crop does; and as much of it
		// showing as `opacity` says; its corners rounded as far as `radius` says.
		void picture(const one::desktop::picture& p, const rect& area, const rect& from = {0, 0, 1, 1}, float opacity = 1, float radius = 0);

		// Drawn once everything else is, over it, outside any clip: a button's name
		// shown while it's pointed at.
		void later(std::function<void(canvas&)> draw) { later_.push_back(std::move(draw)); }
		// Draws what was left for later; what draws a window calls it last.
		void finish();

		// Draws only within an area until the scope ends, within any clip already in force.
		class clip_scope {
		public:
			~clip_scope();
			clip_scope(const clip_scope&) = delete;
			clip_scope& operator=(const clip_scope&) = delete;

		private:
			friend class canvas;
			explicit clip_scope(canvas& c) : owner_(c) {}
			canvas& owner_;
		};
		[[nodiscard]] clip_scope clip(const rect& area);
		// Where what's drawn shows now: the clip in force, or nothing while there's none,
		// as what draws only what's in view asks.
		std::optional<rect> clipped() const { return clips_.empty() ? std::nullopt : std::optional(clips_.back()); }

		explicit canvas(renderer& r) : renderer_(r) {}

	private:
		renderer& renderer_;
		std::vector<rect> clips_;
		std::vector<std::function<void(canvas&)>> later_;
	};

}  // namespace one::desktop
