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

#include <algorithm>
#include <any>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
	// A moment as a record's details say it: its day and its time of day, like Mar 4, 2025, 14:32.
	std::string said_with_time(const value& v);

	// A time a screen names, worked out as it's read: counted from now, like +10m or
	// -7d; or by the calendar, d for a day, w for a week from Monday and M for a month,
	// each at its start on this machine's calendar and so many on, like d+1 for
	// tomorrow and w+1 for next week, and at a time of day when it says one, like
	// d+1@09:00. A day without a time is a day alone, kept at midnight UTC so it's that
	// day wherever it's read. None for anything else.
	std::optional<std::chrono::system_clock::time_point> time_named(std::string_view said,
	                                                                std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
	// And as a value, a time, or none.
	value time_value(std::string_view said);
	// A time as the reader's: a day alone, at midnight UTC, the start of that day where
	// it's read; any other time as it is.
	std::chrono::system_clock::time_point as_read_here(std::chrono::system_clock::time_point at);
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

	// Where a page was left, kept by its address for as long as the app runs, so going
	// back to it, or opening it again, finds it as it was: how far down it was scrolled,
	// and what was last opened from it, like a card, or what has taken that one's place.
	struct place {
		state<float> scroll;
		std::string opened;
		// The addresses each of its lists opens, in the order it shows them, as it was last
		// built, by the list: for a record opened from one to step through.
		std::map<std::string, std::shared_ptr<const std::vector<std::string>>> lists = {};
	};

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
		screens::place& where_left() { return *place_; }
		// Where the page it was opened from was left, while there was one: kept as it's
		// stepped on from, so its record steps through the list it was opened from.
		const screens::place* opened_from() const { return from_.get(); }
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

		// Opens another page by its address, after this one; or in this one's place, so
		// going back doesn't come here, as a screen's on open does.
		void open(const std::string& address);
		void open_instead(const std::string& address);
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
		// What the page told the person it just did, like "Undid place", which a workspace's
		// messages show until the next; saying something clears its problem().
		state<std::string> told() { return told_; }
		void say(std::string words);
		// What turned a command down the last time it ran here, until it runs again.
		std::string error(const std::string& command) const;
		void clear(const std::string& command);
		std::optional<person> reader() const { return source_.reader(); }
		// Where a file a field names is on this machine, when the source keeps it here.
		std::optional<std::filesystem::path> file_here(const std::string& kept) const { return source_.file_here(kept); }
		// What it reads from and runs on, which outlives it: for work done away from the
		// app's loop, like keeping a file, that may finish after it's left.
		screens::source& source() const { return source_; }
		std::string me() const { return reader() ? reader()->uid : ""; }

		// What's being worked on now in what the page reads: each record someone holds
		// on a lease that hasn't run out, like a card an agent took, as its title and,
		// when the view reads it, who holds it, like "@uione/react 0.15.0 (npm)".
		std::vector<std::string> worked() const;

		// A title read from views, like "#{number} {title}", which it shows once they've arrived.
		void title(std::vector<part> parts) { title_ = std::move(parts); }
		std::string title() const;
		// The time now, to the second, ticking while the page reads it: for running
		// times, like how long an agent has been working on a card.
		std::chrono::sys_seconds now();

		// Drawn across the screen, as a board is, rather than at a page's reading width.
		void wide() { wide_ = true; }
		bool take_wide() { return std::exchange(wide_, false); }
		// The whole window, as a drawing app's workspace is: no bar, no title, no
		// margin, only what the page is made of.
		void fill() { fill_ = true; }
		bool take_fill() { return std::exchange(fill_, false); }
		// Only its picture, edge to edge, its bars drawn over it, as a photo's own screen is.
		void alone() { alone_ = true; }
		bool take_alone() { return std::exchange(alone_, false); }

		// The keys its lines are run by, like ctrl+z, gathered each time it's built,
		// while they're shown: the first said for a key takes it.
		void on_key(std::string chord, std::function<void()> run) { keys_.emplace_back(std::move(chord), std::move(run)); }
		const std::vector<std::pair<std::string, std::function<void()>>>& keys() const { return keys_; }
		void forget_keys() { keys_.clear(); }

		// Whether one of its toggles is on, named by the screen's address and its own,
		// as the person left it on this machine (decision 0017); and turning it.
		bool toggled(const std::string& name);
		void toggle(const std::string& name);
		// What a choice of toggles over an enum is at, like the tool a drawing's in: `first`
		// until another's chosen, for as long as the page is open.
		std::string chosen(const std::string& name, const std::string& first) { return kept<std::string>("choice " + name, first).get(); }
		void choose(const std::string& name, const std::string& choice) { kept<std::string>("choice " + name).set(choice); }

		// The row of a list it picked, by its id, named by the screen's address and the
		// pick's name; an empty id picks none.
		void pick(const std::string& name, std::string id) { kept<std::string>("pick " + name).set(std::move(id)); }
		std::string picked_id(const std::string& name) { return kept<std::string>("pick " + name).get(); }

		// Whether what's being built is in a toolbar's group, and a small one: its
		// buttons drawn as tools.
		std::optional<bool> in_group() const { return in_group_; }
		void in_group(std::optional<bool> small) { in_group_ = small; }
		// Whether what's being built is a tool's options, each a choice drawn with what
		// it's for: whether they're open, which a choice made closes.
		const std::optional<state<bool>>& in_options() const { return in_options_; }
		void in_options(std::optional<state<bool>> open) { in_options_ = std::move(open); }
		// Whether what's being built in a toolbar doesn't apply now: drawn, so the
		// toolbar keeps its shape, but it can't be pressed. Built by `build` while
		// `holds` doesn't, and while what it's in doesn't apply either.
		bool off() const { return off_; }
		template <class F>
		auto off_unless(bool holds, F build) {
			bool was = std::exchange(off_, off_ || !holds);
			auto made = build();
			off_ = was;
			return made;
		}

		// Something the page keeps while it's open, made the first time it's asked for,
		// like what's typed in a form: the same each time the screen is built.
		template <class T>
		state<T> kept(const std::string& name, T initial = {}) {
			auto found = kept_.find(name);
			if (found == kept_.end()) found = kept_.emplace(name, state<T>(std::move(initial))).first;
			return std::any_cast<state<T>>(found->second);
		}

	private:
		friend class navigator_base;
		screens::source& source_;
		std::shared_ptr<screens::place> place_ = std::make_shared<screens::place>();
		std::shared_ptr<screens::place> from_;
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
		bool fill_ = false;
		bool alone_ = false;
		std::vector<std::pair<std::string, std::function<void()>>> keys_;
		std::optional<bool> in_group_;
		std::optional<state<bool>> in_options_;
		bool off_ = false;
		std::map<std::string, std::any> kept_;
		state<std::map<std::string, int>> running_;          // commands on their way, by name
		state<std::map<std::string, std::string>> errors_;   // why each was last turned down
		state<std::string> problem_;
		state<std::string> told_;
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

	// notify planner.reminders at remind_at { title  task::finish "Done" }: whatever
	// screen is open, a row whose time has come, said once by the system with what it
	// shows and its buttons, each acting on the row (decision 0024).
	struct notice_button {
		std::string command, label;
		std::function<value()> given = {};  // what it's sent besides the row, worked out as it's pressed
	};
	struct notice_spec {
		std::string view, list, at;  // like orbit::planner, reminders and remind_at
		std::vector<std::string> shown;  // what it says, its first line first, like title
		std::vector<notice_button> buttons = {};
	};

	// A notice due: what it says, and the row its buttons act on.
	struct notice_due {
		std::size_t spec = 0;
		std::string row, title, body;
	};

	// A site's notices, watched apart from any screen: each view listened to, and what's
	// been said remembered on this machine, so each is said once, a time that passed
	// while the app was closed said when it's next open.
	class notice_watch {
	public:
		notice_watch(source& from, std::vector<notice_spec> specs);
		// What's come due by now and isn't said yet, marked said.
		std::vector<notice_due> due(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
		// One of a notice's buttons pressed: its command run on the row, with what it's given.
		void press(const notice_due& notice, std::size_t button);
		const std::vector<notice_spec>& specs() const { return specs_; }

	private:
		source& from_;
		std::vector<notice_spec> specs_;
		std::vector<view_state> latest_;
		std::vector<listening> listening_;
	};

	// What the app remembers across its runs for whoever runs it, like the way each
	// board was last shown: names and values, kept in ~/.local/state/<app>/remembered
	// ($XDG_STATE_HOME's, when it's set). Until the app says which it is, nothing is
	// kept past the run.
	namespace remembered {
		void open(const std::string& app);
		std::optional<std::string> get(const std::string& name);
		void set(const std::string& name, const std::string& value);
	}  // namespace remembered

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
		std::optional<Look> dark = {};  // how it looks dark, which a switch picks, when its theme says
		std::vector<basic_screen<View>> screens = {};
		std::vector<badge> badges = {};
		std::function<View(basic_page<View>&)> footer = {};
		// The addresses the top bar lists first, in this order, like /projects and /docs;
		// the other screens follow in their own order.
		std::vector<std::string> navigation = {};
		std::vector<notice_spec> notices = {};  // said whatever screen is open (decision 0024)
	};

	// A site's screens in the top bar's order: those at its navigation's addresses
	// first, by where each is in it, then the rest as they were.
	template <typename Site>
	auto in_navigation(const Site& s) {
		std::vector<const typename decltype(s.screens)::value_type*> out;
		for (const auto& screen : s.screens) out.push_back(&screen);
		auto place = [&](const auto* screen) {
			std::string base = screen->route.substr(0, screen->route.find("/:"));
			auto at = std::find(s.navigation.begin(), s.navigation.end(), base.empty() ? "/" : base);
			return at - s.navigation.begin();
		};
		std::stable_sort(out.begin(), out.end(), [&](const auto* a, const auto* b) { return place(a) < place(b); });
		return out;
	}

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
		// What's being worked on now in the pages opened, each said once.
		std::vector<std::string> worked() const;
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
		std::unique_ptr<page_base> retired_;  // the page last replaced, kept until the next
		std::map<std::string, std::shared_ptr<place>> places_;  // where each address was left
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
