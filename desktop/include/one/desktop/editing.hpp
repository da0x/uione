// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Text being edited: the text, the caret, and the other end of what's selected, with
// what each key does to them. It knows nothing of how the text is drawn, so a field
// that does moves its caret up and down a line through move_to.
//
//     one::desktop::editing e("Hello");
//     e.end(false);
//     e.type(", world");   // "Hello, world", the caret after it
//     e.left(true, true);  // "world" selected, a word to the left
//     e.erase_back(false); // "Hello, "
//     e.undo();            // "Hello, world" again
//
// Places in the text are bytes of its UTF-8, and always the start of a character.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace one::desktop {

	class editing {
	public:
		explicit editing(std::string text = {}, bool multiline = false);

		const std::string& text() const { return text_; }
		std::size_t caret() const { return caret_; }
		std::size_t anchor() const { return anchor_; }
		bool multiline() const { return multiline_; }

		// What's selected, from its start to its end, and its text; empty when the
		// caret is all there is.
		std::pair<std::size_t, std::size_t> selection() const;
		bool selecting() const { return caret_ != anchor_; }
		std::string selected() const;

		// Takes text from elsewhere, like a value that changed underneath: the caret
		// stays where it was, within it. What was done before can't be undone past it.
		void replace_all(std::string text);

		// Typing, which takes the place of what's selected. A field of one line takes
		// a newline as a space.
		void type(std::string_view words);
		// Backspace and Delete: what's selected, or a character, or a word.
		void erase_back(bool word);
		void erase_forward(bool word);
		// Bytes before and after the caret, as an input method asks, kept to whole characters.
		void erase_around(std::size_t before, std::size_t after);

		// Moving the caret, and with `extend` the selection's end with it.
		void move_to(std::size_t at, bool extend);
		void left(bool word, bool extend);
		void right(bool word, bool extend);
		void line_start(bool extend);  // of the line the caret is on, between newlines
		void line_end(bool extend);
		void start(bool extend);  // of the whole text
		void end(bool extend);
		void select_all();
		void select_word(std::size_t at);  // what a double click picks
		void select_line(std::size_t at);  // and a triple

		// What was typed, erased or replaced, a word at a time while typing.
		bool undo();
		bool redo();

		// A place in the text, moved by a character or a word, never into one.
		static std::size_t previous_character(std::string_view text, std::size_t at);
		static std::size_t next_character(std::string_view text, std::size_t at);
		static std::size_t previous_word(std::string_view text, std::size_t at);
		static std::size_t next_word(std::string_view text, std::size_t at);

	private:
		struct snapshot {
			std::string text;
			std::size_t caret = 0, anchor = 0;
		};
		enum class doing { nothing, typing, erasing };

		// Kept before a change, unless it goes on from the one before, like the next
		// letter of a word.
		void keep(doing what, bool goes_on);
		void erase(std::size_t from, std::size_t to);
		std::size_t clamped(std::size_t at) const;

		std::string text_;
		std::size_t caret_ = 0, anchor_ = 0;
		bool multiline_ = false;
		std::vector<snapshot> undone_, redone_;
		doing last_ = doing::nothing;
	};

}  // namespace one::desktop
