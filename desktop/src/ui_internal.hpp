// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a window tells the elements while it lays them out and draws them, and how it
// keeps what an element had when a view is built again.

#pragma once

#include <optional>
#include <string>

#include "one/desktop/ui.hpp"

namespace one::desktop::detail {

	// The clipboard, as a field copies to it and pastes from it.
	void copy_text(std::string text);
	std::optional<std::string> pasted_text();

	// The scale and theme in use while a window builds, lays out and draws.
	double& layout_scale();
	const theme*& theme_in_use();

	// Matches a newly built tree with the one before, by key or by position and kind,
	// each element adopting what its match kept.
	void reconcile(element& now, element& before);

	// In tree order: the focusable elements, and the one focused.
	void focusables(element& root, std::vector<element*>& into);
	element* focused_in(element& root);

}  // namespace one::desktop::detail

namespace one::desktop {
	// For a list under the pointer: which row it's over, or none when it's left.
	void point_at_list(element& e, std::optional<point> at);
}  // namespace one::desktop
