// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "fonts.hpp"
#include "owned.hpp"

namespace one::desktop::fonts {

	namespace {

		struct library {
			FT_Library freetype = nullptr;
			FcConfig* config = nullptr;
			std::mutex lock;
			std::map<std::string, std::unique_ptr<face>> faces;           // by file, index and size
			std::map<std::string, std::pair<std::string, int>> matched;   // a style's file and index
			std::map<std::pair<std::string, char32_t>, std::pair<std::string, int>> fallbacks;  // by style and character

			library() {
				if (FT_Init_FreeType(&freetype)) throw std::runtime_error("FreeType didn't start");
				config = FcInitLoadConfigAndFonts();
			}
			~library() {
				faces.clear();
				FT_Done_FreeType(freetype);
				FcConfigDestroy(config);
			}
		};

		library& fonts() {
			static library one;
			return one;
		}

		using pattern = owned<FcPattern, FcPatternDestroy>;
		using character_set = owned<FcCharSet, FcCharSetDestroy>;

		// A pattern asking for a style, to which fontconfig adds what it's configured to.
		pattern asking(const text_style& style) {
			pattern p = FcPatternCreate();
			FcPatternAddString(p, FC_FAMILY, reinterpret_cast<const FcChar8*>(style.family.c_str()));
			FcPatternAddInteger(p, FC_WEIGHT, FcWeightFromOpenType(style.weight));
			FcPatternAddInteger(p, FC_SLANT, style.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
			FcPatternAddBool(p, FC_COLOR, FcFalse);
			return p;
		}

		std::pair<std::string, int> file_of(FcPattern* found) {
			FcChar8* file = nullptr;
			int index = 0;
			if (!found || FcPatternGetString(found, FC_FILE, 0, &file) != FcResultMatch) return {"", 0};
			FcPatternGetInteger(found, FC_INDEX, 0, &index);
			return {reinterpret_cast<const char*>(file), index};
		}

		const face& opened(library& l, const std::pair<std::string, int>& where, float pixel_size) {
			std::string key = std::format("{}#{}@{}", where.first, where.second, pixel_size);
			if (auto it = l.faces.find(key); it != l.faces.end()) return *it->second;
			auto f = std::make_unique<face>();
			f->file = where.first;
			if (FT_New_Face(l.freetype, where.first.c_str(), where.second, &f->freetype)) throw std::runtime_error("FreeType can't open " + where.first);
			FT_Set_Char_Size(f->freetype, 0, static_cast<FT_F26Dot6>(std::lround(pixel_size * 64)), 72, 72);
			f->harfbuzz = hb_ft_font_create_referenced(f->freetype);
			hb_ft_font_set_load_flags(f->harfbuzz, FT_LOAD_TARGET_LIGHT);
			f->pixel_size = pixel_size;
			const auto& metrics = f->freetype->size->metrics;
			f->ascent = metrics.ascender / 64.f;
			f->descent = -metrics.descender / 64.f;
			float units = static_cast<float>(f->freetype->units_per_EM);
			if (units > 0 && FT_IS_SCALABLE(f->freetype)) {
				f->underline_position = -f->freetype->underline_position * pixel_size / units;
				f->underline_thickness = std::max(1.f, f->freetype->underline_thickness * pixel_size / units);
			} else {
				f->underline_position = f->descent / 2;
			}
			return *l.faces.emplace(key, std::move(f)).first->second;
		}

		std::string style_key(const text_style& style) { return std::format("{}/{}/{}", style.family, style.weight, style.italic); }

	}  // namespace

	face::~face() {
		if (harfbuzz) hb_font_destroy(harfbuzz);  // which lets go of the FreeType face it references
	}

	const face& find(const text_style& style, float pixel_size) {
		library& l = fonts();
		std::lock_guard hold(l.lock);
		std::string key = style_key(style);
		auto it = l.matched.find(key);
		if (it == l.matched.end()) {
			pattern p = asking(style);
			FcConfigSubstitute(l.config, p, FcMatchPattern);
			FcDefaultSubstitute(p);
			FcResult result;
			pattern found = FcFontMatch(l.config, p, &result);
			auto where = file_of(found);
			if (where.first.empty()) throw std::runtime_error("fontconfig found no font for " + style.family);
			it = l.matched.emplace(key, where).first;
		}
		return opened(l, it->second, pixel_size);
	}

	const face& for_character(const face& first, const text_style& style, char32_t character) {
		if (FT_Get_Char_Index(first.freetype, character) != 0) return first;
		library& l = fonts();
		std::lock_guard hold(l.lock);
		auto key = std::pair{style_key(style), character};
		auto it = l.fallbacks.find(key);
		if (it == l.fallbacks.end()) {
			pattern p = asking(style);
			character_set wanted = FcCharSetCreate();
			FcCharSetAddChar(wanted, character);
			FcPatternAddCharSet(p, FC_CHARSET, wanted);
			FcConfigSubstitute(l.config, p, FcMatchPattern);
			FcDefaultSubstitute(p);
			FcResult result;
			pattern found = FcFontMatch(l.config, p, &result);
			// Only a face that has it; otherwise the first, which draws its missing glyph.
			FcCharSet* has = nullptr;
			std::pair<std::string, int> where{first.file, static_cast<int>(first.freetype->face_index)};
			if (found && FcPatternGetCharSet(found, FC_CHARSET, 0, &has) == FcResultMatch && FcCharSetHasChar(has, character)) where = file_of(found);
			it = l.fallbacks.emplace(key, where).first;
		}
		return opened(l, it->second, first.pixel_size);
	}

	bitmap draw(const face& f, std::uint32_t glyph, float subpixel) {
		library& l = fonts();
		std::lock_guard hold(l.lock);
		bitmap out;
		if (FT_Load_Glyph(f.freetype, glyph, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)) return out;
		FT_GlyphSlot slot = f.freetype->glyph;
		if (slot->format == FT_GLYPH_FORMAT_OUTLINE) FT_Outline_Translate(&slot->outline, static_cast<FT_Pos>(std::lround(subpixel * 64)), 0);
		if (FT_Render_Glyph(slot, FT_RENDER_MODE_LIGHT)) return out;
		const FT_Bitmap& b = slot->bitmap;
		out.width = static_cast<int>(b.width);
		out.height = static_cast<int>(b.rows);
		out.left = slot->bitmap_left;
		out.top = slot->bitmap_top;
		out.coverage.resize(static_cast<std::size_t>(out.width) * out.height);
		for (int y = 0; y < out.height; ++y) {
			const unsigned char* row = b.buffer + static_cast<std::ptrdiff_t>(y) * b.pitch;
			std::copy_n(row, out.width, out.coverage.data() + static_cast<std::size_t>(y) * out.width);
		}
		return out;
	}

}  // namespace one::desktop::fonts

namespace one::desktop {

	namespace {

		char32_t next_character(std::string_view text, std::size_t& i) {
			unsigned char c = static_cast<unsigned char>(text[i]);
			int more = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
			char32_t point = more == 0 ? c : more == 1 ? c & 0x1f : more == 2 ? c & 0x0f : c & 0x07;
			++i;
			for (int k = 0; k < more && i < text.size(); ++k, ++i) point = (point << 6) | (static_cast<unsigned char>(text[i]) & 0x3f);
			return point;
		}

		// A word and the space after it, shaped, in one style: what a line is filled with.
		struct word {
			std::vector<placed_glyph> glyphs;  // x from the word's start
			float width = 0;                   // without the trailing space
			float advance = 0;                 // with it
			bool breaks_after = false;         // a newline ends the line after it
			const fonts::face* first = nullptr;
			const text_style* style = nullptr;
		};

		// Shapes a piece of text in its style, splitting it where a character needs
		// another face.
		void shape(std::string_view text, const text_style& style, const fonts::face& first, word& into) {
			owned<hb_buffer_t, hb_buffer_destroy> buffer = hb_buffer_create();
			std::size_t i = 0;
			while (i < text.size()) {
				std::size_t start = i;
				const fonts::face* f = &fonts::for_character(first, style, next_character(text, i));
				std::size_t end = i;
				while (end < text.size()) {
					std::size_t peek = end;
					if (&fonts::for_character(first, style, next_character(text, peek)) != f) break;
					end = peek;
				}
				i = end;
				hb_buffer_clear_contents(buffer);
				hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), static_cast<unsigned>(start), static_cast<int>(end - start));
				hb_buffer_guess_segment_properties(buffer);
				hb_shape(f->harfbuzz, buffer, nullptr, 0);
				unsigned count = 0;
				hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, &count);
				hb_glyph_position_t* position = hb_buffer_get_glyph_positions(buffer, &count);
				for (unsigned g = 0; g < count; ++g) {
					into.glyphs.push_back({f, info[g].codepoint, into.advance + position[g].x_offset / 64.f, -position[g].y_offset / 64.f, style.color});
					into.advance += position[g].x_advance / 64.f;
				}
			}
		}

	}  // namespace

	text_layout lay_out(std::span<const text_span> spans, float width, double scale) {
		text_layout out;
		out.scale_ = scale;
		float limit = width > 0 ? static_cast<float>(width * scale) : 0;

		// Words, each a run of text up to and including the spaces after it.
		std::vector<word> words;
		for (const auto& span : spans) {
			float pixel_size = static_cast<float>(span.style.size * scale);
			const fonts::face& first = fonts::find(span.style, pixel_size);
			std::string_view text = span.text;
			std::size_t i = 0;
			while (i < text.size()) {
				std::size_t end = i;
				while (end < text.size() && text[end] != ' ' && text[end] != '\n') ++end;
				std::size_t word_end = end;
				while (end < text.size() && text[end] == ' ') ++end;
				word w;
				w.first = &first;
				w.style = &span.style;
				shape(text.substr(i, word_end - i), span.style, first, w);
				w.width = w.advance;
				if (end > word_end) {
					word spaces;
					shape(text.substr(word_end, end - word_end), span.style, first, spaces);
					w.advance += spaces.advance;
				}
				if (end < text.size() && text[end] == '\n') {
					w.breaks_after = true;
					++end;
				}
				i = end;
				words.push_back(std::move(w));
			}
		}

		// Lines, filled with words while they fit.
		float top = 0;
		std::size_t at = 0;
		auto finish = [&](std::size_t from, std::size_t to) {
			float ascent = 0, descent = 0, height = 0, x = 0, line_width = 0;
			for (std::size_t k = from; k < to; ++k) {
				ascent = std::max(ascent, words[k].first->ascent);
				descent = std::max(descent, words[k].first->descent);
				height = std::max(height, static_cast<float>(words[k].style->size * words[k].style->line_height * scale));
			}
			if (from == to && !spans.empty()) {
				const auto& style = spans.back().style;
				const auto& f = fonts::find(style, static_cast<float>(style.size * scale));
				ascent = f.ascent, descent = f.descent, height = static_cast<float>(style.size * style.line_height * scale);
			}
			height = std::max(height, ascent + descent);
			float baseline = std::round(top + (height - ascent - descent) / 2 + ascent);
			for (std::size_t k = from; k < to; ++k) {
				const word& w = words[k];
				for (const auto& g : w.glyphs) out.glyphs_.push_back({g.face, g.id, x + g.x, baseline + g.y, g.color});
				float drawn = k + 1 == to ? w.width : w.advance;
				if (w.style->underline || w.style->strike) {
					const auto& f = *w.first;
					float y = w.style->underline ? baseline + f.underline_position : baseline - f.ascent * 0.32f;
					out.rules_.push_back({x, std::round(y), drawn, std::round(f.underline_thickness), w.style->color});
				}
				line_width = x + w.width;
				x += w.advance;
			}
			out.lines_.push_back({top, baseline, height, line_width});
			out.width_ = std::max(out.width_, line_width);
			top += height;
		};
		while (at < words.size()) {
			std::size_t end = at;
			float x = 0;
			while (end < words.size()) {
				const word& w = words[end];
				if (end > at && limit > 0 && x + w.width > limit) break;
				x += w.advance;
				++end;
				if (w.breaks_after) break;
			}
			finish(at, end);
			at = end;
		}
		if (words.empty()) finish(0, 0);
		out.height_ = top;
		return out;
	}

}  // namespace one::desktop
