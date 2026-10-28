// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Views: what a window shows, said as C++ values and built again whenever a state they
// read changes:
//
//     window.show([=] {
//         return one::desktop::column(
//             one::desktop::text(page->title, theme.heading()),
//             one::desktop::scroll(one::desktop::list(page->issues, &issue::id, [](const issue& i) {
//                 return one::desktop::row(one::desktop::edge(urgency(i)), one::desktop::text(i.title)).spacing(8);
//             }, [](const std::string& id) { open(id); }))
//         ).spacing(12).padding(24);
//     });
//
// What's built is matched with what was there before, by position and kind, or by
// key where one is given, so a list's chosen row, a scroll's offset and focus stay
// where they were when the view changes underneath them.

#pragma once

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "one/desktop/canvas.hpp"
#include "one/desktop/input.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/text.hpp"
#include "one/desktop/theme.hpp"

namespace one::desktop {

	// Space inside a view's edge.
	struct edges {
		float top = 0, right = 0, bottom = 0, left = 0;
		edges() = default;
		edges(float all) : top(all), right(all), bottom(all), left(all) {}
		edges(float vertical, float horizontal) : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
		edges(float t, float r, float b, float l) : top(t), right(r), bottom(b), left(l) {}
	};

	// A size in logical pixels, fractional.
	struct extent {
		float width = 0, height = 0;
	};

	enum class align { start, center, end, stretch };

	enum class cursor { normal, pointer, text, grab, grabbing, resize_across, resize_up_down, move, resize_falling, resize_rising, crosshair };

	// What an element typed into tells an input method, the way a language like
	// Japanese is typed: the text around the caret and where the caret is; and what
	// it's told, text being composed, shown but not yet typed, and text typed.
	class text_entry {
	public:
		virtual ~text_entry() = default;
		// The text, and the caret and the selection's other end in it, in bytes.
		virtual const std::string& surrounding(std::size_t& caret, std::size_t& anchor) const = 0;
		virtual bool multiline() const = 0;
		virtual rect caret_area() = 0;  // in the window
		virtual void compose(std::string text, int caret) = 0;  // the caret in it, or -1 for none
		// Erases bytes before and after the caret, then types `text` there.
		virtual void commit(std::string text, std::size_t erase_before, std::size_t erase_after) = 0;
	};

	// An element of a built view. Most programs use the functions below instead of
	// these; a new kind of element derives from it.
	class element {
	public:
		virtual ~element() = default;

		// What every element can be given.
		std::string key;
		edges padding;
		std::optional<float> fixed_width, fixed_height;
		float grow = 0;         // a share of what's left in a row or column
		float spacing = 0;      // between children, in rows and columns
		align cross = align::stretch;  // children across a row or column
		bool fills_across = false;     // takes a row's whole height, or a column's width, however its siblings align
		bool wants_focus = false;      // has the keyboard when the view is first shown
		box_style background;
		std::optional<float> fixed_baseline;  // where its line sits, down from its top, for one that isn't text
		std::function<void()> on_click;
		std::function<bool(const key_event&)> on_key;

		std::vector<std::unique_ptr<element>> children;
		element* parent = nullptr;
		rect frame;  // where it was placed, in the window

		// Layout: the size wanted within a width, then the place given.
		extent measure(float width);
		// Where its first line sits, down from its top, within a width, when it has one;
		// a flow lines its children up on it, as words are.
		std::optional<float> baseline(float width);
		void place(const rect& area);
		void paint(canvas& c);

		// Input, each saying whether it was handled.
		virtual bool handle_key(const key_event& e);
		virtual bool click(point at);
		// The button pressed on it, once, or twice or three times quickly; when it's
		// taken, the pointer's moves while it's held are told to drag.
		virtual bool press(point, int) { return false; }
		virtual void drag(point) {}
		// The button let go after a press it took, with what's under the pointer then.
		virtual void release(point, element*) {}
		virtual bool wheel(point at, float dx, float dy);
		virtual element* hit(point at);
		virtual bool focusable() const { return false; }
		// Whether a press gives it the keyboard; a toolbar's button acts on what's being
		// worked on and leaves the keyboard with it, though Tab still reaches it.
		virtual bool focused_by_press() const { return true; }
		virtual bool prefers_focus() const { return false; }  // given the keyboard first, as a list is
		virtual text_entry* entry() { return nullptr; }       // typed into, as a field is
		virtual one::desktop::cursor cursor() const { return on_click ? cursor::pointer : cursor::normal; }

		// Takes over what a view kept from the element it replaces.
		virtual void adopt(element& before);

		bool focused = false;
		bool focused_by_default = false;  // given focus because nothing better took keys yet
		bool focused_by_pointer = false;  // given focus by a click, rather than moved to with keys
		// Whether its focus ring shows: only once the keyboard moved to it, as the web's
		// :focus-visible, not after a click or where the window put the keyboard first.
		bool shows_focus() const { return focused && !focused_by_default && !focused_by_pointer; }
		bool carried = false;             // took over from an element of the view before, rather than new
		bool hovered = false;

		// Brings an area of this element into view, through any scroll above it.
		void reveal(const rect& area);
		// Brought to the top of what scrolls it, once, when it's first placed, as a
		// section is when a link to it is followed.
		bool to_top = false, brought = false;
		void bring_to_top();

	protected:
		virtual extent measure_content(float width);
		virtual std::optional<float> baseline_content(float) { return fixed_baseline; }
		virtual void place_content(const rect& inner);
		virtual void paint_content(canvas& c);
		virtual bool reveal_here(const rect&) { return false; }
		virtual bool top_here(const rect&) { return false; }
		rect inner() const;
	};

	// A view as it's being said: one element, given things in turn.
	class view {
	public:
		view() = default;
		template <std::derived_from<element> E>
		view(std::unique_ptr<E> e) : element_(std::move(e)) {}

		view&& padding(edges e) && { element_->padding = e; return std::move(*this); }
		view&& width(float w) && { element_->fixed_width = w; return std::move(*this); }
		view&& height(float h) && { element_->fixed_height = h; return std::move(*this); }
		view&& grow(float share = 1) && { element_->grow = share; return std::move(*this); }
		view&& spacing(float s) && { element_->spacing = s; return std::move(*this); }
		view&& align(one::desktop::align a) && { element_->cross = a; return std::move(*this); }
		view&& background(box_style s) && { element_->background = s; return std::move(*this); }
		view&& baseline(float b) && { element_->fixed_baseline = b; return std::move(*this); }
		view&& key(std::string k) && { element_->key = std::move(k); return std::move(*this); }
		view&& on_click(std::function<void()> f) && { element_->on_click = std::move(f); return std::move(*this); }
		view&& on_key(std::function<bool(const key_event&)> f) && { element_->on_key = std::move(f); return std::move(*this); }
		view&& autofocus() && { element_->wants_focus = true; return std::move(*this); }
		view&& brought_to_top() && { element_->to_top = true; return std::move(*this); }

		std::unique_ptr<element> take() && { return std::move(element_); }
		element* get() const { return element_.get(); }

	private:
		std::unique_ptr<element> element_;
	};

	// Dragging something to another place, like a card to a column: what's carried
	// while a drag is on, and where the pointer is, for what it's carried over to show.
	struct carried {
		std::string payload;
		point at;
	};
	const std::optional<carried>& carrying();
	// What can be dragged: pressed and moved more than a few pixels, it's carried; a
	// press that hardly moves stays a click on what's inside.
	view drag_source(view content, std::string payload);
	// Where it can be dropped: outlined while something it accepts is carried over it,
	// dimmed while something it doesn't is carried at all; told what's dropped on it.
	view drop_target(view content, std::function<bool(const std::string&)> accepts, std::function<void(const std::string&)> on_drop, box_style over, color dim);
	// What's let go over a file target from another program: files on this machine, as
	// a file manager drags them; a picture's own bytes, as a browser drags one; and
	// addresses on the web, which aren't fetched, so a person can be told why.
	struct dropped {
		std::vector<std::filesystem::path> files;
		std::vector<std::uint8_t> picture;
		std::vector<std::string> elsewhere;
	};
	// Where files or a picture from another program can be let go: outlined while
	// they're carried over it, and told what was let go and where in the window.
	view file_target(view content, std::function<void(dropped what, point at)> on_drop, box_style over);

	// A surface of things placed anywhere, in its own units, panned and zoomed: a thing
	// dragged moves, dragged by its lower right corner resizes, and on_place is told
	// where it's gone once it's let go, snapped to the grid when there is one. A double
	// click, or Enter on the one chosen, opens it.
	struct surface_item {
		std::string key;
		float x = 0, y = 0, width = 150, height = 150;
		view content;
		bool movable = true;
	};
	// A band down a surface, like a board's column: things let go in it belong to it, and
	// the line after it is dragged to make it wider or narrower.
	struct surface_region {
		std::string key, title;
		float x = 0, width = 320;
	};
	struct surface_options {
		std::function<void(const std::string& key, float x, float y, float width, float height)> on_place = {};
		std::vector<surface_region> regions = {};
		std::function<void(const std::string& key, float width)> on_resize = {};
		std::function<void(const std::string& key)> on_open = {};
		float grid = 0, origin_x = 0, origin_y = 0;
		float smallest = 30;  // a thing's least width and height
		float height = 560;   // how tall the surface is drawn
	};
	view surface(std::vector<surface_item> items, surface_options options);

	// Text, wrapped to the width it's given.
	view text(std::string words, std::optional<text_style> style = {});
	view text(std::vector<text_span> spans);

	// Children one under another, or side by side.
	view column(std::vector<view> children);
	view row(std::vector<view> children);
	template <std::convertible_to<view>... V>
	view column(V&&... children) {
		std::vector<view> all;
		(all.push_back(view(std::forward<V>(children))), ...);
		return column(std::move(all));
	}
	template <std::convertible_to<view>... V>
	view row(V&&... children) {
		std::vector<view> all;
		(all.push_back(view(std::forward<V>(children))), ...);
		return row(std::move(all));
	}

	// One view scrolled within the space given, down, or across.
	// Children left to right, going on to a new line when the next doesn't fit, like
	// buttons in a narrow column.
	view flow(std::vector<view> children);
	// The scale text is laid out at in the window being built, measured or drawn, for an
	// element that lays out text of its own.
	double text_scale();
	view scroll(view content);
	view scroll_across(view content);

	// Rows, one of them chosen: moved with the arrows, j and k, Home, End and the page
	// keys, opened with Enter or a click. Each row has a key, so the chosen one stays
	// chosen as rows come and go.
	struct list_row {
		std::string key;
		view shown;
	};
	// `start` is the row chosen when it's first shown, rather than the first.
	view list(std::vector<list_row> rows, std::function<void(const std::string&)> on_open = {}, std::string start = "");
	// With what a key the list doesn't take itself does to its chosen row, like m to move a card.
	struct list_options {
		std::function<void(const std::string&)> on_open = {};
		std::string start = "";
		std::function<bool(const std::string& row, const key_event&)> on_row_key = {};
	};
	view list(std::vector<list_row> rows, list_options options);
	template <std::ranges::input_range R, class Key, class Show>
		requires std::invocable<Key&, std::ranges::range_reference_t<const R>> && std::invocable<Show&, std::ranges::range_reference_t<const R>>
	view list(const R& items, Key key_of, Show show, std::function<void(const std::string&)> on_open = {}) {
		std::vector<list_row> rows;
		for (const auto& item : items) rows.push_back({std::string(std::invoke(key_of, item)), view(std::invoke(show, item))});
		return list(std::move(rows), std::move(on_open));
	}

	// A run of numbers over time, newest at the right, out of `top`, as btop draws one:
	// a line in the accent over a soft fill, across the width it's given, `height`
	// tall; large ones have a grid.
	view graph(std::vector<double> values, double top, float height, bool large = false);
	// What a drawing hears of the pointer, in the window's own coordinates: a press,
	// with how many quickly, which it takes or not; while one it took is held, each
	// move; and the release. cursor is what the pointer looks like where it is. One
	// that hears presses takes the keyboard when pressed.
	struct drawing_pointer {
		std::function<bool(point at, int clicks)> on_press = {};
		std::function<void(point at)> on_drag = {};
		std::function<void(point at)> on_release = {};
		std::function<one::desktop::cursor(point at)> cursor = {};
	};
	// Painted by the app itself into all the space it's given, as a drawing app's
	// workspace is: paint is told the canvas and where on it, each time it's drawn.
	view drawing(std::function<void(canvas& c, rect area)> paint, drawing_pointer pointer = {});
	view button(std::string label, std::function<void()> pressed, bool primary = false);
	// Drawn as an icon, like edit or delete, as the web draws one; its words name it,
	// shown under it while it's pointed at or has the keyboard. One with no such icon
	// is drawn with its words.
	view icon_button(std::string icon, std::string label, std::function<void()> pressed, bool primary = false);
	// A toolbar's button, flat until it's pointed at: large, its icon over its words,
	// or small, its icon alone and its words shown under it while it's pointed at, or
	// its words alone when it has no icon, as a scale like 1.5× does. One that's on,
	// like Bold while words are bold, is tinted in the accent.
	struct tool_options {
		bool large = false;
		bool on = false;
		bool off = false;  // drawn faint, and it can't be pressed
		std::optional<color> swatch = {};  // drawn as a circle of this color, its words said when pointed at
		bool clear = false;                // or of no color, a checkerboard in its circle
	};
	view tool_button(std::string icon, std::string label, std::function<void()> pressed, tool_options options = {});
	// Words that go somewhere: in the accent color, underlined while pointed at or
	// focused, followed with a click or Enter.
	view link(std::string words, std::function<void()> go, std::optional<text_style> style = {});

	// Text typed into a box, kept in a state: a caret moved with the arrows, Home and
	// End, a word at a time with Control; a selection made with Shift or the pointer,
	// a double click picking a word and a triple its line; cut, copy and paste with
	// the clipboard; and undo. Enter submits one of one line, and Control+Enter one of
	// several.
	struct field_options {
		std::string placeholder = "";  // shown while it's empty
		bool multiline = false;
		int lines = 4;  // how tall one of several lines is, at least
		std::function<void()> on_submit = {};
	};
	view field(state<std::string> value, field_options options = {});

	// Puts text on the clipboard, as copying does.
	void copy_text(std::string text);
	// Puts a picture on the clipboard, as a PNG, for any program to paste.
	void copy_picture(const picture& p);
	// What's on the clipboard as text, as pasting would give it; none when there's nothing.
	std::optional<std::string> clipboard_text();
	// A picture on the clipboard, as a browser or a screenshot copies one: its bytes,
	// in its own format, to be decoded as a picture is; none when there's none.
	std::optional<std::vector<std::uint8_t>> clipboard_picture();
	// Files on the clipboard, as a file manager copies them; none when there are none.
	std::vector<std::filesystem::path> clipboard_files();

	// A box ticked or not, with what it says, flipped with a click, Space or Enter.
	view toggle(state<bool> on, std::string label);

	// One of several choices, shown as the one chosen; opened with a click, Space or
	// Enter, its choices are a list under it, chosen from as a list is, and Esc
	// closes it without choosing. Whether it's open is kept in `open`, so it stays
	// open as the view is built again.
	struct option {
		std::string value;
		std::string label;
	};
	view picker(state<std::string> chosen, std::vector<option> options, state<bool> open, std::string placeholder = "Choose…");

	// One of a few choices, side by side, each with a round button before what it
	// says: picked with a click, or with the arrows, Space or Enter once it has the
	// keyboard, which it takes as one; on_pick is told the one picked.
	view radio_buttons(state<std::string> chosen, std::vector<option> options, std::function<void(const std::string&)> on_pick = {});

	// A number from one to another in its steps: a knob along a bar, with the number
	// after it. Dragged or clicked along, moved a step with the arrows, a tenth of the
	// way with Page Up and Page Down, and to either end with Home and End. It's kept
	// as it's written, like a field's text; empty is the least.
	struct number_range {
		double from = 0, to = 100, by = 1;
	};
	view slider(state<std::string> value, number_range range);
	view edge(color c, float width = 3);      // a bar down a row's side, like how urgent it is
	view icon(std::string name, color tint, float size = 16);  // one of the icons, alone, like database
	view avatar(const std::string& name, float size = 24);  // a person's first letter in a color of theirs
	view divider();
	view spacer();  // takes what's left in a row or column

}  // namespace one::desktop
