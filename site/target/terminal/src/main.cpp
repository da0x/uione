// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site.one by one. Do not edit.

// uione as a terminal app, reading its views live from https://www.uione.io, or ONE_SERVER.
//
//   uione            its first screen
//   uione /address   the screen at an address, like /projects

#include <cstdlib>
#include <print>

#include <one/screens/client_source.hpp>

#include "app.hpp"

namespace {

	std::string setting(const char* name, std::string otherwise) {
		const char* given = std::getenv(name);
		return given && *given ? given : otherwise;
	}

	// Where libember tells what it hears: on the app's loop, where the screens' states are set.
	struct on_loop : ember::executor {
		std::function<void(std::function<void()>)> to;
		void post(std::function<void()> task) override { to(std::move(task)); }
	};

}  // namespace

int main(int count, char** given) {
	one::terminal::view_app window_app;
	on_loop deliver;
	deliver.to = [&](std::function<void()> task) { window_app.post(std::move(task)); };
	std::string server = setting("ONE_SERVER", "https://www.uione.io");
	one::client::session session({.server = server, .project = setting("ONE_FIREBASE_PROJECT", ""), .firestore_host = setting("ONE_FIRESTORE_HOST", ""),
	                              .auth_host = setting("ONE_AUTH_HOST", "")}, &deliver);
	one::screens::client_source source(session, deliver.to);
	app::site s{.name = "uione", .id = "org.uione.uione"};
	app::site_parts(s);
	app::about_screens(s);
	app::home_screens(s);
	app::install_screens(s);
	app::language_screens(s);
	app::mission_screens(s);
	app::releases_screens(s);
	return app::run(s, window_app, source, count > 1 ? given[1] : "/");
}
