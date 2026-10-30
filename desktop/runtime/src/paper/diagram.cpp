// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A block diagram: its words in one::diagram's notation, a line at a time, drawn as its
// blocks and the wires between them, laid out by itself. While the caret's in it, it's
// its words, in a well, Enter starting a line; otherwise its drawing, as wide as the page
// at most. Written apart, it's drawn over its words as they're typed.

#include "kinds.hpp"

namespace one::desktop::runtime::paper_kinds {

	namespace {

		struct set {
			set_diagram diagram;
			float down = 0;  // from the block's top
		};

		class diagram final : public kind {
		public:
			text_style words_look(const setting& at) const override {
				text_style s = at.look.code();
				s.line_height = 1.5f;
				return s;
			}
			std::pair<float, float> room() const override { return {10, 18}; }
			bool joins() const override { return false; }
			on_enter enter(const std::string& words, std::size_t caret, bool selecting) const override {
				if (!selecting && caret == words.size() && !words.empty() && words.back() == '\n') return on_enter::leave;
				return on_enter::new_line;
			}
			const written_apart* apart() const override {
				static const written_apart apart{
				    "Diagram",
				    {{"Blocks",
				      {{"Input", "\"in x\"", "in ‸"},
				       {"Output", "\"out y\"", "out ‸"},
				       {"Into", "a -> b", " -> "},
				       {"Gain", "k", " -> *‸"},
				       {"Integrator", "1/s", " -> 1/s"},
				       {"Transfer function", "(s + 1)/(s + 2)", " -> (‸)/()"},
				       {"Plus", "a + b", " + "},
				       {"Minus", "a - b", " - "},
				       {"Name under it", "\"name\"", " \"‸\""},
				       {"Block of its own", "f(a -> b)", "‸( -> )"}}}},
				    [](const std::string& words) { return words; },
				    {},
				    false,
				    true,
				    false,
				    [](const std::string& words, float em) -> std::expected<desktop::view, std::string> {
					    auto set = diagram_set(words, em, current_theme().ink);
					    if (!set.read) return std::unexpected(set.problem);
					    return desktop::row(desktop::drawing([set](desktop::canvas& c, desktop::rect area) { set.draw(c, {area.x, area.y}); }).width(set.width).height(set.height));
				    },
				};
				return &apart;
			}

			void lay_out(laid& l, const setting& at, bool here) const override {
				if (l.b.words.empty()) return;
				float em = at.look.text_size * 1.25f * at.zoom;
				set made{diagram_set(l.b.words, em, at.look.ink), 0};
				if (!made.diagram.read) return;
				// Smaller, to fit across the page.
				if (made.diagram.width > l.width && l.width > 0) made.diagram = diagram_set(l.b.words, em * l.width / made.diagram.width, at.look.ink);
				float tall = made.diagram.height;
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
				float w = made->diagram.width;
				float x = l.b.align == "left" ? at.x : l.b.align == "right" ? at.x + l.width - w : at.x + (l.width - w) / 2;
				made->diagram.draw(c, {x, at.y + made->down});
			}
		};

	}  // namespace

	const kind& diagram_kind() {
		static const diagram k;
		return k;
	}

}  // namespace one::desktop::runtime::paper_kinds
