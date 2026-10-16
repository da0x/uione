// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Charts and monitors, as the web draws them: a chart is a run of numbers over time,
// newest at the right, its title and latest value in ink over a line in the accent;
// a monitor is a tile for each record whose status changes, like an agent, with its
// dot and state, what it's doing and how far, and its charts.

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		color tone_of(const theme& t, const std::string& tone) {
			if (tone == "working") return color::hex(0xd9730d);
			if (tone == "waiting") return t.accent;
			if (tone == "success") return t.success;
			if (tone == "failed") return t.danger;
			return t.muted;
		}

		desktop::view drawn_chart(const chart_model& chart, bool large) {
			const auto& t = current_theme();
			auto named = t.quiet();
			auto latest = t.strong();
			if (large) {
				latest.size = 24;
			} else {
				named.size -= 2;
				latest.size -= 2;
			}
			std::vector<desktop::view> parts;
			parts.push_back(desktop::row(desktop::text(chart.title, named), desktop::spacer(), desktop::text(chart.latest, latest)).align(align::end));
			parts.push_back(desktop::graph(chart.values, chart.top, large ? 140.f : 40.f, large));
			if (large) {
				auto scale = t.quiet();
				scale.size -= 3;
				parts.push_back(desktop::row(desktop::spacer(), desktop::text("of " + chart.top_said, scale)));
			}
			auto shown = desktop::column(std::move(parts)).spacing(large ? 8.f : 4.f);
			if (!large) return shown;
			return std::move(shown).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
		}

	}  // namespace

	desktop::view progress_bar(double step, double steps, float width) {
		const auto& t = current_theme();
		float share = steps > 0 ? static_cast<float>(step / steps) : 0;
		auto small = t.quiet();
		small.size -= 3;
		auto bar = desktop::row(desktop::column().width(std::max(1.f, width * share)).height(4).background({.fill = t.accent, .radius = 2}), desktop::spacer())
		               .width(width)
		               .height(4)
		               .background({.fill = t.sunken, .radius = 2});
		return desktop::row(std::move(bar), desktop::text(std::format("{}/{}", step, steps), small)).spacing(6).align(align::center);
	}

	desktop::view chart(page& p, chart_spec spec) {
		auto charts = charts_of(p, spec);
		if (charts.size() == 1) return drawn_chart(charts.front(), true);
		// Side by side, two to a row where there's room.
		std::vector<desktop::view> shown;
		for (const auto& c : charts) shown.push_back(drawn_chart(c, true).width(400));
		return desktop::flow(std::move(shown)).spacing(16);
	}

	desktop::view monitor(page& p, monitor_spec spec) {
		const auto& t = current_theme();
		auto model = monitor_of(p, spec);
		if (model.loading) return desktop::text("Loading…", t.quiet());
		if (model.tiles.empty()) return desktop::text("Nothing here yet.", t.quiet());
		std::vector<desktop::view> tiles;
		for (const auto& tile : model.tiles) {
			color dot = tone_of(t, tile.tone);
			std::vector<desktop::view> inside;
			auto title = t.strong();
			desktop::view named = tile.address.empty() ? desktop::text(tile.title, title)
			                                           : desktop::link(tile.title, [&p, address = tile.address] { p.open(address); }, title);
			auto dot_style = t.body();
			dot_style.color = dot;
			auto state = t.strong();
			state.size -= 1;
			auto quiet = t.quiet();
			std::vector<desktop::view> status;
			status.push_back(desktop::text("●", dot_style));
			status.push_back(desktop::text(tile.state, state));
			if (!tile.since.empty()) status.push_back(desktop::text(tile.since, quiet));
			inside.push_back(desktop::row(std::move(named).grow(), desktop::row(std::move(status)).spacing(5).align(align::center)).spacing(10).align(align::center));
			inside.push_back(desktop::text(!tile.doing.empty() ? tile.doing : tile.tone == "inactive" ? "Not running" : "Nothing to do", quiet));
			if (tile.progress) inside.push_back(progress_bar(tile.progress->first, tile.progress->second, 200));
			if (!tile.charts.empty()) {
				std::vector<desktop::view> charts;
				for (const auto& c : tile.charts) charts.push_back(drawn_chart(c, false).width(122));
				inside.push_back(desktop::flow(std::move(charts)).spacing(14));
			}
			std::vector<desktop::view> edged;
			edged.push_back(desktop::edge(dot));
			edged.push_back(desktop::column(std::move(inside)).spacing(10).grow());
			tiles.push_back(desktop::row(std::move(edged))
			                    .spacing(12)
			                    .padding(edges(14, 16, 14, 0))
			                    .width(300)
			                    .background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners}));
		}
		p.wide();
		return desktop::flow(std::move(tiles)).spacing(16);
	}

}  // namespace one::desktop::runtime
