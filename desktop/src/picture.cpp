// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/picture.hpp"

#include "offscreen.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

// stb's own code, compiled here once: what reads pictures and what writes PNG.
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#pragma GCC diagnostic pop

namespace one::desktop {

	namespace {

		// Half as wide and as tall, each pixel the average of the four it covers.
		void halve(int& width, int& height, std::vector<std::uint8_t>& rgba) {
			int w = std::max(1, width / 2), h = std::max(1, height / 2);
			std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					for (int c = 0; c < 4; ++c) {
						int sum = 0;
						for (int dy = 0; dy < 2; ++dy) {
							for (int dx = 0; dx < 2; ++dx) {
								int sx = std::min(width - 1, 2 * x + dx), sy = std::min(height - 1, 2 * y + dy);
								sum += rgba[(static_cast<std::size_t>(sy) * width + sx) * 4 + c];
							}
						}
						out[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
					}
				}
			}
			width = w, height = h;
			rgba = std::move(out);
		}

		// Turned and mirrored the way up a camera says it was held, 2 to 8 as EXIF counts
		// them, by tiles, so what's read and what's written stay near each other.
		void upright(int& width, int& height, std::vector<std::uint8_t>& rgba, int orientation) {
			if (orientation < 2 || orientation > 8) return;
			const int w = width, h = height;
			bool turned = orientation >= 5;
			int ow = turned ? h : w, oh = turned ? w : h;
			std::vector<std::uint8_t> out(rgba.size());
			const std::uint8_t* from = rgba.data();
			std::uint8_t* to = out.data();
			constexpr int tile = 64;
			for (int ty = 0; ty < oh; ty += tile) {
				for (int tx = 0; tx < ow; tx += tile) {
					for (int y = ty; y < std::min(oh, ty + tile); ++y) {
						for (int x = tx; x < std::min(ow, tx + tile); ++x) {
							int sx = 0, sy = 0;
							switch (orientation) {
								case 2: sx = w - 1 - x, sy = y; break;
								case 3: sx = w - 1 - x, sy = h - 1 - y; break;
								case 4: sx = x, sy = h - 1 - y; break;
								case 5: sx = y, sy = x; break;
								case 6: sx = y, sy = h - 1 - x; break;
								case 7: sx = w - 1 - y, sy = h - 1 - x; break;
								default: sx = w - 1 - y, sy = x; break;
							}
							std::memcpy(to + (static_cast<std::size_t>(y) * ow + x) * 4, from + (static_cast<std::size_t>(sy) * w + sx) * 4, 4);
						}
					}
				}
			}
			width = ow, height = oh;
			rgba = std::move(out);
		}

	}  // namespace

	std::expected<picture, std::string> picture::decode(std::span<const std::uint8_t> bytes) {
		if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX)) return std::unexpected(std::string("That isn't a picture."));
		// An SVG drawn from its vectors at its own size, no longer on its longer side
		// than any picture.
		if (is_svg(bytes)) {
			std::string_view svg(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			auto size = svg_size(svg);
			if (!size) return std::unexpected(std::string("This SVG couldn't be read. It may be damaged, or have no size."));
			double scale = std::min(1.0, longest_side / std::max(size->first, size->second));
			picture drawn = from_svg(svg, std::max(1, static_cast<int>(std::lround(size->first * scale))), std::max(1, static_cast<int>(std::lround(size->second * scale))));
			if (!drawn) return std::unexpected(std::string("This SVG couldn't be drawn."));
			return drawn;
		}
		auto length = static_cast<int>(bytes.size());
		int width = 0, height = 0, channels = 0;
		// Its size first, so one too large is refused before it takes the memory.
		if (!stbi_info_from_memory(bytes.data(), length, &width, &height, &channels))
			return std::unexpected(std::string("That isn't a picture, or not one of a kind that can be read: SVG, PNG, JPEG, GIF, BMP or TGA."));
		if (static_cast<std::int64_t>(width) * height > most_pixels) return std::unexpected(std::string("This picture is too large to open. Try a smaller copy."));
		unsigned char* decoded = stbi_load_from_memory(bytes.data(), length, &width, &height, &channels, 4);
		if (!decoded) return std::unexpected(std::string("This picture couldn't be read. The file may be damaged."));
		std::vector<std::uint8_t> rgba(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
		stbi_image_free(decoded);
		upright(width, height, rgba, facts_of(bytes).orientation);
		while (width > longest_side || height > longest_side) halve(width, height, rgba);
		return of(width, height, std::move(rgba));
	}

	std::expected<picture, std::string> picture::read(const std::filesystem::path& file) {
		std::ifstream in(file, std::ios::binary);
		if (!in) return std::unexpected("There's no picture at " + file.string() + ".");
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return decode(bytes);
	}

	std::optional<std::pair<int, int>> picture::size_of(const std::filesystem::path& file) {
		std::FILE* in = std::fopen(file.c_str(), "rb");
		if (!in) return std::nullopt;
		std::array<std::uint8_t, 512> start{};
		std::size_t got = std::fread(start.data(), 1, start.size(), in);
		int width = 0, height = 0, channels = 0;
		if (is_svg(std::span(start.data(), got))) {
			std::fclose(in);
			std::ifstream whole(file, std::ios::binary);
			std::string svg((std::istreambuf_iterator<char>(whole)), std::istreambuf_iterator<char>());
			auto size = svg_size(svg);
			if (!size) return std::nullopt;
			return std::pair{std::max(1, static_cast<int>(std::lround(size->first))), std::max(1, static_cast<int>(std::lround(size->second)))};
		}
		std::rewind(in);
		bool known = stbi_info_from_file(in, &width, &height, &channels) != 0;
		std::fclose(in);
		if (!known || width <= 0 || height <= 0) return std::nullopt;
		if (facts_of(file).turned()) std::swap(width, height);
		return std::pair{width, height};
	}

	picture picture::smaller(int longest) const {
		if (!pixels_ || longest <= 0 || std::max(width(), height()) <= longest) return *this;
		double scale = static_cast<double>(longest) / std::max(width(), height());
		int w = std::max(1, static_cast<int>(std::lround(width() * scale))), h = std::max(1, static_cast<int>(std::lround(height() * scale)));
		const auto& from = pixels_->rgba;
		std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
		for (int y = 0; y < h; ++y) {
			int top = y * height() / h, bottom = std::max(top + 1, (y + 1) * height() / h);
			for (int x = 0; x < w; ++x) {
				int left = x * width() / w, right = std::max(left + 1, (x + 1) * width() / w);
				std::array<std::uint64_t, 4> sum{};
				for (int sy = top; sy < bottom; ++sy) {
					const std::uint8_t* row = from.data() + (static_cast<std::size_t>(sy) * width() + left) * 4;
					for (int sx = left; sx < right; ++sx, row += 4) {
						for (int c = 0; c < 4; ++c) sum[c] += row[c];
					}
				}
				std::uint64_t count = static_cast<std::uint64_t>(bottom - top) * (right - left);
				for (int c = 0; c < 4; ++c) out[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);
			}
		}
		return of(w, h, std::move(out));
	}

	picture picture::of(int width, int height, std::vector<std::uint8_t> rgba) {
		picture p;
		p.pixels_ = std::make_shared<const pixels>(pixels{width, height, std::move(rgba)});
		return p;
	}

	picture picture::drawn(int width, int height, const std::function<void(canvas&)>& paint) {
		auto drawn = offscreen::draw({width, height}, 1, color{0, 0, 0, 0}, paint);
		return of(drawn.width, drawn.height, std::move(drawn.rgba));
	}

	const std::vector<std::uint8_t>& picture::rgba() const {
		static const std::vector<std::uint8_t> none;
		return pixels_ ? pixels_->rgba : none;
	}

	std::vector<std::uint8_t> picture::png() const {
		std::vector<std::uint8_t> out;
		if (!pixels_) return out;
		auto append = [](void* to, void* data, int size) {
			auto* bytes = static_cast<std::uint8_t*>(data);
			static_cast<std::vector<std::uint8_t>*>(to)->insert(static_cast<std::vector<std::uint8_t>*>(to)->end(), bytes, bytes + size);
		};
		if (!stbi_write_png_to_func(append, &out, width(), height(), 4, pixels_->rgba.data(), width() * 4)) out.clear();
		return out;
	}

	std::expected<void, std::string> picture::write_png(const std::filesystem::path& file) const {
		if (!pixels_) return std::unexpected(std::string("There's no picture to write."));
		if (!stbi_write_png(file.c_str(), width(), height(), 4, pixels_->rgba.data(), width() * 4))
			return std::unexpected("The picture couldn't be written to " + file.string() + ".");
		return {};
	}

}  // namespace one::desktop
