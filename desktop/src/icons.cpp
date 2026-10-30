// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The icons a button or a front page's section can be drawn with, the web's own: each its SVG on a 16 by 16
// grid, its paths, rectangles and circles stroked 1.5 wide with round ends and
// joins, and filled where the web fills them, scaled to the size it's drawn at.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include "one/desktop/icons.hpp"

namespace one::desktop {

	namespace {

		struct icon_part {
			enum class kind { path, rect, circle } shape = kind::path;
			std::string d = "";                   // a path's commands
			float x = 0, y = 0, w = 0, h = 0, r = 0;  // a rectangle's, with its corners' radius, or a circle's center and radius
			bool filled = false;
			float dash = 0;  // a dashed line's dashes and gaps, each this long
		};

		using drawing = std::vector<icon_part>;

		icon_part path(std::string d, bool filled = false) { return {.d = std::move(d), .filled = filled}; }
		icon_part box(float x, float y, float w, float h, float r) { return {.shape = icon_part::kind::rect, .x = x, .y = y, .w = w, .h = h, .r = r}; }
		icon_part ring(float x, float y, float r) { return {.shape = icon_part::kind::circle, .x = x, .y = y, .r = r}; }

		const std::map<std::string, drawing>& icons() {
			static const std::map<std::string, drawing> all{
			    {"edit", {path("M11.2 2.3a1.4 1.4 0 0 1 2 2L5.6 11.9 2.8 12.7l.8-2.8z"), path("M10 3.5l2 2")}},
			    {"add", {path("M8 3v10M3 8h10")}},
			    {"follow", {path("M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z"), path("M6.6 14a1.5 1.5 0 0 0 2.8 0")}},
			    {"following", {path("M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z", true), path("M6.6 14a1.5 1.5 0 0 0 2.8 0")}},
			    // A cog: a ring with teeth, and its hole.
			    {"settings",
			     {path("M6.9 1.5h2.2l.4 1.8 1.2.7 1.8-.6 1.1 1.9-1.4 1.3v1.4l1.4 1.3-1.1 1.9-1.8-.6-1.2.7-.4 1.8H6.9l-.4-1.8-1.2-.7-1.8.6-1.1-1.9 1.4-1.3V7.3L2.4 "
			           "6l1.1-1.9 1.8.6 1.2-.7z"),
			      ring(8, 8, 2)}},
			    {"workflow", {box(1.5f, 2, 4.5f, 4, 1), box(10, 10, 4.5f, 4, 1), path("M6 4h2.5a1.5 1.5 0 0 1 1.5 1.5V10")}},
			    // A bin with its lid.
			    {"delete", {path("M2.5 4.5h11M6.5 4.5V3a1 1 0 0 1 1-1h1a1 1 0 0 1 1 1v1.5"), path("M3.8 4.5l.7 8.6a1 1 0 0 0 1 .9h5a1 1 0 0 0 1-.9l.7-8.6M6.7 7v4.5M9.3 7v4.5")}},
			    // A box with its lid, what's put away.
			    {"archive", {box(1.5f, 2.5f, 13, 3, 1), path("M2.8 5.5v7a1 1 0 0 0 1 1h8.4a1 1 0 0 0 1-1v-7M6.5 8.5h3")}},
			    // A robot, for what programs do, like a project's agents.
			    {"robot", {box(3, 5, 10, 8, 2), path("M8 5V3M6 9h.01M10 9h.01M6.5 11h3M1.5 8.5v2M14.5 8.5v2")}},
			    // A building with its windows and door, what's built and run.
			    {"building", {box(3, 1.5f, 10, 13, 1), path("M6 4.5h1M9 4.5h1M6 7.5h1M9 7.5h1M7 14.5v-3h2v3")}},
			    // Three disks stacked, what's stored.
			    {"database", {box(2.5f, 2, 11, 3.5f, 1.75f), box(2.5f, 6.25f, 11, 3.5f, 1.75f), box(2.5f, 10.5f, 11, 3.5f, 1.75f)}},
			    // Angle brackets around a slash, what's written.
			    {"code", {path("M5.5 4.5L2 8l3.5 3.5M10.5 4.5L14 8l-3.5 3.5M9.25 3l-2.5 10")}},
			    // Two rings joined, what's connected.
			    {"connect", {ring(4, 8, 2.5f), ring(12, 8, 2.5f), path("M6.5 8h3")}},
			    // A shield with a check, what's kept safe.
			    {"shield", {path("M8 1.5l5.5 2v4.5L8 14.5 2.5 8V3.5z"), path("M5.5 8l2 2 3-3.5")}},
			    // A cloud, what runs in it.
			    {"cloud", {path("M4.5 12.5a3 3 0 0 1 0-6a4 4 0 0 1 7.5-1a3.5 3.5 0 0 1 0 7z")}},
			    // Bars over a line, what's measured.
			    {"chart", {path("M2.5 13.5h11M4.5 11V8M8 11V4M11.5 11V6.5")}},
			    // A four-pointed star, what's clever or new.
			    {"spark", {path("M8 1.5l1.5 5 5 1.5-5 1.5L8 14.5 6.5 9.5l-5-1.5 5-1.5z")}},
			    // Two people, the ones it's for.
			    {"people", {ring(6, 5.5f, 2.25f), path("M1.5 13.5a4.5 4.5 0 0 1 9 0"), ring(11.5f, 6, 1.75f), path("M11 9.5a3.5 3.5 0 0 1 3.5 4")}},
			    // A padlock, what's closed to others.
			    {"lock", {box(3, 7, 10, 7, 1.5f), path("M5.5 7V5a2.5 2.5 0 0 1 5 0v2")}},
			    // A globe, what reaches everywhere.
			    {"globe", {ring(8, 8, 6.25f), path("M1.75 8h12.5M8 1.75a9 9 0 0 1 0 12.5a9 9 0 0 1 0-12.5")}},
			    // Layers stacked, what's built on what.
			    {"layers", {path("M8 2l6 3-6 3-6-3z"), path("M2 8l6 3 6-3M2 11l6 3 6-3")}},
			    // A month's page, its rings above; a bell that rings; a tray things come into;
			    // and the ways back and on.
			    {"calendar", {box(2, 3, 12, 11, 1.5f), path("M2 6.5h12M5.5 1.5v3M10.5 1.5v3")}},
			    {"bell", {path("M4 11V7a4 4 0 0 1 8 0v4l1.25 1.5H2.75L4 11zM6.5 14a1.5 1.5 0 0 0 3 0")}},
			    {"inbox", {path("M2 9.5l2-6.5h8l2 6.5v3.5a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1zM2 9.5h3.5l1 1.5h3l1-1.5H14")}},
			    {"previous", {path("M10 3L5 8l5 5")}},
			    {"next", {path("M6 3l5 5-5 5")}},
			    // About it: an i in a ring.
			    {"info", {ring(8, 8, 6.5f), path("M8 7.25V11.5M8 4.75v.5")}},
			    // Play: a triangle pointing on.
			    {"play", {path("M5 3.5v9l7-4.5z")}},
			    // A timeline drawn as a ruler: its edge, a mark for each day as long as how many,
			    // and where it's scrolled to.
			    {"timeline", {path("M13.5 1.5v13M13.5 3.5h-3M13.5 6h-5M13.5 10.5h-2M13.5 13h-4"), path("M2.5 8.25h11")}},
			    // A table of contents: headings, and what's under them set in.
			    {"contents", {path("M2.5 3.5h11M5 6.5h8.5M5 9.5h8.5M2.5 12.5h11")}},
			    // What a picture's toolbar does to it: crop or copy it, turn or flip it, raise
			    // or lower it among others, and keep what was done, or don't.
			    {"crop", {path("M4.5 1.5v10h10M1.5 4.5h10v10")}},
			    {"copy", {box(5.5f, 5.5f, 9, 9, 1.5f), path("M10.5 5.5V3a1 1 0 0 0-1-1H3a1 1 0 0 0-1 1v6.5a1 1 0 0 0 1 1h2.5")}},
			    {"raise", {path("M8 14V5.5M4.5 9L8 5.5 11.5 9M3 2.5h10")}},
			    {"lower", {path("M8 2v8.5M4.5 7L8 10.5 11.5 7M3 13.5h10")}},
			    // A circle drawn in short strokes, its arrow at the end it turns toward.
			    {"rotate_left", {path("M3.5 5.9L2.98 7.15 2.8 8.5 2.98 9.85 3.5 11.1 4.32 12.18 5.4 13 6.65 13.52 8 13.7 9.35 13.52 10.6 13 11.68 12.18 12.5 11.1 13.02 9.85 13.2 8.5 13.02 7.15 12.5 5.9 11.68 4.82 10.6 4 9.35 3.48 8 3.3 6.65 3.48 5.4 4M9.1 4.4L5.4 4 6.9 .59")}},
			    {"rotate_right", {path("M12.5 5.9L13.02 7.15 13.2 8.5 13.02 9.85 12.5 11.1 11.68 12.18 10.6 13 9.35 13.52 8 13.7 6.65 13.52 5.4 13 4.32 12.18 3.5 11.1 2.98 9.85 2.8 8.5 2.98 7.15 3.5 5.9 4.32 4.82 5.4 4 6.65 3.48 8 3.3 9.35 3.48 10.6 4M6.9 4.4L10.6 4 9.1 .59")}},
			    {"flip_horizontal", {path("M8 1.5v13"), path("M6 4L2.5 12H6z"), path("M10 4l3.5 8H10z")}},
			    {"flip_vertical", {path("M1.5 8h13"), path("M4 6l8-3.5V6z"), path("M4 10l8 3.5V10z")}},
			    {"check", {path("M3 8.5l3.2 3.2L13 4.8")}},
			    {"close", {path("M4 4l8 8M12 4l-8 8")}},
			    // What a drawing app's toolbar does: paste, save, add words or a picture, see
			    // every piece of work, fill the screen, and switch light and dark.
			    {"paste", {box(3.5f, 3, 9, 11.5f, 1.5f), box(6, 1.5f, 4, 3, 0.75f), path("M6 8h4M6 10.5h4")}},
			    {"save", {path("M8 2v8M4.5 6.5L8 10l3.5-3.5"), path("M2.5 10.5V14h11v-3.5")}},
			    {"export", {path("M8 10V2M4.5 5.5L8 2l3.5 3.5"), path("M2.5 10.5V14h11v-3.5")}},
			    // Back a step and forward again: an arrow turning back on itself, in short strokes.
			    {"undo", {path("M3 6L9.5 6 10.41 6.12 11.25 6.47 11.97 7.03 12.53 7.75 12.88 8.59 13 9.5 12.88 10.41 12.53 11.25 11.97 11.97 11.25 12.53 10.41 12.88 9.5 13 6 13M6 3L3 6 6 9")}},
			    {"redo", {path("M13 6L6.5 6 5.59 6.12 4.75 6.47 4.03 7.03 3.47 7.75 3.12 8.59 3 9.5 3.12 10.41 3.47 11.25 4.03 11.97 4.75 12.53 5.59 12.88 6.5 13 10 13M10 3L13 6 10 9")}},
			    {"text", {path("M3 3.5h10M8 3.5V13M6 13h4")}},
			    {"screenshot", {box(1.5f, 4.5f, 13, 9.5f, 1.5f), ring(8, 9.25f, 2.6f), path("M5.5 4.5L6.75 2.5h2.5l1.25 2")}},
			    {"picture", {box(2, 3, 12, 10, 1.5f), ring(5.5f, 6.5f, 1.2f), path("M2.5 12l3.5-3.5 2.5 2.5 2-2 3 3")}},
			    // A folder: its tab at the top left.
			    {"folder", {path("M2 4.5a1 1 0 0 1 1-1h3.2l1.5 1.8H13a1 1 0 0 1 1 1V12a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1z")}},
			    {"gallery", {box(2, 2, 5, 5, 1), box(9, 2, 5, 5, 1), box(2, 9, 5, 5, 1), box(9, 9, 5, 5, 1)}},
			    {"fullscreen", {path("M2 6V2h4M10 2h4v4M14 10v4h-4M6 14H2v-4")}},
			    {"sun", {ring(8, 8, 3), path("M8 1v1.5M8 13.5V15M1 8h1.5M13.5 8H15M3.05 3.05l1.06 1.06M11.89 11.89l1.06 1.06M3.05 12.95l1.06-1.06M11.89 4.11l1.06-1.06")}},
			    {"moon", {path("M13.5 9.5A5.5 5.5 0 1 1 6.5 2.5a4.5 4.5 0 0 0 7 7z")}},
			    // Reading: an eye, read rather than written; and two pages side by side.
			    {"eye", {path("M1.5 8A9 9 0 0 1 14.5 8 9 9 0 0 1 1.5 8zM10 8A2 2 0 1 1 6 8 2 2 0 1 1 10 8")}},
			    // Closed, to be opened, and open: what a list that opens to more shows.
			    {"chevron_right", {path("M6 4l4 4-4 4")}},
			    {"chevron_down", {path("M4 6l4 4 4-4")}},
			    // And not seen, as a layer hidden is: the eye struck through.
			    {"eye_off", {path("M1.5 8A9 9 0 0 1 14.5 8 9 9 0 0 1 1.5 8zM10 8A2 2 0 1 1 6 8 2 2 0 1 1 10 8M2.5 2.5l11 11")}},
			    // Objects held as one, in a frame, and set apart again.
			    {"group", {path("M1.5 1.5h13v13h-13zM4 4h5v5H4zM7 7h5v5H7z")}},
			    {"ungroup", {path("M2 2.5h6v6H2zM8 7.5h6v6H8z")}},
			    // How see-through: a circle, half of it shaded.
			    // Cut away: a pair of scissors.
			    {"cut", {path("M5.5 4.5A2 2 0 1 1 1.5 4.5 2 2 0 1 1 5.5 4.5zM5.5 11.5A2 2 0 1 1 1.5 11.5 2 2 0 1 1 5.5 11.5zM5.2 5.6l9.3 6.4M5.2 10.4l9.3-6.4")}},
			    // A star of five points, and a polygon of five corners, as tools draw them.
			    {"star", {path("M8.0 1.9 L9.72 6.03 L14.18 6.39 L10.78 9.3 L11.82 13.66 L8.0 11.33 L4.18 13.66 L5.22 9.3 L1.82 6.39 L6.28 6.03z")}},
			    {"polygon", {path("M8.0 1.9 L14.18 6.39 L11.82 13.66 L4.18 13.66 L1.82 6.39z")}},
			    // Looked at nearer or further: a glass with a plus or a minus.
			    {"zoom_in", {path("M11.5 7A4.5 4.5 0 1 1 2.5 7 4.5 4.5 0 1 1 11.5 7zM10.2 10.2l3.8 3.8M5 7h4M7 5v4")}},
			    {"zoom_fit", {path("M11.5 7A4.5 4.5 0 1 1 2.5 7 4.5 4.5 0 1 1 11.5 7zM10.2 10.2l3.8 3.8M5 5.5h4v3H5z")}},
			    {"zoom_out", {path("M11.5 7A4.5 4.5 0 1 1 2.5 7 4.5 4.5 0 1 1 11.5 7zM10.2 10.2l3.8 3.8M5 7h4")}},
			    // Any color: a ring of every hue, drawn in its colors, whatever it's tinted.
			    {"spectrum", {path("M14 8A6 6 0 1 1 2 8 6 6 0 1 1 14 8z")}},
			    {"opacity", {path("M14 8A6 6 0 1 1 2 8 6 6 0 1 1 14 8zM8 2v12M8 6l4.5-2.6M8 9.5l5.6-3.2M8 13l4.4-2.5")}},
			    {"spread", {path("M1.5 3.5h5.5v9h-5.5zM9 3.5h5.5v9H9z")}},
			    // A warm reading mode: a cup of coffee.
			    {"coffee", {path("M2.5 6.5h8.5v3.5a3.5 3.5 0 0 1-3.5 3.5H6A3.5 3.5 0 0 1 2.5 10zM11 7.5h1.25a1.75 1.75 0 0 1 0 3.5H11M5 2.5v2M8.5 2.5v2")}},
			    // Words made bold, smaller or larger.
			    {"bold", {path("M4.5 2.5h4.25a2.75 2.75 0 0 1 0 5.5H4.5zM4.5 8h5a3 3 0 0 1 0 6h-5z")}},
			    {"smaller", {path("M1.5 13l3.5-9 3.5 9M2.9 10h4.2M10.5 8.5h4")}},
			    {"larger", {path("M1.5 13l3.5-9 3.5 9M2.9 10h4.2M10.5 8.5h4M12.5 6.5v4")}},
			    // How a picture sits in a frame: filling it, fitting in it, stretched to it, or
			    // at its own size in the middle; and what fills around it: a color, or its
			    // edges drawn out.
			    {"fill", {box(1.5f, 3, 13, 10, 1), path("M3 4.5h10v7H3z", true)}},
			    {"fit", {box(1.5f, 3, 13, 10, 1), path("M5.5 4.5h5v7h-5z", true)}},
			    {"stretch", {box(1.5f, 3, 13, 10, 1), path("M4 8h8M6 6L4 8l2 2M10 6l2 2-2 2")}},
			    {"center", {box(1.5f, 3, 13, 10, 1), path("M6 6.5h4v3H6z", true)}},
			    {"color", {path("M8 1L11.96 7.65A4.6 4.6 0 1 1 4.04 7.65z")}},
			    {"edges", {box(5, 5, 6, 6, 1), path("M1.5 8H5M11 8h3.5M8 1.5V5M8 11v3.5")}},
			    {"clear", {box(5, 5, 6, 6, 1), path("M1.5 4V1.5H4M12 1.5h2.5V4M14.5 12v2.5H12M4 14.5H1.5V12")}},
			    {"square", {path("M3 13V3h10")}},
			    // A drawing app's tools: pointing and picking, and drawing a rectangle, an
			    // ellipse or a line.
			    {"pointer", {path("M4 2.5v10.5l2.8-2.8 1.9 4.3 1.8-.8-1.9-4.3H13z")}},
			    {"rectangle", {path("M2.5 4h11v8h-11z")}},
			    {"ellipse", {path("M14 8A6 4.5 0 1 1 2 8 6 4.5 0 1 1 14 8z")}},
			    {"line", {path("M3 13L13 3")}},
			    {"italic", {path("M7 3h5M4 13h5M10 3L6 13")}},
			    // The faces a page is set in, for the writing it is: a book, plain modern
			    // letters, a lyric's quill, and a typewriter.
			    {"book", {path("M8 4.5A9 9 0 0 0 1.5 3v9.5A9 9 0 0 1 8 14 9 9 0 0 1 14.5 12.5V3A9 9 0 0 0 8 4.5zM8 4.5V14")}},
			    {"letters", {path("M1.5 13L5 3.5 8.5 13M2.8 9.8h4.4M14.5 8.5V13M14.5 11A2.1 2.1 0 1 1 10.3 11 2.1 2.1 0 1 1 14.5 11")}},
			    {"quill", {path("M2.5 13.5L13.5 2.5M13.5 2.5A11 11 0 0 0 5.5 10.5M13.5 2.5A11 11 0 0 1 5.5 10.5")}},
			    {"typewriter", {path("M4.5 7V2.5h7V7M1.5 7h13v6.5h-13zM4 9.5h1M7.5 9.5h1M11 9.5h1M5 11.5h6")}},
			    // What a page's toolbar does to the block the caret is in: its alignment, its
			    // kind, and an equation set in it.
			    {"align_left", {path("M2.5 4h11M2.5 7h7M2.5 10h11M2.5 13h7")}},
			    {"align_center", {path("M2.5 4h11M4.5 7h7M2.5 10h11M4.5 13h7")}},
			    {"align_right", {path("M2.5 4h11M6.5 7h7M2.5 10h11M6.5 13h7")}},
			    {"title", {path("M3 2.5h10M3 4.5h10M8 4.5v9")}},
			    {"heading", {path("M4 3v10M12 3v10M4 8h8")}},
			    {"quote", {path("M3.5 3.5v9M6.5 5h7M6.5 8h7M6.5 11h5")}},
			    {"equation", {path("M2.5 4.5h4M4.5 2.5v4M9.5 4.5h4M2.5 11.5l3-3M2.5 8.5l3 3M9.5 10h4M9.5 12.5h4")}},
			    // A wand and the sparks off its tip.
			    {"magic", {path("M2.5 13.5l8-8M12 1.5v2M12 7.5v2M8 5.5h2M14 5.5h2")}},
			    {"rounded", {path("M3 13V8a5 5 0 0 1 5-5h5")}},
			    {"blend", {ring(5.5f, 8, 4), ring(10.5f, 8, 4)}},
			    {"harmonize", {ring(8, 5.5f, 3.5f), ring(5.5f, 10, 3.5f), ring(10.5f, 10, 3.5f)}},
			    // The ways a switch shows a list.
			    {"pipeline", {ring(3, 5, 1.5f), ring(8, 5, 1.5f), ring(13, 5, 1.5f), path("M4.5 5h2M9.5 5h2M2 11h12")}},
			    {"whiteboard", {box(2, 2.5f, 12, 11, 1.5f), icon_part{.d = "M8 2.5v11", .dash = 1.5f}, box(3.5f, 4.5f, 3, 3, 0.5f), box(9.5f, 8, 3, 3, 0.5f)}},
			    {"table", {box(2, 2.5f, 12, 11, 1.5f), path("M2 6h12M2 9.5h12")}},
			    {"board", {box(2, 2.5f, 3.5f, 11, 1), box(6.25f, 2.5f, 3.5f, 7, 1), box(10.5f, 2.5f, 3.5f, 9, 1)}},
			};
			return all;
		}

		// A path's outlines, each a run of points, its arcs made of short lines.
		std::vector<std::vector<point>> outlines(const std::string& d) {
			std::vector<std::vector<point>> out;
			std::size_t at = 0;
			auto skip = [&] {
				while (at < d.size() && (std::isspace(static_cast<unsigned char>(d[at])) || d[at] == ',')) ++at;
			};
			auto number = [&] {
				skip();
				const char* start = d.c_str() + at;
				char* end = nullptr;
				float v = std::strtof(start, &end);
				at += static_cast<std::size_t>(end - start);
				return v;
			};
			point now{}, began{};
			char command = 0;
			auto to = [&](point p) {
				if (out.empty()) out.push_back({now});
				out.back().push_back(p);
				now = p;
			};
			// An arc of a circle from now to the end, as SVG says one: by its radius, which
			// of the two ways round, and which side.
			auto arc = [&](float radius, bool large, bool sweep, point end) {
				float dx = (now.x - end.x) / 2, dy = (now.y - end.y) / 2;
				float half = std::sqrt(dx * dx + dy * dy);
				if (half < 1e-4f) return;
				radius = std::max(radius, half);
				float across = std::sqrt(std::max(0.f, radius * radius - half * half)) / half;
				if (large == sweep) across = -across;
				point middle{(now.x + end.x) / 2, (now.y + end.y) / 2};
				point center{middle.x + across * dy, middle.y - across * dx};
				float from = std::atan2(now.y - center.y, now.x - center.x), until = std::atan2(end.y - center.y, end.x - center.x);
				float turn = until - from;
				if (sweep && turn < 0) turn += 2 * std::numbers::pi_v<float>;
				if (!sweep && turn > 0) turn -= 2 * std::numbers::pi_v<float>;
				int steps = std::max(4, static_cast<int>(std::abs(turn) / (std::numbers::pi_v<float> / 12)));
				for (int i = 1; i <= steps; ++i) {
					float a = from + turn * static_cast<float>(i) / static_cast<float>(steps);
					to(i == steps ? end : point{center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
				}
			};
			while (true) {
				skip();
				if (at >= d.size()) break;
				if (std::isalpha(static_cast<unsigned char>(d[at]))) command = d[at++];
				bool relative = std::islower(static_cast<unsigned char>(command));
				point base = relative ? now : point{};
				switch (std::tolower(static_cast<unsigned char>(command))) {
					case 'm': {
						float x = number(), y = number();
						now = began = {base.x + x, base.y + y};
						out.push_back({now});
						command = relative ? 'l' : 'L';  // what follows a move is drawn to
						break;
					}
					case 'l': {
						float x = number(), y = number();
						to({base.x + x, base.y + y});
						break;
					}
					case 'h': to({(relative ? now.x : 0) + number(), now.y}); break;
					case 'v': to({now.x, (relative ? now.y : 0) + number()}); break;
					case 'a': {
						float rx = number();
						(void)number();  // ry, the same for a circle's
						(void)number();  // its turn, none for a circle
						bool large = number() != 0, sweep = number() != 0;
						float x = number(), y = number();
						arc(rx, large, sweep, {base.x + x, base.y + y});
						break;
					}
					case 'z':
						to(began);
						break;
					default: return out;  // what an icon here never says
				}
			}
			return out;
		}

	}  // namespace

	bool has_icon(const std::string& name) { return icons().contains(name); }

	namespace {
		// A ring of every hue, round the way the clock goes from red at its top, made
		// once, larger than it's drawn so it stays smooth.
		const picture& spectrum() {
			static const picture made = [] {
				constexpr int side = 96;
				std::vector<std::uint8_t> rgba(side * side * 4, 0);
				for (int y = 0; y < side; ++y) {
					for (int x = 0; x < side; ++x) {
						double dx = x + 0.5 - side / 2.0, dy = y + 0.5 - side / 2.0, r = std::hypot(dx, dy);
						double outer = side * 6.5 / 16, inner = side * 3.5 / 16;
						double coverage = std::clamp(std::min(outer - r, r - inner) + 0.5, 0.0, 1.0);
						if (coverage <= 0) continue;
						double h = std::fmod(std::atan2(dx, -dy) * 180 / std::numbers::pi + 360, 360), f = h / 60 - std::floor(h / 60);
						double rgb[3];
						switch (static_cast<int>(h / 60)) {
							case 0: rgb[0] = 1, rgb[1] = f, rgb[2] = 0; break;
							case 1: rgb[0] = 1 - f, rgb[1] = 1, rgb[2] = 0; break;
							case 2: rgb[0] = 0, rgb[1] = 1, rgb[2] = f; break;
							case 3: rgb[0] = 0, rgb[1] = 1 - f, rgb[2] = 1; break;
							case 4: rgb[0] = f, rgb[1] = 0, rgb[2] = 1; break;
							default: rgb[0] = 1, rgb[1] = 0, rgb[2] = 1 - f; break;
						}
						std::size_t at = (static_cast<std::size_t>(y) * side + x) * 4;
						for (int i = 0; i < 3; ++i) rgba[at + i] = static_cast<std::uint8_t>(std::lround(rgb[i] * 255));
						rgba[at + 3] = static_cast<std::uint8_t>(std::lround(coverage * 255));
					}
				}
				return picture::of(side, side, std::move(rgba));
			}();
			return made;
		}
	}  // namespace

	void draw_icon(canvas& c, const std::string& name, rect area, color tint) {
		auto found = icons().find(name);
		if (found == icons().end()) return;
		if (name == "spectrum") {
			float side = std::min(area.width, area.height);
			c.picture(spectrum(), {area.x + (area.width - side) / 2, area.y + (area.height - side) / 2, side, side});
			return;
		}
		float s = std::min(area.width, area.height) / 16;
		point origin{area.x + (area.width - 16 * s) / 2, area.y + (area.height - 16 * s) / 2};
		auto at = [&](point p) { return point{origin.x + p.x * s, origin.y + p.y * s}; };
		// Drawn in a fine line, well under the usual 1.5, so they don't feel heavy, and
		// never more than eight tenths of a pixel, so a large tool's is as fine as a small
		// one's.
		const float weight = 0.6f;
		float stroke = std::min(weight * s, 0.8f);
		for (const auto& part : found->second) {
			switch (part.shape) {
				case icon_part::kind::rect: {
					// A stroke on the edge, half in and half out, as SVG draws one.
					rect r{origin.x + (part.x - weight / 2) * s, origin.y + (part.y - weight / 2) * s, (part.w + weight) * s, (part.h + weight) * s};
					c.box(r, {.border = tint, .border_width = stroke, .radius = (part.r + weight / 2) * s});
					break;
				}
				case icon_part::kind::circle: {
					float r = (part.r + weight / 2) * s;
					point center = at({part.x, part.y});
					c.box({center.x - r, center.y - r, 2 * r, 2 * r}, {.border = tint, .border_width = stroke, .radius = r});
					break;
				}
				case icon_part::kind::path:
					for (const auto& outline : outlines(part.d)) {
						std::vector<point> placed;
						for (const auto& p : outline) placed.push_back(at(p));
						if (part.filled) c.fill(placed, tint);
						for (std::size_t i = 0; i + 1 < placed.size(); ++i) {
							point a = placed[i], b = placed[i + 1];
							if (part.dash <= 0) {
								c.line(a, b, stroke, tint);
								continue;
							}
							float length = std::hypot(b.x - a.x, b.y - a.y), dash = part.dash * s;
							for (float from = 0; from < length; from += 2 * dash) {
								float until = std::min(length, from + dash);
								c.line({a.x + (b.x - a.x) * from / length, a.y + (b.y - a.y) * from / length},
								       {a.x + (b.x - a.x) * until / length, a.y + (b.y - a.y) * until / length}, stroke, tint);
							}
						}
					}
					break;
			}
		}
	}

}  // namespace one::desktop
