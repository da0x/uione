// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What each thing on a screen works out before it's drawn, the same for every
// toolkit: which rows a table shows on which tab and page, where a board's cards
// are and where they may go, what a timeline says of each change, what a form
// sends. A toolkit draws the result its own way, and calls back to act.

#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "one/screens/palette.hpp"
#include "one/screens/specs.hpp"

namespace one::screens {

	// Words

	// What a change did, in words, around what it changed: ["changed title of",
	// "from a to b"]; said of "this", as a thing's own timeline says it; a command's
	// action as done, like close as closed; and a time as it's said.
	std::pair<std::string, std::string> phrase(const value& field, const value& before, const value& after, const value& command);
	std::string changed(const value& field, const value& before, const value& after, const value& command);
	std::string done(const std::string& action);
	std::string when(const value& at);
	// The choices a list on the screen offers, like a project's roles by their
	// titles; for people, each by their name, once, and chosen as who they are.
	std::vector<option> list_choices(const state<view_state>& from, const std::string& list, const std::string& shown, const std::string& person = "");
	// What a view holds, as Markdown to paste somewhere else whole.
	std::string markdown_of(const std::string& title, const value& data, const std::vector<copied_field>& fields, const std::vector<copied_list>& lists,
	                        const std::map<std::string, std::vector<option>>& choices = {});
	// Code as an example to read, without the license header its file starts with.
	std::string without_license(std::string source);

	// Whether the person reading may run a command within a place, from the view of
	// their roles: by what their role's record allows, or by holding a role that grants it.
	bool allows(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::string& command, const std::string& allowed = "role.may");
	bool holds(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::vector<std::string>& granting);

	// A row's tone, as Trac colored a ticket's priority: where its choice is among the
	// field's, the first the most urgent, spread over 1 to 5; 3, plain, for one without.
	int tone_of(const value& row, const std::optional<tint_spec>& tint);

	// What a screen's conditions read, as `one build` writes them: a button's when,
	// like issue_page.held_by != me, is ready(issue_page) && !same(at(issue_page,
	// "held_by"), me(p)). Values compare as the web compares them: the same value of
	// the same kind; a list holding one; and times and numbers in order, nothing
	// before everything.
	bool ready(const state<view_state>& v);
	const value& at(const state<view_state>& v, const std::string& field);
	bool none(const value& v);
	bool same(const value& a, const value& b);
	bool has(const value& list, const value& item);
	int compared(const value& a, const value& b);
	inline value me(const page_base& p) { return p.me().empty() ? value() : value(p.me()); }
	// A value worked out, like layer.size * 1.1: two numbers added (+), taken away (-),
	// multiplied (*) or divided (/); nothing where either isn't a number.
	value worked(char op, const value& a, const value& b);
	// The row of a view's list the screen picked, named by its address and the pick's
	// name, as the view has it now; nothing while there's none (decision 0017).
	value picked(page_base& p, const std::string& name, const state<view_state>& v, const std::string& list);

	// Around a page

	// The pages above this one, each by its address, filled, and what it's called.
	struct crumb_link {
		std::string address, title;
	};
	std::vector<crumb_link> crumb_links(page_base& p, const std::vector<crumb>& trail);

	// A menu's links, each by its address, filled, and whether it's the page open.
	struct menu_entry {
		std::string address, label;
		bool here = false;
	};
	std::vector<menu_entry> menu_entries(page_base& p, const std::vector<menu_link>& links);

	// A link's address: another page's, filled with this one's parameters; or
	// another site's, opened in the browser.
	bool leaves(const std::string& to);
	std::string address_of(page_base& p, const std::string& to);

	// Commands

	// A command's button, when it's there: what it says, working while it's on its
	// way, and what it sends.
	struct command_model {
		std::string label;
		bool busy = false;
		value fields;
	};
	std::optional<command_model> command_of(page_base& p, const command_button& b);
	// What a command's buttons last gave, kept by the screen for what's drawn next, as
	// a drawing program draws in the colors last chosen: each value as the last one to
	// give it gave it.
	value last_given(page_base& p, const std::string& command);
	void remember_given(page_base& p, const std::string& command, const value& given);
	// Whether what a button gives is what's remembered, so it's drawn pressed.
	bool holds_last_given(page_base& p, const command_button& b);
	// What a screen does as it's opened, once a visit, the first of its lines that
	// applies, decided once every view they read is there: whether it's still deciding.
	bool opening_now(page_base& p, bool ready, const std::vector<opening>& lines);

	// The steps something can take from where it is: a button for each the reader's
	// roles may take, or words saying there are none for them.
	struct step_button {
		std::string label;
		value fields;
	};
	struct steps_model {
		std::vector<step_button> buttons;
		std::string none;  // said when there's no step for their roles
		bool busy = false;
	};
	steps_model steps_of(page_base& p, const steps_spec& spec);

	// Forms

	// What a form keeps while it's open, its fields' values and boxes, and what it
	// does: follow a view's document until something's changed, and send.
	class form_model {
	public:
		form_model(page_base& p, const form_spec& spec);

		bool shown() const { return spec_.when && spec_.allowed; }
		// Opened from its button, or always there.
		bool open() const;
		void set_open(bool now) const;
		const std::string& says() const { return says_; }    // what its button says
		const std::string& opens() const { return opens_; }  // what the button opening it says
		const std::vector<form_field>& fields() const { return spec_.fields; }
		std::string shown_label(const form_field& f) const { return f.label.empty() ? label(f.name) : f.label; }
		// A field's value as it's written, and, for a box or several choices, each ticked or not.
		state<std::string> written(const form_field& f) const;
		state<bool> ticked(const form_field& f, const std::string& choice) const;
		// A picker's open state, kept with the form.
		state<bool> picking(const form_field& f) const;
		// The choices a choice field offers, None first unless it's required.
		std::vector<option> choices_of(const form_field& f) const;
		// Whether a choice is a few, drawn as radio buttons rather than a list that
		// opens, as the web draws them: one of three or fewer, each always one of them.
		static bool few(const form_field& f) { return f.type == "choice" && f.choices.size() <= 3; }
		std::string error() const { return page_.error(spec_.command); }
		bool busy() const { return page_.running(spec_.command); }
		void send() const;

	private:
		std::string current(const form_field& f) const;
		void follow();
		page_base& page_;
		form_spec spec_;
		std::string key_, says_, opens_;
	};

	// Lists

	// The cards above a list for another list's records, and which is picked.
	struct track_record {
		std::string id, title;
		std::vector<std::string> details;
		long count = 0;
		bool picked = false;
		std::string status = "";  // success, once every row in it is at a success end; failed, once any row in it has failed
	};
	struct track_model {
		std::string label;
		std::vector<track_record> records;
		std::string picked;
		state<std::string> picking;
		bool above = false;  // drawn above a switch, not by the way it's shown
		bool picked_last = false;  // the last is picked until another is, so none picked is said as every
		bool keeps(const value& row, const std::string& field) const { return picked.empty() || field_of(row, field).as_string() == picked; }
		void pick(const std::string& id) const { picking.set(picked == id ? (picked_last ? std::string(every) : "") : id); }
		static constexpr const char* every = "\x01every";
	};
	std::optional<track_model> track_of(page_base& p, const std::string& key, const std::optional<track_spec>& spec, const std::vector<value>& rows, const std::string& noun);

	// The rows in order, with where they've just been put; and moving one up (-1) or
	// down (+1) among `around`, the rows it's shown with.
	std::vector<value> ordered(page_base& p, const std::string& key, const std::optional<reorder_spec>& spec, std::vector<value> rows);
	bool move_row(page_base& p, const std::string& key, const reorder_spec& spec, const std::vector<value>& around, const std::string& id, int by);

	// A table: the rows on the open tab's page, found by what's typed, in order.
	struct table_tab {
		std::string value, label;
		long count = 0;
		bool open = false;
	};
	struct table_model {
		std::string key;
		std::vector<value> rows;  // those shown
		std::vector<value> kept;  // every row it keeps, on every page, as a download saves them
		std::vector<table_tab> tabs;
		std::size_t first = 0, last = 0, total = 0;  // the page shown, of all that are kept
		state<std::string> query, tab, acting;
		state<int> at;
		std::optional<track_model> track;
		bool hidden = false;     // empty, and hidden while it is
		bool arranging = false;  // rows may be put in order now
	};
	template <class View>
	table_model table_of(page_base& p, const basic_table_spec<View>& spec);
	// Rows as comma-separated values: a line of the columns' names, then a line a row.
	std::string csv_of(const std::vector<column>& columns, const std::map<std::string, std::vector<option>>& choices, const std::vector<value>& rows);
	// Saves a file where downloads go, by its name and never in another folder, as
	// name-2.csv when name.csv is there: the path it's saved at, or why it wasn't.
	std::expected<std::string, std::string> save_download(const std::string& name, const std::string& text);
	// A cell's words: a choice as it's shown, and anything else as it reads.
	std::string cell_text(const std::map<std::string, std::vector<option>>& choices, const value& row, const std::string& column);
	// The names of the people a cell holds, one or several.
	std::vector<std::string> people_in(const value& row, const std::string& column);
	// The row a table's action asks about, as a view's document for its form.
	state<view_state> acted_on(page_base& p, const std::string& list, const std::string& command, const value& row);

	// A chart: a run of numbers, newest last, the top of its scale, and how they read.
	struct chart_model {
		std::string title;
		std::vector<double> values = {};
		double top = 1;
		std::string latest = "", top_said = "", unit = "";
	};
	// A field of a record by its path: a joined field whole, like assignee.name, and
	// otherwise a name within a field of data, like stats.cpu.
	value value_at(const value& record, const std::string& path);
	// A value as it reads: 42%, 1.5 GiB, 3.2 MiB/s, or 12.5.
	std::string read_value(double v, const std::string& unit);
	// The top of a chart's scale: 100 for a percent, else the least round number at or
	// above its highest value.
	double scale_top(const std::vector<double>& values, const std::string& unit);
	// What a status word says, as a dot: working, waiting, success, inactive or failed.
	std::string tone_of_state(const std::string& word);
	chart_model chart_of(const std::string& title, const value& numbers, const std::string& unit);
	std::vector<chart_model> charts_of(page_base& p, const chart_spec& spec);

	struct monitor_tile {
		std::string id = "", title = "", address = "";
		std::string state = "", tone = "";  // Working, and its dot
		std::string since = "", doing = "";
		std::optional<std::pair<double, double>> progress = std::nullopt;  // step, of steps
		std::vector<chart_model> charts = {};
	};
	struct monitor_model {
		bool loading = false;
		std::vector<monitor_tile> tiles;
	};
	// Its tiles, each record's status, offline once it's gone unheard for seen's
	// seconds, read again each second as the page's time moves.
	monitor_model monitor_of(page_base& p, const monitor_spec& spec);

	// A switch's pick on a page: the person's own, kept while the page is open, and
	// first the one remembered from the last time they were here, by its name, so a
	// board opens as it was left, even after the app is started again.
	state<int> switch_pick(page_base& p, const std::string& id, const std::vector<std::string>& options);
	void remember_switch(page_base& p, const std::string& id, const std::string& option);
	// The icon a way of showing a list is drawn as, as on the web: table, board,
	// whiteboard or pipeline.
	std::string switch_icon(const std::string& option);

	// A board: its columns, each with its cards, and a card's move being chosen.
	struct board_card {
		std::string id, title;
		std::vector<std::string> details;  // like #12
		std::vector<std::string> labels, people;
		int tone = 3;
		std::string link;
		// How it's going, as a dot: inactive, waiting, working, done or failed, or
		// nothing; for how long, like 4m; and which runner is on it, like runner 2.
		std::string status, running, runner;
		std::vector<std::string> reaches = {};  // the columns it may be moved to, for one of the reader's roles
		std::optional<std::pair<double, double>> progress = std::nullopt;  // how far in its stage, as its holder says: step, of steps
	};

	// How a status is said in words, for a dot's title and for screen readers.
	std::string status_words(const std::string& status);
	// A running time: 35s, 4m, 1h 12m, 2d 3h.
	std::string running_time(std::chrono::seconds took);
	struct board_column {
		std::string id, title;
		std::vector<board_card> cards;
		std::vector<value> rows;  // the cards' rows, for putting them in order
		bool notify = false;      // it chimes when a card arrives, and shows a bell
	};
	struct board_offer {
		std::string card, title;
		std::vector<option> steps;
	};
	struct board_model {
		std::string key;
		bool loading = false;
		std::vector<board_column> columns;
		std::optional<board_offer> offer;
		std::optional<track_model> track;
		state<std::string> query, moving;
		std::string error;
		bool arranging = false;
	};
	board_model board_of(page_base& p, const board_spec& spec);
	// Tells whoever has the app open: chime --message, when chime is installed, or the
	// terminal's bell.
	void chime(const std::string& message, bool failed = false);  // failed: its error sound, as a card arriving where things fail
	// Where chimes go instead, in a test, until it's given an empty one.
	void chime_into(std::function<void(const std::string& message, bool failed)> heard);
	// Moves a card to a column, shown there at once and back if it's turned down.
	void move_card(page_base& p, const board_spec& spec, const std::string& card, const std::string& to);

	// A calendar: the days it shows, a week's or a month's from Monday, each with the
	// rows on it, those that are a day alone first, then by their time of day.
	struct calendar_card {
		std::string id, title;
		std::vector<std::string> details;
		std::string time;  // its time of day, like 09:00, or nothing for a day alone
		int tone = 3;
		bool picked = false;
	};
	struct calendar_day {
		std::string date;     // 2026-10-28
		std::string weekday;  // Wed
		int day = 1;          // 28
		bool today = false;
		bool in_month = true;  // in a month's grid, a day of that month, not one around it
		std::vector<calendar_card> cards;
	};
	struct calendar_model {
		std::string title;  // October 2026, or the week of Oct 26, 2026
		bool month = false, loading = false;
		std::vector<calendar_day> days;
		std::string error;
	};
	calendar_model calendar_of(page_base& p, const calendar_spec& spec);
	// The calendar a week or a month on, or back, by -1 or 1; or back to today, by 0.
	void calendar_go(page_base& p, const calendar_spec& spec, int by);
	// A row moved to another day, like 2026-10-30: sent to the move command with its date
	// there, a day alone or at the same time of day.
	void calendar_move(page_base& p, const calendar_spec& spec, const std::string& row, const std::string& date);

	// A pipeline: a lane for each kind, in the order their stages come, each a row for
	// a card of that kind: the stages it goes through, each done, the one it's at now,
	// one still to come, or the one it failed in.
	struct pipeline_step {
		std::string id, title;
		std::string at;  // done, now, later or failed
	};
	struct pipeline_row {
		std::string id, title, link;
		std::vector<std::string> details;
		std::vector<pipeline_step> steps;
		std::string status, running, runner;  // its dot, as a board's card says it
		std::string note;                     // what its holder last said, like waiting on the packages
		std::optional<std::pair<double, double>> progress = std::nullopt;  // how far in its stage: step, of steps
	};
	struct pipeline_lane {
		std::string title;
		std::vector<pipeline_row> rows;
	};
	struct pipeline_model {
		bool loading = false;
		std::vector<pipeline_lane> lanes;
	};
	pipeline_model pipeline_of(page_base& p, const pipeline_spec& spec);

	// A canvas: its cards, each where it's placed, in the canvas's own units; those not
	// placed yet laid out in columns, a grid step apart, as their row's by field says.
	struct canvas_card {
		std::string id, title, kind;
		std::vector<std::string> details;
		int tone = 3;
		std::string link;
		double x = 0, y = 0, w = 150, h = 150;
		bool placed = false;  // whether its row says where it is, or it's laid out
		std::string status, running, runner;
		std::optional<std::pair<double, double>> progress = std::nullopt;  // how far in its stage: step, of steps
	};
	struct canvas_region {
		std::string id, title;
		double x = 0, width = 0;
	};
	struct canvas_model {
		std::string key;
		bool loading = false;
		bool placing = false;   // whether its cards can be moved: it names a command for it
		bool resizing = false;  // whether its regions can be made wider or narrower
		std::vector<canvas_region> regions;
		double grid = 0, origin_x = 0, origin_y = 0;
		std::vector<canvas_card> cards;
		std::string error;
	};
	canvas_model canvas_of(page_base& p, const canvas_spec& spec);
	// Where a point lands on the canvas's grid, or the point itself without one.
	double snapped(double at, double origin, double grid);
	// Places a card: shown there at once, sent to the canvas's command, and back where
	// it was if it's turned down.
	void place_card(page_base& p, const canvas_spec& spec, const std::string& card, double x, double y, double w, double h);
	// Makes a region as wide as it's dragged to: shown so at once, sent to the canvas's
	// resize command, and back if it's turned down.
	void resize_region(page_base& p, const canvas_spec& spec, const std::string& region, double width);
	// Makes a card of each file pasted, one path a line, at x and y, a little apart;
	// false when there's nothing to make or no way to make it.
	bool paste_on_canvas(page_base& p, const canvas_spec& spec, const std::string& pasted, double x, double y);

	// Cards: each a row with what it shows, a tally of what it holds, and links
	// opening it filtered.
	struct card_link {
		std::string label, address, color;
	};
	struct card_model {
		std::string id, title, address, tally;
		std::vector<std::pair<std::string, int>> counted;  // the tally's parts in order, like To do and 3, which a bar draws
		std::vector<std::string> counted_states = {};      // what each part means: none, partial, done or failed
		std::vector<std::string> details;
		std::vector<std::string> tags;  // its choices beside its title, like Private
		std::vector<card_link> filters;
	};
	std::vector<card_model> cards_of(page_base& p, const cards_spec& spec);

	// What find found in each list, and how many more.
	struct found {
		std::string label;
		std::vector<std::pair<std::string, std::string>> results;  // what's said, and the address it opens
		std::size_t more = 0;
	};
	std::vector<found> find_in(page_base& p, const std::string& query, const std::vector<find_source>& sources);

	// What goes between a list's things: their names, and for each two the entry
	// from one to the other and what it shows.
	struct between_model {
		std::vector<value> things, entries;
		std::string name(const between_spec& spec, const value& thing) const;
		const value* between(const between_spec& spec, const value& a, const value& b) const;
		std::string says(const between_spec& spec, const value& entry) const;
		bool may(const between_spec& spec, bool entry) const;
	};
	between_model between_of(const between_spec& spec);

	// A thread's entries and a timeline's, as they're said.
	struct thread_entry {
		std::string id, author, when, body;
	};
	std::vector<thread_entry> thread_of(const view_state& v, const std::string& list);
	struct timeline_entry {
		std::string id, who, what, subject, address, after, when;
		bool fresh = false;
	};
	struct timeline_model {
		std::vector<timeline_entry> entries;
		std::string title;  // with how many are new
	};
	timeline_model timeline_of(page_base& p, const timeline_spec& spec);

	// A document's fields as details say them, each with the tone of the one it's tinted by.
	struct detail_line {
		std::string field, label, value;
		int tone = 3;
	};
	// Files said by their names, the last part of where each is kept.
	std::string file_names(const value& held);
	std::vector<detail_line> details_of(const view_state& v, const std::vector<column>& fields, const std::map<std::string, std::vector<option>>& choices,
	                                    const std::optional<tint_spec>& tint);

	// table_of, a template for the toolkit's view in a table's tools.
	table_model table_rows(page_base& p, const state<view_state>& view, const std::string& list, const std::vector<std::pair<std::string, std::string>>& only, bool hide_empty,
	                       const std::string& sort, const std::optional<reorder_spec>& reorder, const std::optional<track_spec>& track,
	                       const std::vector<std::string>& search, const std::map<std::string, std::vector<option>>& choices, const std::string& by, int page_size);
	template <class View>
	table_model table_of(page_base& p, const basic_table_spec<View>& spec) {
		return table_rows(p, spec.view, spec.list, spec.only, spec.hide_empty, spec.sort, spec.reorder, spec.track, spec.search, spec.choices, spec.by, spec.page);
	}

	// Themes made at random from a palette, tried on the whole app: one for every page,
	// since a theme is the app's. Each toolkit draws the app in the one tried, told by
	// on_tried, and in its own again when there's none.
	namespace tried_themes {
		state<std::optional<palette::theme>> theme();
		state<std::string> scheme();
		// A theme from a new seed, in the scheme picked.
		void make_new();
		// The scheme picked, the theme tried made again in it.
		void pick(const std::string& scheme);
		// The app's own theme again.
		void put_back();
		// What draws the app in a theme, or in its own for none.
		void on_tried(std::function<void(const std::optional<palette::theme>&)> apply);
	}  // namespace tried_themes

}  // namespace one::screens
