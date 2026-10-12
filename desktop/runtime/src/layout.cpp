// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// How a screen is laid out, drawn: buttons together, what's beside its title,
// regions, a menu down the side, ways of showing the same thing, and the steps
// something moves along.

#include <algorithm>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view actions(std::vector<desktop::view> all) {
		std::erase_if(all, [](const desktop::view& v) { return !v.get(); });
		if (all.empty()) return {};
		return desktop::flow(std::move(all)).spacing(8);
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
		for (const auto& entry : menu_entries(p, links)) {
			if (entry.here) {
				auto s = t.strong();
				s.color = t.accent;
				side.push_back(desktop::text(entry.label, s).padding(edges(6, 10)).background({.fill = t.sunken, .radius = std::min(t.corners, 6.f)}));
			} else {
				side.push_back(desktop::link(entry.label, [&p, to = entry.address] { p.open(to); }, t.body()).padding(edges(6, 10)));
			}
		}
		return desktop::row(desktop::column(std::move(side)).spacing(2).width(180), std::move(beside).grow()).spacing(24).align(align::start);
	}

	desktop::view switched(page& p, std::string id, std::string label_text, std::vector<std::string> options, std::vector<desktop::view> ways) {
		const auto& t = current_theme();
		auto chosen = p.kept<int>("switched:" + id, 0);
		int now = std::clamp(chosen.get(), 0, static_cast<int>(ways.size()) - 1);
		std::vector<desktop::view> buttons;
		buttons.push_back(desktop::text(label_text, t.quiet()));
		for (std::size_t i = 0; i < options.size() && i < ways.size(); ++i) {
			int which = static_cast<int>(i);
			buttons.push_back(desktop::button(options[i], [chosen, which] { chosen.set(which); }, which == now));
		}
		return desktop::column(desktop::row(std::move(buttons)).spacing(8).align(align::center), std::move(ways[static_cast<std::size_t>(now)])).spacing(12);
	}

	desktop::view steps(page& p, steps_spec spec) {
		auto model = steps_of(p, spec);
		if (!model.none.empty()) return desktop::text(model.none, current_theme().quiet());
		if (model.buttons.empty()) return {};
		std::vector<desktop::view> buttons;
		for (const auto& b : model.buttons) {
			buttons.push_back(desktop::button(b.label, [&p, command_name = spec.command, fields = b.fields, busy = model.busy] {
				if (!busy) p.run(command_name, fields);
			}, true));
		}
		return desktop::flow(std::move(buttons)).spacing(8);
	}

}  // namespace one::desktop::runtime
