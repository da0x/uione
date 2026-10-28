// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

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
	// says, like violet: section({"More", "violet"}, text(...), ...). On a front page,
	// a hero's or a section's title can have a short line over it, a section an icon
	// beside it, and a step of a section as steps its number.
	struct section_title {
		std::string title;
		std::string color = "";
		std::string icon = "";
		int step = 0;
		std::string overline = "";
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
		std::string swatch = "";              // in a toolbar, drawn as a circle of this color, like #ffffff
		std::vector<std::string> keys = {};  // run by these too, like ctrl+s, while it's shown
		bool pressed = false;                // drawn pressed, like a choice already made
		std::string then_open = "";          // a route like /:poster, opened with what it made
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
		// text, email, date, number, slider (a number in `range`), markdown, boolean,
		// list, choice (one of `choices`, a few of them radio buttons), pick (one of a
		// list on the screen's), choices (several), file or files
		std::string type = "text";
		std::string hint = "";
		std::vector<option> choices = {};
		std::string start = "";  // what a new one starts as
		bool required = false;   // a choice that's always one of them
		struct number_range {
			double from = 0, to = 0, by = 1;
		};
		number_range range = {};  // a slider's
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
		std::string then_open = "";  // a route like /boards/:board, opened with what it made
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
		bool hide_empty = false;  // a record none of the rows names isn't on it, unless it's picked
		// In a switch, whose pick it is, shared by every way the list is shown, and that
		// it's drawn once above the switch rather than by each.
		std::string key = "";
		bool above = false;
		// What each record's dot is worked out from, as a board's cards' are: the rows'
		// field naming their stage, the stages, and each stage's status, from a field of
		// its own or as written.
		std::string by = "";
		std::optional<state<view_state>> over = std::nullopt;
		std::string over_list = "", status_by = "";
		std::map<std::string, std::string> statuses = {};
		// The last record is picked until another is, or none is, like the newest milestone.
		bool picked_last = false;
	};

	// Rows put in order by a number, Alt with Up or Down moving the one chosen
	// past its neighbor, which runs the command with the number between its new
	// neighbors', as dragging does on the web; it stays where it's put until the view
	// says so too.
	struct reorder_spec {
		std::string command;
		std::string field;
		bool allowed = true;
		bool descending = false;  // the highest first
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
		bool anywhere = false;  // or to any column from any other, as nothing says the steps
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
		std::vector<std::string> notify = {};  // the columns that chime when a card arrives
		std::map<std::string, std::string> statuses = {};  // a column's status, the dot its cards show unless they say
		// Columns kept as records: the field of theirs that lists how they notify, and
		// the word in it that chimes; and the field that's their status.
		std::string notify_by = "", notify_has = "", status_by = "";
		// The field listing what a card waits on: while any isn't in a column whose status
		// is success, it's waiting, saying on what.
		std::string waits_on = "";
		std::string pick = "";  // the screen's pick of its cards, by name: a card's click picks it
	};

	// A list's rows on the days a date of theirs says: a week of days side by side, or a
	// month's grid while month holds; one dragged to another day sent to move with its
	// date there, its time of day kept (decision 0023).
	struct calendar_spec {
		state<view_state> view;
		std::string list;
		std::string by;               // the date placing a row on its day, like due
		bool month = false;           // a month's grid, or a week
		std::vector<column> columns = {};  // what a row shows, its title first
		std::string move = "";        // the command a row dragged to another day is sent to
		std::optional<tint_spec> tint = std::nullopt;
		std::string pick = "";        // the screen's pick of its rows, by name: a row's click picks it
		std::string key = "";         // what keeps the day it's on apart from another calendar's
	};

	// A pipeline: a list's rows in a lane for each kind its `lanes` field says, each a
	// row of the stages of `over` its kind goes through, as their `path` field lists.
	struct pipeline_spec {
		state<view_state> view;
		std::string list;
		std::string by;
		state<view_state> over;
		std::string over_list;
		std::string shown = "title";       // what a stage is called
		std::vector<column> columns = {};  // what a row shows, its title first
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::string lanes = "labels", path = "kinds";
		std::string status_by = "";  // the stage's field that's its status
		std::string waits_on = "";   // the field listing what a card waits on, as a board's
		std::optional<track_spec> track = std::nullopt;  // in a switch, the track above, whose pick keeps its rows
	};

	// A canvas: a list's rows as cards placed anywhere, each where its x, y, w and h
	// say, moved and resized, and placed by `place` once let go, snapped to the grid
	// when there's one. One not placed yet is laid out in columns by `by`, over the
	// rows of `over`, as a board would show it.
	struct canvas_spec {
		state<view_state> view;
		std::string list;
		std::vector<column> columns = {};  // what a card shows, its title first
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::string x, y, w, h;  // the fields a card's place is kept in
		std::string kind = "";   // the field saying how each card's drawn, like note or text
		std::string place = "";  // the command a card's place is sent to
		double grid = 0, origin_x = 0, origin_y = 0;
		std::string by = "";
		std::optional<state<view_state>> over = std::nullopt;
		std::string over_list = "";
		std::optional<tint_spec> tint = std::nullopt;
		// A band down it for each of over's rows, as wide as its `regions` field says, or
		// two grid steps: a card let go in another band moves there by `move`, and the
		// line between two is dragged to resize one, sent to `resize`.
		std::string regions = "";
		std::string move = "";
		std::string resize = "";
		std::optional<track_spec> track = std::nullopt;  // in a switch, the track above, whose pick keeps its rows
		// A file pasted on it, as its path, makes a card by `paste`: the file in
		// `paste_into`, named from it in `paste_named`, with what's `given` besides.
		std::string paste = "";
		std::string paste_into = "";
		std::string paste_named = "";
		std::map<std::string, std::string> given = {};
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
		std::string icon = "";  // delete "Remove" icon delete: drawn as that, its words said when pointed at
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
		std::string download = "";  // a button saving every row it keeps in a file of this name, like audit.csv
		std::string pick = "";      // the screen's pick of its rows, by name: a row's click picks it
		std::string picture = "";   // a file field's small picture before each row's cells
		bool fit = false;           // all of it fitted in, or filling its place
	};

	// crop "Crop", edit words "Edit" and add words "Text": what's asked of the screen's
	// collage, by what names it, carried out as it's next built.
	// on open: one of what a screen does as it's opened, tried in order. A row opened:
	// the first of a view's list `where` holds for, at `open` with its id. Or a command
	// run, as its button would.
	struct opening {
		std::optional<command_button> run = std::nullopt;
		std::string open = "";
		state<view_state> view = {};
		std::string list = "";
		std::function<bool(const value&)> where = {};
		bool when = true;
	};

	struct collage_ask {
		std::string key, does, label, icon;
		std::vector<std::string> keys = {};
	};

	// draw layer::create { fill = "#2e86de" } when tool == tool::box: a drag on the
	// collage makes a layer by `command`, as big as the drag, given `fields`, while
	// `when` holds; then each choice its when reads, by its key, goes back to its first.
	struct collage_draw {
		std::string command;
		value fields = {};
		bool when = true;
		std::vector<std::pair<std::string, std::string>> back = {};
	};

	// A picture made of layers, as big as two of the view's values say, in its own
	// pixels (decision 0021): its rows, lowest first, each placed by x y w h and drawn
	// from picture, crop_x crop_y crop_w crop_h, turns, mirrored and around, words,
	// size, color and weight.
	struct collage_spec {
		state<view_state> view;
		std::string list;
		std::string width, height;
		// The field each of its parts is kept in, by part, like words: caption. It reads
		// and sends only parts; these say which of the app's fields they are.
		std::map<std::string, std::string> fields = {};
		std::string place = "";  // the command a layer moved or resized is sent to, with x y w h
		bool snap = false;       // to the collage's edges and middle, and every other layer's
		bool match_colors = false;  // its pictures' colors moved toward one another's, so they look like one picture
		std::string empty = "";  // said while it has no layers
		std::string pick = "";   // the screen's pick of its layers, which a click picks
		std::vector<command_button> over = {};  // buttons above the picked layer
		std::string subject = "";               // the id of what the view is per
		bool fills = false;                     // in a workspace, as big as its region; elsewhere 560 tall
		std::string add = "";     // the command that makes a layer, sent `parent` = subject
		std::vector<collage_draw> draws = {};  // what a drag on it draws, the first whose when holds
		std::string crop = "";    // and the one a picture cropped is sent to, its crop and its place
		std::string write = "";   // words written, or resized: their words, size and place
		std::string remove = "";  // a layer deleted, like words written away to nothing
		std::string parent = "";  // the field a layer names what the view is per by, like poster
		std::string key = "";     // what its buttons, like crop and add words, name it by
		std::vector<collage_ask> asks = {};  // over the picked layer too, before the commands, like crop
		bool paste = false;       // a file dropped or pasted made a picture, by add
		std::string update = "";  // the command a picture's own size is filled in by, when it isn't kept yet
		std::string preview = "";            // the command a small picture of it is sent to, once changes settle
		std::string preview_into = "";       // in this file field of what the view is per
		int preview_wide = 480;              // this many pixels wide
	};

	// A list's rows as one upright sheet of paper, written on in place, as a page of a
	// document is: each row a block, in the order its position says, set as its kind
	// says (title, subtitle, heading, text, quote, math or code) and aligned left, center or
	// right as its align says, or code. Typing writes the block the caret is in; Enter splits it
	// in two, the part after the caret made a block by add; Backspace at a block's
	// start joins it to the one before, deleting it. The caret's block is the screen's
	// pick, which the toolbar acts on.
	// insert "Equation" icon equation { kind = kind::math }: a block made after the one the
	// paper has the caret in, given these fields, the caret then in it.
	struct paper_ask {
		std::string key, label, icon;
		std::vector<std::string> keys = {};
		value fields = {};
		int sizes = 0;  // or, larger or smaller, the reader's page made a step larger, 1, or smaller, -1
		bool pastes = false;  // or what's on the clipboard pasted into the paper
		bool chooses = false;  // or pictures chosen from the device's files
	};

	struct paper_spec {
		state<view_state> view;
		std::string list;
		// The field each of its parts is kept in, by part, like words: body.
		std::map<std::string, std::string> fields = {};
		std::string write = "";   // the command a block's words written are sent to, with its kind when a shortcut like "# " changes it
		bool writes_kind = false;  // whether that command changes the kind, so "# " can make a heading
		std::string add = "";     // the command that makes a block: id-less, with position, words, kind and align
		std::string remove = "";  // the one that deletes a block joined into the one before
		std::string parent = "";  // the field a block names what the view is per by, like document
		std::string subject = "";  // the id of what the view is per
		std::string pick = "";     // the screen's pick of its blocks, the one the caret is in
		std::string key = "";
		std::string empty = "";    // what an empty page says where its first words go
		bool fills = false;        // in a workspace, as big as its region, scrolling
		bool outline = false;      // its title and headings listed beside it, the caret's section marked, each going there
		bool outline_drawer = false;  // and as a drawer at its side, there while outline holds, as a toggle says
		bool paste = false;        // pictures pasted or dropped, made picture blocks after the caret
		bool progress = false;     // a bar across its top, as far along as it's been read
		bool warm = false;         // set on warm paper, as under a reading lamp: its page, sheet, ink and lines
		bool spread = false;       // its pages two at a time, side by side
		bool read_only = false;    // read, not written on: no caret, and keys left for the page
		double size = 1;           // the reader's size, as a book is zoomed, read as it's built
		// The face it's all set in, book, modern, lyric or typewriter, as its view's face
		// part says; read as it's built.
		std::string face = "";
	};

	// A collage's view as its parts: its own values and each of its layers, with the
	// fields its parts are kept in read as those parts.
	inline value collage_parts_of(const value& data, const std::string& list, const std::map<std::string, std::string>& fields) {
		if (!data.is_map()) return data;
		auto as_parts = [&](const value& from) {
			value out = from;
			for (const auto& [part, field] : fields) out.fields.erase(part);
			for (const auto& [part, field] : fields) {
				if (const value* held = from.find(field)) out.fields[part] = *held;
			}
			return out;
		};
		value out = as_parts(data);
		std::vector<value> layers;
		for (const auto& layer : data[list].as_array()) layers.push_back(as_parts(layer));
		out.fields[list] = value(std::move(layers));
		return out;
	}

	// And what it sends, its parts named as the fields they're kept in.
	inline value collage_fields_of(const value& sent, const std::map<std::string, std::string>& fields) {
		value out = sent;
		for (const auto& [part, field] : fields) out.fields.erase(part);
		for (const auto& [part, field] : fields) {
			if (const value* held = sent.find(part)) out.fields[field] = *held;
		}
		return out;
	}

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
		std::string status = "";  // like stage.status: what each part means, not started, under way, done or failed
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
		std::map<std::string, std::vector<option>> choices = {};  // its choice columns, each a tag beside a card's title, as it's shown
		bool hide_empty = false;                                  // not there while it has no cards, or can't be read
		std::string picture = "";                                 // a file field's picture on top of each card
		bool fit = false;                                         // all of it fitted in, or filling its place
	};

	// A chart's run of numbers: by its field and a name in it, like stats.cpu; its
	// title; and its unit, "", percent, bytes or rate.
	struct chart_line {
		std::string path;
		std::string title;
		std::string unit = "";
	};

	// One chart, or several side by side.
	struct chart_spec {
		state<view_state> view;
		std::vector<chart_line> series;
	};

	// Records whose status changes, each a tile, like a project's agents.
	struct monitor_spec {
		state<view_state> view;
		std::string list;
		std::string title = "title";
		std::string status = "status";
		std::string doing = "";
		std::string since = "";
		std::string step = "", steps = "";
		std::string seen = "";
		int seen_seconds = 0;  // one not heard from in this long is offline
		std::vector<chart_line> charts = {};
		std::string link = "";
		std::vector<std::string> keyed = {};
		std::map<std::string, int> named = {};
		std::vector<option> choices = {};  // its status's, as each is said
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
