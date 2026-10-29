// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a desktop app uione writes runs on: one::screens' sites, pages and
// components, drawn in a window by one::desktop. A generated app's screens are the
// same C++ a terminal app's are; this is what draws them on a desktop:
//
//     one::desktop::runtime::screen issue{"", "/:project/issues/:issue", "", [](page& p) {
//         auto issue_page = p.view("projects::issue_page", p.param("issue"));
//         p.title({live(issue_page, "title")});
//         return items(markdown(p, issue_page, "body"), thread(p, {.view = issue_page, .list = "comments"}));
//     }};
//
// The navigator keeps the trail of pages opened, the window shows the last, and Esc
// goes back. F11 fills the screen with it. Everything here runs on the loop's thread.

#pragma once

#include <one/screens/models.hpp>

#include "one/desktop/app.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/theme.hpp"
#include "one/desktop/ui.hpp"

namespace one::desktop::runtime {

	using namespace one::screens;

	using page = screens::basic_page<desktop::view>;
	using screen = screens::basic_screen<desktop::view>;
	using site = screens::basic_site<desktop::view, desktop::theme>;
	using navigator = screens::basic_navigator<desktop::view, desktop::theme>;
	using table_spec = screens::basic_table_spec<desktop::view>;
	using thread_spec = screens::basic_thread_spec<desktop::view>;
	using region = screens::basic_region<desktop::view>;

	// The window's view: the bar across its top, and the page shown.
	desktop::view drawn(navigator& nav);
	// A page's own part: its title, what's around it, and what it's built of, scrolled;
	// or only what it's built of, when it fills the window or the app fills the
	// screen, which alone then says.
	desktop::view page_view(navigator& nav, page& p, bool* alone = nullptr);

	// Whether the window may close now. When closing stops agents started with the
	// app, it asks first while they're working on something, naming it, and closing
	// again closes it.
	bool may_close(navigator& nav, bool stops_agents);
	// What a key does that nothing in the view took: Esc puts away the question
	// about closing, stops filling the screen, or goes back, and F11 fills the screen
	// with the app, or stops.
	bool key(navigator& nav, const key_event& e);

	// The screen the app's window is on, in that screen's own pixels, and whether
	// the app fills it, in which case a page is only what it's made of. Read in a
	// view, they're current: the window builds its view again when either changes.
	std::optional<desktop::size> screen_size();
	// What's being typed into that isn't a field, like words written on a collage:
	// given every key before anything else, while the page last built says so.
	void typing(std::function<bool(const key_event&)> taking);
	bool fullscreen();
	// The app's name, as its site says, like studio: what the folder its saved
	// pictures go in is named. Empty while no app is running.
	std::string app_name();
	// Fills the screen with the app, or stops, as F11 does.
	void fullscreen(bool on);
	// Whether the app is drawn dark, and switching it, kept for its next run. A site
	// whose theme has no dark colors stays light.
	bool dark();
	void dark(bool on);
	bool offers_dark();  // whether its theme has dark colors to switch to
	// Hands work from another thread to the window's, as a picture drawn in the
	// background comes back to be shown; it runs soon after, and what it changes is
	// shown. Nothing runs once the app has stopped.
	void post(std::function<void()> task);
	// Whether there's a window for work handed back to reach: none while drawing to a
	// file, or in a test, where what would be worked out away is worked out now.
	bool posting();
	// Work done away from the window's thread, like reading, keeping or making smaller
	// a picture coming in, on the few threads every part of the app shares rather than
	// one of its own; what it finds goes back by post. With no window to hand it back
	// to, it's done now.
	void in_background(std::function<void()> work);
	// What that work found, handed back to the window's thread by post; or, with no
	// window, done now, as the work was.
	void handed_back(std::function<void()> task);
	// The window drawn again, its view as it is, as what's drawn only once it's in view,
	// like a mosaic's pictures, is when they're read.
	void redraw();
	// For a test of what's worked out away: while held, work is done in the background
	// as in a window, and what's handed back waits until it's run, in turn, as the
	// window's loop would run it, no more than `most` at a time; how many were.
	void hold_posted(bool held);
	std::size_t run_posted(std::size_t most = SIZE_MAX);

	// Whether an address was handed to a copy of the app already running, as
	// launching it again with one does when the first holds its data: that copy shows
	// it in place of its page, and this one has nothing more to do.
	bool hand_over(const std::string& site_id, const std::string& address);

	// Runs a site in a window until it's closed: opens `address`, or the first
	// screen's, and goes back with Esc. ONE_QUIT_STOPS_AGENTS=1 says the agents
	// started with it stop when it closes, so it asks first while they're working.
	int run(const site& s, desktop::app& app, screens::source& from, const std::string& address = "/");

}  // namespace one::desktop::runtime
