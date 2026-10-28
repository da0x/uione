// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Calendars, drawn: a list's rows on their days, a week of days side by side or a
// month's grid, under what it's on and the ways back, to today and on; a row picked
// with a click and dragged to another day to move it there (decision 0023).

#include <format>
#include <map>
#include <optional>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		// A row on its day, as wide as it: its tone down its side, its time of day if it
		// has one, its title, then what else it shows.
		desktop::view calendar_card_of(const theme& t, const calendar_card& card) {
			std::vector<desktop::view> inside;
			auto quiet = t.quiet();
			if (!card.time.empty()) inside.push_back(desktop::text(card.time, quiet));
			inside.push_back(desktop::text(card.title, t.body()));
			for (const auto& d : card.details) inside.push_back(desktop::text(d, quiet));
			std::vector<desktop::view> shown;
			auto tone = tone_color(card.tone);
			if (tone) shown.push_back(desktop::edge(*tone));
			shown.push_back(desktop::column(std::move(inside)).spacing(2).grow());
			return desktop::row(std::move(shown))
			    .spacing(8)
			    .padding(edges(6, 8, 6, tone ? 0.f : 8.f))
			    .background({.fill = t.surface,
			                 .border = card.picked ? t.accent : t.line,
			                 .border_width = card.picked ? 2.f : 1.f,
			                 .radius = std::min(t.corners, 6.f)});
		}

	}  // namespace

	desktop::view calendar(page& p, calendar_spec spec) {
		const auto& t = current_theme();
		auto model = calendar_of(p, spec);
		if (model.loading) return desktop::text("Loading…", t.quiet());
		p.wide();  // a calendar takes the window's width
		// What it's on, and the ways back, to today and on, Page Up and Page Down too.
		auto back = [&p, spec] { calendar_go(p, spec, -1); };
		auto on = [&p, spec] { calendar_go(p, spec, 1); };
		p.on_key("page_up", back);
		p.on_key("page_down", on);
		std::vector<desktop::view> parts;
		auto heading = t.heading(18);
		parts.push_back(desktop::row(desktop::text(model.title, heading).grow(), desktop::tool_button("previous", model.month ? "Last month" : "Last week", back),
		                             desktop::button("Today", [&p, spec] { calendar_go(p, spec, 0); }), desktop::tool_button("next", model.month ? "Next month" : "Next week", on))
		                    .spacing(6)
		                    .align(align::center));
		if (!model.error.empty()) {
			auto s = t.body();
			s.color = t.danger;
			parts.push_back(desktop::text(model.error, s));
		}
		// A day: its name and number, the accent's today, then its rows, each picked with a
		// click and dragged to another day; in a month's grid, the first few, then how
		// many more.
		auto day_of = [&](const calendar_day& day) {
			std::vector<desktop::view> inside;
			auto said = day.today ? t.strong() : t.quiet();
			if (day.today) said.color = t.accent;
			if (!day.in_month) said.color.alpha = 0.5f;
			inside.push_back(model.month ? desktop::text(std::to_string(day.day), said) : desktop::text(std::format("{} {}", day.weekday, day.day), said));
			std::size_t shown = model.month ? std::min<std::size_t>(day.cards.size(), 3) : day.cards.size();
			std::vector<desktop::list_row> rows;
			for (std::size_t i = 0; i < shown; ++i) {
				auto card = calendar_card_of(t, day.cards[i]);
				if (!spec.move.empty()) card = desktop::drag_source(std::move(card), day.cards[i].id);
				rows.push_back({day.cards[i].id, std::move(card)});
			}
			if (!rows.empty()) {
				desktop::list_options options;
				if (!spec.pick.empty()) {
					options.on_open = [&p, name = spec.pick](const std::string& row) { p.pick(name, row); };
					options.start = p.picked_id(spec.pick);
				}
				inside.push_back(desktop::list(std::move(rows), std::move(options)));
			}
			if (shown < day.cards.size()) inside.push_back(desktop::text(std::format("{} more", day.cards.size() - shown), t.quiet()));
			auto drawn = desktop::column(std::move(inside))
			                 .align(align::stretch)
			                 .spacing(6)
			                 .padding(8)
			                 .grow()
			                 .background({.fill = day.in_month ? t.surface : t.sunken, .border = t.line, .border_width = 1, .radius = t.corners});
			// A row dropped on another day moves there.
			if (!spec.move.empty()) {
				auto page = t.page;
				page.alpha = 0.55f;
				drawn = desktop::drop_target(
				    std::move(drawn), [](const std::string&) { return true; }, [&p, spec, date = day.date](const std::string& row) { calendar_move(p, spec, row, date); },
				    {.border = t.accent, .border_width = 2, .radius = t.corners}, page);
			}
			return std::move(drawn).grow();
		};
		std::vector<desktop::view> weeks;
		for (std::size_t first = 0; first < model.days.size(); first += 7) {
			std::vector<desktop::view> days;
			for (std::size_t i = first; i < std::min(first + 7, model.days.size()); ++i) days.push_back(day_of(model.days[i]));
			weeks.push_back(desktop::row(std::move(days)).spacing(8).align(align::stretch).grow());
		}
		parts.push_back(desktop::column(std::move(weeks)).spacing(8).grow());
		return desktop::column(std::move(parts)).spacing(12).grow();
	}

}  // namespace one::desktop::runtime
