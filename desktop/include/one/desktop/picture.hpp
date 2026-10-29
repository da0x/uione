// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture, decoded once and shared: its pixels, eight bits each of red, green,
// blue and alpha in sRGB, row after row. Copies share the pixels, and the renderer
// keeps one texture for them however often and wherever they're drawn.
//
// PNG, JPEG, GIF, BMP and TGA are read, and PNG written, by stb_image and
// stb_image_write, pinned in src/stb and compiled in, so an app needs nothing more
// installed to show a picture.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace one::desktop {

	// One shape of an SVG broken apart, as the pinned nanosvg reads it: its path, its
	// points where they are in the SVG's own units, with its transforms applied, in SVG
	// path notation, M, C and Z; its fill and stroke, each #rrggbb or none, a gradient's
	// its first stop's color; its stroke's width; and how much of it shows.
	struct svg_shape {
		std::string path;
		std::string fill = "none", stroke = "none";
		double stroke_width = 0;
		double opacity = 1;
	};
	// Its shapes, lowest first, those it says aren't seen left out.
	std::vector<svg_shape> svg_shapes(std::string_view svg);

	class canvas;

	class picture {
	public:
		// The most a picture may hold, 256 million pixels; one larger is refused rather
		// than taking all the memory there is.
		static constexpr std::int64_t most_pixels = 256'000'000;
		// The longest side drawn; a longer picture is halved until it fits.
		static constexpr int longest_side = 16384;

		picture() = default;
		// Read from a file, or from its bytes, as a picture pasted arrives; why not,
		// in words a person can act on, when it can't be.
		static std::expected<picture, std::string> read(const std::filesystem::path& file);
		static std::expected<picture, std::string> decode(std::span<const std::uint8_t> bytes);
		// How big a picture in a file is, as its file says, without reading all of it: what
		// a picture coming in is measured by. Nothing when it isn't one this can read.
		static std::optional<std::pair<int, int>> size_of(const std::filesystem::path& file);
		// Pixels made by the app, `width` × `height`, four bytes each.
		static picture of(int width, int height, std::vector<std::uint8_t> rgba);
		// Drawn with no window, as a canvas in one is, `width` × `height` pixels on
		// nothing: what a drawing app saves, or keeps as a small preview of its work.
		static picture drawn(int width, int height, const std::function<void(canvas&)>& paint);
		// An SVG document drawn, `width` × `height` pixels, its width stretched to them, its
		// edges smooth, by the pinned nanosvg (decision 0025): what a collage's shapes are
		// drawn from. Nothing when it can't be read.
		static picture from_svg(std::string_view svg, int width, int height);
		// Whether bytes are an SVG document, which is drawn from its vectors rather than
		// read as pixels; and its own size, as nanosvg reads it, when it can be.
		static bool is_svg(std::span<const std::uint8_t> bytes);
		static std::optional<std::pair<double, double>> svg_size(std::string_view svg);

		// A smaller copy, its longer side at most `longest`, each pixel the average of those
		// it covers: what's drawn where the whole of a large one would be wasted, like a card.
		// Itself when it's that small already.
		picture smaller(int longest) const;

		int width() const { return pixels_ ? pixels_->width : 0; }
		int height() const { return pixels_ ? pixels_->height : 0; }
		const std::vector<std::uint8_t>& rgba() const;
		explicit operator bool() const { return pixels_ != nullptr; }

		// Written as a PNG, compressed: to a file, or as its bytes, as the clipboard
		// offers a picture copied.
		std::expected<void, std::string> write_png(const std::filesystem::path& file) const;
		std::vector<std::uint8_t> png() const;

		// What every copy shares, for whatever keeps something per picture, like a
		// texture, to know the picture by and to see it's gone.
		std::weak_ptr<const void> shared() const { return pixels_; }

	private:
		struct pixels {
			int width = 0, height = 0;
			std::vector<std::uint8_t> rgba;
		};
		std::shared_ptr<const pixels> pixels_;
	};

}  // namespace one::desktop
