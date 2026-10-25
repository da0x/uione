// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <system_error>

#include "one/local/local.hpp"

namespace one::local {

	namespace {

		std::string said(int error) { return std::strerror(error); }

		// SHA-256 (FIPS 180-4), fed in pieces so a file is never read whole.
		class hashing {
		public:
			void add(const unsigned char* data, std::size_t size) {
				length_ += size;
				for (std::size_t i = 0; i < size; ++i) {
					block_[filled_++] = data[i];
					if (filled_ == 64) {
						compress();
						filled_ = 0;
					}
				}
			}

			std::string hex() {
				std::uint64_t bits = length_ * 8;
				unsigned char end = 0x80;
				add(&end, 1);
				unsigned char zero = 0;
				while (filled_ != 56) add(&zero, 1);
				for (int i = 7; i >= 0; --i) {
					unsigned char b = static_cast<unsigned char>(bits >> (i * 8));
					add(&b, 1);
				}
				static constexpr char digits[] = "0123456789abcdef";
				std::string out;
				for (std::uint32_t word : h_)
					for (int shift = 28; shift >= 0; shift -= 4) out += digits[(word >> shift) & 15];
				return out;
			}

		private:
			void compress() {
				static constexpr std::uint32_t k[64] = {
					0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
					0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
					0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
					0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
					0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
					0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
					0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
					0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
				auto rotate = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
				std::uint32_t w[64];
				for (int i = 0; i < 16; ++i)
					w[i] = std::uint32_t(block_[4 * i]) << 24 | std::uint32_t(block_[4 * i + 1]) << 16 | std::uint32_t(block_[4 * i + 2]) << 8 | block_[4 * i + 3];
				for (int i = 16; i < 64; ++i) {
					std::uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
					std::uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
					w[i] = w[i - 16] + s0 + w[i - 7] + s1;
				}
				std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
				for (int i = 0; i < 64; ++i) {
					std::uint32_t t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
					std::uint32_t t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
					h = g;
					g = f;
					f = e;
					e = d + t1;
					d = c;
					c = b;
					b = a;
					a = t1 + t2;
				}
				h_[0] += a;
				h_[1] += b;
				h_[2] += c;
				h_[3] += d;
				h_[4] += e;
				h_[5] += f;
				h_[6] += g;
				h_[7] += h;
			}

			std::array<std::uint32_t, 8> h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
			std::array<unsigned char, 64> block_{};
			std::size_t filled_ = 0;
			std::uint64_t length_ = 0;
		};

		// Writes all of data to a file that's open, then waits until it's on disk.
		bool write_all(int fd, std::string_view data) {
			while (!data.empty()) {
				ssize_t wrote = ::write(fd, data.data(), data.size());
				if (wrote < 0) {
					if (errno == EINTR) continue;
					return false;
				}
				data.remove_prefix(static_cast<std::size_t>(wrote));
			}
			return ::fsync(fd) == 0;
		}

		// A folder's own entry, so a file made or renamed in it is on disk too.
		void sync_folder(const std::filesystem::path& at) {
			int fd = ::open(at.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (fd < 0) return;
			::fsync(fd);
			::close(fd);
		}

	}  // namespace

	std::string sha256(std::istream& in) {
		hashing h;
		std::array<char, 1 << 16> buffer;
		while (in) {
			in.read(buffer.data(), buffer.size());
			h.add(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<std::size_t>(in.gcount()));
		}
		return h.hex();
	}

	std::filesystem::path folder::where(const std::string& project) {
		if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data == '/') return std::filesystem::path(data) / project;
		const char* home = std::getenv("HOME");
		return std::filesystem::path(home && *home ? home : ".") / ".local" / "share" / project;
	}

	std::expected<folder, std::string> folder::open(const std::string& project) { return open_at(where(project)); }

	std::expected<folder, std::string> folder::open_at(const std::filesystem::path& root) {
		std::error_code error;
		std::filesystem::create_directories(root / "files", error);
		if (error) return std::unexpected("can't make " + root.string() + ": " + error.message());
		int lock = ::open((root / "lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		if (lock < 0) return std::unexpected("can't open " + (root / "lock").string() + ": " + said(errno));
		if (::flock(lock, LOCK_EX | LOCK_NB) != 0) {
			int why = errno;
			::close(lock);
			if (why == EWOULDBLOCK) return std::unexpected(root.string() + " is open in another copy of the app already");
			return std::unexpected("can't lock " + root.string() + ": " + said(why));
		}
		return folder(root, lock);
	}

	folder::folder(folder&& other) noexcept : root_(std::move(other.root_)), lock_(std::exchange(other.lock_, -1)) {}

	folder& folder::operator=(folder&& other) noexcept {
		if (this != &other) {
			if (lock_ >= 0) ::close(lock_);
			root_ = std::move(other.root_);
			lock_ = std::exchange(other.lock_, -1);
		}
		return *this;
	}

	folder::~folder() {
		if (lock_ >= 0) ::close(lock_);  // which lets go of the lock
	}

	std::expected<std::vector<event>, std::string> folder::read() const {
		std::vector<event> events;
		std::ifstream in(root_ / "events.jsonl", std::ios::binary);
		if (!in) return events;  // nothing's happened yet
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		std::size_t at = 0, number = 0;
		while (at < text.size()) {
			std::size_t end = text.find('\n', at);
			++number;
			if (end == std::string::npos) break;  // cut short as it was written
			std::string_view line(text.data() + at, end - at);
			at = end + 1;
			if (line.empty()) continue;
			auto e = from_line(line);
			if (!e) return std::unexpected((root_ / "events.jsonl").string() + ":" + std::to_string(number) + ": " + e.error());
			events.push_back(std::move(*e));
		}
		return events;
	}

	std::expected<void, std::string> folder::append(const event& e) {
		std::filesystem::path log = root_ / "events.jsonl";
		bool made = !std::filesystem::exists(log);
		int fd = ::open(log.c_str(), O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
		if (fd < 0) return std::unexpected("can't open " + log.string() + ": " + said(errno));
		// A line a crash cut short is cut off first: its command never answered, and
		// what follows it has to start a line of its own.
		if (!made) {
			std::error_code error;
			std::uintmax_t size = std::filesystem::file_size(log, error);
			char last = '\n';
			if (!error && size > 0 && ::pread(fd, &last, 1, static_cast<off_t>(size - 1)) != 1) last = '\n';
			if (last != '\n') {
				std::ifstream in(log, std::ios::binary);
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				std::size_t end = text.rfind('\n');
				off_t keep = end == std::string::npos ? 0 : static_cast<off_t>(end + 1);
				if (::ftruncate(fd, keep) != 0) {
					int why = errno;
					::close(fd);
					return std::unexpected("can't mend " + log.string() + ": " + said(why));
				}
			}
		}
		bool wrote = write_all(fd, to_line(e) + "\n");
		int why = errno;
		::close(fd);
		if (!wrote) return std::unexpected("can't write " + log.string() + ": " + said(why));
		if (made) sync_folder(root_);
		return {};
	}

	std::expected<std::string, std::string> folder::keep(const std::filesystem::path& file) {
		std::ifstream in(file, std::ios::binary);
		if (!in) return std::unexpected("can't read " + file.string());
		// Copied while it's hashed, into a name of its own, then given its hash's name.
		static std::mt19937 random{std::random_device{}()};
		std::filesystem::path copying = root_ / "files" / (".keeping-" + std::to_string(random()));
		int fd = ::open(copying.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (fd < 0) return std::unexpected("can't write " + copying.string() + ": " + said(errno));
		hashing h;
		std::array<char, 1 << 16> buffer;
		bool wrote = true;
		while (in && wrote) {
			in.read(buffer.data(), buffer.size());
			std::size_t got = static_cast<std::size_t>(in.gcount());
			h.add(reinterpret_cast<const unsigned char*>(buffer.data()), got);
			std::string_view piece(buffer.data(), got);
			while (!piece.empty()) {
				ssize_t n = ::write(fd, piece.data(), piece.size());
				if (n < 0 && errno == EINTR) continue;
				if (n < 0) {
					wrote = false;
					break;
				}
				piece.remove_prefix(static_cast<std::size_t>(n));
			}
		}
		wrote = wrote && !in.bad() && ::fsync(fd) == 0;
		int why = errno;
		::close(fd);
		std::error_code ignored;
		if (!wrote) {
			std::filesystem::remove(copying, ignored);
			return std::unexpected("can't copy " + file.string() + ": " + said(why));
		}
		std::string kept = "files/" + h.hex();
		if (std::filesystem::exists(root_ / kept)) {
			std::filesystem::remove(copying, ignored);  // the same file, kept before
			return kept;
		}
		std::error_code error;
		std::filesystem::rename(copying, root_ / kept, error);
		if (error) {
			std::filesystem::remove(copying, ignored);
			return std::unexpected("can't keep " + file.string() + ": " + error.message());
		}
		sync_folder(root_ / "files");
		return kept;
	}

}  // namespace one::local
