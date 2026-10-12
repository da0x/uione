// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// A window, cleared to a color, that follows the compositor's size and scale.

#include <print>

#include "loom/app.hpp"

int main() {
	auto app = loom::app::connect();
	if (!app) {
		std::println(stderr, "hello: {}", app.error());
		return 1;
	}
	auto& window = app->open({.title = "Hello, loom", .id = "org.loom.hello", .size = {900, 640}});
	window.background = loom::color::hex(0x2457d6);
	return app->run();
}
