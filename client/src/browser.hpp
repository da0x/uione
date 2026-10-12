// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Signing in through the browser: SHA-256 and base64url for the PKCE challenge, and a
// listener on 127.0.0.1 for the one request the app's /authorize page sends back. The
// text inside one::client is exactly what the compiler writes into an app's C++
// client (compiler/generators/cpp_browser.hpp), and a test of the compiler's holds
// the two the same.

#pragma once

// POSIX sockets for now: a Windows build gives sign_in_with_browser a Winsock
// listener of its own.
#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

#include "one/client/client.hpp"

namespace one::client {

#if defined(__unix__) || defined(__APPLE__)
// Signing in through the browser: a challenge only this program can answer, and
// where the browser sends the answer back.
namespace detail {
	// SHA-256 (FIPS 180-4), its 32 bytes.
	inline std::string sha256(std::string_view data) {
		static constexpr std::uint32_t k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
		std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		std::string m(data);
		std::uint64_t bits = std::uint64_t(data.size()) * 8;
		m.push_back(char(0x80));
		while (m.size() % 64 != 56) m.push_back('\0');
		for (int i = 7; i >= 0; --i) m.push_back(char(bits >> (i * 8)));
		auto rotate = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
		auto byte = [&m](std::size_t at) { return std::uint32_t(std::uint8_t(m[at])); };
		for (std::size_t at = 0; at < m.size(); at += 64) {
			std::uint32_t w[64];
			for (int i = 0; i < 16; ++i) w[i] = byte(at + 4 * i) << 24 | byte(at + 4 * i + 1) << 16 | byte(at + 4 * i + 2) << 8 | byte(at + 4 * i + 3);
			for (int i = 16; i < 64; ++i) {
				std::uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
				std::uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], j = h[7];
			for (int i = 0; i < 64; ++i) {
				std::uint32_t t1 = j + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
				std::uint32_t t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
				j = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
			}
			h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += j;
		}
		std::string out;
		for (auto x : h) {
			for (int i = 3; i >= 0; --i) out.push_back(char(x >> (i * 8)));
		}
		return out;
	}

	// base64url without padding, as PKCE writes it.
	inline std::string base64url(std::string_view data) {
		static constexpr char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string out;
		std::uint32_t held = 0;
		int bits = 0;
		for (unsigned char c : data) {
			held = held << 8 | c;
			bits += 8;
			while (bits >= 6) out.push_back(letters[(held >> (bits -= 6)) & 63]);
		}
		if (bits > 0) out.push_back(letters[(held << (6 - bits)) & 63]);
		return out;
	}

	// Random bytes from the system, written in base64url.
	inline std::string random_text(std::size_t bytes) {
		std::string raw(bytes, '\0');
		std::ifstream random("/dev/urandom", std::ios::binary);
		if (!random.read(raw.data(), std::streamsize(bytes))) throw refused(0, "this computer gave no random bytes");
		return base64url(raw);
	}

	// A query's values, like code and state in ?code=...&state=..., decoded.
	inline std::string query_value(std::string_view query, std::string_view name) {
		while (!query.empty()) {
			auto end = query.find('&');
			auto pair = query.substr(0, end);
			query = end == std::string_view::npos ? std::string_view() : query.substr(end + 1);
			auto equals = pair.find('=');
			if (pair.substr(0, equals) != name || equals == std::string_view::npos) continue;
			std::string out;
			auto value = pair.substr(equals + 1);
			for (std::size_t i = 0; i < value.size(); ++i) {
				if (value[i] == '+') out += ' ';
				else if (value[i] == '%' && i + 2 < value.size() && std::isxdigit((unsigned char)value[i + 1]) && std::isxdigit((unsigned char)value[i + 2])) {
					out += char(std::stoi(std::string(value.substr(i + 1, 2)), nullptr, 16));
					i += 2;
				} else out += value[i];
			}
			return out;
		}
		return {};
	}

	// What the browser sent back: a code, or why there's none.
	struct answer {
		std::string code, error;
	};

	// Listens on 127.0.0.1 at a port the system picks, for the browser to come back.
	class loopback {
	public:
		loopback() {
			socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
			if (socket_ < 0) throw refused(0, "couldn't listen on this computer for the browser");
			sockaddr_in at{};
			at.sin_family = AF_INET;
			at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			socklen_t size = sizeof at;
			if (::bind(socket_, reinterpret_cast<sockaddr*>(&at), size) != 0 || ::listen(socket_, 4) != 0 ||
			    ::getsockname(socket_, reinterpret_cast<sockaddr*>(&at), &size) != 0) {
				::close(socket_);
				throw refused(0, "couldn't listen on this computer for the browser");
			}
			port_ = ntohs(at.sin_port);
		}
		loopback(const loopback&) = delete;
		loopback& operator=(const loopback&) = delete;
		~loopback() { ::close(socket_); }

		int port() const { return port_; }

		// Waits for the browser to bring back an answer with this state, sending it on to
		// done, with done=yes or done=no added. Anything else that calls is turned away.
		answer wait(const std::string& state, const std::string& done, std::chrono::seconds limit) {
			auto until = std::chrono::steady_clock::now() + limit;
			for (;;) {
				auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count();
				if (left <= 0) throw refused(0, "nobody answered in the browser in time; run login again");
				pollfd waiting{socket_, POLLIN, 0};
				if (::poll(&waiting, 1, int(left)) <= 0) continue;
				int call = ::accept(socket_, nullptr, nullptr);
				if (call < 0) continue;
				std::string request;
				char chunk[2048];
				pollfd reading{call, POLLIN, 0};
				while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384 && ::poll(&reading, 1, 5000) > 0) {
					auto got = ::read(call, chunk, sizeof chunk);
					if (got <= 0) break;
					request.append(chunk, std::size_t(got));
				}
				// GET /?code=...&state=... HTTP/1.1
				std::string_view line(request);
				line = line.substr(0, line.find("\r\n"));
				std::string_view target;
				if (line.starts_with("GET ")) target = line.substr(4, line.rfind(' ') - 4);
				auto question = target.find('?');
				std::string_view query = question == std::string_view::npos ? std::string_view() : target.substr(question + 1);
				answer got{query_value(query, "code"), query_value(query, "error")};
				bool ours = !state.empty() && query_value(query, "state") == state && (!got.code.empty() || !got.error.empty());
				std::string reply = ours ? "HTTP/1.1 302 Found\r\nLocation: " + done + (got.code.empty() ? "?done=no" : "?done=yes") +
				                               "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
				                         : "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
				for (std::size_t sent = 0; sent < reply.size();) {
					auto wrote = ::write(call, reply.data() + sent, reply.size() - sent);
					if (wrote <= 0) break;
					sent += std::size_t(wrote);
				}
				::close(call);
				if (ours) return got;
			}
		}

	private:
		int socket_ = -1;
		int port_ = 0;
	};
} // namespace detail
#endif

}  // namespace one::client
