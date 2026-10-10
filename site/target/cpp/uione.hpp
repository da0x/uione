// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from uione/ by one. Do not edit.

// uione's C++ client: its records, the ids they're stored under, its commands,
// and its views, read live. Built with libember (github.com/da0x/libember):
//
//   g++ -std=c++23 program.cpp $(pkg-config --cflags --libs botan-3)

#pragma once

#include <cctype>
#include <chrono>
#include <cstdio>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <fstream>
#endif

#include <ember.hpp>

namespace uione {

	// When something happened, to the nanosecond.
	using time = std::chrono::sys_time<std::chrono::nanoseconds>;

	// What the app said when it refused a command: why, and its HTTP status.
	struct refused : std::runtime_error {
		int status;
		refused(int status, const std::string& message) : std::runtime_error(message), status(status) {}
	};

	namespace detail {
		inline std::string text(const ember::value& v) { return v.is_string() ? v.as_string() : std::string(); }
		inline double number(const ember::value& v) { return v.is_integer() ? double(v.as_integer()) : v.is_double() ? v.as_double() : 0; }
		inline bool flag(const ember::value& v) { return v.is_bool() && v.as_bool(); }
		inline time when(const ember::value& v) { return v.is_time() ? v.as_time() : time{}; }
		inline std::vector<std::string> texts(const ember::value& v) {
			std::vector<std::string> out;
			if (v.is_array()) {
				for (const auto& item : v.as_array()) out.push_back(text(item));
			}
			return out;
		}
		template <class Choice> Choice choice(const ember::value& v) {
			Choice out{};
			from_string(text(v), out);
			return out;
		}
		// A string as JSON.
		inline std::string json(const std::string& s) {
			std::string out = "\"";
			for (unsigned char c : s) {
				if (c == '"' || c == '\\') out += '\\', out += char(c);
				else if (c < 0x20) { char hex[8]; std::snprintf(hex, sizeof hex, "\\u%04x", c); out += hex; }
				else out += char(c);
			}
			return out + "\"";
		}
		inline std::string json(double n) { char s[32]; std::snprintf(s, sizeof s, "%.17g", n); return s; }
		inline std::string json(bool b) { return b ? "true" : "false"; }
		inline std::string json(const std::vector<std::string>& list) {
			std::string out = "[";
			for (std::size_t i = 0; i < list.size(); ++i) out += (i ? "," : "") + json(list[i]);
			return out + "]";
		}
		inline std::string json(time t) {
			return json(std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::milliseconds>(t)));
		}
	} // namespace detail

	// The id an entity is stored under, from its keys in order, as the app makes it:
	// each part escaped as in a path, and a dash after the first part escaped too, so
	// project engine and person x-1 is engine-x%2D1.
	inline std::string key(std::initializer_list<std::string> parts) {
		std::string out;
		bool first = true;
		for (const auto& part : parts) {
			if (!first) out += '-';
			for (unsigned char c : part) {
				bool plain = std::isalnum(c) || c == '_' || c == '.' || c == '~' || c == '$' || c == '&' || c == '+' || c == ':' || c == '=' || c == '@';
				if (plain || (c == '-' && first)) { out += char(c); continue; }
				char hex[4];
				std::snprintf(hex, sizeof hex, "%%%02X", c);
				out += hex;
			}
			first = false;
		}
		return out;
	}

	// A number as a key holds it, 42 rather than 42.000000.
	inline std::string key_part(double n) {
		char s[32];
		std::snprintf(s, sizeof s, "%.15g", n);
		return s;
	}
	inline std::string key_part(const std::string& s) { return s; }

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

	namespace about {

	} // namespace about

	namespace install {

	} // namespace install

	namespace language {

	} // namespace language

	namespace mission {

	} // namespace mission

	namespace releases {

	} // namespace releases

	namespace studio {

		struct project {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string name{};
			std::string owner{};

			static project from(const ember::value& v) {
				project out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.name = detail::text(v["name"]);
				out.owner = detail::text(v["owner"]);
				return out;
			}
		};

		// What project::create is sent.
		struct project_create {
			std::optional<std::string> name{};
			std::optional<std::string> owner{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (name) add("name", detail::json(*name));
				if (owner) add("owner", detail::json(*owner));
				return out + "}";
			}
		};

		struct projects {
			bool exists = false;  // whether it's been made, and may be read
			struct rows_row {
				std::string id{};
				std::string name{};
				time created_at{};
				static rows_row from(const ember::value& v) {
					rows_row out;
					out.id = detail::text(v["id"]);
					out.name = detail::text(v["name"]);
					out.created_at = detail::when(v["created_at"]);
					return out;
				}
			};
			std::vector<rows_row> rows{};

			static struct projects from(const ember::snapshot& s) {
				struct projects out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				if (v["rows"].is_array()) {
					for (const auto& row : v["rows"].as_array()) out.rows.push_back(rows_row::from(row));
				}
				return out;
			}
			static std::string path(const std::string& of) { return "views/studio::projects:" + of; }
		};

	} // namespace studio

	namespace waitlist {

		struct signup {
			std::string id{};
			time created_at{};
			std::string created_by{};
			time updated_at{};
			std::string updated_by{};
			std::string email{};

			static signup from(const ember::value& v) {
				signup out;
				out.id = detail::text(v["id"]);
				out.created_at = detail::when(v["created_at"]);
				out.created_by = detail::text(v["created_by"]);
				out.updated_at = detail::when(v["updated_at"]);
				out.updated_by = detail::text(v["updated_by"]);
				out.email = detail::text(v["email"]);
				return out;
			}
		};
		inline std::string signup_id(const std::string& email) { return key({key_part(email)}); }

		// What signup::create is sent.
		struct signup_create {
			std::optional<std::string> email{};

			std::string json() const {
				std::string out = "{";
				auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? "," : "") + detail::json(std::string(name)) + ":" + value; };
				if (email) add("email", detail::json(*email));
				return out + "}";
			}
		};

		struct signups {
			bool exists = false;  // whether it's been made, and may be read
			ember::value total{};

			static struct signups from(const ember::snapshot& s) {
				struct signups out;
				out.exists = s.exists;
				const ember::value& v = s.data;
				out.total = v["total"];
				return out;
			}
			static std::string path() { return "views/waitlist::signups"; }
		};

	} // namespace waitlist

	// A view to read once, or to listen to as it changes.
	template <class View> class live {
		public:
		live(ember::database& store, std::string path) : store_(store), path_(std::move(path)) {}
		View get() { return View::from(store_.get(path_)); }
		ember::database::registration listen(std::function<void(const View&)> on_change, std::function<void(const ember::error&)> on_error = {}) {
			return store_.listen(path_, [on_change](const ember::snapshot& s) { on_change(View::from(s)); }, std::move(on_error));
		}
		private:
		ember::database& store_;
		std::string path_;
	};

	// Who's asking: a person, or a service and the project and role it holds.
	struct caller {
		std::string id{}, name{}, place{}, role{};
		bool service = false;
	};

	// Where the app is: its address, and, for the emulators, where they listen.
	struct options {
		std::string server{};          // like https://neotrac.org
		std::string project{};         // its Firebase project; asked of the server when empty
		std::string api_key{};         // asked of the server with the project when empty
		std::string firestore_host{};  // the Firestore emulator, like localhost:8080
		std::string auth_host{};       // the Auth emulator, like localhost:9099
	};

	// The app: sign in as a person or with a service's key, run its commands, and read
	// its views live. Callbacks run on the executor given, or on libember's own thread.
	class client {
		public:
		explicit client(options o, ember::executor* deliver_on = nullptr) : options_(std::move(o)), deliver_on_(deliver_on) {}

		// Signs in as a service: its key runs commands, and trades for a sign-in of
		// the service's own that reads its views.
		void sign_in_with_key(const std::string& key) {
			key_ = key;
			auto token = post("/api/token", "{}");
			connect();
			auth_->sign_in_with_custom_token(detail::text(ember::parse_json(token)["token"]));
		}

		// Signs in as a person, with the refresh token a sign-in kept.
		void sign_in_with_refresh_token(const std::string& token) {
			connect();
			auth_->sign_in_with_refresh_token(token);
		}

		// Signs in as a person, with a GitHub or Google access token, like one a device's
		// sign-in gave: github.com or google.com.
		void sign_in_with_provider(const std::string& provider, const std::string& access_token) {
			connect();
			auth_->sign_in_with_idp(provider, access_token);
		}

		#if defined(__unix__) || defined(__APPLE__)
		// Signs in as a person through their browser: open(url) shows them the app's
		// authorize page, where they sign in as they always do and allow this program. It
		// waits up to five minutes on 127.0.0.1 for the code the page sends back, then
		// trades the code, with a verifier only it knows, for a sign-in of theirs.
		void sign_in_with_browser(const std::function<void(const std::string& url)>& open) {
			detail::loopback waiting;
			std::string verifier = detail::random_text(32), state = detail::random_text(16);
			open(options_.server + "/authorize?port=" + std::to_string(waiting.port()) + "&state=" + state + "&challenge=" + detail::base64url(detail::sha256(verifier)));
			auto got = waiting.wait(state, options_.server + "/authorize", std::chrono::minutes(5));
			if (got.code.empty()) throw refused(403, got.error == "denied" ? "it was turned down in the browser" : "the browser sent no sign-in: " + got.error);
			key_.clear();
			auto answer = ember::https_post(options_.server + "/api/token", {{"Content-Type", "application/json"}},
			                                "{\"code\":" + detail::json(got.code) + ",\"verifier\":" + detail::json(verifier) + "}");
			if (answer.status != 200) throw refused(answer.status, why(answer));
			connect();
			auth_->sign_in_with_custom_token(detail::text(ember::parse_json(answer.body)["token"]));
		}
		#endif

		std::string refresh_token() { connect(); return auth_->refresh_token(); }

		caller me() {
			auto v = ember::parse_json(get("/api/me"));
			caller out{detail::text(v["id"]), detail::text(v["name"]), "", detail::text(v["role"]), detail::flag(v["service"])};
			for (const auto& [name, value] : v.as_map()) if (name != "id" && name != "name" && name != "role" && name != "service") out.place = detail::text(value);
			return out;
		}

		// project::create: the id of what it changed, or refused.
		std::string project_create(const studio::project_create& sent) {
			return detail::text(ember::parse_json(post("/api/studio/project/create", sent.json()))["id"]);
		}
		// What it shows the one signed in.
		live<studio::projects> projects() { connect(); return {*store_, studio::projects::path(auth_->current_user().value().uid)}; }
		// signup::create: the id of what it changed, or refused.
		std::string signup_create(const waitlist::signup_create& sent) {
			return detail::text(ember::parse_json(post("/api/waitlist/signup/create", sent.json()))["id"]);
		}
		live<waitlist::signups> signups() { connect(); return {*store_, waitlist::signups::path()}; }

		private:
		options options_;
		ember::executor* deliver_on_;
		std::string key_;
		std::unique_ptr<ember::auth> auth_;
		std::unique_ptr<ember::database> store_;

		// Finds the app's Firebase project and key, from what Firebase Hosting serves at
		// /__/firebase/init.json, once, then makes its sign-in and its store.
		void connect() {
			if (store_) return;
			ember::options o = ember::options::for_project(options_.project);
			o.api_key = options_.api_key;
			o.firestore_host = options_.firestore_host;
			o.auth_host = options_.auth_host;
			if (o.project.empty()) {
				auto found = ember::https_get(options_.server + "/__/firebase/init.json", {});
				if (found.status != 200) throw refused(found.status, "the app's settings weren't found at " + options_.server);
				auto settings = ember::parse_json(found.body);
				o.project = detail::text(settings["projectId"]);
				if (o.api_key.empty()) o.api_key = detail::text(settings["apiKey"]);
			}
			auth_ = std::make_unique<ember::auth>(o);
			store_ = std::make_unique<ember::database>(o, auth_.get(), deliver_on_);
		}

		// Who's asking, for a command: a service's key, or a person's sign-in.
		std::vector<std::pair<std::string, std::string>> credentials() {
			std::vector<std::pair<std::string, std::string>> headers{{"Content-Type", "application/json"}};
			if (!key_.empty()) headers.emplace_back("Authorization", "Bearer " + key_);
			else if (auth_ && auth_->current_user()) headers.emplace_back("Authorization", "Bearer " + auth_->id_token());
			return headers;
		}

		std::string post(const std::string& route, const std::string& body) {
			auto answer = ember::https_post(options_.server + route, credentials(), body);
			if (answer.status != 200) throw refused(answer.status, why(answer));
			return answer.body;
		}

		std::string get(const std::string& route) {
			auto answer = ember::https_get(options_.server + route, credentials());
			if (answer.status != 200) throw refused(answer.status, why(answer));
			return answer.body;
		}

		static std::string why(const ember::response& answer) {
			try {
				auto said = detail::text(ember::parse_json(answer.body)["error"]);
				if (!said.empty()) return said;
			} catch (...) {}
			return "the app answered " + std::to_string(answer.status);
		}
	};

} // namespace uione
