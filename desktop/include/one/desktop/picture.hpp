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
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace one::desktop {

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
		// Pixels made by the app, `width` × `height`, four bytes each.
		static picture of(int width, int height, std::vector<std::uint8_t> rgba);

		int width() const { return pixels_ ? pixels_->width : 0; }
		int height() const { return pixels_ ? pixels_->height : 0; }
		const std::vector<std::uint8_t>& rgba() const;
		explicit operator bool() const { return pixels_ != nullptr; }

		// Written as a PNG, compressed.
		std::expected<void, std::string> write_png(const std::filesystem::path& file) const;

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
