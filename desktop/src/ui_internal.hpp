// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a window tells the elements while it lays them out and draws them, and how it
// keeps what an element had when a view is built again.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "one/desktop/ui.hpp"

namespace one::desktop::detail {

	// The clipboard, as a field copies to it and pastes from it.
	void copy_text(std::string text);
	std::optional<std::string> pasted_text();

	// Files dragged from another program: where they are in the window they're over,
	// while they're carried; the paths a text/uri-list names; and handing those let go
	// to the file target under where they were.
	std::optional<point>& files_carried();
	std::vector<std::filesystem::path> paths_of(const std::string& uri_list);
	std::vector<std::string> elsewhere_of(const std::string& uri_list);  // the addresses that aren't files here
	void drop_files(element& root, point at, dropped what);

	// The scale and theme in use while a window builds, lays out and draws.
	double& layout_scale();
	const theme*& theme_in_use();

	// Matches a newly built tree with the one before, by key or by position and kind,
	// each element adopting what its match kept.
	void reconcile(element& now, element& before);

	// Open popovers that a press at `at` isn't on, closed, as a press elsewhere closes a
	// menu.
	void close_popovers_outside(point at);
	// What an open popover's panel has at `at`, over everything else; none when it's
	// not on one.
	element* popover_hit(point at);
	// An open dialog's panel, which the keyboard stays in while it's there; none when
	// there's none.
	element* open_dialog();

	// In tree order: the focusable elements, and the one focused.
	void focusables(element& root, std::vector<element*>& into);
	element* focused_in(element& root);

}  // namespace one::desktop::detail
