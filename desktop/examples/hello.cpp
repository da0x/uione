// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A window, cleared to a color, that follows the compositor's size and scale.

#include <print>

#include "one/desktop/app.hpp"

int main() {
	auto app = one::desktop::app::connect();
	if (!app) {
		std::println(stderr, "hello: {}", app.error());
		return 1;
	}
	auto& window = app->open({.title = "Hello", .id = "org.uione.hello", .size = {900, 640}});
	window.background = one::desktop::color::hex(0x2457d6);
	return app->run();
}
