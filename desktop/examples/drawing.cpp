// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What one::desktop draws with: boxes with corners, borders and shadows, and text in a few
// styles, wrapped to fit a card.

#include <print>

#include "one/desktop/app.hpp"
#include "one/desktop/canvas.hpp"

int main() {
	auto app = one::desktop::app::connect();
	if (!app) {
		std::println(stderr, "drawing: {}", app.error());
		return 1;
	}
	auto& window = app->open({.title = "Drawing", .id = "org.uione.drawing", .size = {720, 480}});
	window.background = one::desktop::color::hex(0xf7f8fa);
	window.on_draw = [](one::desktop::canvas& c) {
		using one::desktop::color;
		one::desktop::text_style body{.size = 15};
		one::desktop::text_style heading{.size = 22, .weight = 600};
		one::desktop::text_style muted{.size = 13, .color = color::hex(0x5b6577)};

		one::desktop::rect card{40, 40, 400, 220};
		c.shadow(card, 10, 18, color::hex(0x0e1726, 0.18f), {0, 6});
		c.box(card, {.fill = color::hex(0xffffff), .border = color::hex(0xdde2ea), .border_width = 1, .radius = 10});
		c.box({card.x, card.y + 16, 4, 28}, {.fill = color::hex(0xc2312d), .radius = 2});
		auto title = one::desktop::lay_out({"Title is already taken after renaming a phase", heading}, card.width - 48, c.scale());
		c.text(title, {card.x + 24, card.y + 16});
		std::vector<one::desktop::text_span> words{
		    {"Renaming a phase keeps its old ", body},
		    {"name", {.family = "monospace", .size = 14, .color = color::hex(0x2457d6)}},
		    {", so a new phase titled the same is ", body},
		    {"refused", {.size = 15, .weight = 700}},
		    {". It should take the new title's slug instead. ✓ Ünïcödé, 日本語 and العربية too.", body},
		};
		auto text = one::desktop::lay_out(words, card.width - 48, c.scale());
		c.text(text, {card.x + 24, card.y + 24 + title.height()});
		auto note = one::desktop::lay_out({"#16 · Reported · opened 2 days ago", muted}, 0, c.scale());
		c.text(note, {card.x + 24, card.bottom() - 16 - note.height()});

		c.box({480, 40, 200, 44}, {.fill = color::hex(0x2457d6), .radius = 8});
		auto button = one::desktop::lay_out({"Take it", {.size = 15, .weight = 600, .color = color::hex(0xffffff)}}, 0, c.scale());
		c.text(button, {480 + (200 - button.width()) / 2, 40 + (44 - button.height()) / 2});
		c.box({480, 100, 200, 44}, {.fill = color::hex(0xffffff), .border = color::hex(0xdde2ea), .border_width = 1, .radius = 8});
		auto second = one::desktop::lay_out({"Let it go", body}, 0, c.scale());
		c.text(second, {480 + (200 - second.width()) / 2, 100 + (44 - second.height()) / 2});
		for (int i = 0; i < 5; ++i) c.box({480.f + i * 40, 170, 32, 32}, {.fill = color::hex(0x2457d6, 0.2f * (i + 1)), .radius = 16});
	};
	return app->run();
}
