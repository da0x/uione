// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Charts and monitors in a terminal, as btop draws them: a run of numbers in braille,
// newest at the right, under its title and latest value; and a tile for each record
// whose status changes, like an agent, its dot and state, what it's doing, how far,
// in blocks, and its charts side by side.

#include <format>

#include "one/terminal/runtime/components.hpp"

namespace one::terminal::runtime {

	terminal::view status_dot(const std::string& status);

	namespace {

		terminal::view drawn_chart(const chart_model& chart, int rows) {
			auto accent = terminal::meter_look(0);
			return terminal::column(terminal::row(terminal::text(chart.title, {.dim = true}).grow(), terminal::text(chart.latest, {.bold = true})).spacing(1),
			                        terminal::graph(chart.values, chart.top, rows, accent));
		}

	}  // namespace

	terminal::view chart(page& p, chart_spec spec) {
		std::vector<terminal::view> shown;
		for (const auto& model : charts_of(p, spec)) {
			shown.push_back(terminal::column(drawn_chart(model, 5), terminal::text("of " + model.top_said, {.dim = true})).padding(0, 1).boxed().width(38));
		}
		return terminal::flow(std::move(shown)).spacing(1);
	}

	terminal::view monitor(page& p, monitor_spec spec) {
		auto model = monitor_of(p, spec);
		if (model.loading) return terminal::text("Loading…", {.dim = true});
		if (model.tiles.empty()) return terminal::text("Nothing here yet.", {.dim = true});
		std::vector<terminal::list_row> shown;
		std::map<std::string, std::string> opens;
		for (const auto& tile : model.tiles) {
			opens[tile.id] = tile.address;
			std::vector<terminal::view> inside;
			std::string state = tile.state + (tile.since.empty() ? "" : " " + tile.since);
			inside.push_back(terminal::row(status_dot(tile.tone), terminal::text(tile.title, {.bold = true}).grow(), terminal::text(state)).spacing(1));
			inside.push_back(terminal::text(!tile.doing.empty() ? tile.doing : tile.tone == "inactive" ? "Not running" : "Nothing to do", {.dim = true}));
			if (tile.progress) {
				auto [step, steps] = *tile.progress;
				inside.push_back(terminal::row(terminal::meter({{step, terminal::meter_look(0)}, {steps - step, {.dim = true, .foreground = color::bright_black}}}).width(20),
				                               terminal::text(std::format("{}/{}", step, steps), {.dim = true}))
				                     .spacing(1));
			}
			if (!tile.charts.empty()) {
				std::vector<terminal::view> charts;
				for (const auto& c : tile.charts) charts.push_back(drawn_chart(c, 2).width(18));
				inside.push_back(terminal::flow(std::move(charts)).spacing(3));
			}
			shown.push_back({tile.id, terminal::column(std::move(inside))});
		}
		return terminal::list(std::move(shown), [&p, opens](const std::string& id) {
			if (auto found = opens.find(id); found != opens.end() && !found->second.empty()) p.open(found->second);
		});
	}

}  // namespace one::terminal::runtime
