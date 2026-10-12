// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/terminal/terminal.hpp"

#include <curses.h>

#include <algorithm>
#include <clocale>
#include <cwchar>

namespace one::terminal {

	static_assert(keys::down == KEY_DOWN && keys::up == KEY_UP && keys::left == KEY_LEFT && keys::right == KEY_RIGHT);
	static_assert(keys::home == KEY_HOME && keys::backspace == KEY_BACKSPACE && keys::page_down == KEY_NPAGE && keys::page_up == KEY_PPAGE);
	static_assert(keys::enter == KEY_ENTER && keys::end == KEY_END && keys::resize == KEY_RESIZE);

	namespace {

		std::size_t length_of(unsigned char lead) { return lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4; }

		int width_of(std::string_view character) {
			std::mbstate_t state{};
			wchar_t wide = 0;
			if (std::mbrtowc(&wide, character.data(), character.size(), &state) == static_cast<std::size_t>(-1)) return 1;
			int columns = ::wcwidth(wide);
			return columns < 0 ? 0 : columns;
		}

		// Color pairs, made as they're first asked for: a foreground and a background,
		// -1 for the terminal's own.
		short pair_of(short foreground, short background) {
			static std::map<std::pair<short, short>, short> made;
			static short next = 1;
			if (!has_colors()) return 0;
			auto it = made.find({foreground, background});
			if (it != made.end()) return it->second;
			if (next >= COLOR_PAIRS) return 0;
			init_pair(next, foreground, background);
			made[{foreground, background}] = next;
			return next++;
		}

		short curses_color(color c) {
			bool bright = COLORS >= 16;
			switch (c) {
				case color::black: return COLOR_BLACK;
				case color::red: return COLOR_RED;
				case color::green: return COLOR_GREEN;
				case color::yellow: return COLOR_YELLOW;
				case color::blue: return COLOR_BLUE;
				case color::magenta: return COLOR_MAGENTA;
				case color::cyan: return COLOR_CYAN;
				case color::white: return COLOR_WHITE;
				case color::bright_black: return bright ? 8 : COLOR_BLACK;
				case color::bright_red: return bright ? 9 : COLOR_RED;
				case color::bright_green: return bright ? 10 : COLOR_GREEN;
				case color::bright_yellow: return bright ? 11 : COLOR_YELLOW;
				case color::bright_blue: return bright ? 12 : COLOR_BLUE;
				case color::bright_magenta: return bright ? 13 : COLOR_MAGENTA;
				case color::bright_cyan: return bright ? 14 : COLOR_CYAN;
				case color::bright_white: return bright ? 15 : COLOR_WHITE;
				case color::none: return -1;
			}
			return -1;
		}

		attr_t attributes_of(const style& look) {
			attr_t a = A_NORMAL;
			if (look.bold) a |= A_BOLD;
			if (look.italic) a |= A_ITALIC;
			if (look.underline) a |= A_UNDERLINE;
			if (look.dim) a |= A_DIM;
			if (look.reverse) a |= A_REVERSE;
			short foreground = curses_color(look.foreground), background = curses_color(look.background);
			if (foreground != -1 || background != -1) a |= COLOR_PAIR(pair_of(foreground, background));
			return a;
		}

		// The terminal in curses' hands while it lives, given back as it was when it
		// goes, however it goes, so an exception never leaves a shell in raw mode.
		class session {
		public:
			session() {
				std::setlocale(LC_ALL, "");
				initscr();
				cbreak();
				noecho();
				keypad(stdscr, TRUE);
				curs_set(0);
				set_escdelay(25);
				timeout(100);
				if (has_colors()) {
					start_color();
					use_default_colors();
				}
			}
			~session() { endwin(); }
			session(const session&) = delete;
			session& operator=(const session&) = delete;
		};

	}  // namespace

	int display_width(std::string_view text) {
		int columns = 0;
		for (std::size_t i = 0; i < text.size();) {
			std::size_t length = length_of(static_cast<unsigned char>(text[i]));
			columns += width_of(text.substr(i, length));
			i += length;
		}
		return columns;
	}

	int display_width(const row& r) {
		int columns = 0;
		for (const auto& piece : r) columns += display_width(piece.text);
		return columns;
	}

	int columns() { return COLS; }
	int lines() { return LINES; }

	int put(int y, int x, std::string_view text, style look, int width) {
		if (y < 0 || y >= LINES || x >= COLS) return 0;
		if (width < 0 || x + width > COLS) width = COLS - x;
		if (width <= 0) return 0;
		std::string fitted;
		int used = 0;
		for (std::size_t i = 0; i < text.size();) {
			unsigned char lead = static_cast<unsigned char>(text[i]);
			std::size_t length = length_of(lead);
			std::string_view character = text.substr(i, length);
			int wide = width_of(character);
			if (used + wide > width) break;
			if (lead >= 0x20 || lead == '\t') fitted += character;
			used += wide;
			i += length;
		}
		attr_t a = attributes_of(look);
		attron(a);
		mvaddstr(y, x, fitted.c_str());
		attroff(a);
		return used;
	}

	int put_row(int y, int x, const row& r, int width) {
		if (width < 0) width = COLS - x;
		int used = 0;
		for (const auto& piece : r) {
			if (width - used <= 0) break;
			used += put(y, x + used, piece.text, piece.look, width - used);
		}
		return used;
	}

	void fill(int y, int x, int width, style look) {
		if (width <= 0) return;
		attr_t a = attributes_of(look);
		attron(a);
		mvhline(y, x, ' ', width);
		attroff(a);
	}

	std::string rule(int width) {
		std::string out;
		for (int i = 0; i < width; ++i) out += "─";
		return out;
	}

	// list_screen

	void list_screen::choose_first_that_opens() {
		if (choices.empty()) return;
		chosen = std::min(chosen, choices.size() - 1);
		if (choices[chosen].opens) return;
		for (std::size_t i = chosen; i < choices.size(); ++i) {
			if (choices[i].opens) {
				chosen = i;
				return;
			}
		}
	}

	void list_screen::draw(int top, int height) {
		choose_first_that_opens();
		int wanted = static_cast<int>(chosen);
		if (wanted < scrolled) scrolled = wanted;
		if (wanted >= scrolled + height) scrolled = wanted - height + 1;
		for (int y = 0; y < height && scrolled + y < static_cast<int>(choices.size()); ++y) {
			const auto& c = choices[static_cast<std::size_t>(scrolled + y)];
			bool here = static_cast<std::size_t>(scrolled + y) == chosen && c.opens;
			if (here) {
				fill(top + y, 1, COLS - 2, {.reverse = true});
				put(top + y, 1, "▸", {.bold = true, .reverse = true});
			}
			int x = c.heading ? 1 : 3;
			for (auto piece : c.shown) {
				if (here) piece.look.dim = false, piece.look.reverse = true;
				x += put(top + y, x, piece.text, piece.look, COLS - x - 1);
			}
		}
	}

	std::unique_ptr<screen> list_screen::key(int pressed) {
		if (choices.empty()) return nullptr;
		auto step = [&](int by) {
			std::size_t at = chosen;
			for (;;) {
				if (by < 0 && at == 0) return;
				if (by > 0 && at + 1 >= choices.size()) return;
				at = by < 0 ? at - 1 : at + 1;
				if (choices[at].opens) {
					chosen = at;
					return;
				}
			}
		};
		if (pressed == keys::up || pressed == 'k') step(-1);
		else if (pressed == keys::down || pressed == 'j') step(1);
		else if (pressed == keys::page_up) for (int i = 0; i < 10; ++i) step(-1);
		else if (pressed == keys::page_down) for (int i = 0; i < 10; ++i) step(1);
		else if (pressed == keys::newline || pressed == keys::enter || pressed == 'l' || pressed == keys::right) {
			if (chosen < choices.size() && choices[chosen].opens) return choices[chosen].opens();
		}
		return nullptr;
	}

	// document_screen

	void document_screen::draw(int top, int height) {
		shown_height = height;
		scrolled = std::clamp(scrolled, 0, std::max(0, static_cast<int>(rows.size()) - height));
		for (int y = 0; y < height && scrolled + y < static_cast<int>(rows.size()); ++y) put_row(top + y, 2, rows[static_cast<std::size_t>(scrolled + y)], COLS - 4);
		if (static_cast<int>(rows.size()) > height) {
			int percent = 100 * (scrolled + height) / static_cast<int>(rows.size());
			put(top + height - 1, COLS - 6, std::to_string(std::min(percent, 100)) + "%", dim());
		}
	}

	std::unique_ptr<screen> document_screen::key(int pressed) {
		if (pressed == keys::up || pressed == 'k') --scrolled;
		else if (pressed == keys::down || pressed == 'j') ++scrolled;
		else if (pressed == keys::page_up || pressed == 'b') scrolled -= shown_height - 2;
		else if (pressed == keys::page_down || pressed == ' ') scrolled += shown_height - 2;
		else if (pressed == keys::home || pressed == 'g') scrolled = 0;
		else if (pressed == keys::end || pressed == 'G') scrolled = static_cast<int>(rows.size());
		return nullptr;
	}

	// changes

	void changes::saw(const std::string& id, const std::string& now) {
		auto it = before_.find(id);
		if (!first_ && (it == before_.end() || it->second != now)) marked_[id] = std::chrono::steady_clock::now();
		before_[id] = now;
	}

	bool changes::fresh(const std::string& id) const {
		auto it = marked_.find(id);
		return it != marked_.end() && std::chrono::steady_clock::now() - it->second < std::chrono::seconds(4);
	}

	// app

	app::app(app_options options) : options_(std::move(options)) {}
	app::~app() = default;

	void app::post(std::function<void()> task) {
		std::lock_guard hold(lock_);
		posted_.push_back(std::move(task));
	}

	void app::draw_frame() {
		erase();
		const screen& s = *trail_.back();
		// The top: the app's name, where you are, and what status says.
		fill(0, 0, COLS, {.reverse = true});
		int x = put(0, 1, options_.name, {.bold = true, .reverse = true, .foreground = options_.accent});
		row right = status ? status(s) : row{};
		for (auto& piece : right) piece.look.reverse = true;
		int right_width = display_width(right);
		put(0, x + 3, s.address(), {.reverse = true}, COLS - x - right_width - 6);
		put_row(0, COLS - right_width - 1, right);
		int height = LINES - 3;
		if (!s.problem.empty()) put(2, 2, s.problem, colored(color::red, true));
		else if (!s.loaded) put(2, 2, "Loading…", dim());
		else trail_.back()->draw(2, height);
		put(LINES - 1, 1, trail_.size() > 1 ? "enter opens   esc back   q quits" : "enter opens   q quits", dim());
		refresh();
	}

	int app::run(std::unique_ptr<screen> first) {
		std::vector<std::unique_ptr<screen>> trail;
		trail.push_back(std::move(first));
		return run(std::move(trail));
	}

	int app::run(std::vector<std::unique_ptr<screen>> trail) {
		trail_ = std::move(trail);
		if (trail_.empty()) return 0;
		session terminal;
		auto last_draw = std::chrono::steady_clock::now();
		for (;;) {
			int pressed = getch();
			if (pressed == 'q' || pressed == 'Q') break;
			bool going_back = pressed == keys::escape || pressed == keys::backspace || pressed == keys::delete_character ||
			                  ((pressed == 'h' || pressed == keys::left) && !trail_.back()->takes_left());
			if (going_back) {
				if (trail_.size() > 1) trail_.pop_back();
				dirty_ = true;
			} else if (pressed == keys::resize) {
				dirty_ = true;
			} else if (pressed != ERR) {
				if (auto next = trail_.back()->key(pressed)) trail_.push_back(std::move(next));
				dirty_ = true;
			}
			std::vector<std::function<void()>> tasks;
			{
				std::lock_guard hold(lock_);
				tasks.swap(posted_);
			}
			for (auto& task : tasks) task();
			if (!tasks.empty()) dirty_ = true;
			// Marks fade, and leases run out, without a change to hear of.
			if (dirty_ || std::chrono::steady_clock::now() - last_draw > std::chrono::seconds(1)) {
				draw_frame();
				dirty_ = false;
				last_draw = std::chrono::steady_clock::now();
			}
		}
		trail_.clear();
		return 0;
	}

}  // namespace one::terminal
