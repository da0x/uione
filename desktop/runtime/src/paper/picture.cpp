// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture: as much of the page's width as it says, a third, a half or all of it, its
// shape its own, aligned as the block says, its corners square or rounded; and under
// it, its caption, which is the block's words, typed where they are.

#include "kinds.hpp"

namespace one::desktop::runtime::paper_kinds {

	namespace {

		struct placed {
			float left = 0, across = 0, down = 0;  // the picture's place in its block
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

			void lay_out(laid& l, const setting& at, bool) const override {
				double share = l.b.width > 0 ? std::min(1.0, l.b.width) : 1.0;
				float across = l.width * static_cast<float>(share);
				double shape = l.b.picture_width > 0 && l.b.picture_height > 0 ? l.b.picture_height / l.b.picture_width : 2.0 / 3.0;
				float down = across * static_cast<float>(shape);
				float left = l.b.align == "left" ? 0 : l.b.align == "right" ? l.width - across : (l.width - across) / 2;
				l.own = placed{left, across, down};
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
			}
		};

	}  // namespace

	const kind& picture_kind() {
		static const picture k;
		return k;
	}

}  // namespace one::desktop::runtime::paper_kinds
