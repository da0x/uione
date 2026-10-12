// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// How things look: the same roles a uione theme gives, so an app looks as it does on
// the web, and the text styles worked out from them.

#pragma once

#include <string>

#include "loom/app.hpp"
#include "loom/text.hpp"

namespace loom {

	struct theme {
		color page = color::hex(0xf7f8fa);     // behind everything
		color surface = color::hex(0xffffff);  // panels and cards
		color sunken = color::hex(0xeef1f5);   // wells, and what's chosen
		color ink = color::hex(0x0e1726);      // text
		color muted = color::hex(0x5b6577);    // text that says less
		color line = color::hex(0xdde2ea);     // borders and rules
		color accent = color::hex(0x2457d6);   // what's pressed, chosen or linked
		color danger = color::hex(0xc2312d);
		color success = color::hex(0x1a7f37);
		color warning = color::hex(0x9a6700);
		std::string text_family = "IBM Plex Sans";
		std::string code_family = "IBM Plex Mono";
		float corners = 10;
		bool raised = true;  // panels cast a shadow, or sit flat
		float text_size = 15;

		// uione's harbor, which its other themes start from.
		static theme harbor() { return {}; }

		text_style body() const { return {.family = text_family, .size = text_size, .color = ink}; }
		text_style quiet() const { return {.family = text_family, .size = text_size - 2, .color = muted}; }
		text_style strong() const { return {.family = text_family, .size = text_size, .weight = 600, .color = ink}; }
		text_style heading(float size = 22) const { return {.family = text_family, .size = size, .weight = 600, .color = ink, .line_height = 1.3f}; }
		text_style code() const { return {.family = code_family, .size = text_size - 1, .color = ink}; }
	};

	// The theme views are built and drawn with: the window's, while it builds and draws.
	const theme& current_theme();

}  // namespace loom
