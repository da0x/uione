// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Text: spans of styled words, shaped by HarfBuzz in fonts fontconfig finds, and laid
// out in lines that fit a width. A layout is made for one scale, so its glyphs land
// on whole pixels where they should and are drawn sharp.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "one/desktop/app.hpp"

namespace one::desktop {

	struct text_style {
		std::string family = "sans-serif";  // a fontconfig family, like "IBM Plex Sans", or sans-serif, serif, monospace
		float size = 15;                    // in logical pixels
		int weight = 400;                   // 400 regular, 600 semibold, 700 bold
		bool italic = false;
		one::desktop::color color = color::hex(0x0e1726);
		bool underline = false;
		bool strike = false;
		float line_height = 1.45f;  // of size, the distance between lines' baselines
	};

	// Words in one style.
	struct text_span {
		std::string text;
		text_style style;
	};

	// One glyph, placed in pixels from the layout's top left, at the layout's scale.
	struct placed_glyph {
		const void* face = nullptr;  // the font it's from, at its pixel size
		std::uint32_t id = 0;
		float x = 0, y = 0;  // its origin on the baseline
		one::desktop::color color;
	};

	// A line under or through some glyphs, in pixels.
	struct text_rule {
		float x = 0, y = 0, width = 0, thickness = 1;
		one::desktop::color color;
	};

	struct text_line {
		float top = 0;       // in pixels from the layout's top
		float baseline = 0;  // likewise
		float height = 0;
		float width = 0;
	};

	class text_layout {
	public:
		// Its size in logical pixels.
		float width() const { return width_ / static_cast<float>(scale_); }
		float height() const { return height_ / static_cast<float>(scale_); }
		double scale() const { return scale_; }
		const std::vector<text_line>& lines() const { return lines_; }
		const std::vector<placed_glyph>& glyphs() const { return glyphs_; }
		const std::vector<text_rule>& rules() const { return rules_; }

	private:
		friend text_layout lay_out(std::span<const text_span>, float, double);
		double scale_ = 1;
		float width_ = 0, height_ = 0;  // in pixels
		std::vector<text_line> lines_;
		std::vector<placed_glyph> glyphs_;
		std::vector<text_rule> rules_;
	};

	// Lays spans out within a width in logical pixels, breaking lines between words
	// and at newlines; 0 means one line for each paragraph, however long.
	text_layout lay_out(std::span<const text_span> spans, float width, double scale);
	inline text_layout lay_out(const text_span& span, float width, double scale) { return lay_out(std::span(&span, 1), width, scale); }

}  // namespace one::desktop
