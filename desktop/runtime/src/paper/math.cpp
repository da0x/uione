// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An equation: its words in one::math's notation, set as math. While the caret's in it,
// it's its words, in a well; otherwise its equation, aligned as the block says. It keeps
// the room of the larger, so the page doesn't move as the caret comes and goes.

#include "kinds.hpp"

namespace one::desktop::runtime::paper_kinds {

	namespace {

		struct set {
			set_equation equation;
			float down = 0;  // from the block's top
		};

		class math final : public kind {
		public:
			text_style words_look(const setting& at) const override {
				text_style s = at.look.code();
				s.line_height = 1.5f;
				return s;
			}
			std::pair<float, float> room() const override { return {10, 18}; }
			bool joins() const override { return false; }

			void lay_out(laid& l, const setting& at, bool here) const override {
				if (l.b.words.empty()) return;
				set made{equation_set(l.b.words, at.look.text_size * 1.25f * at.zoom, true, at.look.ink), 0};
				if (!made.equation.read) return;
				float tall = made.equation.ascent + made.equation.descent;
				made.down = std::max(0.f, l.height - tall) / 2;
				l.words_y = std::max(0.f, tall - l.height) / 2;
				l.height = std::max(l.height, tall);
				l.raw = here;
				l.own = made;
			}

			void paint_behind(canvas& c, const laid& l, point at, const setting& s) const override {
				if (l.raw) c.box({at.x - 8, at.y - 4, l.width + 16, l.height + 8}, {.fill = s.look.sunken, .radius = 4});
			}

			void paint_front(canvas& c, const laid& l, point at, const setting&) const override {
				const auto* made = std::any_cast<set>(&l.own);
				if (l.raw || !made) return;
				float w = made->equation.width;
				float x = l.b.align == "left" ? at.x : l.b.align == "right" ? at.x + l.width - w : at.x + (l.width - w) / 2;
				made->equation.draw(c, {x, at.y + made->down});
			}
		};

	}  // namespace

	const kind& math_kind() {
		static const math k;
		return k;
	}

}  // namespace one::desktop::runtime::paper_kinds
