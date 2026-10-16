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

	// Records whose status changes, as btop's process list is: a line each under a
	// heading, its dot and name, its state and for how long, what it's doing, how far
	// in squares, and a braille line of each thing it reports with its latest value.
	terminal::view monitor(page& p, monitor_spec spec) {
		auto model = monitor_of(p, spec);
		if (model.loading) return terminal::text("Loading…", {.dim = true});
		if (model.tiles.empty()) return terminal::text("Nothing here yet.", {.dim = true});
		std::vector<std::string> charts;
		for (const auto& c : model.tiles.front().charts) charts.push_back(c.title);
		std::vector<terminal::view> heading;
		heading.push_back(terminal::text("  Name", {.bold = true}).width(22));
		heading.push_back(terminal::text("State", {.bold = true}).width(12));
		heading.push_back(terminal::text("Doing", {.bold = true}).grow());
		heading.push_back(terminal::text("Progress", {.bold = true}).width(11));
		for (const auto& title : charts) heading.push_back(terminal::text(title, {.bold = true}).width(16));
		std::vector<terminal::list_row> shown;
		std::map<std::string, std::string> opens;
		for (const auto& tile : model.tiles) {
			opens[tile.id] = tile.address;
			bool off = tile.tone == "inactive";
			std::vector<terminal::view> line;
			line.push_back(terminal::row(status_dot(tile.tone), terminal::text(tile.title, {.bold = !off, .dim = off}).grow()).spacing(1).width(22));
			line.push_back(terminal::text(tile.state + (tile.since.empty() ? "" : " " + tile.since), {.dim = off}).width(12));
			// One line, cut where the column ends, as a process list's command is.
			line.push_back(terminal::text_rows({terminal::plain(tile.doing, {.dim = true})}).grow());
			if (tile.progress) {
				auto [step, steps] = *tile.progress;
				line.push_back(terminal::row(terminal::meter({{step, terminal::meter_look(0)}, {steps - step, {.dim = true, .foreground = color::bright_black}}}).width(5),
				                             terminal::text(std::format("{}/{}", step, steps), {.dim = true}))
				                   .spacing(1)
				                   .width(11));
			} else {
				line.push_back(terminal::text("").width(11));
			}
			for (std::size_t i = 0; i < charts.size(); ++i) {
				if (off || i >= tile.charts.size()) {
					line.push_back(terminal::text("–", {.dim = true}).width(16));
					continue;
				}
				const auto& c = tile.charts[i];
				line.push_back(terminal::row(terminal::graph(c.values, c.top, 1, terminal::meter_look(0)).width(7).height(1), terminal::text(c.latest)).spacing(1).width(16));
			}
			shown.push_back({tile.id, terminal::row(std::move(line)).spacing(1)});
		}
		return terminal::column(terminal::row(std::move(heading)).spacing(1), terminal::list(std::move(shown), [&p, opens](const std::string& id) {
			                        if (auto found = opens.find(id); found != opens.end() && !found->second.empty()) p.open(found->second);
		                        }));
	}

}  // namespace one::terminal::runtime
