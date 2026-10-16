// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Views in a terminal, as one::desktop has them in a window: what a terminal shows,
// said as C++ values, laid out in character cells, and built again whenever a state
// they read changes:
//
//     one::terminal::view_app app;
//     return app.run([=] {
//         return one::terminal::column(one::terminal::text(page->title, one::terminal::bold()),
//                                      one::terminal::list(rows, [](const std::string& id) { open(id); }));
//     });
//
// What's built is matched with what was there before, by position and kind, or by
// key, so a list's chosen row, a scroll and focus stay where they were. Tab moves
// between what takes keys, and so do the arrows and h j k l when what has them
// doesn't take them. Views draw into cells, which the app copies to the terminal,
// and a test reads as text.

#pragma once

#include <one/screens/state.hpp>

#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "one/terminal/terminal.hpp"

namespace one::terminal {

	using screens::state;

	// A key pressed: one of keys:: for a key that types nothing, or the text it types,
	// with Alt or Control held.
	struct key_press {
		int code = 0;
		std::string text = "";
		bool alt = false, control = false;
		bool is(int k) const { return code == k; }
		bool is(char c) const { return !alt && !control && text.size() == 1 && text[0] == c; }
	};

	namespace keys {
		inline constexpr int tab = 9, left_tab = 0541, delete_key = 0512;
	}  // namespace keys

	struct area {
		int x = 0, y = 0, width = 0, height = 0;
		int right() const { return x + width; }
		int bottom() const { return y + height; }
		area intersect(const area& other) const;
	};

	struct extent {
		int width = 0, height = 0;
	};

	// Cells a view draws into: each a character, or the second half of a wide one,
	// in a style. A test reads them as text; the app copies them to the terminal.
	class canvas {
	public:
		canvas(int width, int height);
		int width() const { return width_; }
		int height() const { return height_; }
		// Text at a row and a column in no more than `limit` columns, within the clip;
		// the columns it took.
		int put(int y, int x, std::string_view text, style look = {}, int limit = -1);
		void fill(const area& a, style look);
		// Turns an area's cells around, as the row a list has chosen is shown.
		void reverse(const area& a);
		// Draws only within an area until the scope ends.
		class clip_scope {
		public:
			~clip_scope() { owner_.clips_.pop_back(); }
			clip_scope(const clip_scope&) = delete;
			clip_scope& operator=(const clip_scope&) = delete;

		private:
			friend class canvas;
			explicit clip_scope(canvas& c) : owner_(c) {}
			canvas& owner_;
		};
		[[nodiscard]] clip_scope clip(const area& a);

		// Each row as its text, trailing spaces left off.
		std::vector<std::string> rows() const;
		std::string text() const;
		// The style of a cell, as a test looks at it.
		const style& look_at(int y, int x) const { return cells_[static_cast<std::size_t>(y * width_ + x)].look; }
		// Copies itself to the terminal.
		void show() const;

	private:
		struct cell {
			std::string glyph = " ";
			style look;
			bool covered = false;  // the right half of a wide character
		};
		int width_, height_;
		std::vector<cell> cells_;
		std::vector<area> clips_;
	};

	enum class align { start, center, end, stretch };

	// An element of a built view. Most programs use the functions below instead.
	class element {
	public:
		virtual ~element() = default;

		std::string key;
		int pad_top = 0, pad_right = 0, pad_bottom = 0, pad_left = 0;
		std::optional<int> fixed_width, fixed_height;
		float grow = 0;
		int spacing = 0;
		align cross = align::stretch;
		bool wants_focus = false;
		bool boxed = false;  // a line drawn around it, inside its padding
		std::string box_title;
		color box_color = color::none;  // its title's, the accent's unless it says
		std::function<bool(const key_press&)> on_key;

		std::vector<std::unique_ptr<element>> children;
		element* parent = nullptr;
		area frame;

		extent measure(int width);
		void place(const area& a);
		void paint(canvas& c);

		virtual bool handle_key(const key_press& e);
		virtual bool focusable() const { return false; }
		virtual bool prefers_focus() const { return false; }
		virtual void adopt(element& before);
		void reveal(const area& a);
		// Brought to the top of what scrolls it, once, when it's first placed, as a
		// section is when a link to it is followed.
		bool to_top = false, brought = false;
		void bring_to_top();

		bool focused = false;
		bool focused_by_default = false;
		bool carried = false;

	protected:
		virtual extent measure_content(int width);
		virtual void place_content(const area& inner);
		virtual void paint_content(canvas& c);
		virtual bool reveal_here(const area&) { return false; }
		virtual bool top_here(const area&) { return false; }
		area inner() const;
		int frame_width() const { return boxed ? 2 : 0; }
	};

	class view {
	public:
		view() = default;
		template <std::derived_from<element> E>
		view(std::unique_ptr<E> e) : element_(std::move(e)) {}

		view&& padding(int all) && { return std::move(*this).padding(all, all); }
		view&& padding(int vertical, int horizontal) && {
			element_->pad_top = element_->pad_bottom = vertical;
			element_->pad_left = element_->pad_right = horizontal;
			return std::move(*this);
		}
		view&& padding(int top, int right, int bottom, int left) && {
			element_->pad_top = top, element_->pad_right = right, element_->pad_bottom = bottom, element_->pad_left = left;
			return std::move(*this);
		}
		view&& width(int w) && { element_->fixed_width = w; return std::move(*this); }
		view&& height(int h) && { element_->fixed_height = h; return std::move(*this); }
		view&& grow(float share = 1) && { element_->grow = share; return std::move(*this); }
		view&& spacing(int s) && { element_->spacing = s; return std::move(*this); }
		view&& align(terminal::align a) && { element_->cross = a; return std::move(*this); }
		view&& key(std::string k) && { element_->key = std::move(k); return std::move(*this); }
		view&& on_key(std::function<bool(const key_press&)> f) && { element_->on_key = std::move(f); return std::move(*this); }
		view&& autofocus() && { element_->wants_focus = true; return std::move(*this); }
		view&& brought_to_top() && { element_->to_top = true; return std::move(*this); }
		// A panel, as btop draws one: a line with round corners around it, and a title in
		// its top edge, in a color of its own or the accent.
		view&& boxed(std::string title = "", color title_color = color::none) && {
			element_->boxed = true;
			element_->box_title = std::move(title);
			element_->box_color = title_color;
			return std::move(*this);
		}

		std::unique_ptr<element> take() && { return std::move(element_); }
		element* get() const { return element_.get(); }

	private:
		std::unique_ptr<element> element_;
	};

	// Text, wrapped to the width it's given.
	view text(std::string words, style look = {});
	view text(terminal::line runs);
	view text_rows(std::vector<terminal::line> rows);  // already laid out, row by row
	// Rows laid out for the width given, as Markdown is.
	view text_laid(std::function<std::vector<terminal::line>(int width)> lay_out);

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

	// Views across, going on to the next line where there's no more room, like a row of
	// buttons in a narrow column.
	view flow(std::vector<view> children);

	// A main view and what goes beside it, side by side where the width is wide_from or
	// more, a third of it the side's, else one after the other, main first.
	view beside(view main, view side, int wide_from = 100);

	// One view scrolled within the rows given, following what has the keyboard.
	view scroll(view content);

	struct list_row {
		std::string key;
		view shown;
	};
	struct list_options {
		std::function<void(const std::string&)> on_open = {};
		std::string start = "";
		std::function<bool(const std::string& row, const key_press&)> on_row_key = {};
	};
	// Rows, one of them chosen: moved with the arrows, j and k, Home, End and the page
	// keys, opened with Enter or l.
	view list(std::vector<list_row> rows, list_options options = {});
	view list(std::vector<list_row> rows, std::function<void(const std::string&)> on_open);

	// A surface of things placed anywhere, in its own units, drawn as boxes in cells, a
	// cell across units_across of them and a row down units_down. n chooses one; the
	// arrows move it a grid step and with Shift resize it, and on_place is told where
	// it's gone; with none chosen the arrows look around. Enter opens the one chosen.
	struct surface_item {
		std::string key;
		double x = 0, y = 0, width = 150, height = 150;
		view content;
		bool movable = true;
	};
	// A band down a surface, like a board's column, its title along its top; < and >
	// make the one the chosen thing is in narrower or wider.
	struct surface_region {
		std::string key, title;
		double x = 0, width = 320;
	};
	struct surface_options {
		std::function<void(const std::string& key, double x, double y, double width, double height)> on_place = {};
		std::vector<surface_region> regions = {};
		std::function<void(const std::string& key, double width)> on_resize = {};
		std::function<void(const std::string& key)> on_open = {};
		double grid = 0;
		double smallest = 30;
		double units_across = 10, units_down = 20;  // a cell's width and height in the surface's units
		int height = 20;                            // how many rows it's drawn in
	};
	view surface(std::vector<surface_item> items, surface_options options);

	view button(std::string label, std::function<void()> pressed, bool primary = false);
	view link(std::string words, std::function<void()> go, style look = {});
	view divider();
	view spacer();

	struct field_options {
		std::string placeholder = "";
		bool multiline = false;
		int lines = 4;
		std::function<void()> on_submit = {};
	};
	// Text typed into a line, or several, kept in a state, edited as one::screens'
	// editing edits it: the arrows, Home and End, a word at a time with Alt, Control
	// with A, E, K, U and W as a shell takes them, and Control+Z to undo.
	view field(state<std::string> value, field_options options = {});
	view toggle(state<bool> on, std::string label);
	struct option {
		std::string value;
		std::string label;
	};
	view picker(state<std::string> chosen, std::vector<option> options, state<bool> open, std::string placeholder = "Choose…");
	// One of a few choices on a line, (•) before the one chosen: the arrows move it,
	// and on_pick is told the one picked.
	view radio_buttons(state<std::string> chosen, std::vector<option> options, std::function<void(const std::string&)> on_pick = {});
	// A number from one to another in its steps, as a bar with a knob and the number
	// after it: the arrows move it a step, Page Up and Page Down a tenth of the way, and
	// Home and End to either end. It's kept as it's written; empty is the least.
	struct number_range {
		double from = 0, to = 100, by = 1;
	};
	view slider(state<std::string> value, number_range range);

	// A bar of btop's blocks, as wide as it's given, shared among its parts by how much
	// each is, each in its own color; a part that's anything takes a block at least.
	struct meter_part {
		double amount = 0;
		style look = {};
	};
	view meter(std::vector<meter_part> parts);
	// The look of a meter's nth part: the accent, fading as btop's gradients do.
	style meter_look(std::size_t nth);
	// A run of numbers, newest at the right, as btop draws one: braille dots, two
	// samples a cell across and four steps a cell up, filled below each sample, `rows`
	// cells high, out of `top`, in the look given.
	view graph(std::vector<double> values, double top, int rows, style look);

	// The app: a view built and drawn when a state it read changes or a key's
	// pressed, keys given to what has focus, and work handed in from other threads.
	class view_app {
	public:
		view_app() = default;
		view_app(const view_app&) = delete;
		view_app& operator=(const view_app&) = delete;

		// Shows the view until `on_key` or quit() ends it. Returns 0.
		int run(std::function<view()> build, std::function<bool(const key_press&)> on_key = {});
		void quit() { quitting_ = true; }
		void post(std::function<void()> task);

	private:
		std::mutex lock_;
		std::vector<std::function<void()>> posted_;
		bool quitting_ = false;
	};

	// A view's part, as the app and tests run it: its elements built, placed in an
	// area, given keys, and drawn.
	class view_host {
	public:
		explicit view_host(std::function<view()> build) : build_(std::move(build)) {}
		// Builds it again when a state it read changed, and places it.
		void bring_up_to_date(int width, int height);
		// A key, to what has focus and up, then moving focus; false when nothing took it.
		bool deliver(const key_press& e);
		void draw(canvas& c);
		bool needs_drawing() const { return dirty_; }
		element* root() { return root_.get(); }
		element* focused();
		// What has the keyboard's text cursor, for the terminal's: its row and column.
		std::optional<std::pair<int, int>> cursor;

	private:
		std::function<view()> build_;
		std::unique_ptr<element> root_;
		std::shared_ptr<screens::detail::reader> reading_;
		bool needs_building_ = true, dirty_ = true;
		int width_ = 0, height_ = 0;
	};

}  // namespace one::terminal
