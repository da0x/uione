// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What a screen says of each thing on it, worked out by the compiler and written as
// these, the same for every toolkit: a table's view, columns and link, a board's
// move, a form's fields. A toolkit draws each with a function of the same name,
// like one::desktop::runtime::table(page, table_spec).

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "one/screens/screens.hpp"

namespace one::screens {

	// A screen laid out in regions, like two_columns' main and side; a layout it
	// doesn't know is one column, its regions one after another.
	template <class View>
	struct basic_region {
		std::string name;
		View inside;
	};

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

	// A section: a title over what's in it, in one of the library's colors when it
	// says, like violet: section({"More", "violet"}, text(...), ...).
	struct section_title {
		std::string title;
		std::string color = "";
	};

	// A button that runs a command with fields, like Close issue. It isn't there for
	// someone it isn't allowed, and says it's working while the command's on its way.
	struct command_button {
		std::string command;
		std::string label;
		value fields = value::map();
		bool allowed = true;
		bool primary = false;
		std::string id = "";  // what it acts on, sent as id
		bool when = true;     // whether it applies now, like Follow for someone who doesn't
		std::string icon = "";
	};

	// Links down the side to a part's pages, like a project's settings, beside what
	// this page shows; the one open marked.
	struct menu_link {
		std::string to;
		std::string label;
	};

	// The steps something can be moved along from where it is, like an issue's stage,
	// a button for each the person's roles may take.
	struct steps_spec {
		std::string command;
		std::string id = "";
		std::string field;
		state<view_state> current;
		state<view_state> steps;
		std::string list;
		std::string shown = "";  // a step's own title, like Start work
		std::string to = "";     // the title of where it goes, like to.title
		std::string from = "";
		std::string here = "";
		std::string held = "";
		std::optional<state<view_state>> roles = std::nullopt;
		std::string within = "";
		std::string place = "project";
		std::string role = "role";
	};

	// A field of a view, as a column or a detail shows it, called by its label.
	struct column {
		std::string field;
		std::string label;
	};

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
		std::vector<option> choices = {};
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
		std::function<void()> on_done = {};  // once what it sent is done, like closing what it opened in
		std::string key = "";  // what keeps what's typed apart from another form for the command; its id unless it says
		std::string icon = "";  // drawn on the button opening it
	};

	// A field's choices, the first the most urgent, which color what shows it: the
	// first red, then orange, none in the middle, then green and blue.
	struct tint_spec {
		std::string field;
		std::vector<std::string> order;
	};

	// Cards above a list for another list's records, like a board's milestones, each
	// with how many rows are in it: picking one keeps the rows whose field is it.
	struct track_spec {
		std::string field;
		state<view_state> view;
		std::string list;
		std::string shown = "title";
		std::vector<column> columns = {};
	};

	// Rows put in order by a number, Alt with Up or Down moving the one chosen
	// past its neighbor, which runs the command with the number between its new
	// neighbors', as dragging does on the web; it stays where it's put until the view
	// says so too.
	struct reorder_spec {
		std::string command;
		std::string field;
		bool allowed = true;
	};

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
		std::optional<track_spec> track = std::nullopt;
		std::optional<reorder_spec> reorder = std::nullopt;
	};

	// A view's rows, or one of its lists, a column each for the fields named: a row
	// opens `link`, filled from it, and runs each action on it, as its id, or asks
	// first with the action's form, opened under it and started from it.
	struct row_action {
		std::string command;
		std::string label;
		bool allowed = true;
		std::function<bool(const value& row)> when = {};  // only the rows it holds for
		std::vector<form_field> form = {};
		std::string submit = "";
	};
	template <class View>
	struct basic_table_spec {
		state<view_state> view;
		std::string list = "";
		std::vector<column> columns = {};
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::vector<row_action> actions = {};
		std::map<std::string, std::string> people = {};  // a person's column, by name, and their picture
		std::map<std::string, std::vector<option>> choices = {};  // a choice column's values, as they're shown
		std::vector<std::string> labels = {};  // columns of words, each shown on its own
		std::string by = "";                   // a choice column whose choices are tabs
		std::vector<std::string> search = {};
		std::string sort = "";                 // like -number, largest first
		int page = 0;                          // how many rows a page has
		std::function<View()> tools = {};  // buttons beside its search, like New issue
		bool hide_empty = false;
		std::vector<std::pair<std::string, std::string>> only = {};  // the rows it keeps, like due<= +2w
		std::optional<tint_spec> tint = std::nullopt;
		std::optional<track_spec> track = std::nullopt;
		std::optional<reorder_spec> reorder = std::nullopt;
		std::string color = "";  // what its rows open in this color, rather than the theme's
		std::string empty = "Nothing here yet.";
	};

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

	// A box finding things across a page's lists, like a project's issues by number
	// or title and its wiki by title, each found opening its address.
	struct find_source {
		state<view_state> view;
		std::string list;
		std::string label;
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::vector<std::string> by = {};
	};

	// What a view holds, as Markdown to paste somewhere else whole, and a button
	// copying it to the clipboard, saying so.
	struct copied_field {
		std::string name, label;
		bool markdown = false;
	};
	struct copied_list {
		std::string name, label;
		std::string kind;  // thread, changes or rows
		std::vector<std::string> columns = {};
	};

	// What goes between two of a list's things, like the moves between a board's
	// stages: a row and a column for each, and in each cell the entry from the row's
	// to the column's. Pressing an empty cell asks create's form with the two filled
	// in; a full one, update's, started from it, with remove beside it. A diagram is
	// the same, said as a list of what goes from where to where.
	struct between_command {
		std::string command;
		std::vector<form_field> fields = {};
		std::string submit = "";
		std::map<std::string, std::string> given = {};
		bool allowed = true;
	};
	struct between_spec {
		state<view_state> view;
		std::string list;
		std::string from, to;
		std::string cell = "";
		state<view_state> over;
		std::string over_list;
		std::string shown = "title";
		std::optional<between_command> create = std::nullopt, update = std::nullopt, remove = std::nullopt;
	};

	// One of a view's lists as a thread, like an issue's comments: each by who wrote
	// it and when, its body drawn by `body`, like markdown_view, or as text.
	template <class View>
	struct basic_thread_spec {
		state<view_state> view;
		std::string list;
		std::function<View(const std::string&)> body = {};
	};

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

	// A set of Markdown pages under one address, like /docs/:page: the page its
	// address names, or the first, beside links to them all.
	struct written_page {
		std::string name;    // its part of the address, like getting-started
		std::string title;
		std::string source;  // its Markdown
	};

}  // namespace one::screens
