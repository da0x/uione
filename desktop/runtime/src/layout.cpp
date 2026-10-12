// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// How a screen is laid out: buttons together, what's beside its title, regions,
// a menu down the side, ways of showing the same thing, and the steps something
// moves along.

#include <algorithm>
#include <set>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view actions(std::vector<desktop::view> all) {
		std::erase_if(all, [](const desktop::view& v) { return !v.get(); });
		if (all.empty()) return {};
		return desktop::row(std::move(all)).spacing(8).align(align::center);
	}

	desktop::view heading(page& p, desktop::view beside) {
		if (beside.get()) p.beside_title(std::move(beside));
		return {};
	}

	desktop::view layout(std::string name, std::vector<region> regions) {
		std::erase_if(regions, [](const region& r) { return !r.inside.get(); });
		if (name == "two_columns" && regions.size() >= 2) {
			std::vector<desktop::view> columns;
			for (std::size_t i = 0; i < regions.size(); ++i) columns.push_back(std::move(regions[i].inside).grow(i == 0 ? 2.f : 1.f));
			return desktop::row(std::move(columns)).spacing(28).align(align::start);
		}
		std::vector<desktop::view> stacked;
		for (auto& r : regions) stacked.push_back(std::move(r.inside));
		return desktop::column(std::move(stacked)).spacing(14);
	}

	desktop::view menu(page& p, std::vector<menu_link> links, desktop::view beside) {
		const auto& t = current_theme();
		std::vector<desktop::view> side;
		for (const auto& l : links) {
			std::string to = filled(p, l.to);
			bool here = to == p.address().substr(0, p.address().find('?'));
			if (here) {
				auto s = t.strong();
				s.color = t.accent;
				side.push_back(desktop::text(l.label, s).padding(edges(6, 10)).background({.fill = t.sunken, .radius = std::min(t.corners, 6.f)}));
			} else {
				side.push_back(desktop::link(l.label, [&p, to] { p.open(to); }, t.body()).padding(edges(6, 10)));
			}
		}
		return desktop::row(desktop::column(std::move(side)).spacing(2).width(180), std::move(beside).grow()).spacing(24).align(align::start);
	}

	desktop::view switched(page& p, std::string id, std::string label, std::vector<std::string> options, std::vector<desktop::view> ways) {
		const auto& t = current_theme();
		auto chosen = p.kept<int>("switched:" + id, 0);
		int now = std::clamp(chosen.get(), 0, static_cast<int>(ways.size()) - 1);
		std::vector<desktop::view> buttons;
		buttons.push_back(desktop::text(label, t.quiet()));
		for (std::size_t i = 0; i < options.size() && i < ways.size(); ++i) {
			int which = static_cast<int>(i);
			buttons.push_back(desktop::button(options[i], [chosen, which] { chosen.set(which); }, which == now));
		}
		return desktop::column(desktop::row(std::move(buttons)).spacing(8).align(align::center), std::move(ways[static_cast<std::size_t>(now)])).spacing(12);
	}

	desktop::view steps(page& p, steps_spec spec) {
		const view_state& current = *spec.current;
		const view_state& steps_view = *spec.steps;
		if (current.status != status::ready || steps_view.status != status::ready) return {};
		std::string now = field_of(current.data, spec.field).as_string();
		std::set<std::string> mine;
		if (spec.roles) {
			for (const auto& row : rows_of(spec.roles->get())) {
				if (field_of(row, spec.place).as_string() == spec.within) mine.insert(field_of(row, spec.role).as_string());
			}
		}
		std::vector<const value*> leaving, open;
		for (const auto& step : rows_of(steps_view, spec.list)) {
			if (step["from"].as_string() != now) continue;
			leaving.push_back(&step);
			if (spec.held.empty() || std::ranges::any_of(field_of(step, spec.held).as_array(), [&](const value& r) { return mine.contains(r.as_string()); })) open.push_back(&step);
		}
		const auto& t = current_theme();
		// Someone who holds a role there is told when none of theirs moves it on.
		if (open.empty()) {
			if (spec.held.empty() || mine.empty() || now.empty()) return {};
			std::string here = !spec.here.empty() ? show(field_of(current.data, spec.here)) : !spec.from.empty() && !leaving.empty() ? show(field_of(*leaving.front(), spec.from)) : "";
			return desktop::text(here.empty() ? "No moves from here for your roles" : "No moves from " + here + " for your roles", t.quiet());
		}
		std::vector<desktop::view> buttons;
		bool busy = p.running(spec.command);
		for (const value* step : open) {
			std::string said = !spec.shown.empty() ? show(field_of(*step, spec.shown)) : "";
			if (said.empty() && !spec.to.empty() && !show(field_of(*step, spec.to)).empty()) said = "Move to " + show(field_of(*step, spec.to));
			if (said.empty()) said = label(spec.command.substr(spec.command.rfind("::") + 2));
			std::map<std::string, value> fields{{spec.field, (*step)["to"]}};
			if (!spec.id.empty()) fields["id"] = value(spec.id);
			buttons.push_back(desktop::button(busy ? said + "…" : said, [&p, command = spec.command, fields, busy] {
				if (!busy) p.run(command, value(fields));
			}, true));
		}
		return desktop::row(std::move(buttons)).spacing(8);
	}

}  // namespace one::desktop::runtime
