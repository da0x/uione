// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An equation drawn as one::math sets it: its glyphs in the math font, which the app
// brings with it, its rules and its lines, each where the layout puts it, as the web
// draws the same equation.

#include <cstdint>
#include <string>

#include <one/math.hpp>
#include <one/math_layout.hpp>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		// The math font, built into the app, offered once.
		void use_math_font() {
			static bool used = [] {
				static constexpr unsigned char font[] = {
#embed "../../../math/font/noto-sans-math.ttf"
				};
				return desktop::use_font(std::span<const std::uint8_t>(font, sizeof font));
			}();
			(void)used;
		}

		std::string utf8(std::uint32_t code) {
			std::string out;
			if (code < 0x80) out += static_cast<char>(code);
			else if (code < 0x800) out += static_cast<char>(0xC0 | (code >> 6)), out += static_cast<char>(0x80 | (code & 0x3F));
			else if (code < 0x10000) out += static_cast<char>(0xE0 | (code >> 12)), out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)), out += static_cast<char>(0x80 | (code & 0x3F));
			else {
				out += static_cast<char>(0xF0 | (code >> 18)), out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)), out += static_cast<char>(0x80 | (code & 0x3F));
			}
			return out;
		}

	}  // namespace

	set_equation equation_set(const std::string& written, float em, bool displayed, color ink) {
		auto read = math::parse(written);
		if (!read) return {};
		use_math_font();
		math::laid_out laid = math::lay_out(*read, em, displayed);
		set_equation out{.read = true, .width = static_cast<float>(laid.width), .ascent = static_cast<float>(laid.ascent), .descent = static_cast<float>(laid.descent)};
		out.draw = [laid, ink](desktop::canvas& c, desktop::point at) {
			float x0 = at.x, baseline = at.y + static_cast<float>(laid.ascent);
			for (const auto& d : laid.drawn) {
				if (d.type == math::placed::kind::glyph) {
					text_style s{.family = math::measures::font, .size = static_cast<float>(d.size), .color = ink, .line_height = 1};
					auto glyph = desktop::lay_out(text_span{utf8(d.code), s}, 0, c.scale());
					float top = glyph.lines().empty() ? 0 : glyph.lines().front().baseline / static_cast<float>(c.scale());
					c.text(glyph, {x0 + static_cast<float>(d.x), baseline + static_cast<float>(d.y) - top});
				} else if (d.type == math::placed::kind::rule) {
					c.box({x0 + static_cast<float>(d.x), baseline + static_cast<float>(d.y), static_cast<float>(d.width), static_cast<float>(d.height)}, {.fill = ink});
				} else {
					for (std::size_t i = 0; i + 1 < d.points.size(); ++i) {
						auto [ax, ay] = d.points[i];
						auto [bx, by] = d.points[i + 1];
						c.line({x0 + static_cast<float>(ax), baseline + static_cast<float>(ay)}, {x0 + static_cast<float>(bx), baseline + static_cast<float>(by)}, static_cast<float>(d.size), ink);
					}
				}
			}
		};
		return out;
	}

	desktop::view equation(std::string written, bool displayed) {
		const auto& t = current_theme();
		set_equation set = equation_set(written, t.text_size * (displayed ? 1.25f : 1.1f), displayed, t.ink);
		// One that can't be read is shown as it's written, saying so in its color.
		if (!set.read) {
			auto s = t.code();
			s.color = t.danger;
			return desktop::text(std::move(written), s);
		}
		// In a row, as a drawing alone takes the whole width it's given.
		return desktop::row(desktop::drawing([set](desktop::canvas& c, desktop::rect area) { set.draw(c, {area.x, area.y}); }).width(set.width).height(set.ascent + set.descent))
		    .baseline(set.ascent);
	}

}  // namespace one::desktop::runtime
