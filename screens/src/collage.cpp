// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/screens/collage.hpp"

#include <algorithm>
#include <cmath>

namespace one::screens::collage {

	namespace {

		bool slides_sitting(std::string_view sits) { return sits.empty() || sits == "fill" || sits == "fit"; }

	} // namespace

	box sitting(std::string_view sits, double picture_width, double picture_height, double width, double height) {
		if (sits == "stretch") return {0, 0, width, height};
		if (picture_width <= 0 || picture_height <= 0) return {0, 0, width, height};
		double s = sits == "fill" ? std::max(width / picture_width, height / picture_height)
		           : sits == "center" ? 1
		                              : std::min(width / picture_width, height / picture_height);
		double w = picture_width * s, h = picture_height * s;
		return {(width - w) / 2, (height - h) / 2, w, h};
	}

	std::pair<double, double> slide_range(double side, double length) { return {std::min(0.0, side - length), std::max(0.0, side - length)}; }

	box background_at(const backdrop& b, double picture_width, double picture_height) {
		box at = sitting(b.sits, picture_width, picture_height, b.width, b.height);
		double zoom = b.zoom > 0 ? b.zoom : 1;
		at = {at.x + at.w * (1 - zoom) / 2, at.y + at.h * (1 - zoom) / 2, at.w * zoom, at.h * zoom};
		if (slides_sitting(b.sits) || zoom != 1) {
			auto [left, right] = slide_range(b.width, at.w);
			auto [top, bottom] = slide_range(b.height, at.h);
			at.x = left + (right - left) * std::clamp(b.slide_x, 0.0, 1.0);
			at.y = top + (bottom - top) * std::clamp(b.slide_y, 0.0, 1.0);
		}
		return at;
	}

	bool slides(const backdrop& b, box at) {
		return (slides_sitting(b.sits) || (b.zoom > 0 && b.zoom != 1)) && (std::abs(at.w - b.width) > 0.5 || std::abs(at.h - b.height) > 0.5);
	}

	box whole_of(box shown, box crop) {
		double w = shown.w / crop.w, h = shown.h / crop.h;
		return {shown.x - crop.x * w, shown.y - crop.y * h, w, h};
	}

	lines snap_lines(double width, double height, const std::vector<box>& others) {
		lines out{{0, width / 2, width}, {0, height / 2, height}};
		for (const auto& o : others) {
			out.across.insert(out.across.end(), {o.x, o.x + o.w / 2, o.x + o.w});
			out.down.insert(out.down.end(), {o.y, o.y + o.h / 2, o.y + o.h});
		}
		return out;
	}

	guided moved(box from, double dx, double dy, const lines& to, double width, double height, double close, bool snapping) {
		guided out{{from.x + dx, from.y + dy, from.w, from.h}, {}, {}, {}};
		if (!snapping) return out;
		box& b = out.at;
		// Its edges or its middle, whichever is nearest a line.
		auto snap = [&](double start, double size, const std::vector<double>& lines_to, std::optional<double>& guide) {
			double best = close + 1, by = 0;
			for (double line : lines_to) {
				for (double edge : {start, start + size / 2, start + size}) {
					if (std::abs(line - edge) < std::abs(best)) {
						best = line - edge;
						by = line;
					}
				}
			}
			if (std::abs(best) > close) return start;
			guide = by;
			return start + best;
		};
		b.x = snap(b.x, b.w, to.across, out.guide_x);
		b.y = snap(b.y, b.h, to.down, out.guide_y);
		// Evenly in a corner: as far from the edge across as from the edge down there,
		// the way not snapped yet following the other.
		bool east = b.x + b.w / 2 > width / 2, south = b.y + b.h / 2 > height / 2;
		double margin_x = east ? width - (b.x + b.w) : b.x, margin_y = south ? height - (b.y + b.h) : b.y;
		bool snapped_x = out.guide_x.has_value(), snapped_y = out.guide_y.has_value();
		if (margin_x > close && margin_y > close && std::abs(margin_x - margin_y) <= close && !(snapped_x && snapped_y)) {
			if (!snapped_y) b.y = south ? height - b.h - margin_x : margin_x;
			else b.x = east ? width - b.w - margin_y : margin_y;
			double margin = snapped_y ? margin_y : margin_x;
			out.corner = std::pair{point{east ? width : 0, south ? height : 0}, point{east ? width - margin : margin, south ? height - margin : margin}};
		}
		return out;
	}

	guided resized(box from, int corner, double dx, double dy, bool keep_shape, const lines& to, double close, bool snapping) {
		double left = from.x, top = from.y, right = from.x + from.w, bottom = from.y + from.h;
		bool west = corner == 0 || corner == 3, north = corner == 0 || corner == 1;
		double w = std::max(smallest, west ? right - (left + dx) : right + dx - left);
		double h = std::max(smallest, north ? bottom - (top + dy) : bottom + dy - top);
		bool kept = keep_shape && from.w > 0 && from.h > 0;
		if (kept) {
			double grown = std::max(w / from.w, h / from.h);
			w = from.w * grown;
			h = from.h * grown;
		}
		guided out;
		if (snapping) {
			auto to_line = [&](double edge, const std::vector<double>& lines_to) -> std::optional<double> {
				std::optional<double> best;
				for (double line : lines_to) {
					if (std::abs(line - edge) <= close && (!best || std::abs(line - edge) < std::abs(*best - edge))) best = line;
				}
				return best;
			};
			auto across = to_line(west ? right - w : left + w, to.across), down = to_line(north ? bottom - h : top + h, to.down);
			auto width_at = [&](double line) { return std::max(smallest, west ? right - line : line - left); };
			auto height_at = [&](double line) { return std::max(smallest, north ? bottom - line : line - top); };
			if (kept) {
				double off_across = across ? std::abs(width_at(*across) - w) : 1e9, off_down = down ? std::abs(height_at(*down) - h) : 1e9;
				if (across && off_across <= off_down) {
					w = width_at(*across);
					h = w * from.h / from.w;
					out.guide_x = across;
				} else if (down) {
					h = height_at(*down);
					w = h * from.w / from.h;
					out.guide_y = down;
				}
			} else {
				if (across) {
					w = width_at(*across);
					out.guide_x = across;
				}
				if (down) {
					h = height_at(*down);
					out.guide_y = down;
				}
			}
		}
		out.at = {west ? right - w : left, north ? bottom - h : top, w, h};
		return out;
	}

	guided slid(box from, double dx, double dy, std::pair<double, double> across, std::pair<double, double> down, double width, double height,
	            double close, bool snapping) {
		guided out{from, {}, {}, {}};
		box& b = out.at;
		b.x = std::clamp(b.x + dx, across.first, across.second);
		b.y = std::clamp(b.y + dy, down.first, down.second);
		if (!snapping) return out;
		double middle_x = (across.first + across.second) / 2, middle_y = (down.first + down.second) / 2;
		if (across.second > across.first && std::abs(b.x - middle_x) <= close) {
			b.x = middle_x;
			out.guide_x = width / 2;
		}
		if (down.second > down.first && std::abs(b.y - middle_y) <= close) {
			b.y = middle_y;
			out.guide_y = height / 2;
		}
		return out;
	}

	box cropped(box kept, int edges, double dx, double dy, box whole) {
		if (edges == 0) {
			return {std::clamp(kept.x + dx, whole.x, whole.x + whole.w - kept.w), std::clamp(kept.y + dy, whole.y, whole.y + whole.h - kept.h), kept.w, kept.h};
		}
		double left = kept.x, top = kept.y, right = kept.x + kept.w, bottom = kept.y + kept.h;
		if (edges & 1) left = std::clamp(left + dx, whole.x, right - smallest);
		if (edges & 4) right = std::clamp(right + dx, left + smallest, whole.x + whole.w);
		if (edges & 2) top = std::clamp(top + dy, whole.y, bottom - smallest);
		if (edges & 8) bottom = std::clamp(bottom + dy, top + smallest, whole.y + whole.h);
		return {left, top, right - left, bottom - top};
	}

	int corner_at(box r, point at, double handle) {
		const point corners[] = {{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}};
		for (int i = 0; i < 4; ++i) {
			if (std::abs(at.x - corners[i].x) <= handle && std::abs(at.y - corners[i].y) <= handle) return i;
		}
		return -1;
	}

	int crop_edges_at(box r, point at, double handle) {
		bool across = at.x >= r.x - handle && at.x <= r.x + r.w + handle, down = at.y >= r.y - handle && at.y <= r.y + r.h + handle;
		if (!across || !down) return -1;
		int edges = 0;
		if (std::abs(at.x - r.x) <= handle) edges |= 1;
		else if (std::abs(at.x - (r.x + r.w)) <= handle) edges |= 4;
		if (std::abs(at.y - r.y) <= handle) edges |= 2;
		else if (std::abs(at.y - (r.y + r.h)) <= handle) edges |= 8;
		return edges || r.contains(at) ? edges : -1;
	}

} // namespace one::screens::collage
