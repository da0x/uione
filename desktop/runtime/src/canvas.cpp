// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Canvases, drawn: a list's rows as cards placed anywhere on a surface, dragged and
// resized, and placed by the canvas's command once let go.

#include <algorithm>
#include <map>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view canvas_board(page& p, canvas_spec spec) {
		const auto& t = current_theme();
		auto model = canvas_of(p, spec);
		if (model.loading) return desktop::text("Loading…", t.quiet());
		p.wide();  // a canvas takes the window's width
		std::vector<desktop::surface_item> items;
		std::map<std::string, std::string> links;
		for (const auto& card : model.cards) {
			links[card.id] = card.link;
			std::vector<desktop::view> inside;
			// Text is drawn large, on its own; a note with what it says of itself.
			auto title = card.kind == "text" ? t.heading(18) : t.body();
			if (card.status.empty()) {
				inside.push_back(desktop::text(card.title, title));
			} else {
				auto dot = t.body();
				dot.color = card.status == "inactive" ? t.muted
				            : card.status == "waiting" ? t.accent
				            : card.status == "working" ? color::hex(0xd9730d)
				            : card.status == "done"    ? t.success
				                                       : t.danger;
				inside.push_back(desktop::row(desktop::text("●", dot), desktop::text(card.title, title).grow()).spacing(6));
			}
			if (card.kind != "text") {
				for (const auto& d : card.details) inside.push_back(desktop::text(d, t.quiet()));
				if (!card.runner.empty() || !card.running.empty()) inside.push_back(desktop::text(card.runner + (card.runner.empty() ? "" : " ") + card.running, t.quiet()));
			}
			auto tone = tone_color(card.tone);
			std::vector<desktop::view> shown;
			if (tone) shown.push_back(desktop::edge(*tone));
			inside.push_back(desktop::spacer());  // what it says, from the top
			shown.push_back(desktop::column(std::move(inside)).spacing(4).grow());
			items.push_back({.key = card.id,
			                 .x = static_cast<float>(card.x),
			                 .y = static_cast<float>(card.y),
			                 .width = static_cast<float>(card.w),
			                 .height = static_cast<float>(card.h),
			                 .content = desktop::row(std::move(shown)).spacing(8).padding(edges(10, 12)).align(align::stretch),
			                 .movable = model.placing});
		}
		desktop::surface_options options{.grid = static_cast<float>(model.grid), .origin_x = static_cast<float>(model.origin_x), .origin_y = static_cast<float>(model.origin_y)};
		if (model.placing) {
			options.on_place = [&p, spec](const std::string& card, float x, float y, float w, float h) { place_card(p, spec, card, x, y, w, h); };
		}
		if (!spec.link.empty()) {
			options.on_open = [&p, links](const std::string& card) {
				if (auto found = links.find(card); found != links.end() && !found->second.empty()) p.open(found->second);
			};
		}
		std::vector<desktop::view> parts;
		if (auto note = filter_note(p); note.get()) parts.push_back(std::move(note));
		parts.push_back(desktop::surface(std::move(items), std::move(options)));
		parts.push_back(desktop::text("Drag a card to move it, or its corner to resize it; drag the space to look around, and + or − to zoom.", t.quiet()));
		return desktop::column(std::move(parts)).spacing(8);
	}

}  // namespace one::desktop::runtime
