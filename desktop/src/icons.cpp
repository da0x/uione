// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The icons a button can be drawn as, the web's own: each its SVG on a 16 by 16
// grid, its paths, rectangles and circles stroked 1.5 wide with round ends and
// joins, and filled where the web fills them, scaled to the size it's drawn at.

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include "one/desktop/icons.hpp"

namespace one::desktop {

	namespace {

		struct icon_part {
			enum class kind { path, rect, circle } shape = kind::path;
			std::string d = "";                   // a path's commands
			float x = 0, y = 0, w = 0, h = 0, r = 0;  // a rectangle's, with its corners' radius, or a circle's center and radius
			bool filled = false;
			float dash = 0;  // a dashed line's dashes and gaps, each this long
		};

		using drawing = std::vector<icon_part>;

		icon_part path(std::string d, bool filled = false) { return {.d = std::move(d), .filled = filled}; }
		icon_part box(float x, float y, float w, float h, float r) { return {.shape = icon_part::kind::rect, .x = x, .y = y, .w = w, .h = h, .r = r}; }
		icon_part ring(float x, float y, float r) { return {.shape = icon_part::kind::circle, .x = x, .y = y, .r = r}; }

		const std::map<std::string, drawing>& icons() {
			static const std::map<std::string, drawing> all{
			    {"edit", {path("M11.2 2.3a1.4 1.4 0 0 1 2 2L5.6 11.9 2.8 12.7l.8-2.8z"), path("M10 3.5l2 2")}},
			    {"add", {path("M8 3v10M3 8h10")}},
			    {"follow", {path("M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z"), path("M6.6 14a1.5 1.5 0 0 0 2.8 0")}},
			    {"following", {path("M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z", true), path("M6.6 14a1.5 1.5 0 0 0 2.8 0")}},
			    // A cog: a ring with teeth, and its hole.
			    {"settings",
			     {path("M6.9 1.5h2.2l.4 1.8 1.2.7 1.8-.6 1.1 1.9-1.4 1.3v1.4l1.4 1.3-1.1 1.9-1.8-.6-1.2.7-.4 1.8H6.9l-.4-1.8-1.2-.7-1.8.6-1.1-1.9 1.4-1.3V7.3L2.4 "
			           "6l1.1-1.9 1.8.6 1.2-.7z"),
			      ring(8, 8, 2)}},
			    {"workflow", {box(1.5f, 2, 4.5f, 4, 1), box(10, 10, 4.5f, 4, 1), path("M6 4h2.5a1.5 1.5 0 0 1 1.5 1.5V10")}},
			    // A bin with its lid.
			    {"delete", {path("M2.5 4.5h11M6.5 4.5V3a1 1 0 0 1 1-1h1a1 1 0 0 1 1 1v1.5"), path("M3.8 4.5l.7 8.6a1 1 0 0 0 1 .9h5a1 1 0 0 0 1-.9l.7-8.6M6.7 7v4.5M9.3 7v4.5")}},
			    // A box with its lid, what's put away.
			    {"archive", {box(1.5f, 2.5f, 13, 3, 1), path("M2.8 5.5v7a1 1 0 0 0 1 1h8.4a1 1 0 0 0 1-1v-7M6.5 8.5h3")}},
			    // The ways a switch shows a list.
			    {"pipeline", {ring(3, 5, 1.5f), ring(8, 5, 1.5f), ring(13, 5, 1.5f), path("M4.5 5h2M9.5 5h2M2 11h12")}},
			    {"whiteboard", {box(2, 2.5f, 12, 11, 1.5f), icon_part{.d = "M8 2.5v11", .dash = 1.5f}, box(3.5f, 4.5f, 3, 3, 0.5f), box(9.5f, 8, 3, 3, 0.5f)}},
			    {"table", {box(2, 2.5f, 12, 11, 1.5f), path("M2 6h12M2 9.5h12")}},
			    {"board", {box(2, 2.5f, 3.5f, 11, 1), box(6.25f, 2.5f, 3.5f, 7, 1), box(10.5f, 2.5f, 3.5f, 9, 1)}},
			};
			return all;
		}

		// A path's outlines, each a run of points, its arcs made of short lines.
		std::vector<std::vector<point>> outlines(const std::string& d) {
			std::vector<std::vector<point>> out;
			std::size_t at = 0;
			auto skip = [&] {
				while (at < d.size() && (std::isspace(static_cast<unsigned char>(d[at])) || d[at] == ',')) ++at;
			};
			auto number = [&] {
				skip();
				const char* start = d.c_str() + at;
				char* end = nullptr;
				float v = std::strtof(start, &end);
				at += static_cast<std::size_t>(end - start);
				return v;
			};
			point now{}, began{};
			char command = 0;
			auto to = [&](point p) {
				if (out.empty()) out.push_back({now});
				out.back().push_back(p);
				now = p;
			};
			// An arc of a circle from now to the end, as SVG says one: by its radius, which
			// of the two ways round, and which side.
			auto arc = [&](float radius, bool large, bool sweep, point end) {
				float dx = (now.x - end.x) / 2, dy = (now.y - end.y) / 2;
				float half = std::sqrt(dx * dx + dy * dy);
				if (half < 1e-4f) return;
				radius = std::max(radius, half);
				float across = std::sqrt(std::max(0.f, radius * radius - half * half)) / half;
				if (large == sweep) across = -across;
				point middle{(now.x + end.x) / 2, (now.y + end.y) / 2};
				point center{middle.x - across * dy, middle.y + across * dx};
				float from = std::atan2(now.y - center.y, now.x - center.x), until = std::atan2(end.y - center.y, end.x - center.x);
				float turn = until - from;
				if (sweep && turn < 0) turn += 2 * std::numbers::pi_v<float>;
				if (!sweep && turn > 0) turn -= 2 * std::numbers::pi_v<float>;
				int steps = std::max(4, static_cast<int>(std::abs(turn) / (std::numbers::pi_v<float> / 12)));
				for (int i = 1; i <= steps; ++i) {
					float a = from + turn * static_cast<float>(i) / static_cast<float>(steps);
					to(i == steps ? end : point{center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
				}
			};
			while (true) {
				skip();
				if (at >= d.size()) break;
				if (std::isalpha(static_cast<unsigned char>(d[at]))) command = d[at++];
				bool relative = std::islower(static_cast<unsigned char>(command));
				point base = relative ? now : point{};
				switch (std::tolower(static_cast<unsigned char>(command))) {
					case 'm': {
						float x = number(), y = number();
						now = began = {base.x + x, base.y + y};
						out.push_back({now});
						command = relative ? 'l' : 'L';  // what follows a move is drawn to
						break;
					}
					case 'l': {
						float x = number(), y = number();
						to({base.x + x, base.y + y});
						break;
					}
					case 'h': to({(relative ? now.x : 0) + number(), now.y}); break;
					case 'v': to({now.x, (relative ? now.y : 0) + number()}); break;
					case 'a': {
						float rx = number();
						(void)number();  // ry, the same for a circle's
						(void)number();  // its turn, none for a circle
						bool large = number() != 0, sweep = number() != 0;
						float x = number(), y = number();
						arc(rx, large, sweep, {base.x + x, base.y + y});
						break;
					}
					case 'z':
						to(began);
						break;
					default: return out;  // what an icon here never says
				}
			}
			return out;
		}

	}  // namespace

	bool has_icon(const std::string& name) { return icons().contains(name); }

	void draw_icon(canvas& c, const std::string& name, rect area, color tint) {
		auto found = icons().find(name);
		if (found == icons().end()) return;
		float s = std::min(area.width, area.height) / 16;
		point origin{area.x + (area.width - 16 * s) / 2, area.y + (area.height - 16 * s) / 2};
		auto at = [&](point p) { return point{origin.x + p.x * s, origin.y + p.y * s}; };
		float stroke = 1.5f * s;
		for (const auto& part : found->second) {
			switch (part.shape) {
				case icon_part::kind::rect: {
					// A stroke on the edge, half in and half out, as SVG draws one.
					rect r{origin.x + (part.x - 0.75f) * s, origin.y + (part.y - 0.75f) * s, (part.w + 1.5f) * s, (part.h + 1.5f) * s};
					c.box(r, {.border = tint, .border_width = stroke, .radius = (part.r + 0.75f) * s});
					break;
				}
				case icon_part::kind::circle: {
					float r = (part.r + 0.75f) * s;
					point center = at({part.x, part.y});
					c.box({center.x - r, center.y - r, 2 * r, 2 * r}, {.border = tint, .border_width = stroke, .radius = r});
					break;
				}
				case icon_part::kind::path:
					for (const auto& outline : outlines(part.d)) {
						std::vector<point> placed;
						for (const auto& p : outline) placed.push_back(at(p));
						if (part.filled) c.fill(placed, tint);
						for (std::size_t i = 0; i + 1 < placed.size(); ++i) {
							point a = placed[i], b = placed[i + 1];
							if (part.dash <= 0) {
								c.line(a, b, stroke, tint);
								continue;
							}
							float length = std::hypot(b.x - a.x, b.y - a.y), dash = part.dash * s;
							for (float from = 0; from < length; from += 2 * dash) {
								float until = std::min(length, from + dash);
								c.line({a.x + (b.x - a.x) * from / length, a.y + (b.y - a.y) * from / length},
								       {a.x + (b.x - a.x) * until / length, a.y + (b.y - a.y) * until / length}, stroke, tint);
							}
						}
					}
					break;
			}
		}
	}

}  // namespace one::desktop
