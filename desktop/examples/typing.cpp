// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Fields: a title of one line, submitted with Enter, and a description of several,
// with what's typed shown beneath as it's kept.

#include <format>
#include <print>

#include "one/desktop/app.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/theme.hpp"
#include "one/desktop/ui.hpp"

int main() {
	auto app = one::desktop::app::connect();
	if (!app) {
		std::println(stderr, "typing: {}", app.error());
		return 1;
	}
	auto& window = app->open({.title = "Typing", .id = "org.uione.typing", .size = {640, 480}});
	one::desktop::state<std::string> title, description;
	one::desktop::state<std::string> submitted;
	window.background = one::desktop::theme::harbor().page;
	window.show([=] {
		const auto& t = one::desktop::current_theme();
		return one::desktop::column(one::desktop::text("New issue", t.heading()),
		                            one::desktop::field(title, {.placeholder = "Title", .on_submit = [=] { submitted.set(title.peek()); }}).autofocus(),
		                            one::desktop::field(description, {.placeholder = "Describe it", .multiline = true}),
		                            one::desktop::text(submitted->empty() ? "Nothing submitted yet" : std::format("Submitted: {}", *submitted), t.quiet()))
		    .spacing(12)
		    .padding(24);
	});
	return app->run();
}
