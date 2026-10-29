// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a camera wrote in a photo, its EXIF, found without decoding the photo: in a
// JPEG's APP1 segment, a PNG's eXIf chunk, a WebP's EXIF chunk, or a TIFF, which is
// what EXIF itself is. Only what's asked for is read, every offset and count checked
// against what's there, and every walk bounded, so a file cut short or made wrong
// reads as far as it's sound and no further.

#include "one/desktop/picture.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <optional>

namespace one::desktop {

	namespace {

		// Bytes at an offset: up to `n`, fewer past the end.
		class bytes_at {
		public:
			virtual ~bytes_at() = default;
			virtual std::size_t read(std::uint64_t at, std::uint8_t* into, std::size_t n) const = 0;
			bool read_all(std::uint64_t at, std::uint8_t* into, std::size_t n) const { return read(at, into, n) == n; }
		};

		class in_memory : public bytes_at {
		public:
			explicit in_memory(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
			std::size_t read(std::uint64_t at, std::uint8_t* into, std::size_t n) const override {
				if (at >= bytes_.size()) return 0;
				std::size_t got = std::min<std::uint64_t>(n, bytes_.size() - at);
				if (got) std::memcpy(into, bytes_.data() + at, got);
				return got;
			}

		private:
			std::span<const std::uint8_t> bytes_;
		};

		class in_file : public bytes_at {
		public:
			explicit in_file(std::FILE* f) : file_(f) {}
			std::size_t read(std::uint64_t at, std::uint8_t* into, std::size_t n) const override {
				if (at > static_cast<std::uint64_t>(LONG_MAX) || std::fseek(file_, static_cast<long>(at), SEEK_SET) != 0) return 0;
				return std::fread(into, 1, n, file_);
			}

		private:
			std::FILE* file_;
		};

		std::uint16_t big16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] << 8 | p[1]); }
		std::uint32_t big32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
		std::uint32_t little32(const std::uint8_t* p) { return std::uint32_t(p[3]) << 24 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[1]) << 8 | p[0]; }

		// A TIFF's structure, from `base` and no further than `end`: its first directory,
		// for the camera and which way up, and the EXIF directory it points to, for when.
		class tiff {
		public:
			tiff(const bytes_at& in, std::uint64_t base, std::uint64_t end) : in_(in), base_(base), end_(end) {}

			photo_facts read() {
				photo_facts out;
				std::array<std::uint8_t, 8> head{};
				if (end_ < base_ + 8 || !in_.read_all(base_, head.data(), 8)) return out;
				if (head[0] == 'I' && head[1] == 'I') little_ = true;
				else if (head[0] != 'M' || head[1] != 'M') return out;
				if (u16(head.data() + 2) != 42) return out;
				std::optional<std::uint32_t> exif;
				each_entry(u32(head.data() + 4), [&](std::uint16_t tag, std::uint16_t type, std::uint32_t count, const std::uint8_t* value) {
					if (tag == 0x010f) out.make = text(type, count, value);
					else if (tag == 0x0110) out.model = text(type, count, value);
					else if (tag == 0x0112 && type == 3 && count >= 1) {
						int turned = u16(value);
						if (turned >= 1 && turned <= 8) out.orientation = turned;
					} else if (tag == 0x8769 && (type == 4 || type == 13) && count == 1) exif = u32(value);
				});
				if (exif) {
					each_entry(*exif, [&](std::uint16_t tag, std::uint16_t type, std::uint32_t count, const std::uint8_t* value) {
						if (tag == 0x9003) out.taken = text(type, count, value);
						else if (tag == 0x9011) out.offset = text(type, count, value);
					});
				}
				return out;
			}

		private:
			std::uint16_t u16(const std::uint8_t* p) const { return little_ ? static_cast<std::uint16_t>(p[1] << 8 | p[0]) : big16(p); }
			std::uint32_t u32(const std::uint8_t* p) const { return little_ ? little32(p) : big32(p); }

			// Each of a directory's entries: its tag, type, count and the four bytes that
			// hold its value or where it is. A directory says how many it has, at most
			// as many as fit before the end, and never more than a camera writes.
			template <class F>
			void each_entry(std::uint32_t at, F&& told) {
				std::array<std::uint8_t, 2> counted{};
				if (at > end_ - base_ || end_ - base_ - at < 2 || !in_.read_all(base_ + at, counted.data(), 2)) return;
				std::uint64_t count = std::min<std::uint64_t>(u16(counted.data()), (end_ - base_ - at - 2) / 12);
				count = std::min<std::uint64_t>(count, 1024);
				if (count == 0) return;
				std::vector<std::uint8_t> entries(count * 12);
				if (!in_.read_all(base_ + at + 2, entries.data(), entries.size())) return;
				for (std::uint64_t i = 0; i < count; ++i) {
					const std::uint8_t* e = entries.data() + i * 12;
					told(u16(e), u16(e + 2), u32(e + 4), e + 8);
				}
			}

			// An ASCII value, in the entry when it fits, or where it points; up to its first
			// NUL, only what's printable, without the spaces a camera pads it with.
			std::string text(std::uint16_t type, std::uint32_t count, const std::uint8_t* value) const {
				if (type != 2 || count == 0) return {};
				std::size_t n = std::min<std::uint32_t>(count, 256);
				std::array<std::uint8_t, 256> held{};
				if (count <= 4) {
					std::memcpy(held.data(), value, n);
				} else {
					std::uint32_t at = u32(value);
					if (at > end_ - base_ || end_ - base_ - at < n || !in_.read_all(base_ + at, held.data(), n)) return {};
				}
				std::string out;
				for (std::size_t i = 0; i < n && held[i] != 0; ++i) {
					if (held[i] >= 0x20 && held[i] < 0x7f) out += static_cast<char>(held[i]);
				}
				std::size_t first = out.find_first_not_of(' '), last = out.find_last_not_of(' ');
				return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
			}

			const bytes_at& in_;
			std::uint64_t base_, end_;
			bool little_ = false;
		};

		// The ways a photo's EXIF is found, by what its file starts with.
		photo_facts found_in(const bytes_at& in, std::uint64_t size) {
			std::array<std::uint8_t, 16> start{};
			std::size_t got = in.read(0, start.data(), start.size());
			if (got >= 4 && ((start[0] == 'I' && start[1] == 'I' && start[2] == 42 && start[3] == 0) || (start[0] == 'M' && start[1] == 'M' && start[2] == 0 && start[3] == 42)))
				return tiff(in, 0, size).read();
			// A JPEG's segments, one after another, until its picture starts: the APP1 that
			// says Exif is the one.
			if (got >= 2 && start[0] == 0xff && start[1] == 0xd8) {
				std::uint64_t at = 2;
				for (int segments = 0; segments < 1024 && at + 4 <= size; ++segments) {
					std::array<std::uint8_t, 10> head{};
					if (!in.read_all(at, head.data(), 4) || head[0] != 0xff) break;
					std::uint8_t marker = head[1];
					if (marker == 0xff) {
						++at;  // padding before a marker
						continue;
					}
					if (marker == 0xd9 || marker == 0xda) break;
					if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
						at += 2;
						continue;
					}
					std::uint16_t length = big16(head.data() + 2);
					if (length < 2) break;
					if (marker == 0xe1 && length >= 14 && in.read_all(at + 4, head.data() + 4, 6) && std::memcmp(head.data() + 4, "Exif\0\0", 6) == 0)
						return tiff(in, at + 10, std::min(size, at + 2 + length)).read();
					at += 2 + std::uint64_t(length);
				}
				return {};
			}
			// A PNG's chunks, until its end: eXIf's is the TIFF itself.
			static constexpr std::array<std::uint8_t, 8> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
			if (got >= 8 && std::equal(png.begin(), png.end(), start.begin())) {
				std::uint64_t at = 8;
				for (int chunks = 0; chunks < 65536 && at + 8 <= size; ++chunks) {
					std::array<std::uint8_t, 8> head{};
					if (!in.read_all(at, head.data(), 8)) break;
					std::uint64_t length = big32(head.data());
					if (std::memcmp(head.data() + 4, "eXIf", 4) == 0) return tiff(in, at + 8, std::min(size, at + 8 + length)).read();
					if (std::memcmp(head.data() + 4, "IEND", 4) == 0) break;
					at += 12 + length;
				}
				return {};
			}
			// A WebP's chunks, in its RIFF: EXIF's is the TIFF, sometimes after the words
			// Exif and two NULs, as a JPEG's is.
			if (got >= 12 && std::memcmp(start.data(), "RIFF", 4) == 0 && std::memcmp(start.data() + 8, "WEBP", 4) == 0) {
				std::uint64_t end = std::min(size, 8 + std::uint64_t(little32(start.data() + 4)));
				std::uint64_t at = 12;
				for (int chunks = 0; chunks < 65536 && at + 8 <= end; ++chunks) {
					std::array<std::uint8_t, 14> head{};
					if (!in.read_all(at, head.data(), 8)) break;
					std::uint64_t length = little32(head.data() + 4);
					if (std::memcmp(head.data(), "EXIF", 4) == 0) {
						std::uint64_t from = at + 8, to = std::min(end, from + length);
						if (to - from >= 6 && in.read_all(from, head.data() + 8, 6) && std::memcmp(head.data() + 8, "Exif\0\0", 6) == 0) from += 6;
						return tiff(in, from, to).read();
					}
					at += 8 + length + (length & 1);
				}
			}
			return {};
		}

	}  // namespace

	photo_facts facts_of(std::span<const std::uint8_t> bytes) { return found_in(in_memory(bytes), bytes.size()); }

	photo_facts facts_of(const std::filesystem::path& file) {
		std::FILE* f = std::fopen(file.c_str(), "rb");
		if (!f) return {};
		std::uint64_t size = 0;
		if (std::fseek(f, 0, SEEK_END) == 0) {
			long at = std::ftell(f);
			size = at > 0 ? static_cast<std::uint64_t>(at) : 0;
		}
		photo_facts out = found_in(in_file(f), size);
		std::fclose(f);
		return out;
	}

	std::string photo_facts::camera() const {
		if (model.empty()) return make;
		if (make.empty()) return model;
		// Said once: Canon's Canon EOS R5, or NIKON CORPORATION's NIKON Z 6, is the model.
		std::string first = make.substr(0, make.find(' '));
		bool says_it = model.size() >= first.size() && std::equal(first.begin(), first.end(), model.begin(), [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
		return says_it ? model : make + " " + model;
	}

	std::optional<std::chrono::sys_seconds> photo_facts::when() const {
		// Like 2026:10:12 08:30:15, every digit there.
		if (taken.size() < 19) return std::nullopt;
		for (std::size_t i : {4u, 7u}) {
			if (taken[i] != ':') return std::nullopt;
		}
		if (taken[10] != ' ' || taken[13] != ':' || taken[16] != ':') return std::nullopt;
		auto number = [&](std::size_t from, std::size_t n) -> std::optional<int> {
			int out = 0;
			for (std::size_t i = from; i < from + n; ++i) {
				if (!std::isdigit(static_cast<unsigned char>(taken[i]))) return std::nullopt;
				out = out * 10 + (taken[i] - '0');
			}
			return out;
		};
		auto year = number(0, 4), month = number(5, 2), day = number(8, 2), hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
		if (!year || !month || !day || !hour || !minute || !second || *year < 1800) return std::nullopt;
		std::chrono::year_month_day date{std::chrono::year(*year), std::chrono::month(static_cast<unsigned>(*month)), std::chrono::day(static_cast<unsigned>(*day))};
		if (!date.ok() || *hour > 23 || *minute > 59 || *second > 60) return std::nullopt;
		auto local = std::chrono::local_days(date) + std::chrono::hours(*hour) + std::chrono::minutes(*minute) + std::chrono::seconds(std::min(*second, 59));
		// Its offset from UTC, like +02:00, when it says one; else the time where this is.
		if (offset.size() >= 6 && (offset[0] == '+' || offset[0] == '-') && offset[3] == ':') {
			std::string_view o = offset;
			if (std::isdigit(static_cast<unsigned char>(o[1])) && std::isdigit(static_cast<unsigned char>(o[2])) && std::isdigit(static_cast<unsigned char>(o[4])) &&
			    std::isdigit(static_cast<unsigned char>(o[5]))) {
				auto by = std::chrono::hours((o[1] - '0') * 10 + (o[2] - '0')) + std::chrono::minutes((o[4] - '0') * 10 + (o[5] - '0'));
				auto utc = std::chrono::sys_seconds(local.time_since_epoch()) - (o[0] == '-' ? -by : by);
				return utc;
			}
		}
		try {
			return std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_sys(local, std::chrono::choose::earliest));
		} catch (...) {
			return std::chrono::sys_seconds(local.time_since_epoch());
		}
	}

}  // namespace one::desktop
