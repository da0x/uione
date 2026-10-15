// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The window: the bar across its top with the app's name, its navigation, its
// badges and who's reading, and the page shown, with its title and what goes
// around it.

#include "one/desktop/runtime/runtime.hpp"

#include <format>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view page_view(navigator& nav, page& p) {
		const auto& t = current_theme();
		const auto& s = nav.site();
		const auto& shows = nav.shown(p);
		desktop::view built = shows.build ? shows.build(p) : desktop::column();
		auto around = p.take_around();
		std::vector<desktop::view> parts;
		for (auto& v : around.above) parts.push_back(std::move(v));
		// A page called by the app's own name, like its front page, doesn't say it again under the bar.
		std::string title = p.title();
		if (!(title.empty() || title == s.name) || !around.beside.empty()) {
			std::vector<desktop::view> title_row;
			title_row.push_back(desktop::text(title == s.name ? "" : title, t.heading(26)).grow());
			for (auto& v : around.beside) title_row.push_back(std::move(v));
			parts.push_back(desktop::row(std::move(title_row)).spacing(10).align(align::center));
		}
		for (auto& v : around.under) parts.push_back(std::move(v));
		// A question asked before a command runs, answered here.
		auto asked = p.asked();
		if (const auto& asking = asked.get()) {
			parts.push_back(desktop::column(desktop::text(asking->question, t.strong()),
			                                desktop::row(desktop::button("Yes, go ahead", [&p] { p.answer_asked(true); }, true).autofocus(),
			                                             desktop::button("Cancel", [&p] { p.answer_asked(false); }))
			                                    .spacing(8))
			                    .spacing(10)
			                    .padding(16)
			                    .background({.fill = t.surface, .border = t.warning, .border_width = 1.5f, .radius = t.corners}));
		}
		if (std::string wrong = *p.problem(); !wrong.empty()) {
			auto style = t.strong();
			style.color = t.danger;
			parts.push_back(desktop::text(wrong, style));
		}
		parts.push_back(std::move(built));
		if (s.footer) {
			parts.push_back(desktop::divider());
			parts.push_back(s.footer(p));
		}
		if (p.take_wide()) return desktop::scroll(desktop::column(std::move(parts)).spacing(16).padding(edges(24, 32)));
		return desktop::scroll(desktop::column(std::move(parts)).spacing(16).padding(edges(24, 32)).width(880)).align(align::center);
	}

	desktop::view drawn(navigator& nav) {
		const auto& t = current_theme();
		const auto& s = nav.site();
		(void)nav.changes();
		page& here = nav.current();
		auto brand = t.strong();
		brand.color = t.accent;
		brand.size = 17;
		std::vector<desktop::view> bar;
		if (nav.depth() > 1) bar.push_back(desktop::button("‹ Back", [&nav] { nav.back(); }));
		bar.push_back(desktop::link(s.name, [&nav, &s] {
			if (!nav.open("/") && !s.screens.empty()) nav.open(s.screens.front().route);
		}, brand));
		for (const auto* listed : screens::in_navigation(s)) {
			const auto& screen = *listed;
			if (screen.nav.empty()) continue;
			std::string route = screen.route;
			bar.push_back(desktop::link(screen.nav, [&nav, route] { nav.open(route); }, screen.route == nav.shown(here).route ? t.strong() : t.body()));
		}
		for (const auto& b : s.badges) {
			auto counted = screens::show(field_of(nav.view(b.view, here.me())->data, b.field));
			if (counted.empty() || counted == "0") continue;
			auto style = t.strong();
			style.color = color::hex(0xffffff);
			style.size -= 2;
			bar.push_back(desktop::text(counted + " " + b.label, style).padding(edges(2, 8)).background({.fill = t.accent, .radius = 10}));
		}
		bar.push_back(desktop::spacer());
		if (auto who = here.reader(); who && !who->name.empty()) {
			bar.push_back(desktop::avatar(who->name, 24));
			bar.push_back(desktop::text(who->name, t.quiet()));
		}
		auto top = desktop::row(std::move(bar)).spacing(14).align(align::center).padding(edges(10, 20)).background({.fill = t.surface});
		// Keyed by where it is, so a page keeps nothing of the one before it.
		std::string key = std::format("{}#{}", here.address(), nav.depth());
		return desktop::column(std::move(top), desktop::divider(), page_view(nav, here).key(key).grow());
	}

	int run(const site& s, desktop::app& app, screens::source& from, const std::string& address) {
		auto& window = app.open({.title = s.name, .id = s.id, .size = {1200, 800}});
		window.look(s.look);
		window.background = s.look.page;
		// A theme tried from a palette draws the whole window, and the app's own again after.
		screens::tried_themes::on_tried([&window, own = s.look](const std::optional<palette::theme>& tried) {
			window.look(tried ? drawn_in(*tried, own) : own);
		});
		navigator nav(s, from);
		if (!nav.open(address) && !nav.open(s.screens.empty() ? "/" : s.screens.front().route)) return 2;
		window.show([&] {
			auto shown = drawn(nav);
			std::string title = nav.current().title();
			window.title(title.empty() || title == s.name ? s.name : title + " · " + s.name);
			return shown;
		});
		window.on_key = [&](const key_event& e) {
			if (e.is(keys::escape) || e.is(keys::backspace)) return nav.back();
			return false;
		};
		return app.run();
	}

}  // namespace one::desktop::runtime
