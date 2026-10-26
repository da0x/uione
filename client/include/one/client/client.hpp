// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::client: what a desktop or terminal app uione writes uses to reach its app, the
// way @uione/react's data source does for the web. It signs a person in, through the
// browser, a sign-in kept from before, or a service's key; it listens to views by
// name, live, through libember; it runs commands by name, as JSON; and it says what
// the person's roles allow.
//
//     one::client::session app({.server = "https://example.org"}, &loop);
//     app.sign_in_with_refresh_token(kept->refresh_token);
//     auto listening = app.listen("projects::issue_page", "tracker-42", [](const ember::snapshot& s) {
//         std::println("{}", one::client::text(one::client::field(s.data, "title")));
//     });
//     app.run("projects::comment::create", ember::value(std::map<std::string, ember::value>{{"issue", "tracker-42"}, {"body", "Seen."}}));
//
// Views and commands are named as the .one files name them, so what a generated
// screen reads and runs is written plainly, and the names are checked by the
// compiler, not here.

#pragma once

#include <ember.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace one::client {

	struct options {
		std::string server{};          // like https://example.org
		std::string project{};         // its Firebase project; asked of the server when empty
		std::string api_key{};         // asked of the server with the project when empty
		std::string firestore_host{};  // the Firestore emulator, like localhost:8080
		std::string auth_host{};       // the Auth emulator, like localhost:9099
	};

	// A command or a sign-in the app turned down, with what it said, or a status of 0
	// for something that went wrong here.
	struct refused : std::runtime_error {
		int status;
		refused(int status, const std::string& message) : std::runtime_error(message), status(status) {}
	};

	// Who's signed in, as the app's /api/me says.
	struct person {
		std::string id, name, role;
		std::string place;  // what a service's role is in, like a project
		bool service = false;
	};

	// Reading a view's document: a field by its name as the view stores it, like
	// "stage.title", or through maps, like "board.stage"; null when there's none.
	const ember::value& field(const ember::value& document, std::string_view path);
	std::string text(const ember::value& v);
	double number(const ember::value& v);
	bool flag(const ember::value& v);
	std::vector<std::string> texts(const ember::value& v);
	std::chrono::sys_time<std::chrono::nanoseconds> when(const ember::value& v);
	// The rows of a list in a view's document.
	const std::vector<ember::value>& rows(const ember::value& document, std::string_view list = "rows");

	// Whether the person's roles, a <member>_roles view's document, allow a command
	// within a place, like issue::move in a project: a row for that place whose role
	// lists the command. `place_field` names the place in a row, like project, and
	// `allowed` the list of what its role may do, like role.may.
	bool allows(const ember::value& roles, std::string_view place_field, std::string_view place, std::string_view command,
	            std::string_view allowed = "role.may");
	// Whether they hold one of these roles within a place.
	bool holds(const ember::value& roles, std::string_view place_field, std::string_view place, const std::vector<std::string>& granting);

	class session {
	public:
		explicit session(options o, ember::executor* deliver_on = nullptr);
		~session();
		session(const session&) = delete;
		session& operator=(const session&) = delete;

		// Signing in: as a service, by its key; as a person, with a refresh token kept
		// from a sign-in before, or through their browser, where open(url) shows them
		// the app's authorize page and the code comes back to 127.0.0.1.
		void sign_in_with_key(const std::string& key);
		void sign_in_with_refresh_token(const std::string& token);
		void sign_in_with_browser(const std::function<void(const std::string& url)>& open);
		bool signed_in() const;
		std::string uid() const;            // the person's, or the service's
		std::string refresh_token();        // to keep, so they needn't sign in again
		person me();

		// A view's document, as it is and each time it changes: views/<view>, or
		// views/<view>:<subject> for a view per something, and per person with uid().
		ember::database::registration listen(const std::string& view, const std::string& subject, std::function<void(const ember::snapshot&)> on_change,
		                                     std::function<void(const ember::error&)> on_error = {});
		ember::snapshot get(const std::string& view, const std::string& subject = "");
		static std::string path(const std::string& view, const std::string& subject);

		// Runs a command, like projects::issue::move, with its fields; the id of what it
		// changed, or refused with what the app said.
		std::string run(std::string_view command, const ember::value& fields);

		// Uploads a file on this machine for a file field, as the web does: the app says
		// where it goes and may refuse it, like one too large. The path the field then
		// holds, or refused.
		std::string upload(const std::string& local);

		const options& settings() const { return options_; }

	private:
		void connect();
		std::vector<std::pair<std::string, std::string>> credentials();
		std::string post(const std::string& route, const std::string& body);
		std::string read(const std::string& route);

		options options_;
		ember::executor* deliver_on_;
		std::string key_;
		std::unique_ptr<ember::auth> auth_;
		std::unique_ptr<ember::database> store_;
	};

	// A sign-in kept between runs, in $XDG_CONFIG_HOME/<app>/credentials, readable by
	// its owner alone; refused when others can read it.
	struct kept {
		std::string server{}, key{}, refresh_token{};
	};
	std::filesystem::path kept_at(const std::string& app);
	std::optional<kept> read_kept(const std::string& app, std::string& problem);

	// What kind of file a name says it is, like image/png, by its extension.
	std::string type_of(const std::string& name);
	void keep(const std::string& app, const kept& sign_in);

}  // namespace one::client
