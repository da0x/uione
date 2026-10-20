// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/client/client.hpp"


#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include "browser.hpp"

namespace one::client {

	// Reading

	const ember::value& field(const ember::value& document, std::string_view path) {
		if (const ember::value* stored = document.find(path)) return *stored;
		static const ember::value none;
		const ember::value* at = &document;
		std::size_t from = 0;
		while (at && from <= path.size()) {
			std::size_t dot = path.find('.', from);
			std::string_view part = path.substr(from, dot == std::string_view::npos ? std::string_view::npos : dot - from);
			at = at->find(part);
			if (dot == std::string_view::npos) break;
			from = dot + 1;
		}
		return at ? *at : none;
	}

	std::string text(const ember::value& v) { return v.is_string() ? v.as_string() : std::string(); }
	double number(const ember::value& v) { return v.is_integer() || v.is_double() ? v.as_double() : 0; }
	bool flag(const ember::value& v) { return v.is_bool() && v.as_bool(); }
	std::vector<std::string> texts(const ember::value& v) {
		std::vector<std::string> out;
		for (const auto& item : v.as_array()) out.push_back(text(item));
		return out;
	}
	std::chrono::sys_time<std::chrono::nanoseconds> when(const ember::value& v) { return v.as_time(); }
	const std::vector<ember::value>& rows(const ember::value& document, std::string_view list) { return document[list].as_array(); }

	bool allows(const ember::value& roles, std::string_view place_field, std::string_view place, std::string_view command, std::string_view allowed) {
		if (place.empty()) return false;
		return std::ranges::any_of(rows(roles), [&](const ember::value& row) {
			if (text(field(row, place_field)) != place) return false;
			return std::ranges::any_of(field(row, allowed).as_array(), [&](const ember::value& may) { return text(may) == command; });
		});
	}

	bool holds(const ember::value& roles, std::string_view place_field, std::string_view place, const std::vector<std::string>& granting) {
		if (place.empty()) return false;
		return std::ranges::any_of(rows(roles), [&](const ember::value& row) {
			return text(field(row, place_field)) == place && std::ranges::contains(granting, text(row["role"]));
		});
	}

	// session

	session::session(options o, ember::executor* deliver_on) : options_(std::move(o)), deliver_on_(deliver_on) {}
	session::~session() = default;

	void session::sign_in_with_key(const std::string& key) {
		key_ = key;
		auto token = post("/api/token", "{}");
		connect();
		auth_->sign_in_with_custom_token(text(ember::parse_json(token)["token"]));
	}

	void session::sign_in_with_refresh_token(const std::string& token) {
		key_.clear();
		connect();
		auth_->sign_in_with_refresh_token(token);
	}

#if defined(__unix__) || defined(__APPLE__)
	void session::sign_in_with_browser(const std::function<void(const std::string& url)>& open) {
		detail::loopback waiting;
		std::string verifier = detail::random_text(32), state = detail::random_text(16);
		open(options_.server + "/authorize?port=" + std::to_string(waiting.port()) + "&state=" + state +
		     "&challenge=" + detail::base64url(detail::sha256(verifier)));
		auto got = waiting.wait(state, options_.server + "/authorize", std::chrono::minutes(5));
		if (got.code.empty()) throw refused(403, got.error == "denied" ? "it was turned down in the browser" : "the browser sent no sign-in: " + got.error);
		key_.clear();
		auto answer = ember::https_post(options_.server + "/api/token", {{"Content-Type", "application/json"}},
		                                ember::to_json(ember::value(std::map<std::string, ember::value>{{"code", got.code}, {"verifier", verifier}})));
		if (answer.status != 200) throw refused(answer.status, "the app turned the sign-in down");
		connect();
		auth_->sign_in_with_custom_token(text(ember::parse_json(answer.body)["token"]));
	}
#endif

	bool session::signed_in() const { return auth_ && auth_->current_user().has_value(); }

	std::string session::uid() const {
		if (!auth_) return "";
		auto user = auth_->current_user();
		return user ? user->uid : "";
	}

	std::string session::refresh_token() {
		connect();
		return auth_->refresh_token();
	}

	person session::me() {
		auto v = ember::parse_json(read("/api/me"));
		person out{text(v["id"]), text(v["name"]), text(v["role"]), "", flag(v["service"])};
		for (const auto& [name, value] : v.as_map()) {
			if (name != "id" && name != "name" && name != "role" && name != "service") out.place = text(value);
		}
		return out;
	}

	std::string session::path(const std::string& view, const std::string& subject) {
		return "views/" + view + (subject.empty() ? "" : ":" + subject);
	}

	ember::database::registration session::listen(const std::string& view, const std::string& subject, std::function<void(const ember::snapshot&)> on_change,
	                                              std::function<void(const ember::error&)> on_error) {
		connect();
		return store_->listen(path(view, subject), std::move(on_change), std::move(on_error));
	}

	ember::snapshot session::get(const std::string& view, const std::string& subject) {
		connect();
		return store_->get(path(view, subject));
	}

	std::string session::run(std::string_view command, const ember::value& fields) {
		// projects::issue::move is posted to /api/projects/issue/move.
		std::string route = "/api";
		for (std::size_t from = 0; from <= command.size();) {
			std::size_t next = command.find("::", from);
			route += "/" + std::string(command.substr(from, next == std::string_view::npos ? std::string_view::npos : next - from));
			if (next == std::string_view::npos) break;
			from = next + 2;
		}
		return text(ember::parse_json(post(route, ember::to_json(fields.is_map() ? fields : ember::value::map())))["id"]);
	}

	std::string session::upload(const std::string& local) {
		std::ifstream in(local, std::ios::binary | std::ios::ate);
		if (!in) throw refused(0, local + " can't be read");
		std::string body(static_cast<std::size_t>(in.tellg()), '\0');
		in.seekg(0);
		in.read(body.data(), static_cast<std::streamsize>(body.size()));
		std::string name = std::filesystem::path(local).filename().string();
		ember::value asked(std::map<std::string, ember::value>{{"name", name}, {"type", type_of(name)}, {"size", static_cast<double>(body.size())}});
		auto given = ember::parse_json(post("/api/files", ember::to_json(asked)));
		std::vector<std::pair<std::string, std::string>> headers;
		for (const auto& [header, value] : given["headers"].as_map()) headers.emplace_back(header, text(value));
		std::string to = text(given["url"]);
		auto answer = text(given["method"]) == "PUT" ? ember::https_put(to, headers, body) : ember::https_post(to, headers, body);
		if (answer.status >= 300) throw refused(answer.status, name + " didn't upload");
		return text(given["path"]);
	}

	std::string type_of(const std::string& name) {
		static const std::map<std::string, std::string> types{
		    {".png", "image/png"},        {".jpg", "image/jpeg"},      {".jpeg", "image/jpeg"},      {".gif", "image/gif"},   {".webp", "image/webp"},
		    {".svg", "image/svg+xml"},    {".pdf", "application/pdf"}, {".txt", "text/plain"},       {".log", "text/plain"},  {".md", "text/markdown"},
		    {".json", "application/json"}, {".csv", "text/csv"},       {".zip", "application/zip"},  {".mp4", "video/mp4"},   {".webm", "video/webm"},
		    {".mov", "video/quicktime"},  {".mp3", "audio/mpeg"},      {".wav", "audio/wav"},        {".html", "text/html"},  {".tar", "application/x-tar"},
		    {".gz", "application/gzip"}};
		std::string extension = std::filesystem::path(name).extension().string();
		std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		auto found = types.find(extension);
		return found == types.end() ? "application/octet-stream" : found->second;
	}

	// Finds the app's Firebase project and key, from what Firebase Hosting serves at
	// /__/firebase/init.json, once, then makes its sign-in and its store.
	void session::connect() {
		if (store_) return;
		ember::options o = ember::options::for_project(options_.project);
		o.api_key = options_.api_key;
		o.firestore_host = options_.firestore_host;
		o.auth_host = options_.auth_host;
		if (o.project.empty()) {
			auto found = ember::https_get(options_.server + "/__/firebase/init.json", {});
			if (found.status != 200) throw refused(found.status, "the app's settings weren't found at " + options_.server);
			auto settings = ember::parse_json(found.body);
			o.project = text(settings["projectId"]);
			if (o.api_key.empty()) o.api_key = text(settings["apiKey"]);
		}
		if (o.api_key.empty()) o.api_key = "local";  // the emulators take any
		auth_ = std::make_unique<ember::auth>(o);
		store_ = std::make_unique<ember::database>(o, auth_.get(), deliver_on_);
	}

	// Who's asking, for a command: a service's key, or a person's sign-in.
	std::vector<std::pair<std::string, std::string>> session::credentials() {
		std::vector<std::pair<std::string, std::string>> headers{{"Content-Type", "application/json"}};
		if (!key_.empty()) headers.emplace_back("Authorization", "Bearer " + key_);
		else if (auth_ && auth_->current_user()) headers.emplace_back("Authorization", "Bearer " + auth_->id_token());
		return headers;
	}

	namespace {
		std::string why(const ember::response& answer) {
			try {
				auto said = text(ember::parse_json(answer.body)["error"]);
				if (!said.empty()) return said;
			} catch (...) {
			}
			return "the app answered " + std::to_string(answer.status);
		}
	}  // namespace

	namespace {
		// An app that says to slow down is asked again once the wait it gives is over,
		// up to five times, so a program sending many commands slows down rather than
		// stopping partway. A request turned down this way was never run, so sending it
		// again is safe. A wait of more than a minute, like a day's most being made, is
		// passed on rather than waited out.
		template <class Ask>
		ember::response patiently(Ask ask) {
			for (int tries = 1;; ++tries) {
				auto answer = ask();
				if (answer.status != 429 || tries == 5) return answer;
				std::int64_t wait = 1;
				try {
					wait = ember::parse_json(answer.body)["retry_after"].as_integer();
				} catch (...) {
				}
				if (wait > 60) return answer;
				std::this_thread::sleep_for(std::chrono::seconds(std::max<std::int64_t>(wait, 1)));
			}
		}
	}  // namespace

	std::string session::post(const std::string& route, const std::string& body) {
		auto answer = patiently([&] { return ember::https_post(options_.server + route, credentials(), body); });
		if (answer.status != 200) throw refused(answer.status, why(answer));
		return answer.body;
	}

	std::string session::read(const std::string& route) {
		auto answer = patiently([&] { return ember::https_get(options_.server + route, credentials()); });
		if (answer.status != 200) throw refused(answer.status, why(answer));
		return answer.body;
	}

	// Kept sign-ins

	std::filesystem::path kept_at(const std::string& app) {
		const char* config = std::getenv("XDG_CONFIG_HOME");
		const char* home = std::getenv("HOME");
		std::filesystem::path base = config && *config ? std::filesystem::path(config) : std::filesystem::path(home ? home : ".") / ".config";
		return base / app / "credentials";
	}

	std::optional<kept> read_kept(const std::string& app, std::string& problem) {
		auto at = kept_at(app);
		std::error_code missing;
		auto status = std::filesystem::status(at, missing);
		if (missing || !std::filesystem::exists(status)) {
			problem = "not signed in yet";
			return std::nullopt;
		}
		using std::filesystem::perms;
		if ((status.permissions() & (perms::group_all | perms::others_all)) != perms::none) {
			problem = at.string() + " can be read by others; run chmod 600 " + at.string();
			return std::nullopt;
		}
		std::ifstream in(at);
		std::stringstream whole;
		whole << in.rdbuf();
		auto v = ember::parse_json(whole.str());
		return kept{text(v["server"]), text(v["key"]), text(v["refresh_token"])};
	}

	void keep(const std::string& app, const kept& sign_in) {
		auto at = kept_at(app);
		std::filesystem::create_directories(at.parent_path());
		std::map<std::string, ember::value> fields{{"server", sign_in.server}};
		if (!sign_in.key.empty()) fields.emplace("key", sign_in.key);
		if (!sign_in.refresh_token.empty()) fields.emplace("refresh_token", sign_in.refresh_token);
		// Written readable by its owner alone before anything is in it.
		{
			std::ofstream out(at, std::ios::trunc);
		}
		std::filesystem::permissions(at, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
		std::ofstream(at, std::ios::trunc) << ember::to_json(ember::value(std::move(fields))) << "\n";
	}

}  // namespace one::client
