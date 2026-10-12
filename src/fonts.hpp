// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Fonts, kept for the whole process: each face fontconfig finds for a style, open in
// FreeType at one pixel size with a HarfBuzz font over it, and the faces that have
// a character the first one lacks.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "loom/text.hpp"

typedef struct FT_FaceRec_* FT_Face;
typedef struct hb_font_t hb_font_t;

namespace loom::fonts {

	struct face {
		FT_Face freetype = nullptr;
		hb_font_t* harfbuzz = nullptr;
		float pixel_size = 0;
		float ascent = 0, descent = 0;  // in pixels, both positive
		float underline_position = 0, underline_thickness = 1;
		std::string file;
		~face();
	};

	// The face for a style at a size in pixels, and the one to use for a character
	// that face lacks: itself when it has it, or the first of fontconfig's choices
	// that does.
	const face& find(const text_style& style, float pixel_size);
	const face& for_character(const face& first, const text_style& style, char32_t character);

	// A glyph drawn in coverage, its origin moved right by a fraction of a pixel.
	struct bitmap {
		int width = 0, height = 0;
		int left = 0, top = 0;  // from the pen's position to its top left, y up
		std::vector<std::uint8_t> coverage;
	};
	bitmap draw(const face& f, std::uint32_t glyph, float subpixel);

}  // namespace loom::fonts
