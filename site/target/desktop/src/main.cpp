// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site.one by one. Do not edit.

// uione as a desktop app, reading its views live from https://www.uione.io, or ONE_SERVER.
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
	auto window_app = one::desktop::app::connect();
	if (!window_app) {
		std::println(stderr, "uione: {}", window_app.error());
		return 1;
	}
	on_loop deliver;
	deliver.to = [&](std::function<void()> task) { window_app->post(std::move(task)); };
	std::string server = setting("ONE_SERVER", "https://www.uione.io");
	one::client::session session({.server = server, .project = setting("ONE_FIREBASE_PROJECT", ""), .firestore_host = setting("ONE_FIRESTORE_HOST", ""),
	                              .auth_host = setting("ONE_AUTH_HOST", "")}, &deliver);
	if (std::string key = setting("ONE_KEY", ""); !key.empty()) {
		try {
			session.sign_in_with_key(key);
		} catch (const std::exception& e) {
			std::println(stderr, "uione: {}", e.what());
			return 3;
		}
	}
	one::screens::client_source source(session, deliver.to);
	app::site s{.name = "uione", .id = "org.uione.uione"};
	{
		one::desktop::theme t = one::desktop::theme::harbor();
		t.page = one::desktop::color::hex(0xf7f8fa);
		t.surface = one::desktop::color::hex(0xffffff);
		t.sunken = one::desktop::color::hex(0xeef1f5);
		t.ink = one::desktop::color::hex(0x0e1726);
		t.muted = one::desktop::color::hex(0x5b6577);
		t.line = one::desktop::color::hex(0xdde2ea);
		t.accent = one::desktop::color::hex(0x2457d6);
		t.danger = one::desktop::color::hex(0xc2312d);
		t.success = one::desktop::color::hex(0x1a7f37);
		t.warning = one::desktop::color::hex(0x9a6700);
		t.text_family = "IBM Plex Sans";
		t.code_family = "IBM Plex Mono";
		t.corners = 10;
		t.raised = true;
		s.look = t;
	}
	app::site_parts(s);
	app::about_screens(s);
	app::home_screens(s);
	app::install_screens(s);
	app::language_screens(s);
	app::mission_screens(s);
	app::releases_screens(s);
	app::themes_screens(s);
	return app::run(s, *window_app, source, count > 1 ? given[1] : "/");
}
