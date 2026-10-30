// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Code: kept as it's typed, in the code face, in a well, colored as one::syntax reads
// its language. Enter starts a new line, and Enter on an empty last line leaves it for a
// block after it. Written apart, its language is chosen there.

#include "kinds.hpp"

namespace one::desktop::runtime::paper_kinds {

	namespace {

		class code final : public kind {
		public:
			text_style words_look(const setting& at) const override {
				text_style s = at.look.code();
				s.line_height = 1.5f;
				return s;
			}
			std::pair<float, float> room() const override { return {10, 18}; }
			void paint_behind(canvas& c, const laid& l, point at, const setting& s) const override {
				c.box({at.x - 8, at.y - 4, l.width + 16, l.height + 8}, {.fill = s.look.sunken, .radius = 4});
			}
			std::vector<text_span> words_spans(const block& b, const std::string& words, const text_style& look, const setting& at) const override {
				return colored_code(words, b.language, look, at.look);
			}
			const written_apart* apart() const override {
				static const written_apart code{"Code", {}, [](const std::string& words) { return words; }, {}, false, true, true};
				return &code;
			}
			on_enter enter(const std::string& words, std::size_t caret, bool selecting) const override {
				if (!selecting && caret == words.size() && !words.empty() && words.back() == '\n') return on_enter::leave;
				return on_enter::new_line;
			}
		};

	}  // namespace

	const kind& code_kind() {
		static const code k;
		return k;
	}

}  // namespace one::desktop::runtime::paper_kinds
