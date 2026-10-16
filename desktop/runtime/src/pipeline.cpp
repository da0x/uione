// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Pipelines, drawn as the classic deploy screen drew one: a heading for each lane,
// and in it a row for each card, its title, then its stages in order, each a chip:
// done with a tick, the one it's at in its status's color, the one it failed in red,
// those to come faint; and what its holder last said. Dense, a card a line, as on
// the web, so a whole release fits on one screen.

#include <map>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view pipeline_board(page& p, pipeline_spec spec) {
		const auto& t = current_theme();
		auto model = pipeline_of(p, spec);
		if (model.loading) return desktop::text("Loading…", t.quiet());
		if (model.lanes.empty()) return desktop::text("Nothing here yet.", t.quiet());
		auto working = color::hex(0xd9730d);
		auto status_color = [&](const std::string& status) {
			return status == "waiting" ? t.accent : status == "working" ? working : status == "success" ? t.success : status == "failed" ? t.danger : t.muted;
		};
		std::vector<desktop::view> lanes;
		for (const auto& lane : model.lanes) {
			std::vector<desktop::list_row> rows;
			std::map<std::string, std::string> links;
			for (const auto& row : lane.rows) {
				links[row.id] = row.link;
				std::vector<desktop::view> named;
				auto title_style = t.strong();
				title_style.size -= 1;
				named.push_back(desktop::text(row.title, title_style));
				std::string details;
				for (const auto& d : row.details) details += (details.empty() ? "" : " · ") + d;
				auto small = t.quiet();
				small.size -= 2;
				if (!details.empty()) named.push_back(desktop::text(details, small));
				std::vector<desktop::view> chips;
				for (const auto& step : row.steps) {
					auto look = t.quiet();
					look.size -= 2;
					box_style chip{.radius = 9};
					std::string said = step.title;
					if (step.at == "done") {
						look.color = t.success;
						said = "✓ " + said;
						chip.border = t.success, chip.border_width = 1;
					} else if (step.at == "failed") {
						look.color = t.danger;
						said = "✗ " + said;
						chip.border = t.danger, chip.border_width = 1.5f;
					} else if (step.at == "now") {
						look.color = status_color(row.status);
						look.weight = 600;
						said = "● " + said;
						chip.border = look.color, chip.border_width = 1.5f;
					} else {
						look.color = t.muted;
						look.color.alpha = 0.6f;
						chip.border = t.line, chip.border_width = 1;
					}
					chips.push_back(desktop::text(said, look).padding(edges(0, 6)).background(chip));
				}
				std::vector<desktop::view> line;
				line.push_back(desktop::row(std::move(named)).spacing(8).align(align::center).width(280));
				line.push_back(desktop::flow(std::move(chips)).spacing(3).grow());
				if (!row.note.empty()) line.push_back(desktop::text(row.note, small).width(300));
				rows.push_back({row.id, desktop::row(std::move(line)).spacing(12).align(align::center).padding(edges(0, 6))});
			}
			std::vector<desktop::view> shown;
			auto lane_style = t.strong();
			lane_style.size -= 2;
			auto count_style = t.quiet();
			count_style.size -= 2;
			shown.push_back(desktop::row(desktop::text(lane.title, lane_style), desktop::text(std::to_string(lane.rows.size()), count_style)).spacing(6));
			shown.push_back(desktop::list(std::move(rows), [&p, links](const std::string& id) {
				                if (auto found = links.find(id); found != links.end() && !found->second.empty()) p.open(found->second);
			                }).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners}));
			lanes.push_back(desktop::column(std::move(shown)).spacing(4));
		}
		p.wide();
		return desktop::column(std::move(lanes)).spacing(12);
	}

}  // namespace one::desktop::runtime
