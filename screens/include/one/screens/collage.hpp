// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a collage's layers go: a picture made of layers, as big as it says, in its
// own pixels (decision 0021). Where a picture sits when it's placed, the whole picture
// behind a crop, and where a layer moved, resized or cropped ends up, snapped
// to the collage's edges and middle and every other layer's. It's the same model as
// @uione/react's collage.ts, and both are held to tests/collage-cases.json, so the
// desktop, the terminal and the web put a layer in the same place.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

	// The box a drag draws from `from` to `to`, whichever way it went: square when
	// `square`, as far as the longer side. A drag no further than `click` is a click,
	// which draws a square a fifth of the collage's shorter side, centered on it.
	box drawn(point from, point to, bool square, double click, double width, double height);

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

	// A shift made ready to move many pixels' colors, as a large picture's are: each
	// channel's share of the brightness and two colors, and the tone curve and how much
	// the colors follow it, looked up rather than worked out again for every pixel. Its
	// colors are within a hundredth of shifted's.
	class color_shifter {
	public:
		explicit color_shifter(const color_shift& s);
		// Red, green and blue, each 0 to 255, at every `stride` bytes of `pixels`, moved.
		void shift(std::uint8_t* pixels, std::size_t count, std::size_t stride = 4) const;

	private:
		static constexpr int steps = 16;  // a table's places for each of 256 brightnesses
		double share_[3][3][256];         // channel, then brightness and its two colors, by value
		std::vector<double> bright_, follow_;
		double gain_[3], offset_[3];
	};

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

	// A layer that's neither a picture nor words is a shape, as its parts say (decision
	// 0025): a rectangle, its corners rounded as far as corners says, a share of its
	// shorter side; an ellipse; or a path through its points, in SVG path notation, M, L,
	// C and Z, each point a fraction of its box from its top left. Each is filled and
	// stroked as it says, a color written #rrggbb or none, its stroke as wide as it says
	// in the collage's pixels, half of it outside its outline as SVG draws it.
	struct shape {
		std::string kind = "rectangle";  // rectangle, ellipse or path
		std::string points;
		std::string fill = "none", stroke = "none";
		double stroke_width = 0;
		double corners = 0;
	};

	// A line drawn by a drag from `from` to `to`, as a path: its box, a pixel wide
	// where it's straight across or down, and its two points in it. With `straight`, it
	// goes across, down or at 45 degrees, whichever it's nearest. A drag no further
	// than `click` is a click, which draws a line across a fifth of the collage's
	// shorter side, centered on it.
	struct line {
		box at;
		std::string points;
	};
	line drawn_line(point from, point to, bool straight, double click, double width, double height);

	// A path written with its points where they are, in SVG path notation, M, L, C and
	// Z, as an SVG broken apart gives each of its shapes: the box its points are in,
	// curves' control points too; and its points as fractions of a box, as a layer
	// keeps them.
	box path_bounds(std::string_view absolute);
	std::string path_fractions(std::string_view absolute, box in);

	// A line drawn by hand, from the points a drag went through: only those that keep its
	// shape, the rest within `tolerance` of the line between them left out, joined by a
	// smooth curve through each, as a path where it is, M and C. Nothing for fewer than
	// two.
	std::string freehand(const std::vector<point>& drawn, double tolerance);

	// A node the pen put down: where it is, and its handle, where a drag from it went,
	// from it; the curve leaves it that way and comes into it the other, so it's smooth.
	// No handle, it's a corner.
	struct pen_node {
		double x = 0, y = 0;
		double handle_x = 0, handle_y = 0;
	};
	// The path through them, where it is: a line between two corners, a curve where
	// either has a handle; closed, back to the first and Z.
	std::string pen_path(const std::vector<pen_node>& nodes, bool closed);

	// What a group is: the box around all it holds; nothing, around nothing.
	box around(const std::vector<box>& boxes);
	// Where one of a group's goes as the group goes from `from` to `to`: as far along
	// and as big for its size, so moved it moves with it, and resized it's resized.
	box carried(const box& one, const box& from, const box& to);

	// A path's nodes, where it is, written as path_in writes it: where each of its
	// segments ends, M, L and C alike, in order.
	std::vector<point> path_nodes(std::string_view absolute);
	// The path with its node at `node` moved by dx and dy, the control points either
	// side of it moving with it, as a drawing program moves them.
	std::string path_moved(std::string_view absolute, std::size_t node, double dx, double dy);
	// A control point of a path's node, where it is: the one its own curve comes into it
	// by, or the one the next curve leaves it by, `out`. A line's end has none.
	struct path_handle {
		point at;
		bool out = false;
	};
	std::vector<path_handle> path_handles(std::string_view absolute, std::size_t node);
	// The path with a control point of its node at `node` moved by dx and dy. When the
	// node's two were in line, as a smooth node's are, the other turns to stay in line,
	// as long as it was.
	std::string path_handle_moved(std::string_view absolute, std::size_t node, bool out, double dx, double dy);
	// Whether a press at `at` reaches the path: within `reach` of its outline, or, when
	// it's `filled` and closed, inside it.
	bool path_reaches(std::string_view absolute, point at, double reach, bool filled);

	// A path's points in the box they're fractions of, at x y, w by h: M 0 0 L 1 1 in a
	// 100 by 50 box at 10 20 is M 10 20 L 110 70. What isn't a command and a number is
	// left out.
	std::string path_in(std::string_view points, double x, double y, double w, double h);

	// The shape as one SVG element in the box at x y, w by h: <rect>, <ellipse> or <path>,
	// what a saved drawing holds and what the desktop draws.
	std::string svg_element(const shape& s, double x, double y, double w, double h);

	// As a whole SVG document, w by h and as much more around as its stroke reaches,
	// `reach` on each side, so none of it is cut off.
	std::string svg_alone(const shape& s, double w, double h, double reach);

	// A layer as a saved drawing holds it, in the collage's own units, `kind` saying
	// which it is: a shape; words, a line each, the top of their first at its top; a
	// picture's pixels, by an address that holds them, like a data: one, what its crop
	// keeps of them, mirrored then turned a quarter clockwise as many times as `turns`,
	// its corners round as far as they say; or an SVG's own text, `natural` its own
	// size, stretched to its box.
	struct saved_layer {
		std::string kind = "shape";  // shape, words, picture or svg
		box at;
		double opacity = 1;
		shape drawn = {};
		std::string words = "", color = "", family = "";
		double size = 0;
		int weight = 700;
		bool italic = false;
		std::string href = "";
		box crop = {0, 0, 1, 1};
		int turns = 0;
		bool mirrored = false;
		double corners = 0;
		std::string svg = "";
		box natural = {};
	};

	// A drawing saved as SVG: `width` by `height` in its own units, its layers lowest
	// first, from its vectors, nothing behind them, so it's clear wherever nothing is.
	std::string svg_document(double width, double height, const std::vector<saved_layer>& layers);

	// How far a collage matching its colors moves each picture's: its cast all the way,
	// so the same face in two lights looks alike, and the rest most of the way.
	inline constexpr double matching = 0.85, matching_cast = 1;

} // namespace one::screens::collage
