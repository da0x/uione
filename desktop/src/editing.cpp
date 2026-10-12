// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include "one/desktop/editing.hpp"

#include <algorithm>

namespace one::desktop {

	namespace {

		bool continuing(unsigned char c) { return (c & 0xc0) == 0x80; }

		// A word is letters and digits, and any character beyond ASCII, so a word in
		// Arabic or Japanese is one too.
		bool in_word(unsigned char c) { return c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

	}  // namespace

	editing::editing(std::string text, bool multiline) : text_(std::move(text)), multiline_(multiline) {
		caret_ = anchor_ = text_.size();
	}

	std::pair<std::size_t, std::size_t> editing::selection() const { return {std::min(caret_, anchor_), std::max(caret_, anchor_)}; }

	std::string editing::selected() const {
		auto [from, to] = selection();
		return text_.substr(from, to - from);
	}

	std::size_t editing::clamped(std::size_t at) const {
		at = std::min(at, text_.size());
		while (at > 0 && at < text_.size() && continuing(static_cast<unsigned char>(text_[at]))) --at;
		return at;
	}

	void editing::replace_all(std::string text) {
		if (text == text_) return;
		text_ = std::move(text);
		caret_ = clamped(caret_);
		anchor_ = clamped(anchor_);
		undone_.clear();
		redone_.clear();
		last_ = doing::nothing;
	}

	void editing::keep(doing what, bool goes_on) {
		if (!(goes_on && last_ == what)) undone_.push_back({text_, caret_, anchor_});
		redone_.clear();
		last_ = what;
	}

	void editing::type(std::string_view words) {
		std::string typed(words);
		if (!multiline_) std::ranges::replace(typed, '\n', ' ');
		std::erase(typed, '\r');
		if (typed.empty() && !selecting()) return;
		// A word typed letter by letter is undone whole; a space, a word after one, or
		// a selection begins anew.
		auto spacing = [](char c) { return c == ' ' || c == '\n'; };
		bool goes_on = !selecting() && typed.size() == 1 && !spacing(typed[0]) && caret_ > 0 && !spacing(text_[caret_ - 1]);
		keep(doing::typing, goes_on);
		auto [from, to] = selection();
		text_.replace(from, to - from, typed);
		caret_ = anchor_ = from + typed.size();
	}

	void editing::erase(std::size_t from, std::size_t to) {
		text_.erase(from, to - from);
		caret_ = anchor_ = from;
	}

	void editing::erase_back(bool word) {
		if (selecting()) {
			keep(doing::erasing, false);
			auto [from, to] = selection();
			return erase(from, to);
		}
		if (caret_ == 0) return;
		keep(doing::erasing, !word);
		erase(word ? previous_word(text_, caret_) : previous_character(text_, caret_), caret_);
	}

	void editing::erase_forward(bool word) {
		if (selecting()) {
			keep(doing::erasing, false);
			auto [from, to] = selection();
			return erase(from, to);
		}
		if (caret_ == text_.size()) return;
		keep(doing::erasing, !word);
		erase(caret_, word ? next_word(text_, caret_) : next_character(text_, caret_));
	}

	void editing::erase_around(std::size_t before, std::size_t after) {
		if (before == 0 && after == 0) return;
		keep(doing::erasing, false);
		std::size_t from = clamped(caret_ > before ? caret_ - before : 0);
		std::size_t to = after >= text_.size() - caret_ ? text_.size() : caret_ + after;
		while (to < text_.size() && continuing(static_cast<unsigned char>(text_[to]))) ++to;
		erase(from, to);
	}

	void editing::move_to(std::size_t at, bool extend) {
		caret_ = clamped(at);
		if (!extend) anchor_ = caret_;
		last_ = doing::nothing;
	}

	void editing::left(bool word, bool extend) {
		// Without shift, a selection collapses to its start.
		if (selecting() && !extend && !word) return move_to(selection().first, false);
		move_to(word ? previous_word(text_, caret_) : previous_character(text_, caret_), extend);
	}

	void editing::right(bool word, bool extend) {
		if (selecting() && !extend && !word) return move_to(selection().second, false);
		move_to(word ? next_word(text_, caret_) : next_character(text_, caret_), extend);
	}

	void editing::line_start(bool extend) {
		std::size_t newline = caret_ == 0 ? std::string::npos : text_.rfind('\n', caret_ - 1);
		move_to(newline == std::string::npos ? 0 : newline + 1, extend);
	}

	void editing::line_end(bool extend) {
		std::size_t newline = text_.find('\n', caret_);
		move_to(newline == std::string::npos ? text_.size() : newline, extend);
	}

	void editing::start(bool extend) { move_to(0, extend); }
	void editing::end(bool extend) { move_to(text_.size(), extend); }

	void editing::select_all() {
		anchor_ = 0;
		caret_ = text_.size();
		last_ = doing::nothing;
	}

	void editing::select_word(std::size_t at) {
		at = clamped(at);
		std::size_t from = at, to = at;
		while (from > 0 && in_word(static_cast<unsigned char>(text_[from - 1]))) --from;
		while (to < text_.size() && in_word(static_cast<unsigned char>(text_[to]))) ++to;
		if (from == to) to = next_character(text_, at);  // a space or a mark alone
		anchor_ = clamped(from);
		caret_ = clamped(to);
		last_ = doing::nothing;
	}

	void editing::select_line(std::size_t at) {
		move_to(at, false);
		line_start(false);
		line_end(true);
	}

	bool editing::undo() {
		if (undone_.empty()) return false;
		redone_.push_back({text_, caret_, anchor_});
		auto [text, caret, anchor] = std::move(undone_.back());
		undone_.pop_back();
		text_ = std::move(text);
		caret_ = caret, anchor_ = anchor;
		last_ = doing::nothing;
		return true;
	}

	bool editing::redo() {
		if (redone_.empty()) return false;
		undone_.push_back({text_, caret_, anchor_});
		auto [text, caret, anchor] = std::move(redone_.back());
		redone_.pop_back();
		text_ = std::move(text);
		caret_ = caret, anchor_ = anchor;
		last_ = doing::nothing;
		return true;
	}

	std::size_t editing::previous_character(std::string_view text, std::size_t at) {
		if (at == 0) return 0;
		do --at;
		while (at > 0 && continuing(static_cast<unsigned char>(text[at])));
		return at;
	}

	std::size_t editing::next_character(std::string_view text, std::size_t at) {
		if (at >= text.size()) return text.size();
		do ++at;
		while (at < text.size() && continuing(static_cast<unsigned char>(text[at])));
		return at;
	}

	// Back over what isn't a word, then over the word: to its start.
	std::size_t editing::previous_word(std::string_view text, std::size_t at) {
		while (at > 0 && !in_word(static_cast<unsigned char>(text[at - 1]))) --at;
		while (at > 0 && in_word(static_cast<unsigned char>(text[at - 1]))) --at;
		return at;
	}

	// On over the word, then over what isn't one: to the next word's start.
	std::size_t editing::next_word(std::string_view text, std::size_t at) {
		while (at < text.size() && in_word(static_cast<unsigned char>(text[at]))) ++at;
		while (at < text.size() && !in_word(static_cast<unsigned char>(text[at]))) ++at;
		return at;
	}

}  // namespace one::desktop
