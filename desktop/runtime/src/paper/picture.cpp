// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture: as much of the page's width as it says, a third, a half or all of it, its
// shape its own, aligned as the block says, its corners square or rounded; and under
// it, its caption, which is the block's words, typed where they are. Never taller than
// a page has room for with its caption, narrower instead. While the caret's in it, it's
// ringed in the accent, so it's plain which picture the toolbar acts on.

#include "kinds.hpp"

#include <algorithm>
#include <limits>

namespace one::desktop::runtime::paper_kinds {

	namespace {

		struct placed {
			float left = 0, across = 0, down = 0;  // the picture's place in its block
			bool here = false;                     // and whether the caret's in it
		};

		class picture final : public kind {
		public:
			static constexpr float gap = 8;  // between the picture and its caption

			text_style words_look(const setting& at) const override {
				text_style s = at.look.body();
				s.size = 14;
				s.line_height = 1.5f;
				s.italic = true;
				s.color = at.look.muted;
				s.family = family_of(at.face, at.look);
				return s;
			}
			std::pair<float, float> room() const override { return {12, 18}; }
			bool joins() const override { return false; }
			std::optional<std::string> continues_as() const override { return "text"; }

			void lay_out(laid& l, const setting& at, bool here) const override {
				double share = l.b.width > 0 ? std::min(1.0, l.b.width) : 1.0;
				float shape = l.b.picture_width > 0 && l.b.picture_height > 0 ? static_cast<float>(l.b.picture_height / l.b.picture_width) : 2.0f / 3.0f;
				float highest = l.page_room > 0 ? std::max(0.0f, l.page_room - gap * at.zoom - l.height) : std::numeric_limits<float>::infinity();
				float down = std::min(l.width * static_cast<float>(share) * shape, highest);
				float across = down / shape;
				float left = l.b.align == "left" ? 0 : l.b.align == "right" ? l.width - across : (l.width - across) / 2;
				l.own = placed{left, across, down, here};
				l.words_y = down + gap * at.zoom;
				l.height += l.words_y;
			}

			void paint_behind(canvas& c, const laid& l, point at, const setting& s) const override {
				const auto* place = std::any_cast<placed>(&l.own);
				if (!place) return;
				rect box{at.x + place->left, at.y, place->across, place->down};
				float radius = static_cast<float>(l.b.corners) * std::min(box.width, box.height);
				const desktop::picture* shown = s.picture && !l.b.picture.empty() ? s.picture(l.b.picture) : nullptr;
				if (shown && *shown) c.picture(*shown, box, {0, 0, 1, 1}, 1, radius);
				else c.box(box, {.fill = s.look.sunken, .radius = radius});
				if (place->here) {
					float out = 3 * s.zoom;
					c.box({box.x - out, box.y - out, box.width + 2 * out, box.height + 2 * out}, {.border = s.look.accent, .border_width = 2 * s.zoom, .radius = radius > 0 ? radius + out : 0});
				}
			}
		};

	}  // namespace

	const kind& picture_kind() {
		static const picture k;
		return k;
	}

}  // namespace one::desktop::runtime::paper_kinds
