// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include "one/desktop/runtime/components.hpp"

#include <one/client/client.hpp>

#include <algorithm>
#include <format>

namespace one::desktop::runtime {

	namespace {

		text_style quieter() {
			auto s = current_theme().strong();
			s.color = current_theme().muted;
			s.size -= 2;
			return s;
		}

		desktop::view quiet(std::string words) { return desktop::text(std::move(words), current_theme().quiet()); }

		// The library's colors, as a section or a tag is drawn in.
		color named_color(const std::string& name) {
			static const std::map<std::string, std::uint32_t> colors{
			    {"red", 0xc2312d}, {"orange", 0xd4700b}, {"amber", 0xb7791f}, {"yellow", 0x9a6700}, {"green", 0x1a7f37}, {"teal", 0x0a7e7a},
			    {"blue", 0x2457d6}, {"indigo", 0x4f46e5}, {"violet", 0x8250df}, {"pink", 0xbf3989}, {"gray", 0x57606a},
			};
			auto found = colors.find(name);
			return found == colors.end() ? current_theme().accent : color::hex(found->second);
		}

		// What a view says while it isn't there to show.
		std::optional<desktop::view> not_shown(const view_state& v) {
			if (v.status == status::loading) return quiet("Loading…");
			if (v.status == status::failed) {
				auto s = current_theme().body();
				s.color = current_theme().danger;
				return desktop::text(v.error.empty() ? "This couldn't be read." : v.error, s);
			}
			return std::nullopt;
		}

	}  // namespace

	desktop::view items(std::vector<desktop::view> all) { return desktop::column(std::move(all)).spacing(14); }

	desktop::view text(std::vector<part> parts) { return desktop::text(said(parts), current_theme().body()); }

	desktop::view subtitle(page& p, std::vector<part> parts) {
		p.under_title(desktop::text(said(parts), current_theme().quiet()));
		return {};
	}

	desktop::view crumbs(page& p, std::vector<crumb> trail) {
		std::vector<desktop::view> shown;
		for (auto& c : trail) {
			std::string to = c.to;
			// Its parameters this page's address doesn't have, from the views that hold them.
			std::string address;
			std::size_t at = 0;
			while (at < to.size()) {
				std::size_t end = to.find('/', at + 1);
				std::string segment = to.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
				address += "/";
				if (segment.starts_with(':')) {
					std::string name = segment.substr(1);
					if (auto f = c.fill.find(name); f != c.fill.end()) {
						std::string held = show(field_of(f->second.from.get().data, f->second.field));
						auto split = f->second.keys > 1 ? parts_of(held, static_cast<std::size_t>(f->second.keys)) : std::nullopt;
						address += split ? split->back() : held;
					} else {
						address += p.param(name);
					}
				} else {
					address += segment;
				}
				at = end == std::string::npos ? to.size() : end;
			}
			if (!shown.empty()) shown.push_back(quiet("›"));
			shown.push_back(desktop::link(said(c.title), [&p, address] { p.open(address); }, current_theme().quiet()));
		}
		p.above_title(desktop::row(std::move(shown)).spacing(8));
		return {};
	}

	desktop::view section(section_title title, std::vector<desktop::view> inside) {
		const auto& t = current_theme();
		auto heading = t.strong();
		heading.size += 2;
		if (!title.color.empty()) heading.color = named_color(title.color);
		std::vector<desktop::view> parts;
		parts.push_back(desktop::text(std::move(title.title), heading));
		for (auto& v : inside) parts.push_back(std::move(v));
		return desktop::column(std::move(parts)).spacing(10).padding(edges(8, 0, 0, 0));
	}

	desktop::view link(page& p, std::string words, std::string to) {
		return desktop::link(std::move(words), [&p, to] { p.open(to); });
	}

	bool allows(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::string& command, const std::string& allowed) {
		return one::client::allows(roles.get().data, place_field, place, command, allowed);
	}

	bool holds(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::vector<std::string>& granting) {
		return one::client::holds(roles.get().data, place_field, place, granting);
	}

	desktop::view command(page& p, command_button b) {
		if (!b.allowed || !b.when) return {};
		bool busy = p.running(b.command);
		std::string command = b.command;
		value fields = b.fields;
		if (!b.id.empty()) fields.fields["id"] = value(b.id);
		if (!fields.is_map()) fields = value::map();
		return desktop::button(busy ? b.label + "…" : b.label, [&p, command, fields, busy] {
			if (!busy) p.run(command, fields);
		}, b.primary);
	}

	desktop::view table(page& p, table_spec spec) {
		const view_state& v = *spec.view;
		if (auto instead = not_shown(v)) return std::move(*instead);
		std::vector<value> rows;
		for (const auto& row : rows_of(v, spec.list)) {
			if (p.keeps(row)) rows.push_back(row);
		}
		auto note = filter_note(p);
		if (rows.empty()) return note.get() ? desktop::column(std::move(note), quiet(spec.empty)).spacing(8) : quiet(spec.empty);
		// The columns' labels, then a row for each, each column an equal share.
		// Each row keeps the same room for its actions, so its columns line up with the
		// others' whichever actions it has.
		float acting = 0;
		for (const auto& a : spec.actions) {
			if (a.allowed) acting += 24 + 9 * static_cast<float>(a.label.size());
		}
		std::vector<desktop::view> labels;
		for (const auto& c : spec.columns) labels.push_back(desktop::text(c.label, quieter()).grow());
		if (acting > 0) labels.push_back(desktop::spacer().grow(0).width(acting));
		std::vector<desktop::list_row> shown;
		for (const auto& row : rows) {
			std::vector<desktop::view> cells;
			for (const auto& c : spec.columns) cells.push_back(desktop::text(show(field_of(row, c.field)), current_theme().body()).grow());
			std::string id = row["id"].as_string();
			std::vector<desktop::view> actions;
			for (const auto& a : spec.actions) {
				if (!a.allowed || (a.when && !a.when(row))) continue;
				std::string command = a.command;
				actions.push_back(desktop::button(p.running(command) ? a.label + "…" : a.label, [&p, command, id] {
					p.run(command, value(std::map<std::string, value>{{"id", id}}));
				}));
			}
			if (acting > 0) {
				actions.insert(actions.begin(), desktop::spacer());
				cells.push_back(desktop::row(std::move(actions)).spacing(6).width(acting));
			}
			shown.push_back({id, desktop::row(std::move(cells)).spacing(12).align(align::center)});
		}
		std::map<std::string, value> by_id;
		for (const auto& row : rows) by_id.emplace(row["id"].as_string(), row);
		std::function<void(const std::string&)> opening;
		if (!spec.link.empty()) {
			opening = [&p, by_id, link = spec.link, keyed = spec.keyed, named = spec.named](const std::string& id) {
				if (auto found = by_id.find(id); found != by_id.end()) p.open(row_link(link, found->second, p.params(), keyed, named));
			};
		}
		return desktop::column(std::move(note), desktop::row(std::move(labels)).spacing(12).padding(edges(0, 8)), desktop::list(std::move(shown), std::move(opening))).spacing(6);
	}

	desktop::view confirm(page& p, std::string command, std::string question) {
		p.ask_before(command, std::move(question));
		return {};
	}

	std::string filled(page& p, const std::string& route) {
		std::string out;
		std::size_t at = 0;
		while (at < route.size()) {
			std::size_t end = route.find('/', at + 1);
			std::string segment = route.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
			out += "/" + (segment.starts_with(':') ? p.param(segment.substr(1)) : segment);
			at = end == std::string::npos ? route.size() : end;
		}
		return out.empty() ? "/" : out;
	}

	desktop::view filter_note(page& p) {
		std::string said = p.filtered();
		if (said.empty()) return {};
		const auto& t = current_theme();
		auto s = t.strong();
		s.color = t.accent;
		return desktop::row(desktop::row(desktop::text(said, s), desktop::link("Show all", [&p] { p.clear_filter(); }, t.quiet()))
		                        .spacing(12)
		                        .padding(edges(6, 12))
		                        .background({.fill = t.sunken, .radius = t.corners}));
	}

	desktop::view cards(page& p, cards_spec spec) {
		const view_state& v = *spec.view;
		if (auto instead = not_shown(v)) return std::move(*instead);
		const auto& t = current_theme();
		std::string title = spec.columns.empty() ? "" : spec.columns.front().field;
		std::vector<desktop::list_row> shown;
		std::map<std::string, std::string> opens;
		for (const auto& row : rows_of(v, spec.list)) {
			std::string id = row["id"].as_string();
			std::string to = spec.link.empty() ? "" : row_link(spec.link, row, p.params(), spec.keyed, spec.named);
			opens[id] = to;
			std::vector<desktop::view> inside;
			inside.push_back(desktop::text(show(field_of(row, title)), t.heading(18)));
			for (std::size_t c = 1; c < spec.columns.size(); ++c) {
				if (std::string said = show(field_of(row, spec.columns[c].field)); !said.empty()) inside.push_back(quiet(spec.columns[c].label + ": " + said));
			}
			// What it holds, counted by what each is at, in order: 3 To do · 1 Done.
			if (spec.tally) {
				struct group {
					std::string label;
					int count = 0;
					double order = 0;
				};
				std::map<std::string, group> groups;
				int total = 0;
				for (const auto& held : rows_of(spec.tally->view.get(), spec.tally->list)) {
					if (field_of(held, spec.tally->by).as_string() != id) continue;
					++total;
					std::string at = show(field_of(held, spec.tally->and_by));
					auto& g = groups[at];
					if (g.label.empty()) g.label = show(field_of(held, spec.tally->shown.empty() ? spec.tally->and_by : spec.tally->shown));
					if (g.label.empty()) g.label = "None";
					if (!spec.tally->order.empty()) g.order = field_of(held, spec.tally->order).as_double();
					++g.count;
				}
				std::vector<group> sorted;
				for (auto& [_, g] : groups) sorted.push_back(g);
				std::ranges::stable_sort(sorted, {}, &group::order);
				std::string counted;
				for (const auto& g : sorted) counted += (counted.empty() ? "" : " · ") + std::format("{} {}", g.count, g.label);
				if (total == 0) counted = "No " + (spec.tally->noun.empty() ? std::string("items") : spec.tally->noun) + " yet";
				inside.push_back(quiet(counted));
			}
			if (!to.empty() && !spec.filters.empty()) {
				std::vector<desktop::view> links;
				for (const auto& f : spec.filters) {
					std::string address = to + "?is=" + f.label;
					for (const auto& [name, wanted] : f.query) address += "&" + name + "=" + wanted;
					auto look = t.quiet();
					look.color = f.color.empty() ? t.accent : named_color(f.color);
					links.push_back(desktop::link(f.label, [&p, address] { p.open(address); }, look));
				}
				inside.push_back(desktop::row(std::move(links)).spacing(14));
			}
			shown.push_back({id, desktop::column(std::move(inside)).spacing(8).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners})});
		}
		if (shown.empty()) return quiet("Nothing here yet.");
		return desktop::list(std::move(shown), [&p, opens](const std::string& id) {
			if (auto found = opens.find(id); found != opens.end() && !found->second.empty()) p.open(found->second);
		});
	}

	desktop::view details(page&, state<view_state> from, std::vector<column> fields) {
		const view_state& v = *from;
		if (auto instead = not_shown(v)) return std::move(*instead);
		const auto& t = current_theme();
		std::vector<desktop::view> shown;
		for (const auto& f : fields) {
			std::string said_now = show(field_of(v.data, f.field));
			shown.push_back(desktop::row(quiet(f.label).width(140), desktop::text(said_now.empty() ? "—" : said_now, t.body()).grow()).spacing(8));
		}
		return desktop::column(std::move(shown)).spacing(8).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
	}

}  // namespace one::desktop::runtime
