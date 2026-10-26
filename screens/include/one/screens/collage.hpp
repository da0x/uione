// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a collage's layers go: a picture made of layers, as big as it says, in its
// own pixels (decision 0019). How its background sits and slides, the whole picture
// behind a crop, and where a layer moved, resized, slid or cropped ends up, snapped
// to the collage's edges and middle and every other layer's. It's the same model as
// @uione/react's collage.ts, and both are held to tests/collage-cases.json, so the
// desktop, the terminal and the web put a layer in the same place.

#pragma once

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace one::screens::collage {

	struct point {
		double x = 0, y = 0;
	};

	struct box {
		double x = 0, y = 0, w = 0, h = 0;
		bool contains(point at) const { return at.x >= x && at.x <= x + w && at.y >= y && at.y <= y + h; }
	};

	// The smallest a layer, or a crop of one, is made by a drag, in the collage's pixels.
	inline constexpr double smallest = 8;

	// How the background sits on a collage: covering it (fill), all of it showing
	// (fit, the default), stretched to it, or at its own size in the middle; zoomed
	// about its middle; and how far along each side it's slid, from 0 to 1.
	struct backdrop {
		std::string_view sits;
		double width = 0, height = 0;
		double zoom = 1;
		double slide_x = 0.5, slide_y = 0.5;
	};

	// Where a picture of its own size sits on a collage, as `sits` says.
	box sitting(std::string_view sits, double picture_width, double picture_height, double width, double height);

	// How far something `length` long can go along a side `side` long: when it's
	// longer, from showing its far end to showing its near one; when it's shorter,
	// from one edge to the other.
	std::pair<double, double> slide_range(double side, double length);

	// Where the background is: sitting as it says, zoomed, and slid as far as it says,
	// never spilling over the collage's edges or leaving a gap where it covers it.
	box background_at(const backdrop& b, double picture_width, double picture_height);

	// Whether a background where it is has room to slide.
	bool slides(const backdrop& b, box at);

	// The whole picture a layer shows part of, `crop` in fractions of it, where it
	// would be on the collage.
	box whole_of(box shown, box crop);

	// The lines a layer moved or resized snaps to, across and down: the collage's
	// edges and middle, and each other layer's.
	struct lines {
		std::vector<double> across, down;
	};
	lines snap_lines(double width, double height, const std::vector<box>& others);

	// Where a gesture took a layer, and the lines it snapped to: one across, one
	// down, and a corner it sits in evenly, the collage's corner and its own.
	struct guided {
		box at;
		std::optional<double> guide_x, guide_y;
		std::optional<std::pair<point, point>> corner;
	};

	// Moved by dx and dy: its edges or middle drawn to a line `close` or nearer, and
	// into a corner evenly when it's as far from both edges there. Nothing snaps
	// unless `snapping`.
	guided moved(box from, double dx, double dy, const lines& to, double width, double height, double close, bool snapping);

	// Resized by a corner, 0 to 3 clockwise from the top left, the opposite one
	// staying put: its shape kept unless not `keep_shape`, and the edges it's dragged
	// by snapped, the one nearer a line when its shape is kept.
	guided resized(box from, int corner, double dx, double dy, bool keep_shape, const lines& to, double close, bool snapping);

	// A background slid by dx and dy, within how far it goes each way, and to the
	// middle of a collage `width` by `height` when it's `close` to it.
	guided slid(box from, double dx, double dy, std::pair<double, double> across, std::pair<double, double> down, double width, double height,
	            double close, bool snapping);

	// A crop's kept part dragged: moved within the whole picture when `edges` is 0,
	// or by its edges, 1 left, 2 top, 4 right and 8 bottom, never past the picture
	// or smaller than the smallest.
	box cropped(box kept, int edges, double dx, double dy, box whole);

	// The corner of a box a point is within `handle` of, 0 to 3 clockwise from the
	// top left, or -1.
	int corner_at(box r, point at, double handle);

	// The edges of a crop a point is on: 1 left, 2 top, 4 right and 8 bottom, two at
	// a corner; 0 inside it, to move it; or -1 outside.
	int crop_edges_at(box r, point at, double handle);

} // namespace one::screens::collage
