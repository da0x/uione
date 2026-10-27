// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

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
		float left = 0;      // where it starts, moved across by how it's aligned
	};

	// Where each line sits across the width it's laid out in.
	enum class text_align { left, center, right };

	// Where a caret can stand: before the character at a byte of the text, every
	// span's text taken together, on a line, at an x in pixels. The text's end is one.
	struct text_caret {
		std::size_t at = 0;
		std::size_t line = 0;
		float x = 0;
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
		// Every place a caret can stand, in the order of the text.
		const std::vector<text_caret>& carets() const { return carets_; }
		// Where the caret stands before a byte: at the start of a line the text wraps
		// onto, rather than the end of the one before.
		text_caret caret(std::size_t at) const;
		// The byte nearest a point in pixels from the layout's top left.
		std::size_t offset_at(float x, float y) const;

	private:
		friend text_layout lay_out(std::span<const text_span>, float, double, text_align);
		double scale_ = 1;
		float width_ = 0, height_ = 0;  // in pixels
		std::vector<text_line> lines_;
		std::vector<placed_glyph> glyphs_;
		std::vector<text_rule> rules_;
		std::vector<text_caret> carets_;
	};

	// A font the app brings with it, from its own bytes, as one compiled into the app
	// is: offered with the system's, by the family it names itself, so a style that
	// asks for that family draws in it. Whether it could be read.
	bool use_font(std::span<const std::uint8_t> bytes);

	// Lays spans out within a width in logical pixels, breaking lines between words
	// and at newlines; 0 means one line for each paragraph, however long. Safe from
	// any thread, as drawing a picture with no window away from the window's is.
	// Each line aligned within the width, or within the widest line when there's none.
	text_layout lay_out(std::span<const text_span> spans, float width, double scale, text_align align = text_align::left);
	inline text_layout lay_out(const text_span& span, float width, double scale, text_align align = text_align::left) {
		return lay_out(std::span(&span, 1), width, scale, align);
	}

}  // namespace one::desktop
