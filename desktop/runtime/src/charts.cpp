// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Charts and monitors, as the web draws them: a chart is a run of numbers over time,
// newest at the right, its title and latest value in ink over a line in the accent;
// a monitor is a tile for each record whose status changes, like an agent, with its
// dot and state, what it's doing and how far, and its charts.

#include <map>

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

	// Records whose status changes, as a process list is: a row each under a heading,
	// its dot and name, its state and for how long, what it's doing, how far, and a
	// small chart of each thing it reports with its latest value.
	desktop::view monitor(page& p, monitor_spec spec) {
		const auto& t = current_theme();
		auto model = monitor_of(p, spec);
		if (model.loading) return desktop::text("Loading…", t.quiet());
		if (model.tiles.empty()) return desktop::text("Nothing here yet.", t.quiet());
		auto head = t.quiet();
		head.size -= 2;
		head.weight = 600;
		auto small = t.quiet();
		small.size -= 2;
		std::vector<std::string> charts;
		for (const auto& c : model.tiles.front().charts) charts.push_back(c.title);
		auto spark = [&](const chart_model& c, bool off) {
			if (off) return desktop::text("–", small).width(170);
			auto latest = t.strong();
			latest.size -= 2;
			return desktop::row(desktop::graph(c.values, c.top, 20.f).grow(), desktop::text(c.latest, latest).width(64)).spacing(8).align(align::center).width(170);
		};
		std::vector<desktop::view> heading;
		heading.push_back(desktop::text("Name", head).width(200));
		heading.push_back(desktop::text("State", head).width(110));
		heading.push_back(desktop::text("Doing", head).grow());
		heading.push_back(desktop::text("Progress", head).width(110));
		for (const auto& title : charts) heading.push_back(desktop::text(title, head).width(170));
		std::vector<desktop::list_row> rows;
		std::map<std::string, std::string> opens;
		for (const auto& tile : model.tiles) {
			opens[tile.id] = tile.address;
			bool off = tile.tone == "inactive";
			auto dot = t.body();
			dot.color = tone_of(t, tile.tone);
			auto name = off ? t.quiet() : t.strong();
			std::vector<desktop::view> line;
			line.push_back(desktop::row(desktop::text("●", dot), desktop::text(tile.title, name)).spacing(6).align(align::center).width(200));
			std::vector<desktop::view> state;
			state.push_back(desktop::text(tile.state, off ? t.quiet() : t.body()));
			if (!tile.since.empty()) state.push_back(desktop::text(tile.since, small));
			line.push_back(desktop::row(std::move(state)).spacing(6).align(align::center).width(110));
			line.push_back(desktop::text(tile.doing, t.quiet()).grow());
			line.push_back(tile.progress ? progress_bar(tile.progress->first, tile.progress->second, 60).width(110) : desktop::text("", small).width(110));
			for (std::size_t i = 0; i < charts.size(); ++i) line.push_back(i < tile.charts.size() ? spark(tile.charts[i], off) : desktop::text("–", small).width(170));
			rows.push_back({tile.id, desktop::row(std::move(line)).spacing(16).align(align::center).padding(edges(2, 8))});
		}
		p.wide();
		std::vector<desktop::view> table;
		table.push_back(desktop::row(std::move(heading)).spacing(16).padding(edges(8, 16)).background({.fill = t.sunken, .radius = t.corners}));
		table.push_back(desktop::list(std::move(rows), [&p, opens](const std::string& id) {
			if (auto found = opens.find(id); found != opens.end() && !found->second.empty()) p.open(found->second);
		}));
		return desktop::column(std::move(table)).spacing(2).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
	}

}  // namespace one::desktop::runtime
