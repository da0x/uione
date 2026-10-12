// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::terminal, uione's toolkit for terminal apps, drawn with ncurses: screens kept
// in a trail, each a list of choices or a document of styled rows, a bar at the top
// saying where you are, and work handed in from other threads, as a live view's
// changes are.
//
//     one::terminal::app app({.name = "neotrac"});
//     return app.run(std::make_unique<home>());
//
// Enter opens what a screen offers, Esc or Backspace goes back, q quits. The header
// leaves curses.h out, so its macros stay out of a program's names.

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace one::terminal {

	// The 16 colors every terminal has; none is the terminal's own.
	enum class color { none, black, red, green, yellow, blue, magenta, cyan, white, bright_black, bright_red, bright_green, bright_yellow, bright_blue, bright_magenta, bright_cyan, bright_white };

	struct style {
		bool bold = false, italic = false, underline = false, dim = false, reverse = false;
		color foreground = color::none, background = color::none;
	};

	// Text in one style, on one row.
	struct run {
		std::string text;
		style look;
	};
	using row = std::vector<run>;

	inline row plain(std::string text, style look = {}) { return {run{std::move(text), look}}; }
	inline style bold() { return {.bold = true}; }
	inline style dim() { return {.dim = true}; }
	inline style colored(color c, bool strong = false) { return {.bold = strong, .foreground = c}; }

	// How many columns text takes, wide characters two.
	int display_width(std::string_view text);
	int display_width(const row& r);

	// Drawing: text at a row and column in no more than `width` columns (to the edge
	// when it's -1), each returning the columns it took.
	int put(int y, int x, std::string_view text, style look = {}, int width = -1);
	int put_row(int y, int x, const row& r, int width = -1);
	void fill(int y, int x, int width, style look);  // a bar of spaces
	std::string rule(int width);                      // ───, as wide as asked
	int columns();
	int lines();

	// Keys, as screens are given them: a character's code, or one of these.
	namespace keys {
		inline constexpr int down = 0402, up = 0403, left = 0404, right = 0405, home = 0406, backspace = 0407;
		inline constexpr int page_down = 0522, page_up = 0523, enter = 0527, end = 0550, resize = 0632;
		inline constexpr int escape = 27, newline = '\n', delete_character = 127;
	}  // namespace keys

	// A screen: drawn in the rows it's given, told the keys pressed, and maybe opening
	// another, which goes on the trail after it.
	class screen {
	public:
		virtual ~screen() = default;
		virtual void draw(int top, int height) = 0;
		virtual std::unique_ptr<screen> key(int pressed) = 0;
		virtual std::string address() const = 0;  // where it is, for the top bar, like /neotrac/boards/main
		// Whether h and ← are its own, as a board's are for moving between columns;
		// otherwise they go back.
		virtual bool takes_left() const { return false; }

		bool loaded = false;   // its first view has arrived
		std::string problem;   // what went wrong, shown instead of it
	};

	// A line on a list screen: what it shows, and the screen Enter opens from it.
	struct choice {
		row shown;
		std::function<std::unique_ptr<screen>()> opens;
		bool heading = false;  // drawn flush left, as a section's title is
	};

	// Choices moved through with the arrows or j and k, opened with Enter or l.
	class list_screen : public screen {
	public:
		void draw(int top, int height) override;
		std::unique_ptr<screen> key(int pressed) override;

	protected:
		std::vector<choice> choices;
		std::size_t chosen = 0;
		int scrolled = 0;

		// Moves the choice onto one that opens, as a heading can't be chosen.
		void choose_first_that_opens();
	};

	// Rows read top to bottom: scrolled with the arrows, j and k, space and b, g and G.
	class document_screen : public screen {
	public:
		void draw(int top, int height) override;
		std::unique_ptr<screen> key(int pressed) override;

	protected:
		std::vector<row> rows;
		int scrolled = 0;
		int shown_height = 1;
	};

	// Things changed since a moment ago, marked a while so a change made elsewhere is
	// seen as it lands. The first time things are seen marks nothing.
	class changes {
	public:
		void saw(const std::string& id, const std::string& now);
		void settled() { first_ = false; }
		bool fresh(const std::string& id) const;

	private:
		std::map<std::string, std::string> before_;
		std::map<std::string, std::chrono::steady_clock::time_point> marked_;
		bool first_ = true;
	};

	struct app_options {
		std::string name = "uione";  // at the top left
		color accent = color::blue;
	};

	// The terminal and its loop: the trail of screens, the top bar, keys, and what
	// other threads post.
	class app {
	public:
		explicit app(app_options options = {});
		~app();
		app(const app&) = delete;
		app& operator=(const app&) = delete;

		// Shows the first screen, and any after it, until q. Returns 0.
		int run(std::vector<std::unique_ptr<screen>> trail);
		int run(std::unique_ptr<screen> first);

		// Hands work to the loop from any thread; it's done there, and the screen is
		// drawn again after.
		void post(std::function<void()> task);
		void redraw() { dirty_ = true; }

		// The top bar's right side, like ● live and who's signed in, made each time
		// it's drawn.
		std::function<row(const screen& shown)> status;

	private:
		void draw_frame();
		app_options options_;
		std::vector<std::unique_ptr<screen>> trail_;
		std::mutex lock_;
		std::vector<std::function<void()>> posted_;
		bool dirty_ = true;
	};

}  // namespace one::terminal
