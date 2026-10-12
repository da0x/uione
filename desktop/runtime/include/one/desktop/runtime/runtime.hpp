// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What a desktop app uione writes runs on: its screens, each found by its address
// and built from the views it reads, live, as @uione/react's screens are on the web.
//
// A generated app is a site: its name, its theme, and its screens, each a route and
// a function building it from the page it's on:
//
//     one::desktop::runtime::screen issue{"", "/:project/issues/:issue", "", [](page& p) {
//         auto issue_page = p.view("projects::issue_page", p.param("issue"));
//         p.title({live(issue_page, "title")});
//         return items(markdown(p, issue_page, "body"), thread(p, issue_page, "comments"));
//     }};
//
// The navigator keeps the trail of pages opened, the window shows the last, and Esc
// goes back. Everything here runs on the loop's thread.

#pragma once

#include <any>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "one/desktop/runtime/source.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/theme.hpp"
#include "one/desktop/ui.hpp"

namespace one::desktop::runtime {

	class page;
	class navigator;

	// A screen: its title, or none when it shows a live one; its address, with
	// parameters like :issue, or :page? for one it opens without; what the
	// navigation calls it, when it's there; and how it's built.
	struct screen {
		std::string title;
		std::string route;
		std::string nav = "";
		std::function<desktop::view(page&)> build = {};
	};

	// The app: its name, the id its windows are grouped by, how it looks, and its
	// screens, the first match for an address opening it.
	struct site {
		std::string name;
		std::string id = "org.uione.app";
		desktop::theme look = desktop::theme::harbor();
		std::vector<screen> screens = {};
	};

	// Text a screen shows: words as written, and values read live from a view.
	struct part {
		std::string text;
		std::optional<state<view_state>> from = std::nullopt;
		std::string field = "";
	};
	inline part words(std::string text) { return {std::move(text)}; }
	inline part live(state<view_state> from, std::string field) { return {"", std::move(from), std::move(field)}; }

	// A value as a person reads it: a number without needless decimals, a list
	// joined, a time as a date, and nothing for nothing.
	std::string show(const value& v);
	// The zone times are said in: the reader's own, unless one's named, as tests do.
	const std::chrono::time_zone* time_zone();
	void use_time_zone(const std::string& name);
	// The parts as they read now, each live one from its view; reading them makes
	// whatever's being built a reader of those views.
	std::string said(const std::vector<part>& parts);
	// A row's or a document's field, by its name as the view stores it, like
	// stage.title, or through its maps.
	const value& field_of(const value& document, const std::string& path);
	// The rows of a view's list, or of the view itself when it's a list.
	const std::vector<value>& rows_of(const view_state& v, const std::string& list = "");

	// The address a row opens: the last parameter is the row, or the row's own field
	// of that name; any before it, the screen's own or the row's. `keyed` names the
	// parts before the last when what's opened is named by its key's parts, and
	// `named` the parameters filled with the last part of an id keyed by so many.
	std::string row_link(const std::string& route, const value& row, const std::map<std::string, std::string>& params, const std::vector<std::string>& keyed = {},
	                     const std::map<std::string, int>& named = {});
	// An id made of a key's parts, and an id's last `count` parts, as keyOf and
	// partsOf make and split them on the web.
	std::string key_of(const std::vector<std::string>& parts);
	std::optional<std::vector<std::string>> parts_of(const std::string& id, std::size_t count);

	// A page that's open: the screen it shows, what its address said, and the views
	// it reads, listened to from when it first reads them until it's left.
	class page {
	public:
		page(navigator& opened_by, runtime::source& from, const screen& shows, std::string address, std::map<std::string, std::string> params);
		page(const page&) = delete;
		page& operator=(const page&) = delete;

		const screen& shown() const { return *screen_; }
		const std::string& address() const { return address_; }
		const std::map<std::string, std::string>& params() const { return params_; }
		std::string param(const std::string& name) const;

		// A view, or a view per something, like the issue this page's address names.
		state<view_state> view(const std::string& name, const std::string& subject = "");

		// Opens another page by its address, after this one.
		void open(const std::string& address);
		// Runs a command, telling `after` what it answered; while it's on its way,
		// running() says so, and what turned it down is its error() and, unless what
		// ran it says so itself, as a form does, the page's problem().
		void run(const std::string& command, const value& fields, std::function<void(const answer&)> after = {}, bool says_why_itself = false);
		bool running(const std::string& command) const;
		state<std::string> problem() { return problem_; }
		// What turned a command down the last time it ran here, until it runs again.
		std::string error(const std::string& command) const;
		void clear(const std::string& command);
		std::optional<person> reader() const { return source_.reader(); }

		// A title read from views, like "#{number} {title}", which it shows once they've arrived.
		void title(std::vector<part> parts) { title_ = std::move(parts); }
		std::string title() const;
		// What goes around the title, given as the screen is built: the pages above it,
		// buttons beside it, like Edit, and words under it.
		void above_title(desktop::view v) { above_.push_back(std::move(v)); }
		void beside_title(desktop::view v) { beside_.push_back(std::move(v)); }
		void under_title(desktop::view v) { under_.push_back(std::move(v)); }
		struct around {
			std::vector<desktop::view> above, beside, under;
		};
		around take_around() { return {std::exchange(above_, {}), std::exchange(beside_, {}), std::exchange(under_, {})}; }

		// Something the page keeps while it's open, made the first time it's asked for,
		// like what's typed in a form: the same each time the screen is built.
		template <class T>
		state<T> kept(const std::string& name, T initial = {}) {
			auto found = kept_.find(name);
			if (found == kept_.end()) found = kept_.emplace(name, state<T>(std::move(initial))).first;
			return std::any_cast<state<T>>(found->second);
		}

	private:
		navigator& navigator_;
		runtime::source& source_;
		const screen* screen_;
		std::string address_;
		std::map<std::string, std::string> params_;
		struct listened {
			state<view_state> current;
			listening keeping;
		};
		std::map<std::string, std::unique_ptr<listened>> views_;
		std::vector<part> title_;
		std::vector<desktop::view> above_, beside_, under_;
		std::map<std::string, std::any> kept_;
		state<std::map<std::string, int>> running_;  // commands on their way, by name
		state<std::map<std::string, std::string>> errors_;  // why each was last turned down
		state<std::string> problem_;
	};

	// The trail of pages opened, the last of them shown.
	class navigator {
	public:
		navigator(const runtime::site& s, runtime::source& from);

		// Opens the page at an address after the one shown; false when no screen has it.
		bool open(const std::string& address);
		// Goes back to the page before; false at the first.
		bool back();
		page& current() { return *trail_.back(); }
		std::size_t depth() const { return trail_.size(); }
		const runtime::site& site() const { return site_; }

		// The window's view: the bar across its top, and the page.
		desktop::view show();
		// A screen's own part: its title and what it's built of, scrolled.
		desktop::view page_view(page& p);

		// The screen whose route an address matches, and what its parameters said.
		const screen* match(const std::string& address, std::map<std::string, std::string>& params) const;

	private:
		const runtime::site& site_;
		runtime::source& source_;
		std::vector<std::unique_ptr<page>> trail_;
		state<int> changed_ = 0;  // read by the window's view, so a page opened or left builds it again
	};

	// Runs a site in a window until it's closed: opens `address`, or the first
	// screen's, and goes back with Esc.
	int run(const runtime::site& s, desktop::app& app, runtime::source& from, const std::string& address = "/");

}  // namespace one::desktop::runtime
