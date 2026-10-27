// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a collage's layers go: a picture made of layers, as big as it says, in its
// own pixels (decision 0021). Where a picture sits when it's placed, the whole picture
// behind a crop, and where a layer moved, resized or cropped ends up, snapped
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

	// Where a picture of its own size sits on a collage, as `sits` says: covering it
	// (fill), all of it showing (fit), stretched to it, or at its own size in the middle.
	box sitting(std::string_view sits, double picture_width, double picture_height, double width, double height);

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

	// A picture's gaps, the cells not `covered`, filled from what's around them
	// (harmonic fill): each settles to the average of its neighbors, so the colors at
	// every edge blend across the gap, like a film stretched between them. `colors` is
	// three channels a cell, row by row; the covered ones stay as they are. Worked out
	// on smaller grids first, then each larger one, so it settles in a few passes.
	std::vector<double> harmonic_fill(int width, int height, std::vector<double> colors, const std::vector<bool>& covered);

	// What a picture lays over itself to melt into what's under it: its own colors,
	// spread `reach` cells past each edge by harmonic fill, clear over its middle, whole
	// a cell either side of its edge, and gone a cell before the end of the reach,
	// smoothly each way. `inner` is how far in from that it starts to show. Four channels a cell, its alpha 0 to 255,
	// over a grid `reach` bigger than the picture on every side.
	std::vector<double> halo(int width, int height, const std::vector<double>& colors, int reach, double inner);

	// How far a harmonized or blended picture's halo reaches past its edges, as a share
	// of its shorter side, and how much of that it starts in from them: a little, so the
	// picture stays sharp nearly to its edge.
	inline constexpr double halo_reach = 0.15, halo_inside = 0.25;

	// How many of a picture's brightnesses its tones are told by: its darkest, its
	// brightest, and evenly between.
	inline constexpr int tone_points = 21;

	// A picture's colors, as matching them needs, each as brightness and two colors
	// (YCbCr: Y, then blue against yellow and red against green, 0 where it's gray): its
	// brightness's average and how far its cells spread from that (their standard
	// deviation); its cast, the two colors' average over its less colorful half alone,
	// its grays, whites and shadows, so a red hat doesn't read as a red light; how
	// strong its colors are, the middle of its cells' (their median), for both colors
	// alike, so a few vivid things don't make it all seem so; its tones, its cells' brightness from the darkest to the brightest at
	// each of the tone points (its quantiles); from three channels a cell, red, green
	// and blue; and how much of the collage it covers, which counts for that much.
	struct color_stats {
		double mean[3] = {0, 0, 0}, spread[3] = {0, 0, 0};
		std::vector<double> tones;
		double weight = 1;
	};
	color_stats stats_of(const std::vector<double>& colors, double weight);

	// What a picture's colors become, as brightness and two colors: its brightness along
	// a tone curve, through each of `from` to the one in `to` and straight between, and
	// each of the two colors times its gain and plus its offset.
	struct color_shift {
		double gain[3] = {1, 1, 1}, offset[3] = {0, 0, 0};
		std::vector<double> from, to;
	};

	// A color, red, green and blue in 0 to 255, as a shift makes it.
	void shifted(const color_shift& s, double rgb[3]);

	// Pictures' colors matched, so they look like one picture, by matching their
	// statistics (color transfer): each one's cast, the average of its two colors, moved
	// `cast` of the way to all of theirs, as a white balance would; its tones, its
	// darkest to its brightest, along a curve to all of theirs averaged, point by point,
	// so its contrast is theirs; and how strong its colors are; those `strength` of the
	// way; each 0 to 1, weighed by how much each covers. Its contrast is never made more
	// than three times or a third; its tones are moved half as far, none more than 24 of
	// 255; how strong its colors are is moved half as far, never past a quarter more or
	// a fifth less; one too even to tell, or nearly gray, is left as it is.
	std::vector<color_shift> matched(const std::vector<color_stats>& pictures, double strength, double cast);

	// How far a collage matching its colors moves each picture's: its cast all the way,
	// so the same face in two lights looks alike, and the rest most of the way.
	inline constexpr double matching = 0.85, matching_cast = 1;

} // namespace one::screens::collage
