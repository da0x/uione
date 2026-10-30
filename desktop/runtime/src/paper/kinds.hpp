// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The kinds of block a paper is made of, each its own: how its words are set, the room
// around it, what it draws on its page besides its words, and what Enter does in it. The
// paper asks a block's kind and knows none of them, so a kind is added as a class of its
// own, in a file of its own, and a line in kinds.cpp.

#pragma once

#include <any>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime::paper_kinds {

	// A block as the view has it.
	struct block {
		std::string id, kind, align, words;
		double position = 0;
		std::string language = "";  // code's, as one::syntax names it, like cpp
		// A picture's: its file, its own size, how much of the page's width it runs, and
		// how round its corners are, a share of its shorter side.
		std::string picture = "";
		double picture_width = 0, picture_height = 0, width = 1, corners = 0;
	};

	// What a kind is told as it's laid out and drawn: the paper's theme, warm or not, the
	// document's face, and the reader's size.
	struct setting {
		const desktop::theme& look;
		const std::string& face;
		float zoom = 1;
		// A picture kept in a file, read once and kept while the app runs; none when it
		// can't be read.
		std::function<const desktop::picture*(const std::string&)> picture = {};
	};

	// A block as it's laid out: its box on its page, its words' place in it, and what its
	// kind keeps of its own, like an equation set.
	struct laid {
		block b;
		text_style look;
		std::optional<text_layout> words;
		float x = 0, y = 0, width = 0, height = 0;  // its box, from the first page's top left
		float words_y = 0;                           // its words, down from its box's top
		bool raw = true;                             // its words drawn, rather than only what its kind draws
		float page_width = 0;                        // of the page it's on
		float page_room = 0;                         // and how tall its room for blocks is, which none should be taller than
		std::any own;                                // what its kind keeps
	};

	// What Enter does: splits the block at the caret, types a new line, or leaves it,
	// its empty last line taken away, for a block after it.
	enum class on_enter { split, new_line, leave };

	// A kind written apart, in a dialog of its own over the page, as an equation is: what
	// it's called, its keys a tab at a time, what its words are set from, and what's said
	// of them as they're typed. Each key has its name, the notation its face is set from,
	// what it puts at the caret, ‸ where the caret goes then, and what it puts around words
	// chosen, § where they go.
	struct key {
		std::string name, face, put, around = "";
	};
	struct key_tab {
		std::string name;
		std::vector<key> keys;
	};
	struct written_apart {
		std::string name;
		std::vector<key_tab> tabs;
		std::function<std::string(const std::string&)> written;
		std::function<std::optional<std::string>(const std::string&)> note = {};
		bool spaced = false;     // a name put after a letter is apart from it, where letters together are one name
		bool lines = false;      // its words run over several lines: Enter starts one, and Control+Enter keeps them
		bool languages = false;  // written in a language chosen from one::syntax's, shown colored as it reads it
	};

	class kind {
	public:
		virtual ~kind() = default;
		// How its words look, before the reader's size.
		virtual text_style words_look(const setting& s) const = 0;
		// The room above and below it, before the reader's size.
		virtual std::pair<float, float> room() const { return {0, 12}; }
		// Whether it goes to the next page with what follows it, as a heading does.
		virtual bool leads() const { return false; }
		// Its level in the outline, 1 a title's and 2 a heading's; not listed at 0.
		virtual int outlined() const { return 0; }
		// How far in its words start, before the reader's size.
		virtual float indent() const { return 0; }
		// Its words as they're laid out, in their look, or colored as its kind reads them.
		virtual std::vector<text_span> words_spans(const block&, const std::string& words, const text_style& look, const setting&) const { return {{words, look}}; }
		// Laid out, once its words are: what else it has, and how tall it is all told.
		// `here` is whether the caret's in it.
		virtual void lay_out(laid&, const setting&, bool) const {}
		// Drawn, its box's top left in the window: behind its words, and in front of them.
		virtual void paint_behind(canvas&, const laid&, point, const setting&) const {}
		virtual void paint_front(canvas&, const laid&, point, const setting&) const {}
		// What Enter does, the caret at a place in its words.
		virtual on_enter enter(const std::string&, std::size_t, bool) const { return on_enter::split; }
		// Whether words can be joined onto its end, as Backspace at the next one's start does.
		virtual bool joins() const { return true; }
		// What it becomes when it starts with a character and a space as it's typed, like
		// "# " a heading.
		virtual std::optional<std::string> shortcut(char) const { return std::nullopt; }
		// The kind of a block made of its words after the caret, by Enter: its own, unless
		// it says another, as a picture's caption goes on as text.
		virtual std::optional<std::string> continues_as() const { return std::nullopt; }
		// Written apart, when it is.
		virtual const written_apart* apart() const { return nullptr; }
	};

	// The kind of that name, or text when there's none.
	const kind& kind_of(const std::string& name);

	// The family a page's face is, for kinds whose words are in it.
	std::string family_of(const std::string& face, const desktop::theme& t);

	// The kinds, each in its own file.
	const kind& title_kind();
	const kind& subtitle_kind();
	const kind& heading_kind();
	const kind& text_kind();
	const kind& quote_kind();
	const kind& math_kind();
	const kind& chem_kind();
	const kind& code_kind();
	const kind& picture_kind();

}  // namespace one::desktop::runtime::paper_kinds
