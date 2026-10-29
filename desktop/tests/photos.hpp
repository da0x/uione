// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Photos made for tests, as a camera would write them: a picture's pixels as a JPEG
// or PNG with an EXIF block in it, or a WebP's chunks around one, the block saying
// when it was taken, with what, and which way up it was held.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "stb/stb_image_write.h"

namespace photos {

	// What the EXIF block says; what's empty, or an orientation of 0, it doesn't.
	struct said {
		bool little = true;  // its numbers least significant byte first, as II says, or else MM
		int orientation = 0;
		std::string make = {}, model = {}, taken = {}, offset = {};
	};

	// A TIFF's structure as EXIF is: its first directory, with the camera and which way
	// up, and pointing to the EXIF directory, with when.
	inline std::vector<std::uint8_t> exif(const said& s) {
		std::vector<std::uint8_t> out;
		auto u16 = [&](std::size_t at, std::uint32_t v) {
			if (out.size() < at + 2) out.resize(at + 2);
			out[at + (s.little ? 0 : 1)] = static_cast<std::uint8_t>(v & 0xff);
			out[at + (s.little ? 1 : 0)] = static_cast<std::uint8_t>(v >> 8 & 0xff);
		};
		auto u32 = [&](std::size_t at, std::uint32_t v) {
			if (out.size() < at + 4) out.resize(at + 4);
			for (int i = 0; i < 4; ++i) out[at + static_cast<std::size_t>(s.little ? i : 3 - i)] = static_cast<std::uint8_t>(v >> (8 * i) & 0xff);
		};
		struct entry {
			std::uint16_t tag, type;
			std::string text;
			std::uint32_t number = 0;
		};
		auto directory = [&](std::size_t at, const std::vector<entry>& entries, std::uint32_t next) {
			u16(at, static_cast<std::uint32_t>(entries.size()));
			std::size_t data = at + 2 + entries.size() * 12 + 4;
			for (std::size_t i = 0; i < entries.size(); ++i) {
				const entry& e = entries[i];
				std::size_t here = at + 2 + i * 12;
				u16(here, e.tag);
				u16(here + 2, e.type);
				if (e.type == 2) {
					std::string with_nul = e.text + '\0';
					u32(here + 4, static_cast<std::uint32_t>(with_nul.size()));
					if (with_nul.size() <= 4) {
						u32(here + 8, 0);
						for (std::size_t c = 0; c < with_nul.size(); ++c) out[here + 8 + c] = static_cast<std::uint8_t>(with_nul[c]);
					} else {
						u32(here + 8, static_cast<std::uint32_t>(data));
						if (out.size() < data + with_nul.size()) out.resize(data + with_nul.size());
						for (std::size_t c = 0; c < with_nul.size(); ++c) out[data + c] = static_cast<std::uint8_t>(with_nul[c]);
						data += with_nul.size() + (with_nul.size() & 1);
					}
				} else {
					u32(here + 4, 1);
					if (e.type == 3) u16(here + 8, e.number), u16(here + 10, 0);
					else u32(here + 8, e.number);
				}
			}
			u32(at + 2 + entries.size() * 12, next);
			if (out.size() < data) out.resize(data);
			return data;
		};
		out = {s.little ? std::uint8_t('I') : std::uint8_t('M'), s.little ? std::uint8_t('I') : std::uint8_t('M')};
		u16(2, 42);
		u32(4, 8);
		std::vector<entry> first, later;
		if (!s.make.empty()) first.push_back({0x010f, 2, s.make});
		if (!s.model.empty()) first.push_back({0x0110, 2, s.model});
		if (s.orientation) first.push_back({0x0112, 3, "", static_cast<std::uint32_t>(s.orientation)});
		if (!s.taken.empty()) later.push_back({0x9003, 2, s.taken});
		if (!s.offset.empty()) later.push_back({0x9011, 2, s.offset});
		// Where the EXIF directory goes is known once the first is laid out, so it's laid
		// out twice, the second time pointing there.
		if (!later.empty()) first.push_back({0x8769, 4, "", 0});
		std::size_t after = directory(8, first, 0);
		if (!later.empty()) {
			first.back().number = static_cast<std::uint32_t>(after);
			directory(8, first, 0);
			directory(after, later, 0);
		}
		return out;
	}

	inline void written(void* into, void* data, int size) {
		auto* bytes = static_cast<std::vector<std::uint8_t>*>(into);
		bytes->insert(bytes->end(), static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
	}

	// Pixels, four bytes each, as a JPEG, its EXIF block in an APP1 segment just after
	// its start, as a camera puts it.
	inline std::vector<std::uint8_t> jpeg(int width, int height, const std::vector<std::uint8_t>& rgba, const std::vector<std::uint8_t>& block) {
		std::vector<std::uint8_t> plain;
		stbi_write_jpg_to_func(written, &plain, width, height, 4, rgba.data(), 95);
		if (block.empty()) return plain;
		std::vector<std::uint8_t> app1{0xff, 0xe1, 0, 0, 'E', 'x', 'i', 'f', 0, 0};
		app1.insert(app1.end(), block.begin(), block.end());
		std::size_t length = app1.size() - 2;
		app1[2] = static_cast<std::uint8_t>(length >> 8), app1[3] = static_cast<std::uint8_t>(length & 0xff);
		std::vector<std::uint8_t> out(plain.begin(), plain.begin() + 2);
		out.insert(out.end(), app1.begin(), app1.end());
		out.insert(out.end(), plain.begin() + 2, plain.end());
		return out;
	}

	inline std::uint32_t crc32(const std::uint8_t* data, std::size_t n) {
		std::uint32_t crc = 0xffffffff;
		for (std::size_t i = 0; i < n; ++i) {
			crc ^= data[i];
			for (int k = 0; k < 8; ++k) crc = crc & 1 ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
		}
		return ~crc;
	}

	// As a PNG, its EXIF block in an eXIf chunk after the one that says its size.
	inline std::vector<std::uint8_t> png(int width, int height, const std::vector<std::uint8_t>& rgba, const std::vector<std::uint8_t>& block) {
		std::vector<std::uint8_t> plain;
		stbi_write_png_to_func(written, &plain, width, height, 4, rgba.data(), width * 4);
		if (block.empty()) return plain;
		std::vector<std::uint8_t> chunk{0, 0, 0, 0, 'e', 'X', 'I', 'f'};
		for (int i = 0; i < 4; ++i) chunk[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(block.size() >> (24 - 8 * i) & 0xff);
		chunk.insert(chunk.end(), block.begin(), block.end());
		std::uint32_t crc = crc32(chunk.data() + 4, chunk.size() - 4);
		for (int i = 0; i < 4; ++i) chunk.push_back(static_cast<std::uint8_t>(crc >> (24 - 8 * i) & 0xff));
		std::size_t after_size = 8 + 25;  // the signature, then IHDR's length, type, 13 bytes and CRC
		std::vector<std::uint8_t> out(plain.begin(), plain.begin() + static_cast<std::ptrdiff_t>(after_size));
		out.insert(out.end(), chunk.begin(), chunk.end());
		out.insert(out.end(), plain.begin() + static_cast<std::ptrdiff_t>(after_size), plain.end());
		return out;
	}

	// A WebP's RIFF with only its EXIF chunk, after the words Exif and two NULs when
	// `prefixed`, as some write it: enough to be read for what it says.
	inline std::vector<std::uint8_t> webp(const std::vector<std::uint8_t>& block, bool prefixed) {
		std::vector<std::uint8_t> data;
		if (prefixed) data = {'E', 'x', 'i', 'f', 0, 0};
		data.insert(data.end(), block.begin(), block.end());
		std::vector<std::uint8_t> out{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', 'X', 10, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'E', 'X', 'I', 'F'};
		for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(data.size() >> (8 * i) & 0xff));
		out.insert(out.end(), data.begin(), data.end());
		if (data.size() & 1) out.push_back(0);
		std::size_t riff = out.size() - 8;
		for (int i = 0; i < 4; ++i) out[4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(riff >> (8 * i) & 0xff);
		return out;
	}

}  // namespace photos
