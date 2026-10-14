// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a screen is made of, drawn in a terminal: the same components, by the same
// names and specs, as one::desktop::runtime draws in a window. one::screens works
// out what each shows, and these draw it in cells.

#pragma once

#include <string>
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
	terminal::view link(page& p, std::string words, std::string to, std::string icon = "");
	// A part of the page a link names, like #projects, brought to the top when it's followed.
	terminal::view anchored(page& p, std::string name, terminal::view part);
	terminal::view hero(std::string title, std::vector<terminal::view> inside);
	template <std::convertible_to<terminal::view>... V>
	terminal::view hero(std::string title, V&&... each) {
		std::vector<terminal::view> all;
		(all.push_back(terminal::view(std::forward<V>(each))), ...);
		return hero(std::move(title), std::move(all));
	}

	terminal::view command(page& p, command_button b);
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
	terminal::view canvas_board(page& p, canvas_spec spec);
	terminal::view pipeline_board(page& p, pipeline_spec spec);
	terminal::view cards(page& p, cards_spec spec);
	terminal::view filter_note(page& p);
	terminal::view find(page& p, std::string label, std::vector<find_source> sources);
	// Themes made at random from a palette, tried on the whole terminal, each written
	// as a project says it, to copy.
	terminal::view themes(page& p);
	// Draws the terminal in a theme from a palette, its dark colors, by setting the
	// terminal's own: the text and what's behind it, and red, green, yellow, blue and
	// grey as danger, success, warning, the accent and quieter text. Nothing puts them
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

	// Markdown, laid out by one::markdown for the width it's given.
	terminal::view markdown_view(const std::string& source);
	terminal::view markdown(page& p, state<view_state> from, std::string field, std::string empty = "");
	terminal::view pages(page& p, std::string base, std::vector<written_page> all);

}  // namespace one::terminal::runtime
