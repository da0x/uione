// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Drawing without a window, for tests: a canvas drawn into an image on the GPU and
// read back, and that image kept as a PNG.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include "one/desktop/app.hpp"
#include "one/desktop/canvas.hpp"

namespace one::desktop::offscreen {

	// Eight bits each of red, green, blue and alpha, in sRGB, row after row.
	struct image {
		int width = 0, height = 0;
		std::vector<std::uint8_t> rgba;
	};

	// Draws at a size in logical pixels and a scale, over a background.
	image draw(one::desktop::size size, double scale, color background, const std::function<void(canvas&)>& paint);

	// PNG, written uncompressed so nothing but this is needed to read it back.
	void write_png(const std::filesystem::path& file, const image& picture);
	std::optional<image> read_png(const std::filesystem::path& file);

	// How many pixels differ by more than `tolerance` in any channel.
	std::size_t differences(const image& a, const image& b, int tolerance);

}  // namespace one::desktop::offscreen
