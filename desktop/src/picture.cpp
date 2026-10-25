// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/desktop/picture.hpp"

#include "offscreen.hpp"

#include <algorithm>
#include <climits>
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

	}  // namespace

	std::expected<picture, std::string> picture::decode(std::span<const std::uint8_t> bytes) {
		if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX)) return std::unexpected(std::string("That isn't a picture."));
		auto length = static_cast<int>(bytes.size());
		int width = 0, height = 0, channels = 0;
		// Its size first, so one too large is refused before it takes the memory.
		if (!stbi_info_from_memory(bytes.data(), length, &width, &height, &channels))
			return std::unexpected(std::string("That isn't a picture, or not one of a kind that can be read: PNG, JPEG, GIF, BMP or TGA."));
		if (static_cast<std::int64_t>(width) * height > most_pixels) return std::unexpected(std::string("This picture is too large to open. Try a smaller copy."));
		unsigned char* decoded = stbi_load_from_memory(bytes.data(), length, &width, &height, &channels, 4);
		if (!decoded) return std::unexpected(std::string("This picture couldn't be read. The file may be damaged."));
		std::vector<std::uint8_t> rgba(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
		stbi_image_free(decoded);
		while (width > longest_side || height > longest_side) halve(width, height, rgba);
		return of(width, height, std::move(rgba));
	}

	std::expected<picture, std::string> picture::read(const std::filesystem::path& file) {
		std::ifstream in(file, std::ios::binary);
		if (!in) return std::unexpected("There's no picture at " + file.string() + ".");
		std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return decode(bytes);
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
