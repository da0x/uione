// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/screens/collage.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace one::screens::collage {

	box sitting(std::string_view sits, double picture_width, double picture_height, double width, double height) {
		if (sits == "stretch") return {0, 0, width, height};
		if (picture_width <= 0 || picture_height <= 0) return {0, 0, width, height};
		double s = sits == "fill" ? std::max(width / picture_width, height / picture_height)
		           : sits == "center" ? 1
		                              : std::min(width / picture_width, height / picture_height);
		double w = picture_width * s, h = picture_height * s;
		return {(width - w) / 2, (height - h) / 2, w, h};
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

	box drawn(point from, point to, bool square, double click, double width, double height) {
		double dx = to.x - from.x, dy = to.y - from.y;
		if (std::abs(dx) <= click && std::abs(dy) <= click) {
			double side = std::min(width, height) / 5;
			return {from.x - side / 2, from.y - side / 2, side, side};
		}
		if (square) {
			double side = std::max(std::abs(dx), std::abs(dy));
			dx = dx < 0 ? -side : side;
			dy = dy < 0 ? -side : side;
		}
		return {std::min(from.x, from.x + dx), std::min(from.y, from.y + dy), std::abs(dx), std::abs(dy)};
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

	std::vector<double> harmonic_fill(int width, int height, std::vector<double> colors, const std::vector<bool>& covered) {
		auto cell = [&](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); };
		if (width <= 0 || height <= 0 || std::ranges::none_of(covered, [](bool c) { return c; })) return colors;
		// Each gap starts from a grid half as big, or from the average of what's covered.
		if (width > 2 && height > 2) {
			int cw = (width + 1) / 2, ch = (height + 1) / 2;
			std::vector<double> small(static_cast<std::size_t>(cw) * ch * 3, 0);
			std::vector<bool> held(static_cast<std::size_t>(cw) * ch, false);
			for (int y = 0; y < ch; ++y) {
				for (int x = 0; x < cw; ++x) {
					double sum[3] = {0, 0, 0};
					int count = 0;
					for (int fy = y * 2; fy < std::min(height, y * 2 + 2); ++fy) {
						for (int fx = x * 2; fx < std::min(width, x * 2 + 2); ++fx) {
							if (!covered[cell(fx, fy)]) continue;
							for (int c = 0; c < 3; ++c) sum[c] += colors[cell(fx, fy) * 3 + c];
							++count;
						}
					}
					std::size_t at = static_cast<std::size_t>(y) * cw + x;
					held[at] = count > 0;
					for (int c = 0; c < 3; ++c) small[at * 3 + c] = count > 0 ? sum[c] / count : 0;
				}
			}
			small = harmonic_fill(cw, ch, std::move(small), held);
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					if (covered[cell(x, y)]) continue;
					std::size_t from = (static_cast<std::size_t>(y / 2) * cw + x / 2) * 3;
					for (int c = 0; c < 3; ++c) colors[cell(x, y) * 3 + c] = small[from + c];
				}
			}
		} else {
			double sum[3] = {0, 0, 0};
			int count = 0;
			for (std::size_t i = 0; i < covered.size(); ++i) {
				if (!covered[i]) continue;
				for (int c = 0; c < 3; ++c) sum[c] += colors[i * 3 + c];
				++count;
			}
			for (std::size_t i = 0; i < covered.size(); ++i) {
				if (!covered[i]) for (int c = 0; c < 3; ++c) colors[i * 3 + c] = sum[c] / count;
			}
		}
		// Then each gap is the average of its neighbors, row by row, again and again.
		int passes = width > 2 && height > 2 ? 40 : 200;
		for (int pass = 0; pass < passes; ++pass) {
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					if (covered[cell(x, y)]) continue;
					double sum[3] = {0, 0, 0};
					int count = 0;
					auto add = [&](int nx, int ny) {
						if (nx < 0 || ny < 0 || nx >= width || ny >= height) return;
						for (int c = 0; c < 3; ++c) sum[c] += colors[cell(nx, ny) * 3 + c];
						++count;
					};
					add(x - 1, y), add(x + 1, y), add(x, y - 1), add(x, y + 1);
					for (int c = 0; c < 3; ++c) colors[cell(x, y) * 3 + c] = sum[c] / count;
				}
			}
		}
		return colors;
	}

	std::vector<double> halo(int width, int height, const std::vector<double>& colors, int reach, double inner) {
		int ow = width + 2 * reach, oh = height + 2 * reach;
		if (width <= 0 || height <= 0 || reach < 0) return {};
		std::vector<double> spread(static_cast<std::size_t>(ow) * oh * 3, 0);
		std::vector<bool> covered(static_cast<std::size_t>(ow) * oh, false);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				std::size_t at = static_cast<std::size_t>(y + reach) * ow + (x + reach);
				covered[at] = true;
				for (int c = 0; c < 3; ++c) spread[at * 3 + c] = colors[(static_cast<std::size_t>(y) * width + x) * 3 + c];
			}
		}
		spread = harmonic_fill(ow, oh, std::move(spread), covered);
		auto smooth = [](double from, double to, double v) {
			double t = to > from ? std::clamp((v - from) / (to - from), 0.0, 1.0) : (v >= to ? 1.0 : 0.0);
			return t * t * (3 - 2 * t);
		};
		std::vector<double> out(static_cast<std::size_t>(ow) * oh * 4, 0);
		for (int y = 0; y < oh; ++y) {
			for (int x = 0; x < ow; ++x) {
				// How far the cell's middle is outside the picture, or, less than none, inside it.
				double px = x + 0.5 - reach, py = y + 0.5 - reach;
				double ox = std::max({0.0, -px, px - width}), oy = std::max({0.0, -py, py - height});
				double d = ox > 0 || oy > 0 ? std::hypot(ox, oy) : -std::min({px, width - px, py, height - py});
				// Whole a cell either side of its edge, so drawn bigger, smoothly between cells, it's
				// whole on the edge itself and the picture's own edge never shows; and gone a
				// cell before the reach's end, so it fades to nothing before the grid ends.
				double alpha = d <= 0 ? smooth(-inner - 1, -1, d) : 1 - smooth(1, reach - 1, d);
				std::size_t at = static_cast<std::size_t>(y) * ow + x;
				for (int c = 0; c < 3; ++c) out[at * 4 + c] = spread[at * 3 + c];
				out[at * 4 + 3] = alpha * 255;
			}
		}
		return out;
	}

	namespace {

		// Red, green and blue as brightness and two colors, and back (YCbCr, as JPEG has it).
		void to_ycc(const double rgb[3], double out[3]) {
			out[0] = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2];
			out[1] = -0.168736 * rgb[0] - 0.331264 * rgb[1] + 0.5 * rgb[2];
			out[2] = 0.5 * rgb[0] - 0.418688 * rgb[1] - 0.081312 * rgb[2];
		}
		void to_rgb(const double ycc[3], double out[3]) {
			out[0] = ycc[0] + 1.402 * ycc[2];
			out[1] = ycc[0] - 0.344136 * ycc[1] - 0.714136 * ycc[2];
			out[2] = ycc[0] + 1.772 * ycc[1];
		}

	} // namespace

	color_stats stats_of(const std::vector<double>& colors, double weight) {
		color_stats out;
		out.weight = weight;
		std::size_t cells = colors.size() / 3;
		if (cells == 0) return out;
		std::vector<double> ycc(cells * 3);
		for (std::size_t i = 0; i < cells; ++i) {
			to_ycc(&colors[i * 3], &ycc[i * 3]);
			for (int c = 0; c < 3; ++c) out.mean[c] += ycc[i * 3 + c];
		}
		out.mean[0] /= static_cast<double>(cells);
		for (std::size_t i = 0; i < cells; ++i) out.spread[0] += (ycc[i * 3] - out.mean[0]) * (ycc[i * 3] - out.mean[0]);
		out.spread[0] = std::sqrt(out.spread[0] / static_cast<double>(cells));
		// How colorful each cell is, and the middle of them.
		std::vector<double> colorful(cells);
		for (std::size_t i = 0; i < cells; ++i) colorful[i] = std::hypot(ycc[i * 3 + 1], ycc[i * 3 + 2]);
		std::vector<double> sorted = colorful;
		std::ranges::sort(sorted);
		double middle = cells % 2 ? sorted[cells / 2] : (sorted[cells / 2 - 1] + sorted[cells / 2]) / 2;
		out.spread[1] = out.spread[2] = middle;
		// Its cast, from the cells no more colorful than that.
		double cast[2] = {0, 0};
		std::size_t plain = 0;
		for (std::size_t i = 0; i < cells; ++i) {
			if (colorful[i] > middle) continue;
			cast[0] += ycc[i * 3 + 1], cast[1] += ycc[i * 3 + 2];
			++plain;
		}
		out.mean[1] = cast[0] / static_cast<double>(plain);
		out.mean[2] = cast[1] / static_cast<double>(plain);
		std::vector<double> bright(cells);
		for (std::size_t i = 0; i < cells; ++i) bright[i] = ycc[i * 3];
		std::ranges::sort(bright);
		for (int q = 0; q < tone_points; ++q) {
			double at = static_cast<double>(cells - 1) * q / (tone_points - 1);
			std::size_t low = static_cast<std::size_t>(at);
			double part = at - static_cast<double>(low);
			out.tones.push_back(low + 1 < cells ? bright[low] + (bright[low + 1] - bright[low]) * part : bright[low]);
		}
		return out;
	}

	void shifted(const color_shift& s, double rgb[3]) {
		double ycc[3];
		to_ycc(rgb, ycc);
		// Its brightness along the tone curve, straight between its points, and as far
		// past its ends as it is from them.
		const auto& from = s.from;
		const auto& to = s.to;
		if (from.size() >= 2 && from.size() == to.size()) {
			double y = ycc[0];
			std::size_t i = 0;
			while (i + 2 < from.size() && y > from[i + 1]) ++i;
			double span = from[i + 1] - from[i];
			ycc[0] = span > 1e-9 ? to[i] + (to[i + 1] - to[i]) * (y - from[i]) / span : to[i] + (y - from[i]);
			if (y < from.front()) ycc[0] = to.front() + (y - from.front());
			if (y > from.back()) ycc[0] = to.back() + (y - from.back());
			// Its colors as much stronger or weaker as it's brighter or darker, so it
			// looks as colorful as it was: darker with the same colors looks more so.
			double follow = std::clamp((ycc[0] + 16) / (y + 16), 0.5, 2.0);
			ycc[1] *= follow, ycc[2] *= follow;
		} else {
			ycc[0] = ycc[0] * s.gain[0] + s.offset[0];
		}
		for (int c = 1; c < 3; ++c) ycc[c] = ycc[c] * s.gain[c] + s.offset[c];
		to_rgb(ycc, rgb);
	}

	color_shifter::color_shifter(const color_shift& s) {
		for (int c = 0; c < 3; ++c) {
			gain_[c] = s.gain[c], offset_[c] = s.offset[c];
			for (int v = 0; v < 256; ++v) {
				double rgb[3] = {0, 0, 0}, ycc[3];
				rgb[c] = v;
				to_ycc(rgb, ycc);
				for (int k = 0; k < 3; ++k) share_[c][k][v] = ycc[k];
			}
		}
		// Brightness reaches past 0 to 255 only by rounding, so the tables cover that
		// and a step either side.
		std::size_t places = 257 * steps + 1;
		bright_.resize(places), follow_.resize(places);
		bool toned = s.from.size() >= 2 && s.from.size() == s.to.size();
		for (std::size_t i = 0; i < places; ++i) {
			double y = static_cast<double>(i) / steps - 1;
			if (!toned) {
				bright_[i] = y * s.gain[0] + s.offset[0], follow_[i] = 1;
				continue;
			}
			const auto& from = s.from;
			const auto& to = s.to;
			std::size_t at = 0;
			while (at + 2 < from.size() && y > from[at + 1]) ++at;
			double span = from[at + 1] - from[at];
			double made = span > 1e-9 ? to[at] + (to[at + 1] - to[at]) * (y - from[at]) / span : to[at] + (y - from[at]);
			if (y < from.front()) made = to.front() + (y - from.front());
			if (y > from.back()) made = to.back() + (y - from.back());
			bright_[i] = made;
			follow_[i] = std::clamp((made + 16) / (y + 16), 0.5, 2.0);
		}
	}

	void color_shifter::shift(std::uint8_t* pixels, std::size_t count, std::size_t stride) const {
		for (std::size_t n = 0; n < count; ++n, pixels += stride) {
			double ycc[3];
			for (int k = 0; k < 3; ++k) ycc[k] = share_[0][k][pixels[0]] + share_[1][k][pixels[1]] + share_[2][k][pixels[2]];
			// The curve's places either side of this brightness, and straight between.
			double at = std::clamp((ycc[0] + 1) * steps, 0.0, static_cast<double>(bright_.size() - 2));
			std::size_t below = static_cast<std::size_t>(at);
			double part = at - static_cast<double>(below);
			double bright = bright_[below] + (bright_[below + 1] - bright_[below]) * part;
			double follow = follow_[below] + (follow_[below + 1] - follow_[below]) * part;
			ycc[0] = bright;
			for (int c = 1; c < 3; ++c) ycc[c] = ycc[c] * follow * gain_[c] + offset_[c];
			double rgb[3];
			to_rgb(ycc, rgb);
			for (int c = 0; c < 3; ++c) pixels[c] = static_cast<std::uint8_t>(std::clamp(std::lround(rgb[c]), 0L, 255L));
		}
	}

	std::vector<color_shift> matched(const std::vector<color_stats>& pictures, double strength, double cast) {
		double total = 0, mean[3] = {0, 0, 0}, spread[3] = {0, 0, 0};
		for (const auto& p : pictures) {
			total += p.weight;
			for (int c = 0; c < 3; ++c) mean[c] += p.mean[c] * p.weight, spread[c] += p.spread[c] * p.weight;
		}
		std::vector<color_shift> out(pictures.size());
		if (total <= 0) return out;
		for (int c = 0; c < 3; ++c) mean[c] /= total, spread[c] /= total;
		// Their tones together: each point the average of theirs there, weighed alike.
		std::vector<double> tones(tone_points, 0);
		bool toned = std::ranges::all_of(pictures, [](const color_stats& p) { return p.tones.size() == static_cast<std::size_t>(tone_points); });
		for (const auto& p : pictures) {
			for (int q = 0; toned && q < tone_points; ++q) tones[static_cast<std::size_t>(q)] += p.tones[static_cast<std::size_t>(q)] * p.weight / total;
		}
		double k = std::clamp(strength, 0.0, 1.0), toward_cast = std::clamp(cast, 0.0, 1.0);
		for (std::size_t i = 0; i < pictures.size(); ++i) {
			for (int c = 0; c < 3; ++c) {
				const auto& p = pictures[i];
				// How strong its colors are changes only half as far, and only so far, so a
				// colorful picture keeps most of it; and one nearly gray is left so, never
				// colorized.
				double g = c == 0 ? (p.spread[c] < 1 ? 1 : std::clamp(spread[c] / p.spread[c], 1.0 / 3, 3.0))
				                  : (p.spread[c] < 4 ? 1 : std::clamp(spread[c] / p.spread[c], 0.8, 1.25));
				double toward = c == 0 ? k : k / 2;
				// Spread about its own average, part of the way; then that average moved
				// toward all of theirs, the two colors' as far as their cast is.
				out[i].gain[c] = 1 - toward + toward * g;
				out[i].offset[c] = p.mean[c] * (1 - out[i].gain[c]) + (c == 0 ? k : toward_cast) * (mean[c] - p.mean[c]);
			}
			// Its brightness along a curve from its tones to theirs, part of the way.
			if (toned) {
				out[i].from = pictures[i].tones;
				// Half as far as the rest, and each tone no more than 24 of 255, so a bright
				// picture comes nearer darker ones without turning dark.
				for (int q = 0; q < tone_points; ++q) {
					double own = pictures[i].tones[static_cast<std::size_t>(q)];
					out[i].to.push_back(own + std::clamp(k / 2 * (tones[static_cast<std::size_t>(q)] - own), -24.0, 24.0));
				}
			}
		}
		return out;
	}

	namespace {
		// A number as SVG writes it: no more decimals than it needs, up to three.
		std::string said(double n) {
			std::string out = std::format("{:.3f}", std::abs(n) < 0.0005 ? 0.0 : n);
			while (out.back() == '0') out.pop_back();
			if (out.back() == '.') out.pop_back();
			return out;
		}

		// A color as it may be written into SVG: #rrggbb, or none for anything else.
		std::string paint(const std::string& written) {
			bool hex = written.size() == 7 && written[0] == '#' &&
			           std::all_of(written.begin() + 1, written.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
			return hex ? written : "none";
		}
	}  // namespace

	std::string path_in(std::string_view points, double x, double y, double w, double h) {
		std::string out;
		bool across = true;  // the next number is a point's x
		for (std::size_t at = 0; at < points.size();) {
			char c = points[at];
			if (c == 'M' || c == 'L' || c == 'C' || c == 'Z') {
				out += (out.empty() ? "" : " ") + std::string(1, c);
				across = true;
				++at;
				continue;
			}
			if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.') {
				std::size_t end = at + 1;
				while (end < points.size() && (std::isdigit(static_cast<unsigned char>(points[end])) || points[end] == '.' || points[end] == 'e')) ++end;
				double n = 0;
				try {
					n = std::stod(std::string(points.substr(at, end - at)));
				} catch (...) {
					n = 0;
				}
				out += " " + said(across ? x + n * w : y + n * h);
				across = !across;
				at = end;
				continue;
			}
			++at;  // a space or a comma between them
		}
		return out;
	}

	std::string svg_element(const shape& s, double x, double y, double w, double h) {
		std::string drawn = std::format(R"(fill="{}" stroke="{}" stroke-width="{}")", paint(s.fill), paint(s.stroke), said(s.stroke_width));
		if (s.kind == "ellipse") return std::format(R"(<ellipse cx="{}" cy="{}" rx="{}" ry="{}" {}/>)", said(x + w / 2), said(y + h / 2), said(w / 2), said(h / 2), drawn);
		if (s.kind == "path") return std::format(R"(<path d="{}" {}/>)", path_in(s.points, x, y, w, h), drawn);
		double round = std::clamp(s.corners, 0.0, 0.5) * std::min(w, h);
		return std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" rx="{}" {}/>)", said(x), said(y), said(w), said(h), said(round), drawn);
	}

	std::string svg_alone(const shape& s, double w, double h, double reach) {
		return std::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" viewBox="{} {} {} {}">{}</svg>)", said(w + 2 * reach), said(h + 2 * reach), said(-reach),
		                   said(-reach), said(w + 2 * reach), said(h + 2 * reach), svg_element(s, 0, 0, w, h));
	}

} // namespace one::screens::collage
