// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The terminal: the bar across its top with the app's name, its navigation, its
// badges and who's reading; the page shown, scrolled, with its title and what goes
// around it; and what the keys do, at the foot.

#include "one/terminal/runtime/runtime.hpp"

#include <format>

namespace one::terminal::runtime {

	terminal::view page_view(navigator& nav, page& p) {
		const auto& s = nav.site();
		const auto& shows = nav.shown(p);
		terminal::view built = shows.build ? shows.build(p) : terminal::column();
		auto around = p.take_around();
		std::vector<terminal::view> parts;
		for (auto& v : around.above) parts.push_back(std::move(v));
		// A page called by the app's own name, like its front page, doesn't say it again.
		std::string title = p.title();
		if (!(title.empty() || title == s.name) || !around.beside.empty()) {
			std::vector<terminal::view> title_row;
			title_row.push_back(terminal::text(title == s.name ? "" : title, {.bold = true}).grow());
			for (auto& v : around.beside) title_row.push_back(std::move(v));
			parts.push_back(terminal::row(std::move(title_row)).spacing(2));
		}
		for (auto& v : around.under) parts.push_back(std::move(v));
		// A question asked before a command runs, answered here.
		auto asked = p.asked();
		if (const auto& asking = asked.get()) {
			parts.push_back(terminal::column(terminal::text(asking->question, {.bold = true}),
			                                 terminal::row(terminal::button("Yes, go ahead", [&p] { p.answer_asked(true); }, true).autofocus(),
			                                               terminal::button("Cancel", [&p] { p.answer_asked(false); }))
			                                     .spacing(2))
			                    .spacing(1)
			                    .padding(0, 1)
			                    .boxed("Are you sure?"));
		}
		if (std::string wrong = *p.problem(); !wrong.empty()) parts.push_back(terminal::text(wrong, colored(color::red, true)));
		parts.push_back(std::move(built));
		if (s.footer) {
			parts.push_back(terminal::divider());
			parts.push_back(s.footer(p));
		}
		(void)p.take_wide();  // a terminal is as wide as it is
		return terminal::column(std::move(parts)).spacing(1).padding(1, 2);
	}

	terminal::view drawn(navigator& nav) {
		const auto& s = nav.site();
		(void)nav.changes();
		page& here = nav.current();
		std::vector<terminal::view> bar;
		bar.push_back(terminal::link(s.name, [&nav, &s] {
			if (!nav.open("/") && !s.screens.empty()) nav.open(s.screens.front().route);
		}, colored(s.look.accent, true)));
		for (const auto& screen : s.screens) {
			if (screen.nav.empty()) continue;
			std::string route = screen.route;
			bar.push_back(terminal::link(screen.nav, [&nav, route] { nav.open(route); }, {.bold = screen.route == nav.shown(here).route}));
		}
		for (const auto& b : s.badges) {
			auto counted = screens::show(field_of(nav.view(b.view, here.me())->data, b.field));
			if (counted.empty() || counted == "0") continue;
			bar.push_back(terminal::text(" " + counted + " " + b.label + " ", {.bold = true, .reverse = true, .foreground = s.look.accent}));
		}
		bar.push_back(terminal::spacer());
		if (auto who = here.reader(); who && !who->name.empty()) bar.push_back(terminal::text(who->name, dim()));
		std::string keys = nav.depth() > 1 ? "tab moves   enter opens   esc back   q quits" : "tab moves   enter opens   q quits";
		// Keyed by where it is, so a page keeps nothing of the one before it.
		std::string key = std::format("{}#{}", here.address(), nav.depth());
		return terminal::column(terminal::row(std::move(bar)).spacing(3).padding(0, 1), terminal::divider(), terminal::scroll(page_view(nav, here)).key(key).grow(),
		                        terminal::text(" " + keys, dim()));
	}

	int run(const site& s, terminal::view_app& app, screens::source& from, const std::string& address) {
		navigator nav(s, from);
		if (!nav.open(address) && !nav.open(s.screens.empty() ? "/" : s.screens.front().route)) return 2;
		return app.run([&] { return drawn(nav); }, [&](const key_press& e) {
			if (e.is(keys::escape) || e.is(keys::backspace)) return nav.back();
			return false;
		});
	}

}  // namespace one::terminal::runtime
