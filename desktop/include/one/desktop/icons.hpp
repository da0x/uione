// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The icons a button can be drawn as, the web's own, like edit, delete, follow,
// archive and workflow; the ways a switch shows a list: table, board, whiteboard and
// pipeline; what a picture's toolbar does: crop, copy, rotate_left, rotate_right,
// flip_horizontal, flip_vertical, raise, lower, check and close; and a drawing
// app's: paste, save, text, picture, gallery, fullscreen, sun, moon, bold, smaller,
// larger, fill, fit, stretch, center, color and edges.

#pragma once

#include <string>

#include "one/desktop/canvas.hpp"

namespace one::desktop {

	bool has_icon(const std::string& name);
	// Draws it in the middle of an area, as large as fits, in a color.
	void draw_icon(canvas& c, const std::string& name, rect area, color tint);

}  // namespace one::desktop
