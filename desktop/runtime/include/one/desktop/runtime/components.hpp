// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a screen is made of, as @uione/react's components are on the web, drawn by
// one::desktop in the site's theme. Each takes the page it's on and what the
// screen says of it, a spec of one::screens'; one::screens works out what each
// shows, and these draw it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "one/desktop/icons.hpp"
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

	// An equation written in one::math's notation, set as a book sets it: on its own
	// lines, or in a line of text; one that can't be read shown as it's written.
	desktop::view equation(std::string written, bool displayed);
	// The same, set at a size, for something that draws it itself: how big it is, and
	// drawing it with its top left at a point. One that can't be read isn't read.
	struct set_equation {
		bool read = false;
		float width = 0, ascent = 0, descent = 0;
		std::function<void(desktop::canvas&, desktop::point)> draw = {};
	};
	set_equation equation_set(const std::string& written, float em, bool displayed, color ink);
	// Text, with values read live from views, quiet as a status line's is; a subtitle
	// says less, under the title.
	desktop::view text(std::vector<part> parts, bool quiet = false);
	desktop::view subtitle(page& p, std::vector<part> parts);
	// The pages above this one, nearest last, each followed back with a click.
	desktop::view crumbs(page& p, std::vector<crumb> trail);
	desktop::view code(std::string language, std::string source);
	// An icon by its name, like edit, as the glyph drawn before what a button or a
	// link says; nothing for one it doesn't know.
	std::string glyph_of(const std::string& icon);

	desktop::view section(section_title title, std::vector<desktop::view> inside);
	template <std::convertible_to<desktop::view>... V>
	desktop::view section(section_title title, V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return section(std::move(title), std::move(all));
	}
	// One of the library's colors, like violet, in the theme's shade of it.
	color section_color(const std::string& name);
	// A tone of one::screens' tone_of, as it's drawn: none for 3, plain.
	std::optional<color> tone_color(int tone);

	// A link to another screen, to a part of this one, like #projects, or out of the app.
	desktop::view link(page& p, std::string words, std::string to, std::string icon = "", std::vector<std::string> keys = {});
	// link next in library.things "Next": a button to the record after this one, or
	// before it, in the record's place; faint, and not pressed, at either end.
	desktop::view step(page& p, step_spec spec, std::string words, std::string icon = "", std::vector<std::string> keys = {});
	// A part of the page a link names, like #projects, brought to the top when it's followed.
	desktop::view anchored(page& p, std::string name, desktop::view part);
	desktop::view hero(std::string title, std::vector<desktop::view> inside);
	template <std::convertible_to<desktop::view>... V>
	desktop::view hero(std::string title, V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return hero(std::move(title), std::move(all));
	}
	// A front page's hero with a short line over its title.
	desktop::view hero(section_title title, std::vector<desktop::view> inside);
	template <std::convertible_to<desktop::view>... V>
	desktop::view hero(section_title title, V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return hero(std::move(title), std::move(all));
	}
	// A front page's one sentence that stands for the site.
	desktop::view statement(std::vector<part> parts);

	desktop::view command(page& p, command_button b);
	// Lines side by side in a toolbar with the same words, like Bold that sets it and
	// Bold that takes it away: one tool, the first whose when holds, or the first, off.
	desktop::view either(page& p, std::vector<command_button> ways);
	desktop::view confirm(page& p, std::string command, std::string question);
	desktop::view actions(std::vector<desktop::view> all);
	template <std::convertible_to<desktop::view>... V>
	desktop::view actions(V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return actions(std::move(all));
	}
	desktop::view heading(page& p, desktop::view beside);
	desktop::view layout(std::string name, std::vector<region> regions);
	template <class... R>
		requires(std::same_as<std::decay_t<R>, region> && ...)
	desktop::view layout(std::string name, R&&... each) {
		std::vector<region> all;
		(all.push_back(std::forward<R>(each)), ...);
		return layout(std::move(name), std::move(all));
	}
	desktop::view menu(page& p, std::vector<menu_link> links, desktop::view beside);

	// A workspace, filling the window as a drawing app does: its bar along the top,
	// its toolbar under it, the work beside what's at its side, and a status line
	// along the bottom. Its bar, toolbar and status line are each a row, along(...).
	desktop::view workspace(page& p, std::vector<region> regions);
	template <class... R>
		requires(std::same_as<std::decay_t<R>, region> && ...)
	desktop::view workspace(page& p, R&&... each) {
		std::vector<region> all;
		(all.push_back(std::forward<R>(each)), ...);
		return workspace(p, std::move(all));
	}
	desktop::view along(std::vector<desktop::view> all);
	template <std::convertible_to<desktop::view>... V>
	desktop::view along(V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return along(std::move(all));
	}
	// A toolbar's group, its name under its tools, which its buttons are built as;
	// small, two rows of icons; at its end, at the toolbar's far end.
	// A small one's tools are laid in rows `across` long, or, when it's 0, in two rows, the
	// first the longer.
	// `said`: its name written under it, unless its one tool says it already.
	desktop::view group(page& p, std::string name, bool small, bool at_end, std::function<std::vector<desktop::view>()> build, int across = 0, bool said = true);
	// A tool that opens its lines, each a choice drawn with what it's for, over
	// everything until one's chosen.
	desktop::view options(page& p, std::string label, std::string icon, std::function<std::vector<desktop::view>()> build);
	// And kept open or closed by `key`, apart from others of the same words.
	desktop::view options(page& p, std::string label, std::string icon, std::string key, std::function<std::vector<desktop::view>()> build);
	// A workspace's tools, a column down its left side, each a small tool, its words its hint.
	desktop::view tool_column(page& p, std::function<std::vector<desktop::view>()> build);
	// A group's lines, each its own.
	template <std::convertible_to<desktop::view>... V>
	std::vector<desktop::view> tools(V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return all;
	}
	// toggle layers "Layers": on or off, as the person left it.
	desktop::view toggle(page& p, std::string name, std::string label, std::string icon, std::vector<std::string> keys);
	// toggle tool::pen: one of an enum's choices, pressed while it's the one the page has.
	desktop::view choose(page& p, std::string name, std::string choice, std::string first, std::string label, std::string icon, std::vector<std::string> keys);
	// undo "Undo", or redo: the page's last step, said in its messages.
	desktop::view history(page& p, bool redo, std::string label, std::string icon, std::vector<std::string> keys);
	// What was just done, or why something was turned down.
	desktop::view messages(page& p);
	desktop::view full_screen(page& p, std::string label, std::string icon, std::vector<std::string> keys);
	// Whether a key pressed is a chord, like ctrl+z, and the chord as people read it, Ctrl+Z.
	bool pressed(const key_event& e, std::string_view chord);
	std::string key_shown(std::string_view chord);
	// What a line says when it's pointed at: its words, and its first key.
	std::string hint_of(const std::string& words, const std::vector<std::string>& keys);
	// Ways of showing the same list, as the person picks: their own pick, kept for them,
	// or else `start`, like the one a board's style says, or the first when it's -1.
	desktop::view switched(page& p, std::string id, std::string label, std::vector<std::string> options, int start, std::vector<desktop::view> ways);
	template <std::convertible_to<desktop::view>... V>
	desktop::view switched(page& p, std::string id, std::string label, std::vector<std::string> options, int start, V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return switched(p, std::move(id), std::move(label), std::move(options), start, std::move(all));
	}
	desktop::view steps(page& p, steps_spec spec);

	desktop::view form(page& p, form_spec spec);
	desktop::view table(page& p, table_spec spec);
	desktop::view board(page& p, board_spec spec);
	// A list's rows on the days a date of theirs says (see calendar_spec).
	desktop::view calendar(page& p, calendar_spec spec);
	desktop::view canvas_board(page& p, canvas_spec spec);
	desktop::view collage(page& p, collage_spec spec);
	// A collage's layers as a list, topmost first: each hidden or shown, previewed, named,
	// moved up or down and deleted.
	desktop::view layer_list(page& p, layers_spec spec);
	// A list's rows as one sheet of paper, written on in place: each a block of words
	// set as its kind says and aligned as it says (see paper_spec).
	desktop::view paper(page& p, paper_spec spec);
	// Where a picture's bytes, like one pasted, are kept until they're sent: this person's
	// own runtime folder, readable by them alone.
	std::filesystem::path kept_for_now(const std::vector<std::uint8_t>& bytes);
	// What a picture is called: its file's name without its extension, or Pasted image
	// for one that came as bytes and was kept for now.
	std::string picture_name(const std::filesystem::path& file);
	// Pictures on the clipboard, as files or a picture's bytes kept for now.
	std::vector<std::filesystem::path> clipboard_pictures();
	// What a screen's paste or add pictures button handed what it names by `key`: the
	// pictures chosen, or, for a paste, the clipboard's, said when there are none.
	std::vector<std::filesystem::path> pictures_handed(page& p, const std::string& key);
	// A view pictures are dropped on, as files or a browser's picture, outlined while
	// they're over it; and those dropped on it since it was last built.
	desktop::view pictures_target(page& p, desktop::view shown, const std::string& key);
	std::vector<std::filesystem::path> pictures_dropped(page& p, const std::string& key);
	// A picture handed to something that takes them, as it comes in: measured, and kept
	// where the source keeps files, away from the window's thread, so a large one, or
	// many, never hold it up. Its place among those handed with it, and whether one
	// before it came in, so what it makes joins theirs as one step to undo.
	struct coming_picture {
		std::filesystem::path file = {};
		std::string kept = {};  // what its file is kept as, or nothing when it's kept with its command
		std::string svg = {};   // its text, when it's an SVG
		int width = 0, height = 0;
		std::optional<long long> bytes = {};
		std::string date = {};    // when it was taken, as its camera says, or else when its file last changed
		std::string camera = {};  // the camera's make and model, as it says them
		std::size_t index = 0, count = 1;
		bool joined = false;
	};
	// Pictures handed to what `key` names, each measured and kept in turn in the
	// background, the workspace saying how far it's got while there are several, and,
	// once they're in, what couldn't be taken; and those come in since it was last
	// built, in order. Given what's `held` already, as the files its records name, a
	// picture kept as one of them, or as one before it, isn't come in again, and once
	// they're in the workspace says how many were added and how many were already in.
	void pictures_coming(page& p, const std::string& key, std::vector<std::filesystem::path> files, std::optional<std::set<std::string>> held = std::nullopt);
	std::vector<coming_picture> pictures_come(page& p, const std::string& key);
	// What's said once they're in, when some were already: "Added 31 pictures, 9 were
	// already in." The same as the web's.
	std::string pictures_already_said(std::size_t added, std::size_t already);
	// Runs a command making something of a picture come in, its file in `field`.
	void run_with_picture(page& p, const std::string& command, value made, const std::string& field, const coming_picture& picture, std::function<void(const answer&)> after);
	desktop::view paper_button(page& p, paper_ask ask);
	desktop::view paper_size(page& p, paper_ask ask);
	// crop "Crop", edit words "Edit", add words "Text": asks the screen's collage.
	desktop::view collage_button(page& p, collage_ask ask);
	// on open: what the screen does as it's opened, and "Opening…" while it decides.
	desktop::view on_open(page& p, bool ready, std::vector<opening> lines);
	// The screen the app is shown on, in its own pixels: screen.width and screen.height,
	// or a common one's while it isn't known.
	double screen_width();
	double screen_height();
	// A file's picture, `height` tall, all of it fitted in, or filling its place, on the
	// sunken color while there's none, as a card's is drawn: read away from the window's
	// thread, and a copy no longer on its longer side than `longest` where that's given,
	// as for a card, where the whole of a large one would be wasted.
	desktop::view picture_of(page& p, const std::string& kept, bool fit, float height, int longest = 0);
	// Several files' pictures set together, `tall` times as tall as it's wide, as a
	// collection's cover is: five of them, chosen and placed by their shapes (see
	// screens::cover_places) once all are read, each filling its place, two pixels apart.
	desktop::view pictures_of(page& p, const std::vector<std::string>& kept, float tall, int longest = 0);
	// A file's picture alone, the whole of the screen, as a photo's own screen is: all of
	// it fitted in on black, edge to edge, the workspace's bars drawn over it. Zoomed by
	// the screen's zoom buttons, by Ctrl and =, - or 0, by the wheel with Ctrl held or a
	// pinch about where they point, and to 100% and back by a double click; dragged, or
	// scrolled, to move about it once it's larger than fits.
	desktop::view picture_alone(page& p, const std::string& kept, const std::string& key);
	// Whether a screen that's only its picture shows its bars and the pointer: for two
	// seconds after the pointer moves or a key is pressed, then not until either is
	// again. wake() is told of the pointer's moves, and rest() is the two seconds over,
	// as a test says without waiting.
	bool awake();
	void wake();
	void rest();
	// A key pressed, before anything hears it, which wakes them unless it's one held
	// for another, like Ctrl; and one that, as it turned out, only stepped to the next
	// picture, which leaves them as they were.
	void key_down(const key_event& e);
	void stepped();
	// A screen is about to be built, which says again whether it's one; one that comes
	// from another screen wakes them.
	void screen_begun();
	// A file's picture as the window draws it, the same way: nothing until it's read,
	// and an empty one when there's no such file or it isn't a picture.
	std::optional<desktop::picture> picture_soon(page& p, const std::string& kept, int longest = 0);
	// The whole of the picture of the record at `address`, which a step on the page goes
	// to, read away from the window's thread, after the page's own, while the page's open.
	void read_ahead(page& p, const std::string& address, const std::string& kept);
	// A step from the page to `to`: the whole pictures read for the records it steps away
	// from let go, so only the one shown and those beside it are kept.
	void stepping(page& p, const std::string& to);
	// Whether the whole of a file's picture is read, to be drawn as soon as it's asked for.
	bool read_whole(page& p, const std::string& kept);
	// Where a step last went, which a picture alone shows the last picture on until its
	// own is read.
	std::string& last_stepped_to();
	// A key pressed or a click made, before anything hears it: a slideshow playing stops,
	// and its own toggle, if that's what it was, doesn't start it again.
	void input_heard();
	bool stopped_just_now(const std::string& toggle);
	desktop::view pipeline_board(page& p, pipeline_spec spec);
	desktop::view cards(page& p, cards_spec spec);
	// A timeline of cards under headings by a date, drawn along them, as dated_timeline_spec says.
	desktop::view dated_timeline(page& p, dated_timeline_spec spec);
	// How far something is in its stage, like 2 of 5: a thin bar in the accent and its count.
	desktop::view progress_bar(double step, double steps, float width);
	// chart agent_page.stats.cpu "CPU" percent: a run of numbers over time, large.
	desktop::view chart(page& p, chart_spec spec);
	// Records whose status changes, like agents, a tile each.
	desktop::view monitor(page& p, monitor_spec spec);
	// What a list on the page is filtered by, with a button showing it all again;
	// nothing when it isn't.
	desktop::view filter_note(page& p);
	desktop::view find(page& p, std::string label, std::vector<find_source> sources);
	desktop::view copy(page& p, state<view_state> from, std::string label, std::vector<copied_field> fields, std::vector<copied_list> lists,
	                   std::map<std::string, std::vector<screens::option>> choices = {});
	// Themes made at random from a palette, tried on the whole window, each written
	// as a project says it, to copy.
	desktop::view themes(page& p);
	// A theme from a palette, as the desktop draws with it: its light colors, on what
	// the app's own theme says besides.
	desktop::theme drawn_in(const palette::theme& tried, desktop::theme own);
	desktop::view grid(page& p, between_spec spec);
	desktop::view diagram(page& p, between_spec spec);
	desktop::view thread(page& p, thread_spec spec);
	desktop::view timeline(page& p, timeline_spec spec);
	desktop::view details(page& p, state<view_state> from, std::vector<screens::column> fields, std::map<std::string, std::vector<screens::option>> choices = {},
	                      std::optional<tint_spec> tint = std::nullopt, std::vector<std::string> labels = {},
	                       std::vector<std::string> files = {});

	// The cards above a list for another list's records, picking one keeping its rows.
	desktop::view track(const std::optional<track_model>& model);
	// A switch's track, drawn once above it, with what's below it: the list's records
	// as the track says, each with its dot, and the pick kept for every way it's shown.
	desktop::view tracked(page& p, track_spec spec, state<view_state> view, std::string list, desktop::view below);

}  // namespace one::desktop::runtime
