// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a screen is made of, as @uione/react's components are on the web, drawn by
// one::desktop in the site's theme. Each takes the page it's on and what the
// screen says of it, a spec of one::screens'; one::screens works out what each
// shows, and these draw it.

#pragma once

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
	desktop::view link(page& p, std::string words, std::string to, std::string icon = "");
	// A part of the page a link names, like #projects, brought to the top when it's followed.
	desktop::view anchored(page& p, std::string name, desktop::view part);
	desktop::view hero(std::string title, std::vector<desktop::view> inside);
	template <std::convertible_to<desktop::view>... V>
	desktop::view hero(std::string title, V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return hero(std::move(title), std::move(all));
	}

	desktop::view command(page& p, command_button b);
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
	desktop::view switched(page& p, std::string id, std::string label, std::vector<std::string> options, std::vector<desktop::view> ways);
	template <std::convertible_to<desktop::view>... V>
	desktop::view switched(page& p, std::string id, std::string label, std::vector<std::string> options, V&&... each) {
		std::vector<desktop::view> all;
		(all.push_back(desktop::view(std::forward<V>(each))), ...);
		return switched(p, std::move(id), std::move(label), std::move(options), std::move(all));
	}
	desktop::view steps(page& p, steps_spec spec);

	desktop::view form(page& p, form_spec spec);
	desktop::view table(page& p, table_spec spec);
	desktop::view board(page& p, board_spec spec);
	desktop::view cards(page& p, cards_spec spec);
	// What a list on the page is filtered by, with a button showing it all again;
	// nothing when it isn't.
	desktop::view filter_note(page& p);
	desktop::view find(page& p, std::string label, std::vector<find_source> sources);
	desktop::view copy(page& p, state<view_state> from, std::string label, std::vector<copied_field> fields, std::vector<copied_list> lists,
	                   std::map<std::string, std::vector<screens::option>> choices = {});
	desktop::view grid(page& p, between_spec spec);
	desktop::view diagram(page& p, between_spec spec);
	desktop::view thread(page& p, thread_spec spec);
	desktop::view timeline(page& p, timeline_spec spec);
	desktop::view details(page& p, state<view_state> from, std::vector<screens::column> fields, std::map<std::string, std::vector<screens::option>> choices = {},
	                      std::optional<tint_spec> tint = std::nullopt);

	// The cards above a list for another list's records, picking one keeping its rows.
	desktop::view track(const std::optional<track_model>& model);

}  // namespace one::desktop::runtime
