// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/picture.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

// nanosvg's own code, compiled here once: what reads an SVG document and draws it.
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <cmath>
#include <cstdio>
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"
#pragma GCC diagnostic pop

namespace one::desktop {

	picture picture::from_svg(std::string_view svg, int width, int height) {
		if (width <= 0 || height <= 0 || static_cast<std::int64_t>(width) * height > most_pixels) return {};
		// nanosvg reads text it may write into, so it's given its own copy.
		std::string text(svg);
		std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> image(nsvgParse(text.data(), "px", 96), nsvgDelete);
		if (!image || image->width <= 0) return {};
		std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> drawer(nsvgCreateRasterizer(), nsvgDeleteRasterizer);
		if (!drawer) return {};
		std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
		nsvgRasterize(drawer.get(), image.get(), 0, 0, static_cast<float>(width) / image->width, rgba.data(), width, height, width * 4);
		return of(width, height, std::move(rgba));
	}

}  // namespace one::desktop
