// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The kinds that are words alone, set in the page's face: a title, a subtitle, a
// heading ruled under across the line, text, and a quote with a bar beside it.

#include "kinds.hpp"

namespace one::desktop::runtime::paper_kinds {

	namespace {

		// Words in the page's face, a typewriter's a size smaller but for headings, as its
		// letters are wide.
		text_style in_face(text_style s, const setting& at, bool heading) {
			s.family = family_of(at.face, at.look);
			if (at.face == "typewriter" && !heading) s.size -= 1;
			return s;
		}

		class title final : public kind {
		public:
			text_style words_look(const setting& at) const override {
				text_style s = at.look.heading(32);
				s.weight = 700;
				s.line_height = 1.2f;
				return in_face(s, at, true);
			}
			std::pair<float, float> room() const override { return {0, 6}; }
			bool leads() const override { return true; }
			int outlined() const override { return 1; }
		};

		class subtitle final : public kind {
		public:
			text_style words_look(const setting& at) const override {
				text_style s = at.look.body();
				s.size = 19;
				s.color = at.look.muted;
				s.line_height = 1.4f;
				return in_face(s, at, false);
			}
			std::pair<float, float> room() const override { return {0, 22}; }
			bool leads() const override { return true; }
		};

		// A heading is ruled under, across the whole line, its words aligned over it.
		class heading final : public kind {
		public:
			static constexpr float rule = 9;  // room, then a line
			text_style words_look(const setting& at) const override { return in_face(at.look.heading(22), at, true); }
			std::pair<float, float> room() const override { return {30, 12}; }
			bool leads() const override { return true; }
			int outlined() const override { return 2; }
			void lay_out(laid& l, const setting& at, bool) const override { l.height += rule * at.zoom; }
			void paint_front(canvas& c, const laid& l, point at, const setting& s) const override {
				c.box({at.x, at.y + l.height - 1, l.width, 1}, {.fill = s.look.line});
			}
		};

		// Text, which "# " makes a heading and "> " a quote as it's typed.
		class text final : public kind {
		public:
			text_style words_look(const setting& at) const override {
				text_style s = at.look.body();
				s.size = 16;
				s.line_height = 1.6f;
				return in_face(s, at, false);
			}
			std::optional<std::string> shortcut(char first) const override {
				if (first == '#') return "heading";
				if (first == '>') return "quote";
				return std::nullopt;
			}
		};

		// A quote, quieter and slanted, a bar down its side.
		class quote final : public kind {
		public:
			text_style words_look(const setting& at) const override {
				text_style s = at.look.body();
				s.size = 16;
				s.line_height = 1.6f;
				s.italic = true;
				s.color = at.look.muted;
				return in_face(s, at, false);
			}
			float indent() const override { return 20; }
			void paint_behind(canvas& c, const laid& l, point at, const setting& s) const override {
				c.box({at.x - indent() * s.zoom, at.y + 4, 3, std::max(0.f, l.height - 8)}, {.fill = s.look.line, .radius = 1.5f});
			}
		};

	}  // namespace

	const kind& title_kind() {
		static const title k;
		return k;
	}
	const kind& subtitle_kind() {
		static const subtitle k;
		return k;
	}
	const kind& heading_kind() {
		static const heading k;
		return k;
	}
	const kind& text_kind() {
		static const text k;
		return k;
	}
	const kind& quote_kind() {
		static const quote k;
		return k;
	}

}  // namespace one::desktop::runtime::paper_kinds
