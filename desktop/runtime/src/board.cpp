// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Boards: a list's rows as cards in columns, moved along the steps between them
// that the person's roles may take.

#include <algorithm>
#include <format>
#include <set>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		std::string lower(std::string s) {
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		// The roles the person holds within a place, by their ids.
		std::set<std::string> roles_held(const board_move& move) {
			std::set<std::string> out;
			if (!move.roles) return out;
			for (const auto& row : rows_of(move.roles->get())) {
				if (field_of(row, move.place).as_string() == move.within) out.insert(field_of(row, move.role).as_string());
			}
			return out;
		}

	}  // namespace

	std::optional<color> tint_of(const value& row, const std::optional<tint_spec>& tint) {
		if (!tint) return std::nullopt;
		const auto& t = current_theme();
		auto at = std::ranges::find(tint->order, show(field_of(row, tint->field)));
		int tone = at == tint->order.end() || tint->order.size() == 1 ? 3 : static_cast<int>(std::lround(1 + static_cast<double>(at - tint->order.begin()) * 4 / static_cast<double>(tint->order.size() - 1)));
		switch (tone) {
			case 1: return t.danger;
			case 2: return color::hex(0xd4700b);
			case 4: return t.success;
			case 5: return t.accent;
			default: return std::nullopt;
		}
	}

	desktop::view board(page& p, board_spec spec) {
		const auto& t = current_theme();
		const view_state& v = *spec.view;
		const view_state& over = *spec.over;
		if (v.status == status::loading || over.status == status::loading) return desktop::text("Loading…", t.quiet());
		std::string key = "board:" + spec.list;
		// Where each moved card is until the view says so too.
		auto moved = p.kept<std::map<std::string, std::string>>(key + ":moved");
		auto moving = p.kept<std::string>(key + ":moving");  // the card whose steps are offered
		auto query = p.kept<std::string>(key + ":search");
		const auto& all = rows_of(v, spec.list);
		// A move the view now shows is settled.
		if (!moved.peek().empty()) {
			auto settled = moved.peek();
			std::erase_if(settled, [&](const auto& m) {
				return std::ranges::any_of(all, [&](const value& card) { return card["id"].as_string() == m.first && field_of(card, spec.by).as_string() == m.second; });
			});
			if (settled.size() != moved.peek().size()) moved.set(settled);
		}
		auto where = [&](const value& card) {
			auto found = moved.get().find(card["id"].as_string());
			return found != moved.get().end() ? found->second : field_of(card, spec.by).as_string();
		};
		std::set<std::string> mine = spec.move ? roles_held(*spec.move) : std::set<std::string>{};
		// The columns a step from where a card is leads to, for one of the person's roles.
		auto reaches = [&](const std::string& at) {
			std::vector<std::string> out;
			if (!spec.move) return out;
			for (const auto& step : rows_of(spec.move->steps.get(), spec.move->list)) {
				if (step["from"].as_string() != at) continue;
				if (!spec.move->held.empty() &&
				    std::ranges::none_of(field_of(step, spec.move->held).as_array(), [&](const value& r) { return mine.contains(r.as_string()); }))
					continue;
				out.push_back(step["to"].as_string());
			}
			return out;
		};
		std::string wanted = lower(query.get());
		auto kept = [&](const value& card) {
			if (wanted.empty()) return true;
			return std::ranges::any_of(spec.search, [&](const std::string& f) { return lower(show(field_of(card, f))).find(wanted) != std::string::npos; });
		};
		const auto& things = rows_of(over, spec.over_list);
		auto title_of = [&](const std::string& id) {
			for (const auto& thing : things) {
				if (thing["id"].as_string() == id) return show(field_of(thing, spec.shown));
			}
			return id;
		};
		std::string title = spec.columns.empty() ? "" : spec.columns.front().field;

		std::vector<desktop::view> parts;
		p.wide();  // a board takes the window's width
		if (!spec.search.empty()) parts.push_back(desktop::row(desktop::field(query, {.placeholder = "Find cards"}).width(320)));
		// The steps offered for the card being moved, above the columns.
		if (!moving.get().empty()) {
			const value* card = nullptr;
			for (const auto& c : all) {
				if (c["id"].as_string() == moving.get()) card = &c;
			}
			if (card) {
				std::vector<desktop::list_row> steps;
				for (const auto& to : reaches(where(*card))) steps.push_back({to, desktop::text(title_of(to), t.body())});
				std::string id = moving.get();
				auto choose = [&p, moving, moved, id, command = spec.move->command, by = spec.by](const std::string& to) {
					moving.set("");
					moved.change([&](auto& now) { now[id] = to; });
					p.run(command, value(std::map<std::string, value>{{"id", id}, {by, to}}), [moved, id](const answer& a) {
						if (!a) moved.change([&](auto& now) { now.erase(id); });
					}, true);
				};
				std::vector<desktop::view> offer;
				offer.push_back(desktop::text(std::format("Move “{}” to", show(field_of(*card, title))), t.strong()));
				if (steps.empty()) offer.push_back(desktop::text("There's no step from here you may take.", t.quiet()));
				else
					offer.push_back(desktop::list(std::move(steps), choose).autofocus().on_key([moving](const key_event& e) {
						if (!e.is(keys::escape)) return false;
						moving.set("");
						return true;
					}));
				offer.push_back(desktop::row(desktop::button("Cancel", [moving] { moving.set(""); })));
				parts.push_back(desktop::row(desktop::column(std::move(offer)).spacing(8).padding(14).width(360).background({.fill = t.surface, .border = t.accent, .border_width = 1.5f, .radius = t.corners})));
			}
		}
		std::vector<desktop::view> lanes;
		std::map<std::string, value> by_id;
		for (const auto& card : all) by_id.emplace(card["id"].as_string(), card);
		for (const auto& thing : things) {
			std::string id = thing["id"].as_string();
			std::vector<desktop::list_row> cards;
			for (const auto& card : all) {
				if (where(card) != id || !kept(card)) continue;
				std::vector<desktop::view> details;
				std::vector<desktop::view> people;
				for (std::size_t c = 1; c < spec.columns.size(); ++c) {
					const auto& f = spec.columns[c].field;
					if (spec.people.contains(f)) {
						for (const auto& name : field_of(card, f).as_array()) people.push_back(desktop::avatar(show(name), 20));
						if (field_of(card, f).is_string() && !field_of(card, f).text.empty()) people.push_back(desktop::avatar(field_of(card, f).text, 20));
					} else if (std::ranges::contains(spec.labels, f)) {
						for (const auto& word : field_of(card, f).as_array()) {
							auto look = t.quiet();
							look.color = t.accent;
							details.push_back(desktop::text(show(word), look).padding(edges(1, 7)).background({.fill = t.sunken, .radius = 9}));
						}
					} else if (std::string said = show(field_of(card, f)); !said.empty()) {
						details.push_back(desktop::text(f == "number" || f.ends_with(".number") ? "#" + said : said, t.quiet()));
					}
				}
				std::vector<desktop::view> inside;
				inside.push_back(desktop::text(show(field_of(card, title)), t.body()));
				if (!details.empty()) inside.push_back(desktop::row(std::move(details)).spacing(6));
				if (!people.empty()) inside.push_back(desktop::row(std::move(people)).spacing(4));
				std::vector<desktop::view> shown;
				auto tone = tint_of(card, spec.tint);
				if (tone) shown.push_back(desktop::edge(*tone));
				shown.push_back(desktop::column(std::move(inside)).spacing(6).grow());
				cards.push_back({card["id"].as_string(), desktop::row(std::move(shown))
				                                             .spacing(10)
				                                             .padding(edges(10, 12, 10, tone ? 0.f : 12.f))
				                                             .background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = std::min(t.corners, 8.f)})});
			}
			std::size_t count = cards.size();
			std::vector<desktop::view> lane;
			lane.push_back(desktop::row(desktop::text(title_of(id), t.strong()), desktop::text(std::to_string(count), t.quiet())).spacing(8));
			if (cards.empty()) {
				lane.push_back(desktop::text("Nothing here", t.quiet()).padding(edges(4, 8)));
			} else {
				desktop::list_options options;
				if (!spec.link.empty()) {
					options.on_open = [&p, by_id, link = spec.link, keyed = spec.keyed, named = spec.named](const std::string& card) {
						if (auto found = by_id.find(card); found != by_id.end()) p.open(row_link(link, found->second, p.params(), keyed, named));
					};
				}
				if (spec.move) options.on_row_key = [moving](const std::string& card, const key_event& e) {
					if (!e.is('m')) return false;
					moving.set(card);
					return true;
				};
				lane.push_back(desktop::list(std::move(cards), std::move(options)));
			}
			lanes.push_back(desktop::column(std::move(lane)).spacing(8).width(290).padding(12).background({.fill = t.sunken, .radius = t.corners}));
		}
		if (std::string why = spec.move ? p.error(spec.move->command) : ""; !why.empty()) {
			auto s = t.body();
			s.color = t.danger;
			parts.push_back(desktop::text(why, s));
		}
		parts.push_back(desktop::scroll_across(desktop::row(std::move(lanes)).spacing(14).align(align::stretch)));
		return desktop::column(std::move(parts)).spacing(12);
	}

}  // namespace one::desktop::runtime
