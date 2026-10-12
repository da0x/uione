// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What the toolkit's parts share of curses: characters' widths, colors and styles
// as attributes, and the terminal held in curses' hands while it's in use.

#pragma once

#include <curses.h>

#include <clocale>
#include <cwchar>
#include <map>
#include <string_view>

#include "one/terminal/terminal.hpp"

namespace one::terminal::detail {


		inline std::size_t length_of(unsigned char lead) { return lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4; }

		inline int width_of(std::string_view character) {
			std::mbstate_t state{};
			wchar_t wide = 0;
			if (std::mbrtowc(&wide, character.data(), character.size(), &state) == static_cast<std::size_t>(-1)) return 1;
			int columns = ::wcwidth(wide);
			return columns < 0 ? 0 : columns;
		}

		// Color pairs, made as they're first asked for: a foreground and a background,
		// -1 for the terminal's own.
		inline short pair_of(short foreground, short background) {
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

		inline short curses_color(color c) {
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

		inline attr_t attributes_of(const style& look) {
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


}  // namespace one::terminal::detail
