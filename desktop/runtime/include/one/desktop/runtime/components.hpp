// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What a screen is made of, as @uione/react's components are on the web: each takes
// the page it's on and what the screen says of it, all worked out by the compiler,
// and is drawn by one::desktop in the site's theme.

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "one/desktop/runtime/runtime.hpp"

namespace one::desktop::runtime {

	// A screen's items, one under another.
	desktop::view items(std::vector<desktop::view> all);
	template <std::convertible_to<desktop::view>... V>
	desktop::view items(V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return items(std::move(all));
	}

	// Text, with values read live from views; a subtitle says less, under the title.
	desktop::view text(std::vector<part> parts);
	desktop::view subtitle(page& p, std::vector<part> parts);

	// The pages above this one, nearest last, each followed back with a click. A
	// page its address doesn't name, like an issue's board, fills its parameters from
	// a view: the board from the issue's page, by its own part when keyed by several.
	struct crumb {
		std::string to;
		std::vector<part> title;
		struct filled {
			state<view_state> from;
			std::string field;
			int keys = 1;
		};
		std::map<std::string, filled> fill = {};
	};
	desktop::view crumbs(page& p, std::vector<crumb> trail);

	// A section: a title over what's in it, in one of the library's colors when it
	// says, like violet: section({"More", "violet"}, text(...), ...).
	struct section_title {
		std::string title;
		std::string color = "";
	};
	desktop::view section(section_title title, std::vector<desktop::view> inside);
	template <std::convertible_to<desktop::view>... V>
	desktop::view section(section_title title, V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return section(std::move(title), std::move(all));
	}
	desktop::view link(page& p, std::string words, std::string to);

	// Whether the person reading may run a command within a place, from the view of
	// their roles: by what their role's record allows, or by holding a role that grants it.
	bool allows(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::string& command, const std::string& allowed = "role.may");
	bool holds(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::vector<std::string>& granting);

	// A button that runs a command with fields, like Close issue. It isn't there for
	// someone it isn't allowed, and says it's working while the command's on its way.
	struct command_button {
		std::string command;
		std::string label;
		value fields = value::map();
		bool allowed = true;
		bool primary = false;
	};
	desktop::view command(page& p, command_button b);

	// A view's rows, or one of its lists, a column each for the fields named: a
	// row opens `link`, filled from it, and runs each action on it, as its id.
	struct column {
		std::string field;
		std::string label;
	};
	struct row_action {
		std::string command;
		std::string label;
		bool allowed = true;
		std::function<bool(const value& row)> when = {};  // only the rows it holds for
	};
	struct table_spec {
		state<view_state> view;
		std::string list = "";
		std::vector<column> columns = {};
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::vector<row_action> actions = {};
		std::string empty = "Nothing here yet.";
	};
	desktop::view table(page& p, table_spec spec);

	// A form for a command: each field asked for as its kind is, and a button that
	// sends them, with what's given besides, like the project an issue is made in. An
	// update starts from a view's document, and follows it until something's typed.
	// With `button`, it's a button that opens the form in place, which is how a screen
	// offers to make something. `after` is where to go once it's done.
	struct form_field {
		std::string name;
		std::string label = "";
		// text, email, date, number, markdown, boolean, list, choice (one of
		// `choices`), pick (one of a list on the screen's) or choices (several)
		std::string type = "text";
		std::string hint = "";
		std::vector<desktop::option> choices = {};
		std::string start = "";  // what a new one starts as
		bool required = false;   // a choice that's always one of them
	};
	struct form_spec {
		std::string command;
		std::vector<form_field> fields = {};
		bool button = false;
		std::optional<state<view_state>> from = std::nullopt;
		std::string id = "";
		std::map<std::string, std::string> given = {};
		std::string submit = "";  // what its button says, rather than the command's name
		std::string opener = "";  // what the button opening it says
		bool when = true;
		bool allowed = true;
		std::string after = "";
	};
	desktop::view form(page& p, form_spec spec);

	// The choices a list on the screen offers, like a project's roles by their
	// titles; for people, each by their name, once, and chosen as who they are.
	std::vector<desktop::option> list_choices(const state<view_state>& from, const std::string& list, const std::string& shown, const std::string& person = "");

	// A readable label, as the web makes one: created_at is "Created at".
	std::string label(std::string_view name);

	// A field's choices, the first the most urgent, which color what shows it: the
	// first red, then orange, none in the middle, then green and blue.
	struct tint_spec {
		std::string field;
		std::vector<std::string> order;
	};
	std::optional<color> tint_of(const value& row, const std::optional<tint_spec>& tint);

	// A list's rows as cards in columns, like a project's issues in its stages: a
	// column for each of the over list's things, in its order, each card in the one
	// its field names. With a move, m on a card offers the columns a step from its own
	// leads to, for one of the person's roles; it's shown there at once, and back if
	// the move's turned down.
	struct board_move {
		std::string command;
		state<view_state> steps;
		std::string list;
		std::string held = "";  // the step's list of roles that may take it
		std::optional<state<view_state>> roles = std::nullopt;
		std::string within = "";  // the place they're held in, like the project's id
		std::string place = "project";
		std::string role = "role";
	};
	struct board_spec {
		state<view_state> view;
		std::string list;
		std::string by;
		state<view_state> over;
		std::string over_list;
		std::string shown = "title";
		std::vector<column> columns = {};  // what a card shows, its title first
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::map<std::string, std::string> people = {};  // a person's column, by name, and their picture
		std::vector<std::string> labels = {};             // lists of words shown each on its own
		std::optional<board_move> move = std::nullopt;
		std::vector<std::string> search = {};
		std::optional<tint_spec> tint = std::nullopt;
	};
	desktop::view board(page& p, board_spec spec);

	// A list's rows as large cards, like a project's boards: each its title, what
	// else it shows, a tally of what it holds, like 3 To do · 1 Done, and links that
	// open it filtered, like Assigned to me.
	struct tally_spec {
		state<view_state> view;
		std::string list;
		std::string by;           // like board
		std::string and_by;       // like stage
		std::string shown = "";   // like stage.title
		std::string order = "";   // like stage.position
		std::string noun = "";    // like issues
	};
	struct card_filter {
		std::string label;
		std::vector<std::pair<std::string, std::string>> query;
		std::string color = "";
	};
	struct cards_spec {
		state<view_state> view;
		std::string list;
		std::vector<column> columns = {};
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::optional<tally_spec> tally = std::nullopt;
		std::vector<card_filter> filters = {};
	};
	desktop::view cards(page& p, cards_spec spec);

	// What a list on the page is filtered by, with a button showing it all again;
	// nothing when it isn't.
	desktop::view filter_note(page& p);

	// One of a view's lists as a thread, like an issue's comments: each by who wrote
	// it and when, its body drawn by `body`, like markdown_view, or as text.
	struct thread_spec {
		state<view_state> view;
		std::string list;
		std::function<desktop::view(const std::string&)> body = {};
	};
	desktop::view thread(page& p, thread_spec spec);

	// One of a view's lists of changes as a timeline, each said in words: who, what
	// they did, like closed this, and when. With `subject`, each says what it was to,
	// like #12 Copy an issue whole, opening `link`. Changes `fresh` holds for are
	// marked new, judged by the views it reads as they were when the page opened, and
	// `on_open` runs once there are any, like a command saying they've been seen.
	struct timeline_spec {
		state<view_state> view;
		std::string list;
		std::vector<std::string> subject = {};
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::string title = "";
		std::vector<state<view_state>> fresh_views = {};
		std::function<bool(const value& row, const std::vector<value>& read)> fresh = {};
		std::string on_open = "";
	};
	desktop::view timeline(page& p, timeline_spec spec);

	// What a change did, in words, around what it changed: ["changed title of",
	// "from a to b"]; said of "this", as a thing's own timeline says it; a command's
	// action as done, like close as closed; and a time as it's said.
	std::pair<std::string, std::string> phrase(const value& field, const value& before, const value& after, const value& command);
	std::string changed(const value& field, const value& before, const value& after, const value& command);
	std::string done(const std::string& action);
	std::string when(const value& at);

	// A document's fields, each beside its label.
	desktop::view details(page& p, state<view_state> from, std::vector<column> fields);

}  // namespace one::desktop::runtime
