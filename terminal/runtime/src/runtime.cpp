// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The terminal: the bar across its top with the app's name, its navigation, its
// badges and who's reading; the page shown, scrolled, with its title and what goes
// around it; and what the keys do, at the foot.

#include "one/terminal/runtime/runtime.hpp"

#include <cstdlib>
#include <format>
#include <functional>
#include <optional>

#include "one/terminal/runtime/components.hpp"

namespace one::terminal::runtime {

	terminal::view page_view(navigator& nav, page& p) {
		const auto& s = nav.site();
		const auto& shows = nav.shown(p);
		p.forget_keys();
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
		(void)p.take_fill();  // and its page always fills it
		return terminal::column(std::move(parts)).spacing(1).padding(1, 2);
	}

	namespace {
		// What agents were working on when q was pressed, while the app asks whether to
		// quit, which stops them.
		state<std::optional<std::vector<std::string>>> asking_to_quit;
		std::function<void()> quit_now;
	}  // namespace

	terminal::view drawn(navigator& nav) {
		const auto& s = nav.site();
		(void)nav.changes();
		page& here = nav.current();
		std::vector<terminal::view> bar;
		bar.push_back(terminal::link(s.name, [&nav, &s] {
			if (!nav.open("/") && !s.screens.empty()) nav.open(s.screens.front().route);
		}, colored(s.look.accent, true)));
		for (const auto* listed : screens::in_navigation(s)) {
			const auto& screen = *listed;
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
		std::vector<terminal::view> parts;
		parts.push_back(terminal::row(std::move(bar)).spacing(3).padding(0, 1));
		parts.push_back(terminal::divider());
		if (const auto& asking = asking_to_quit.get()) {
			std::vector<terminal::view> said;
			said.push_back(terminal::text(asking->size() == 1 ? "An agent is working on this, and quitting stops it midway:"
			                                                          : std::format("Agents are working on these {}, and quitting stops them midway:", asking->size()),
			                              {.bold = true}));
			for (const auto& worked : *asking) said.push_back(terminal::text("  • " + worked));
			said.push_back(terminal::row(terminal::button("Quit", [] { if (quit_now) quit_now(); }, true),
			                             terminal::button("Cancel", [] { asking_to_quit.set(std::nullopt); }).autofocus())
			                   .spacing(2));
			parts.push_back(terminal::column(terminal::column(std::move(said)).spacing(1).padding(0, 1).boxed("Quit?")).padding(0, 2));
			keys = "q quits   esc goes on";
		}
		parts.push_back(terminal::scroll(page_view(nav, here)).key(key).grow());
		parts.push_back(terminal::text(" " + keys, dim()));
		return terminal::column(std::move(parts));
	}

	bool key(navigator& nav, const key_press& e, bool stops_agents) {
		if (asking_to_quit.peek()) {
			if (e.is(keys::escape)) {
				asking_to_quit.set(std::nullopt);
				return true;
			}
			if (e.is('q')) asking_to_quit.set(std::nullopt);  // q again quits
			return false;
		}
		// What a line of the page is run by, like ctrl+z: the first said for a key.
		auto lines = nav.current().keys();
		for (const auto& [chord, run] : lines) {
			if (pressed(e, chord)) {
				run();
				return true;
			}
		}
		if (e.is(keys::escape) || e.is(keys::backspace)) return nav.back();
		if (e.is('q') && stops_agents) {
			if (auto worked = nav.worked(); !worked.empty()) {
				asking_to_quit.set(std::move(worked));
				return true;
			}
		}
		return false;
	}

	int run(const site& s, terminal::view_app& app, screens::source& from, const std::string& address) {
		screens::remembered::open(s.id);  // what this app remembers between runs, like how each board was shown
		navigator nav(s, from);
		if (!nav.open(address) && !nav.open(s.screens.empty() ? "/" : s.screens.front().route)) return 2;
		// The app's theme, or one tried from a palette, draws the whole terminal, whose
		// own colors are put back as the app ends.
		bool colored = !std::getenv("NO_COLOR");
		auto draw = [own = s.look.colors, colored](const std::optional<palette::theme>& tried) {
			if (colored) draw_terminal_in(tried ? tried : own);
		};
		if (s.look.colors) draw(std::nullopt);
		screens::tried_themes::on_tried(draw);
		// Started with agents that stop when it quits, as publish starts its own.
		const char* stops = std::getenv("ONE_QUIT_STOPS_AGENTS");
		bool stops_agents = stops && *stops && std::string(stops) != "0";
		quit_now = [&app] { app.quit(); };
		int ended = app.run([&] { return drawn(nav); }, [&](const key_press& e) { return key(nav, e, stops_agents); });
		asking_to_quit.set(std::nullopt);
		quit_now = nullptr;
		if (colored && (s.look.colors || screens::tried_themes::theme().peek())) draw_terminal_in(std::nullopt);
		return ended;
	}

}  // namespace one::terminal::runtime
