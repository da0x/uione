// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A block diagram drawn as one::diagram lays it out: its blocks' outlines, its wires and
// their arrows, the dots where they branch, and what's written, set as equations are; as
// the web draws the same diagram.

#include <cmath>
#include <string>

#include <one/diagram.hpp>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	set_diagram diagram_set(const std::string& written, float em, color ink) {
		auto laid = one::diagram::lay_out(written);
		if (!laid) return {.problem = laid.error().says};
		const auto& d = *laid;
		std::vector<std::pair<set_equation, desktop::point>> faces;
		for (const auto& f : d.faces) {
			auto set = equation_set(f.written, em * static_cast<float>(f.size), false, ink);
			if (set.read) faces.emplace_back(set, desktop::point{static_cast<float>(f.x) * em, static_cast<float>(f.y) * em - set.ascent});
		}
		set_diagram out{.read = true, .width = static_cast<float>(d.width) * em, .height = static_cast<float>(d.height) * em};
		out.draw = [d, faces, em, ink](desktop::canvas& c, desktop::point at) {
			float stroke = 0.06f * em;
			auto place = [&](double x, double y) { return desktop::point{at.x + static_cast<float>(x) * em, at.y + static_cast<float>(y) * em}; };
			auto line = [&](desktop::point a, desktop::point b) { c.line(a, b, stroke, ink); };
			for (const auto& s : d.shapes) {
				auto top_left = place(s.x, s.y);
				float w = static_cast<float>(s.width) * em, h = static_cast<float>(s.height) * em;
				if (s.type == one::diagram::shape::kind::gain) {
					float back = s.flipped ? top_left.x + w : top_left.x, tip = s.flipped ? top_left.x : top_left.x + w;
					desktop::point a{back, top_left.y}, b{back, top_left.y + h}, p{tip, top_left.y + h / 2};
					line(a, b), line(b, p), line(p, a);
					continue;
				}
				float radius = s.type == one::diagram::shape::kind::sum ? w / 2 : s.type == one::diagram::shape::kind::port ? 0.3f * em : 0;
				c.box({top_left.x, top_left.y, w, h}, {.border = ink, .border_width = stroke, .radius = radius});
			}
			for (const auto& w : d.wires) {
				for (std::size_t i = 0; i + 1 < w.points.size(); ++i) line(place(w.points[i].first, w.points[i].second), place(w.points[i + 1].first, w.points[i + 1].second));
				if (!w.arrow || w.points.size() < 2) continue;
				// Its head, its tip where it ends, pointing the way it last runs.
				auto [tx, ty] = w.points.back();
				auto [px, py] = w.points[w.points.size() - 2];
				double run = std::hypot(tx - px, ty - py);
				if (run == 0) run = 1;
				double ux = (tx - px) / run, uy = (ty - py) / run, bx = tx - ux * 0.38, by = ty - uy * 0.38;
				c.fill({place(tx, ty), place(bx - uy * 0.16, by + ux * 0.16), place(bx + uy * 0.16, by - ux * 0.16)}, ink);
			}
			for (auto [x, y] : d.dots) {
				auto o = place(x, y);
				float r = 0.12f * em;
				c.box({o.x - r, o.y - r, 2 * r, 2 * r}, {.fill = ink, .radius = r});
			}
			for (const auto& [set, from] : faces) set.draw(c, {at.x + from.x, at.y + from.y});
		};
		return out;
	}

	desktop::view diagram(std::string written) {
		const auto& t = current_theme();
		set_diagram set = diagram_set(written, t.text_size * 1.25f, t.ink);
		// One that can't be read is shown as it's written, saying so in its color.
		if (!set.read) {
			auto s = t.code();
			s.color = t.danger;
			return desktop::text(std::move(written), s);
		}
		return desktop::row(desktop::drawing([set](desktop::canvas& c, desktop::rect area) { set.draw(c, {area.x, area.y}); }).width(set.width).height(set.height));
	}

}  // namespace one::desktop::runtime
