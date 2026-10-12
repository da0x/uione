// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include "one/desktop/runtime/components.hpp"

#include <one/client/client.hpp>

#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>

#include <algorithm>
#include <format>
#include <thread>

namespace one::desktop::runtime {

	namespace {

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

		// Another site's address, in the browser, without waiting on it.
		void open_in_browser(const std::string& address) {
			pid_t child = 0;
			char opener[] = "xdg-open";
			std::string copied = address;
			char* arguments[] = {opener, copied.data(), nullptr};
			if (posix_spawnp(&child, opener, nullptr, nullptr, arguments, environ) == 0) {
				std::thread([child] { waitpid(child, nullptr, 0); }).detach();
			}
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

	color section_color(const std::string& name) { return named_color(name); }

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

	std::string glyph_of(const std::string& icon) {
		static const std::map<std::string, std::string> glyphs{{"add", "+"},        {"edit", "✎"},    {"follow", "☆"},  {"following", "★"}, {"settings", "⚙"},
		                                                       {"workflow", "⇄"},   {"delete", "✕"},  {"copy", "⧉"},    {"search", "⌕"},    {"back", "‹"},
		                                                       {"link", "↗"},       {"table", "▤"},   {"board", "▥"},   {"done", "✓"}};
		auto found = glyphs.find(icon);
		return found == glyphs.end() ? "" : found->second;
	}

	desktop::view code(std::string, std::string source) {
		// Without the license header the file it comes from starts with.
		if (source.starts_with("// Copyright ") || source.starts_with("# Copyright ")) {
			std::size_t at = source.find("SPDX-License-Identifier: ");
			if (at != std::string::npos) {
				at = source.find('\n', at);
				while (at != std::string::npos && at + 1 < source.size() && source[at + 1] == '\n') ++at;
				source = at == std::string::npos ? "" : source.substr(at + 1);
			}
		}
		while (!source.empty() && source.back() == '\n') source.pop_back();
		const auto& t = current_theme();
		return desktop::text(source, t.code()).padding(edges(12, 16)).background({.fill = t.sunken, .radius = std::min(t.corners, 8.f)});
	}

	desktop::view link(page& p, std::string words, std::string to, std::string icon) {
		if (std::string g = glyph_of(icon); !g.empty()) words = g + " " + words;
		if (to.starts_with("https://") || to.starts_with("http://") || to.starts_with("mailto:")) {
			return desktop::link(std::move(words), [to] { open_in_browser(to); });
		}
		std::string address = to.find(':') != std::string::npos ? filled(p, to) : to;
		return desktop::link(std::move(words), [&p, address] { p.open(address); });
	}

	desktop::view hero(std::string title, std::vector<desktop::view> inside) {
		std::vector<desktop::view> parts;
		parts.push_back(desktop::text(std::move(title), current_theme().heading(32)));
		for (auto& v : inside) {
			if (v.get()) parts.push_back(std::move(v));
		}
		return desktop::column(std::move(parts)).spacing(14).padding(edges(16, 0, 24, 0));
	}

	bool allows(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::string& command, const std::string& allowed) {
		return one::client::allows(roles.get().data, place_field, place, command, allowed);
	}

	bool holds(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::vector<std::string>& granting) {
		return one::client::holds(roles.get().data, place_field, place, granting);
	}

	desktop::view command(page& p, command_button b) {
		if (!b.allowed || !b.when) return {};
		if (std::string g = glyph_of(b.icon); !g.empty()) b.label = g + " " + b.label;
		bool busy = p.running(b.command);
		std::string command = b.command;
		value fields = b.fields;
		if (!b.id.empty()) fields.fields["id"] = value(b.id);
		if (!fields.is_map()) fields = value::map();
		return desktop::button(busy ? b.label + "…" : b.label, [&p, command, fields, busy] {
			if (!busy) p.run(command, fields);
		}, b.primary);
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

	desktop::view details(page&, state<view_state> from, std::vector<column> fields, std::map<std::string, std::vector<desktop::option>> choices,
	                      std::optional<tint_spec> tint) {
		const view_state& v = *from;
		if (auto instead = not_shown(v)) return std::move(*instead);
		const auto& t = current_theme();
		std::vector<desktop::view> shown;
		for (const auto& f : fields) {
			std::string said_now = show(field_of(v.data, f.field));
			if (auto found = choices.find(f.field); found != choices.end()) {
				for (const auto& o : found->second) {
					if (o.value == said_now) said_now = o.label;
				}
			}
			// The field it's tinted by, in its color.
			auto tone = tint && tint->field == f.field ? tint_of(v.data, tint) : std::nullopt;
			std::vector<desktop::view> value_parts;
			if (tone) value_parts.push_back(desktop::edge(*tone));
			value_parts.push_back(desktop::text(said_now.empty() ? "—" : said_now, t.body()).grow());
			shown.push_back(desktop::row(quiet(f.label).width(140), desktop::row(std::move(value_parts)).spacing(8).grow()).spacing(8));
		}
		return desktop::column(std::move(shown)).spacing(8).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
	}

}  // namespace one::desktop::runtime
