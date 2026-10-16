// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a terminal app uione writes runs on: one::screens' sites, pages and
// components, drawn in a terminal by one::terminal. A generated app's screens are
// the same C++ a desktop app's are; this is what draws them in a terminal, in
// cells, a page scrolled under a bar with the app's name, its navigation and who's
// reading. Tab moves between what takes keys, Enter opens, Esc goes back, q quits.

#pragma once

#include <one/screens/models.hpp>

#include "one/terminal/ui.hpp"

namespace one::terminal::runtime {

	using namespace one::screens;

	// How a terminal app looks: its accent, among the 16 colors every terminal has, and
	// its theme's colors, which the terminal's own are set to while it runs, as btop
	// draws in its theme, where the terminal can be told them and NO_COLOR isn't set.
	struct look {
		terminal::color accent = terminal::color::blue;
		std::optional<palette::theme> colors = std::nullopt;
	};

	using page = screens::basic_page<terminal::view>;
	using screen = screens::basic_screen<terminal::view>;
	using site = screens::basic_site<terminal::view, look>;
	using navigator = screens::basic_navigator<terminal::view, look>;
	using table_spec = screens::basic_table_spec<terminal::view>;
	using thread_spec = screens::basic_thread_spec<terminal::view>;
	using region = screens::basic_region<terminal::view>;

	// The terminal's view: the bar across its top, the page shown, and what the keys do.
	terminal::view drawn(navigator& nav);
	terminal::view page_view(navigator& nav, page& p);

	// What a key does before the page's own: Esc goes back. When quitting stops agents
	// started with the app, q asks first while they're working on something, naming
	// it, and q again quits, Esc goes on. True when it's done; false when it's the
	// page's, or q quits.
	bool key(navigator& nav, const key_press& e, bool stops_agents);

	// Runs a site in the terminal until q. ONE_QUIT_STOPS_AGENTS=1 says the agents
	// started with it stop when it quits, so q asks first while they're working. It opens `address`, or the
	// first screen's.
	int run(const site& s, terminal::view_app& app, screens::source& from, const std::string& address = "/");

}  // namespace one::terminal::runtime
