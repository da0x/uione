// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a screen is made of, drawn in a terminal: the same components, by the same
// names and specs, as one::desktop::runtime draws in a window. one::screens works
// out what each shows, and these draw it in cells.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "one/terminal/runtime/runtime.hpp"

namespace one::terminal::runtime {

	terminal::view items(std::vector<terminal::view> all);
	template <std::convertible_to<terminal::view>... V>
	terminal::view items(V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return items(std::move(all));
	}

	terminal::view text(std::vector<part> parts);
	terminal::view subtitle(page& p, std::vector<part> parts);
	terminal::view crumbs(page& p, std::vector<crumb> trail);
	terminal::view code(std::string language, std::string source);
	terminal::view section(section_title title, std::vector<terminal::view> inside);
	template <std::convertible_to<terminal::view>... V>
	terminal::view section(section_title title, V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return section(std::move(title), std::move(all));
	}
	// A link to another screen, to a part of this one, like #projects, or out of the app.
	terminal::view link(page& p, std::string words, std::string to, std::string icon = "", std::vector<std::string> keys = {});
	// link next in library.things "Next": to the record after this one, or before it, in
	// the record's place; only its words at either end.
	terminal::view step(page& p, step_spec spec, std::string words, std::string icon = "", std::vector<std::string> keys = {});
	// A part of the page a link names, like #projects, brought to the top when it's followed.
	terminal::view anchored(page& p, std::string name, terminal::view part);
	terminal::view hero(std::string title, std::vector<terminal::view> inside);
	template <std::convertible_to<terminal::view>... V>
	terminal::view hero(std::string title, V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return hero(std::move(title), std::move(all));
	}
	// A front page's hero with a short line over its title.
	terminal::view hero(section_title title, std::vector<terminal::view> inside);
	template <std::convertible_to<terminal::view>... V>
	terminal::view hero(section_title title, V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return hero(std::move(title), std::move(all));
	}
	// A front page's one sentence that stands for the site.
	terminal::view statement(std::vector<part> parts);

	terminal::view command(page& p, command_button b);
	// Lines side by side in a toolbar with the same words: one, the first whose when
	// holds, or the first, off.
	terminal::view either(page& p, std::vector<command_button> ways);
	terminal::view confirm(page& p, std::string command, std::string question);
	terminal::view actions(std::vector<terminal::view> all);
	template <std::convertible_to<terminal::view>... V>
	terminal::view actions(V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return actions(std::move(all));
	}
	terminal::view heading(page& p, terminal::view beside);
	terminal::view layout(std::string name, std::vector<region> regions);
	template <class... R>
		requires(std::same_as<std::decay_t<R>, region> && ...)
	terminal::view layout(std::string name, R&&... each) {
		std::vector<region> all;
		(all.push_back(std::forward<R>(each)), ...);
		return layout(std::move(name), std::move(all));
	}
	terminal::view menu(page& p, std::vector<menu_link> links, terminal::view beside);

	// A workspace, as the desktop's: its bar, its toolbar, the work beside what's at
	// its side, and a status line, each across the terminal.
	terminal::view workspace(page& p, std::vector<region> regions);
	template <class... R>
		requires(std::same_as<std::decay_t<R>, region> && ...)
	terminal::view workspace(page& p, R&&... each) {
		std::vector<region> all;
		(all.push_back(std::forward<R>(each)), ...);
		return workspace(p, std::move(all));
	}
	terminal::view along(std::vector<terminal::view> all);
	template <std::convertible_to<terminal::view>... V>
	terminal::view along(V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return along(std::move(all));
	}
	terminal::view group(page& p, std::string name, bool small, bool at_end, std::function<std::vector<terminal::view>()> build);
	// A tool's options, its name and then each choice, in a row.
	terminal::view options(page& p, std::string label, std::string icon, std::function<std::vector<terminal::view>()> build);
	terminal::view options(page& p, std::string label, std::string icon, std::string key, std::function<std::vector<terminal::view>()> build);
	// A workspace's tools, in a row of their own under its toolbar.
	terminal::view tool_column(page& p, std::function<std::vector<terminal::view>()> build);
	template <std::convertible_to<terminal::view>... V>
	std::vector<terminal::view> tools(V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return all;
	}
	terminal::view toggle(page& p, std::string name, std::string label, std::string icon, std::vector<std::string> keys);
	// toggle tool::pen: one of an enum's choices, marked while it's the one the page has.
	terminal::view choose(page& p, std::string name, std::string choice, std::string first, std::string label, std::string icon, std::vector<std::string> keys);
	terminal::view history(page& p, bool redo, std::string label, std::string icon, std::vector<std::string> keys);
	terminal::view messages(page& p);
	// A collage, as a terminal can show one: its layers a list, the background first,
	// a picture by its place and words by what they say; Enter picks one, and the
	// buttons over it follow.
	terminal::view collage(page& p, collage_spec spec);
	// A collage's layers as a list, topmost first, each with its buttons.
	terminal::view layer_list(page& p, layers_spec spec);
	// crop "Crop", edit words "Edit", add words "Text": asks the screen's collage; a
	// terminal can't crop a picture it can't show, so it doesn't offer to.
	terminal::view collage_button(page& p, collage_ask ask);
	// picture poster.preview fit: a terminal draws no pictures, so it shows none.
	terminal::view picture_of(page& p, const std::string& kept, bool fit, float height);
	// picture clipping_page.image fit alone: nor alone, so its screen is its bars.
	terminal::view picture_alone(page& p, const std::string& kept, const std::string& key);
	// on open: what the screen does as it's opened, and "Opening…" while it decides.
	terminal::view on_open(page& p, bool ready, std::vector<opening> lines);
	// A terminal has no screen of its own to measure, so screen.width and
	// screen.height are a common one's.
	double screen_width();
	double screen_height();
	// A terminal has no window to fill, so full screen draws nothing.
	terminal::view full_screen(page& p, std::string label, std::string icon, std::vector<std::string> keys);
	// Whether a key pressed is a chord, like ctrl+z.
	bool pressed(const key_press& e, std::string_view chord);
	// Ways of showing the same list, as the person picks: their own pick, kept for them,
	// or else `start`, like the one a board's style says, or the first when it's -1.
	terminal::view switched(page& p, std::string id, std::string label, std::vector<std::string> options, int start, std::vector<terminal::view> ways);
	template <std::convertible_to<terminal::view>... V>
	terminal::view switched(page& p, std::string id, std::string label, std::vector<std::string> options, int start, V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return switched(p, std::move(id), std::move(label), std::move(options), start, std::move(all));
	}
	terminal::view steps(page& p, steps_spec spec);

	terminal::view form(page& p, form_spec spec);
	terminal::view table(page& p, table_spec spec);
	terminal::view board(page& p, board_spec spec);
	// A list's rows on their days, each day in turn with its rows under it.
	terminal::view calendar(page& p, calendar_spec spec);
	terminal::view canvas_board(page& p, canvas_spec spec);
	terminal::view pipeline_board(page& p, pipeline_spec spec);
	terminal::view cards(page& p, cards_spec spec);
	// chart agent_page.stats.cpu "CPU" percent: a run of numbers in braille, as btop draws one.
	terminal::view chart(page& p, chart_spec spec);
	// Records whose status changes, like agents, a tile each.
	terminal::view monitor(page& p, monitor_spec spec);
	terminal::view filter_note(page& p);
	terminal::view find(page& p, std::string label, std::vector<find_source> sources);
	// Themes made at random from a palette, tried on the whole terminal, each written
	// as a project says it, to copy.
	terminal::view themes(page& p);
	// Draws the terminal in a theme from a palette, its dark colors, by setting the
	// terminal's own: the text and what's behind it, and red, green, yellow, blue and
	// gray as danger, success, warning, the accent and quieter text. Nothing puts them
	// back to the terminal's.
	void draw_terminal_in(const std::optional<palette::theme>& tried);
	terminal::view copy(page& p, state<view_state> from, std::string label, std::vector<copied_field> fields, std::vector<copied_list> lists,
	                    std::map<std::string, std::vector<screens::option>> choices = {});
	terminal::view grid(page& p, between_spec spec);
	terminal::view diagram(page& p, between_spec spec);
	terminal::view thread(page& p, thread_spec spec);
	terminal::view timeline(page& p, timeline_spec spec);
	terminal::view details(page& p, state<view_state> from, std::vector<screens::column> fields, std::map<std::string, std::vector<screens::option>> choices = {},
	                       std::optional<tint_spec> tint = std::nullopt, std::vector<std::string> labels = {},
	                       std::vector<std::string> files = {});
	terminal::view track(const std::optional<track_model>& model);
	// A switch's track, drawn once above it, with what's below it: the list's records
	// as the track says, each with its dot, and the pick kept for every way it's shown.
	terminal::view tracked(page& p, track_spec spec, state<view_state> view, std::string list, terminal::view below);

	// Markdown, laid out by one::markdown for the width it's given.
	terminal::view markdown_view(const std::string& source);
	// Set as a printed document: its headings centered, a break a fleuron.
	terminal::view markdown_document(const std::string& source);
	terminal::view markdown_drawn(const std::string& source, bool document);
	terminal::view markdown(page& p, state<view_state> from, std::string field, std::string empty = "");
	terminal::view pages(page& p, std::string base, std::vector<written_page> all, bool document = false);

}  // namespace one::terminal::runtime
