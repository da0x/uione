// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/picture.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

	bool picture::is_svg(std::span<const std::uint8_t> bytes) {
		std::string_view text(reinterpret_cast<const char*>(bytes.data()), std::min<std::size_t>(bytes.size(), 4096));
		if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
		std::size_t first = text.find_first_not_of(" \t\r\n");
		return first != std::string_view::npos && text[first] == '<' && text.find("<svg") != std::string_view::npos;
	}

	std::optional<std::pair<double, double>> picture::svg_size(std::string_view svg) {
		std::string text(svg);
		std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> image(nsvgParse(text.data(), "px", 96), nsvgDelete);
		if (!image || image->width <= 0 || image->height <= 0) return std::nullopt;
		return std::pair<double, double>{image->width, image->height};
	}

	namespace {
		// A paint as a color, #rrggbb, and how much of it shows, 0 to 1; none and 0 for
		// none. A gradient's is its first stop's.
		std::pair<std::string, double> color_of(const NSVGpaint& paint) {
			unsigned int color = 0;
			if (paint.type == NSVG_PAINT_COLOR) color = paint.color;
			else if ((paint.type == NSVG_PAINT_LINEAR_GRADIENT || paint.type == NSVG_PAINT_RADIAL_GRADIENT) && paint.gradient && paint.gradient->nstops > 0) color = paint.gradient->stops[0].color;
			else return {"none", 0};
			char hex[8];
			std::snprintf(hex, sizeof hex, "#%02x%02x%02x", color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff);
			return {hex, static_cast<double>((color >> 24) & 0xff) / 255};
		}

		std::string number(float n) {
			char out[32];
			std::snprintf(out, sizeof out, "%g", static_cast<double>(n));
			return out;
		}
	}  // namespace

	std::vector<svg_shape> svg_shapes(std::string_view svg) {
		std::string text(svg);
		std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> image(nsvgParse(text.data(), "px", 96), nsvgDelete);
		std::vector<svg_shape> out;
		if (!image) return out;
		for (NSVGshape* s = image->shapes; s; s = s->next) {
			if (!(s->flags & NSVG_FLAGS_VISIBLE)) continue;
			std::string path;
			for (NSVGpath* p = s->paths; p; p = p->next) {
				if (p->npts < 1) continue;
				path += std::string(path.empty() ? "" : " ") + "M " + number(p->pts[0]) + " " + number(p->pts[1]);
				for (int i = 1; i + 2 < p->npts; i += 3) {
					const float* at = &p->pts[i * 2];
					path += " C " + number(at[0]) + " " + number(at[1]) + " " + number(at[2]) + " " + number(at[3]) + " " + number(at[4]) + " " + number(at[5]);
				}
				if (p->closed) path += " Z";
			}
			if (path.empty()) continue;
			auto [fill, filled] = color_of(s->fill);
			auto [stroke, stroked] = color_of(s->stroke);
			// How much shows: its own opacity, by its fill's, or its stroke's when it has no fill.
			double shows = static_cast<double>(s->opacity) * (fill != "none" ? filled : stroked);
			out.push_back({path, fill, stroke, stroke == "none" ? 0 : static_cast<double>(s->strokeWidth), shows});
		}
		return out;
	}

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
