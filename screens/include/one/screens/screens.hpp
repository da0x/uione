// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::screens: what a native app's screens are and do, whatever draws them. A
// desktop app draws them with one::desktop and a terminal app with one::terminal;
// both are written by `one build` from the same screens, as the same C++, and only
// what they're compiled with differs.
//
// A site is its name, its look, and its screens, each a route and a function
// building it from the page it's on. A page knows what its address said, the views
// it reads, live, the commands it runs, with what's on its way and what turned one
// down, and what it keeps while it's open. The navigator keeps the trail of pages
// opened. Values are read as people read them, and addresses and keys are made as
// the web makes them, so a link means the same in every app.

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

#include "one/screens/source.hpp"
#include "one/screens/state.hpp"

namespace one::screens {

	// Values

	// Text a screen shows: words as written, and values read live from a view.
	struct part {
		std::string text;
		std::optional<state<view_state>> from = std::nullopt;
		std::string field = "";
	};
	inline part words(std::string text) { return {std::move(text)}; }
	inline part live(state<view_state> from, std::string field) { return {"", std::move(from), std::move(field)}; }

	// A value as a person reads it: a number without needless decimals, a list
	// joined, a time as a date or, today, as its time, and nothing for nothing.
	std::string show(const value& v);
	// The zone times are said in: the reader's own, unless one's named, as tests do.
	const std::chrono::time_zone* time_zone();
	void use_time_zone(const std::string& name);
	// The parts as they read now, each live one from its view; reading them makes
	// whatever's being built a reader of those views.
	std::string said(const std::vector<part>& parts);
	// The year it is where the app is read, as a footer says it.
	std::string this_year();
	// A row's or a document's field, by its name as the view stores it, like
	// stage.title, or through its maps.
	const value& field_of(const value& document, const std::string& path);
	// The rows of a view's list, or of the view itself when it's a list.
	const std::vector<value>& rows_of(const view_state& v, const std::string& list = "");
	// A readable label, as the web makes one: created_at is "Created at".
	std::string label(std::string_view name);

	// A choice, as a picker offers it: its value and how it's shown.
	struct option {
		std::string value;
		std::string label;
	};
	// A choice's value as it's shown, from the choices offered, or as it is.
	std::string shown_as(const std::vector<option>& choices, const std::string& value);

	// Addresses and keys

	// The address a row opens: the last parameter is the row, or the row's own field
	// of that name; any before it, the screen's own or the row's. `keyed` names the
	// parts before the last when what's opened is named by its key's parts, and
	// `named` the parameters filled with the last part of an id keyed by so many.
	std::string row_link(const std::string& route, const value& row, const std::map<std::string, std::string>& params, const std::vector<std::string>& keyed = {},
	                     const std::map<std::string, int>& named = {});
	// An address with a page's parameters filled in, like /:project/settings.
	std::string filled(const std::string& route, const std::map<std::string, std::string>& params);
	// Whether a row is kept by a filter's parts, each a field and what it holds, like
	// assignees=me, or a time it's before or after, like updated_at>=-7d.
	bool kept(const value& row, const std::vector<std::pair<std::string, std::string>>& picks, const std::string& me);
	// An id made of a key's parts, and an id's last `count` parts, as keyOf and
	// partsOf make and split them on the web.
	std::string key_of(const std::vector<std::string>& parts);
	std::optional<std::vector<std::string>> parts_of(const std::string& id, std::size_t count);
	// An address's query, each name and value decoded, + as a space.
	std::vector<std::pair<std::string, std::string>> query_of(const std::string& address);
	// Of routes, the one an address opens: of those that fit it, the one most
	// particular, a part named outright before one a parameter takes, as the web's
	// router weighs them; and what its parameters said. -1 when none fits.
	int match(const std::vector<std::string>& routes, const std::string& address, std::map<std::string, std::string>& params);

	// Pages

	// A page that's open: the screen it shows, what its address said, and the views
	// it reads, listened to from when it first reads them until it's left.
	class page_base {
	public:
		page_base(screens::source& from, std::size_t screen, std::string screen_title, std::string address, std::map<std::string, std::string> params,
		          std::vector<std::pair<std::string, std::string>> query, std::function<bool(const std::string&)> opener,
		          std::function<bool(const std::string&)> replacer);
		virtual ~page_base() = default;
		page_base(const page_base&) = delete;
		page_base& operator=(const page_base&) = delete;

		std::size_t screen() const { return screen_; }
		const std::string& address() const { return address_; }
		const std::map<std::string, std::string>& params() const { return params_; }
		std::string param(const std::string& name) const;
		// What its address's query says, like ?is=Assigned to me&assignees=me.
		const std::vector<std::pair<std::string, std::string>>& query() const { return query_; }

		// What a list is filtered by from the address, as a card's link opens it:
		// assignees=me keeps the rows whose assignees hold whoever's reading, and
		// updated_at>=-7d those changed in the last week. Said in words, its is= or
		// each part, and cleared by opening the page without it.
		bool keeps(const value& row) const;
		std::string filtered() const;
		void clear_filter();

		// A view, or a view per something, like the issue this page's address names.
		state<view_state> view(const std::string& name, const std::string& subject = "");

		// Opens another page by its address, after this one.
		void open(const std::string& address);
		// Runs a command, telling `after` what it answered; while it's on its way,
		// running() says so, and what turned it down is its error() and, unless what
		// ran it says so itself, as a form does, the page's problem().
		void run(const std::string& command, const value& fields, std::function<void(const answer&)> after = {}, bool says_why_itself = false);
		// The same, once the files given for its file fields are uploaded.
		void run(const std::string& command, const value& fields, attached_files files, std::function<void(const answer&)> after, bool says_why_itself);
		bool running(const std::string& command) const;
		// A question asked before a command runs from this page, like "Remove this
		// person?", answered on the page itself; and the one being asked, if any.
		void ask_before(const std::string& command, std::string question) { questions_[command] = std::move(question); }
		struct asking {
			std::string command, question;
			value fields;
			std::function<void(const answer&)> after;
			bool says_why_itself = false;
			attached_files files = {};
		};
		state<std::optional<asking>> asked() { return asked_; }
		void answer_asked(bool yes);
		state<std::string> problem() { return problem_; }
		// What turned a command down the last time it ran here, until it runs again.
		std::string error(const std::string& command) const;
		void clear(const std::string& command);
		std::optional<person> reader() const { return source_.reader(); }
		std::string me() const { return reader() ? reader()->uid : ""; }

		// A title read from views, like "#{number} {title}", which it shows once they've arrived.
		void title(std::vector<part> parts) { title_ = std::move(parts); }
		std::string title() const;
		// The time now, to the second, ticking while the page reads it: for running
		// times, like how long an agent has been working on a card.
		std::chrono::sys_seconds now();

		// Drawn across the screen, as a board is, rather than at a page's reading width.
		void wide() { wide_ = true; }
		bool take_wide() { return std::exchange(wide_, false); }

		// Something the page keeps while it's open, made the first time it's asked for,
		// like what's typed in a form: the same each time the screen is built.
		template <class T>
		state<T> kept(const std::string& name, T initial = {}) {
			auto found = kept_.find(name);
			if (found == kept_.end()) found = kept_.emplace(name, state<T>(std::move(initial))).first;
			return std::any_cast<state<T>>(found->second);
		}

	private:
		screens::source& source_;
		std::optional<state<std::chrono::sys_seconds>> clock_;
		listening ticking_;
		std::size_t screen_;
		std::string screen_title_;
		std::string address_;
		std::map<std::string, std::string> params_;
		std::vector<std::pair<std::string, std::string>> query_;
		std::function<bool(const std::string&)> opener_, replacer_;
		struct listened {
			state<view_state> current;
			listening keeping;
			std::shared_ptr<std::string> moved = std::make_shared<std::string>();  // where what it reads went, until it's read there
		};
		void follow(listened& l, const std::string& name, const std::string& subject);
		std::map<std::string, std::unique_ptr<listened>> views_;
		std::vector<part> title_;
		bool wide_ = false;
		std::map<std::string, std::any> kept_;
		state<std::map<std::string, int>> running_;          // commands on their way, by name
		state<std::map<std::string, std::string>> errors_;   // why each was last turned down
		state<std::string> problem_;
		std::map<std::string, std::string> questions_;
		state<std::optional<asking>> asked_;
	};

	// A page as a toolkit draws it: with what goes around its title, given as the
	// screen is built, the pages above it, buttons beside it, like Edit, and words
	// under it, each a view of the toolkit's.
	template <class View>
	class basic_page : public page_base {
	public:
		using page_base::page_base;
		using view_type = View;
		void above_title(View v) { above_.push_back(std::move(v)); }
		void beside_title(View v) { beside_.push_back(std::move(v)); }
		void under_title(View v) { under_.push_back(std::move(v)); }
		struct around {
			std::vector<View> above, beside, under;
		};
		around take_around() { return {std::exchange(above_, {}), std::exchange(beside_, {}), std::exchange(under_, {})}; }

	private:
		std::vector<View> above_, beside_, under_;
	};

	// A screen: its title, or none when it shows a live one; its address, with
	// parameters like :issue, or :page? for one it opens without; what the
	// navigation calls it, when it's there; and how it's built.
	template <class View>
	struct basic_screen {
		std::string title;
		std::string route;
		std::string nav = "";
		std::function<View(basic_page<View>&)> build = {};
	};

	// A number beside the app's name, like what's new to whoever's reading, shown
	// while it's more than none.
	struct badge {
		std::string view;
		std::string field;
		std::string label;
	};

	// The app: its name, the id its windows are grouped by, how it looks, its
	// screens, its badges, and what's at the foot of every page.
	template <class View, class Look>
	struct basic_site {
		std::string name;
		std::string id = "org.uione.app";
		Look look = {};
		std::vector<basic_screen<View>> screens = {};
		std::vector<badge> badges = {};
		std::function<View(basic_page<View>&)> footer = {};
	};

	// The trail of pages opened, the last of them shown.
	class navigator_base {
	public:
		navigator_base(std::vector<std::string> routes, screens::source& from);
		virtual ~navigator_base() = default;
		navigator_base(const navigator_base&) = delete;
		navigator_base& operator=(const navigator_base&) = delete;

		// Opens the page at an address after the one shown; false when no screen has it.
		bool open(const std::string& address);
		// Goes back to the page before; false at the first.
		bool back();
		// Shows another address in the current page's place, as clearing a filter does.
		bool replace(const std::string& address);
		std::size_t depth() const { return trail_.size(); }
		page_base& current_page() { return *trail_.back(); }
		// Read by what draws it, so a page opened or left builds it again.
		int changes() const { return *changed_; }

		// Views read for what's around every page, like its badges', kept while the app runs.
		state<view_state> view(const std::string& name, const std::string& subject = "");
		// The screen an address opens, by its place among the site's, and what its parameters said.
		int match(const std::string& address, std::map<std::string, std::string>& params) const { return screens::match(routes_, address, params); }

	protected:
		virtual std::unique_ptr<page_base> made(std::size_t screen, std::string address, std::map<std::string, std::string> params,
		                                        std::vector<std::pair<std::string, std::string>> query) = 0;
		screens::source& source_;

	private:
		std::unique_ptr<page_base> page_at(const std::string& address);
		std::vector<std::string> routes_;
		std::vector<std::unique_ptr<page_base>> trail_;
		state<int> changed_ = 0;
		std::map<std::string, std::pair<state<view_state>, listening>> views_;
	};

	template <class View, class Look>
	class basic_navigator : public navigator_base {
	public:
		using page = basic_page<View>;
		using site_type = basic_site<View, Look>;

		basic_navigator(const site_type& s, screens::source& from) : navigator_base(routes_of(s), from), site_(s) {}

		page& current() { return static_cast<page&>(current_page()); }
		const site_type& site() const { return site_; }
		const basic_screen<View>& shown(const page_base& p) const { return site_.screens[p.screen()]; }
		// The screen whose route an address matches, or none.
		const basic_screen<View>* match(const std::string& address, std::map<std::string, std::string>& params) const {
			int at = navigator_base::match(address, params);
			return at < 0 ? nullptr : &site_.screens[static_cast<std::size_t>(at)];
		}

	protected:
		std::unique_ptr<page_base> made(std::size_t screen, std::string address, std::map<std::string, std::string> params,
		                                std::vector<std::pair<std::string, std::string>> query) override {
			return std::make_unique<page>(source_, screen, site_.screens[screen].title, std::move(address), std::move(params), std::move(query),
			                              [this](const std::string& a) { return open(a); }, [this](const std::string& a) { return replace(a); });
		}

	private:
		static std::vector<std::string> routes_of(const site_type& s) {
			std::vector<std::string> out;
			for (const auto& screen : s.screens) out.push_back(screen.route);
			return out;
		}
		const site_type& site_;
	};

}  // namespace one::screens
