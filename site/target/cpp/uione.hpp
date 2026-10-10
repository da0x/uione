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
