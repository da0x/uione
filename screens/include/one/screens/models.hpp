// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What each thing on a screen works out before it's drawn, the same for every
// toolkit: which rows a table shows on which tab and page, where a board's cards
// are and where they may go, what a timeline says of each change, what a form
// sends. A toolkit draws the result its own way, and calls back to act.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

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
	};
	struct track_model {
		std::string label;
		std::vector<track_record> records;
		std::string picked;
		state<std::string> picking;
		bool keeps(const value& row, const std::string& field) const { return picked.empty() || field_of(row, field).as_string() == picked; }
		void pick(const std::string& id) const { picking.set(picking.peek() == id ? "" : id); }
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
	// A cell's words: a choice as it's shown, and anything else as it reads.
	std::string cell_text(const std::map<std::string, std::vector<option>>& choices, const value& row, const std::string& column);
	// The names of the people a cell holds, one or several.
	std::vector<std::string> people_in(const value& row, const std::string& column);
	// The row a table's action asks about, as a view's document for its form.
	state<view_state> acted_on(page_base& p, const std::string& list, const std::string& command, const value& row);

	// A board: its columns, each with its cards, and a card's move being chosen.
	struct board_card {
		std::string id, title;
		std::vector<std::string> details;  // like #12
		std::vector<std::string> labels, people;
		int tone = 3;
		std::string link;
	};
	struct board_column {
		std::string id, title;
		std::vector<board_card> cards;
		std::vector<value> rows;  // the cards' rows, for putting them in order
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
	// Moves a card to a column, shown there at once and back if it's turned down.
	void move_card(page_base& p, const board_spec& spec, const std::string& card, const std::string& to);

	// Cards: each a row with what it shows, a tally of what it holds, and links
	// opening it filtered.
	struct card_link {
		std::string label, address, color;
	};
	struct card_model {
		std::string id, title, address, tally;
		std::vector<std::string> details;
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

}  // namespace one::screens
