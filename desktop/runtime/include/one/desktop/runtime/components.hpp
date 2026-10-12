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

	// A document's fields, each beside its label.
	desktop::view details(page& p, state<view_state> from, std::vector<column> fields);

}  // namespace one::desktop::runtime
