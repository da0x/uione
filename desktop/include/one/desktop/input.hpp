// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What the keyboard and the pointer say, as a view hears it.

#pragma once

#include <cstdint>
#include <string>

namespace one::desktop {

	// A key pressed, or repeating while held: the symbol it means in the keyboard's
	// layout, the text it types, if any, and the modifiers held with it.
	struct key_event {
		std::uint32_t symbol = 0;  // one of keys::, or the character's own code for printable ones
		std::string text;
		bool shift = false, control = false, alt = false, super = false;

		bool is(std::uint32_t s) const { return symbol == s; }
		bool is(char c) const { return !control && !alt && !super && text.size() == 1 && text[0] == c; }
	};

	namespace keys {
		// XKB's symbols for the keys a view usually listens for.
		inline constexpr std::uint32_t backspace = 0xff08, tab = 0xff09, enter = 0xff0d, escape = 0xff1b;
		inline constexpr std::uint32_t home = 0xff50, left = 0xff51, up = 0xff52, right = 0xff53, down = 0xff54;
		inline constexpr std::uint32_t page_up = 0xff55, page_down = 0xff56, end = 0xff57;
		inline constexpr std::uint32_t keypad_enter = 0xff8d, left_tab = 0xfe20, space = 0x20, delete_key = 0xffff;
	}  // namespace keys

}  // namespace one::desktop
