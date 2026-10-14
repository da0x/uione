// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Boards, drawn: a list's rows as cards in columns, m on a card offering the steps
// its reader's roles may take, and Alt with the arrows putting cards in order.

#include <format>
#include <algorithm>
#include <map>
#include <optional>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		// A status's dot: gray not started, blue waiting, orange working, green done,
		// red failed.
		std::optional<color> status_color(const theme& t, const std::string& status) {
			if (status == "inactive") return t.muted;
			if (status == "waiting") return t.accent;
			if (status == "working") return color::hex(0xd9730d);
			if (status == "done") return t.success;
			if (status == "failed") return t.danger;
			return std::nullopt;
		}

	}  // namespace

	desktop::view board(page& p, board_spec spec) {
		const auto& t = current_theme();
		auto model = board_of(p, spec);
		if (model.loading) return desktop::text("Loading…", t.quiet());
		std::vector<desktop::view> parts;
		if (auto shown = track(model.track); shown.get()) parts.push_back(std::move(shown));
		p.wide();  // a board takes the window's width
		if (auto note = filter_note(p); note.get()) parts.push_back(std::move(note));
		if (!spec.search.empty()) parts.push_back(desktop::row(desktop::field(model.query, {.placeholder = "Find cards"}).width(320)));
		// The steps offered for the card being moved, above the columns.
		if (model.offer) {
			std::vector<desktop::list_row> steps_shown;
			for (const auto& step : model.offer->steps) steps_shown.push_back({step.value, desktop::text(step.label, t.body())});
			auto moving = model.moving;
			std::vector<desktop::view> offer;
			offer.push_back(desktop::text(std::format("Move “{}” to", model.offer->title), t.strong()));
			if (steps_shown.empty()) {
				offer.push_back(desktop::text("There's no step from here you may take.", t.quiet()));
			} else {
				offer.push_back(desktop::list(std::move(steps_shown), [&p, spec, card = model.offer->card](const std::string& to) { move_card(p, spec, card, to); })
				                    .autofocus()
				                    .on_key([moving](const key_event& e) {
					                    if (!e.is(keys::escape)) return false;
					                    moving.set("");
					                    return true;
				                    }));
			}
			offer.push_back(desktop::row(desktop::button("Cancel", [moving] { moving.set(""); })));
			parts.push_back(desktop::row(desktop::column(std::move(offer)).spacing(8).padding(14).width(360).background({.fill = t.surface, .border = t.accent, .border_width = 1.5f, .radius = t.corners})));
		}
		std::vector<desktop::view> lanes;
		std::map<std::string, std::vector<std::string>> reaching;  // where each card may go
		for (const auto& column : model.columns) {
			for (const auto& card : column.cards) reaching[card.id] = card.reaches;
		}
		for (const auto& column : model.columns) {
			std::vector<desktop::list_row> cards_shown;
			std::map<std::string, std::string> links;
			for (const auto& card : column.cards) {
				links[card.id] = card.link;
				std::vector<desktop::view> details;
				for (const auto& d : card.details) details.push_back(desktop::text(d, t.quiet()));
				for (const auto& word : card.labels) {
					auto look = t.quiet();
					look.color = t.accent;
					details.push_back(desktop::text(word, look).padding(edges(1, 7)).background({.fill = t.sunken, .radius = 9}));
				}
				std::vector<desktop::view> people;
				for (const auto& name : card.people) people.push_back(desktop::avatar(name, 20));
				// Who's on it and for how long, beside what it says of itself.
				if (!card.runner.empty()) details.push_back(desktop::text(card.runner, t.quiet()));
				if (!card.running.empty()) details.push_back(desktop::text(card.running, t.quiet()));
				std::vector<desktop::view> inside;
				if (auto dot = status_color(t, card.status)) {
					auto look = t.body();
					look.color = *dot;
					inside.push_back(desktop::row(desktop::text("●", look), desktop::text(card.title, t.body()).grow()).spacing(6));
				} else {
					inside.push_back(desktop::text(card.title, t.body()));
				}
				if (!details.empty()) inside.push_back(desktop::row(std::move(details)).spacing(6));
				if (!people.empty()) inside.push_back(desktop::row(std::move(people)).spacing(4));
				std::vector<desktop::view> shown;
				auto tone = tone_color(card.tone);
				if (tone) shown.push_back(desktop::edge(*tone));
				shown.push_back(desktop::column(std::move(inside)).spacing(6).grow());
				auto drawn = desktop::row(std::move(shown))
				                 .spacing(10)
				                 .padding(edges(10, 12, 10, tone ? 0.f : 12.f))
				                 .background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = std::min(t.corners, 8.f)});
				// A card with somewhere to go is dragged there.
				if (spec.move && !card.reaches.empty()) drawn = desktop::drag_source(std::move(drawn), card.id);
				cards_shown.push_back({card.id, std::move(drawn)});
			}
			std::vector<desktop::view> lane;
			// A column that chimes when a card arrives says so beside its count.
			if (column.notify) {
				lane.push_back(desktop::row(desktop::text(column.title, t.strong()), desktop::text(std::to_string(column.cards.size()), t.quiet()), desktop::text("♪", t.quiet()))
				                   .spacing(8));
			} else {
				lane.push_back(desktop::row(desktop::text(column.title, t.strong()), desktop::text(std::to_string(column.cards.size()), t.quiet())).spacing(8));
			}
			if (cards_shown.empty()) {
				lane.push_back(desktop::text("Nothing here", t.quiet()).padding(edges(4, 8)));
			} else {
				desktop::list_options options;
				if (!spec.link.empty()) {
					options.on_open = [&p, links](const std::string& card) {
						if (auto found = links.find(card); found != links.end() && !found->second.empty()) p.open(found->second);
					};
				}
				if (spec.move || model.arranging) {
					options.on_row_key = [&p, key = model.key, moving = model.moving, has_move = spec.move.has_value(), reorder = spec.reorder, arranging = model.arranging,
					                      in_column = column.rows](const std::string& card, const key_event& e) {
						if (has_move && e.is('m')) {
							moving.set(card);
							return true;
						}
						if (arranging && e.alt && (e.is(keys::up) || e.is(keys::down))) {
							move_row(p, key, *reorder, in_column, card, e.is(keys::up) ? -1 : 1);
							return true;
						}
						return false;
					};
				}
				lane.push_back(desktop::list(std::move(cards_shown), std::move(options)));
			}
			auto drawn = desktop::column(std::move(lane)).spacing(8).width(290).padding(12).background({.fill = t.sunken, .radius = t.corners});
			// A card dropped on a column it may go to moves there; one it can't is dimmed.
			if (spec.move) {
				auto page = t.page;
				page.alpha = 0.55f;
				drawn = desktop::drop_target(
				    std::move(drawn),
				    [reaching, to = column.id](const std::string& card) {
					    auto found = reaching.find(card);
					    return found != reaching.end() && std::ranges::contains(found->second, to);
				    },
				    [&p, spec, to = column.id](const std::string& card) { move_card(p, spec, card, to); },
				    {.border = t.accent, .border_width = 2, .radius = t.corners}, page);
			}
			lanes.push_back(std::move(drawn));
		}
		if (!model.error.empty()) {
			auto s = t.body();
			s.color = t.danger;
			parts.push_back(desktop::text(model.error, s));
		}
		parts.push_back(desktop::scroll_across(desktop::row(std::move(lanes)).spacing(14).align(align::stretch)));
		return desktop::column(std::move(parts)).spacing(12);
	}

}  // namespace one::desktop::runtime
