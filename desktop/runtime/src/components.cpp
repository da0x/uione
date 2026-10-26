// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What's on a screen, drawn: text and titles, the pages above, sections, links,
// commands, cards and details, each from what one::screens works out of it.

#include "one/desktop/runtime/components.hpp"
#include "one/desktop/runtime/markdown.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <format>
#include <thread>

namespace one::desktop::runtime {

	namespace {

		desktop::view quiet(std::string words) { return desktop::text(std::move(words), current_theme().quiet()); }

		// The short line over a front page's title, small and in the accent.
		desktop::view overline(std::string words) {
			const auto& t = current_theme();
			auto style = t.quiet();
			style.weight = 600;
			style.color = t.accent;
			return desktop::text(std::move(words), style);
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

	// The library's colors, as a section or a tag is drawn in.
	color section_color(const std::string& name) {
		static const std::map<std::string, std::uint32_t> colors{
		    {"red", 0xc2312d}, {"orange", 0xd4700b}, {"amber", 0xb7791f}, {"yellow", 0x9a6700}, {"green", 0x1a7f37}, {"teal", 0x0a7e7a},
		    {"blue", 0x2457d6}, {"indigo", 0x4f46e5}, {"violet", 0x8250df}, {"pink", 0xbf3989}, {"gray", 0x57606a},
		};
		auto found = colors.find(name);
		return found == colors.end() ? current_theme().accent : color::hex(found->second);
	}

	std::optional<color> tone_color(int tone) {
		const auto& t = current_theme();
		switch (tone) {
			case 1: return t.danger;
			case 2: return color::hex(0xd4700b);
			case 4: return t.success;
			case 5: return t.accent;
			default: return std::nullopt;
		}
	}

	std::string glyph_of(const std::string& icon) {
		static const std::map<std::string, std::string> glyphs{{"add", "+"},      {"edit", "✎"},   {"follow", "☆"}, {"following", "★"}, {"settings", "⚙"},
		                                                       {"workflow", "⇄"}, {"delete", "✕"}, {"copy", "⧉"},   {"search", "⌕"},    {"back", "‹"},
		                                                       {"link", "↗"},     {"table", "▤"},  {"board", "▥"},  {"done", "✓"},       {"archive", "⊟"}};
		auto found = glyphs.find(icon);
		return found == glyphs.end() ? "" : found->second;
	}

	desktop::view items(std::vector<desktop::view> all) {
		std::erase_if(all, [](const desktop::view& v) { return !v.get(); });
		return desktop::column(std::move(all)).spacing(14);
	}

	// Words with a link written in them, like [Daher](https://…), are Markdown.
	desktop::view text(std::vector<part> parts) {
		std::string words = said(parts);
		if (words.find("](") != std::string::npos) return markdown_view(words);
		return desktop::text(std::move(words), current_theme().body());
	}

	desktop::view subtitle(page& p, std::vector<part> parts) {
		p.under_title(desktop::text(said(parts), current_theme().quiet()));
		return {};
	}

	desktop::view crumbs(page& p, std::vector<crumb> trail) {
		std::vector<desktop::view> shown;
		for (const auto& c : crumb_links(p, trail)) {
			if (!shown.empty()) shown.push_back(quiet("›"));
			shown.push_back(desktop::link(c.title, [&p, address = c.address] { p.open(address); }, current_theme().quiet()));
		}
		p.above_title(desktop::row(std::move(shown)).spacing(8));
		return {};
	}

	desktop::view section(section_title title, std::vector<desktop::view> inside) {
		const auto& t = current_theme();
		auto heading_style = t.strong();
		heading_style.size += 2;
		if (!title.color.empty()) heading_style.color = section_color(title.color);
		std::vector<desktop::view> parts;
		if (!title.overline.empty()) parts.push_back(overline(std::move(title.overline)));
		// A step's number, and an icon, go before the title, in its row.
		if (title.step > 0 || desktop::has_icon(title.icon)) {
			std::vector<desktop::view> heading;
			if (desktop::has_icon(title.icon)) heading.push_back(desktop::icon(title.icon, title.color.empty() ? t.accent : section_color(title.color), 20));
			if (title.step > 0) {
				auto number = heading_style;
				number.color = t.accent;
				heading.push_back(desktop::text((title.step < 10 ? "0" : "") + std::to_string(title.step), number));
			}
			heading.push_back(desktop::text(std::move(title.title), heading_style));
			parts.push_back(desktop::row(std::move(heading)).spacing(10));
		} else {
			parts.push_back(desktop::text(std::move(title.title), heading_style));
		}
		for (auto& v : inside) parts.push_back(std::move(v));
		return desktop::column(std::move(parts)).spacing(10).padding(edges(8, 0, 0, 0));
	}

	desktop::view statement(std::vector<part> parts) {
		return desktop::text(said(parts), current_theme().heading(26)).padding(edges(16, 0));
	}

	desktop::view code(std::string, std::string source) {
		const auto& t = current_theme();
		// A tab as four spaces, as the web shows code.
		std::string shown;
		for (char c : without_license(std::move(source))) shown += c == '\t' ? std::string("    ") : std::string(1, c);
		return desktop::text(std::move(shown), t.code()).padding(edges(12, 16)).background({.fill = t.sunken, .radius = std::min(t.corners, 8.f)});
	}

	// The part of the page a link last brought into view, and how many times one has.
	using anchor = std::pair<std::string, int>;

	desktop::view anchored(page& p, std::string name, desktop::view part) {
		auto [wanted, times] = p.kept<anchor>("anchor").get();
		if (wanted != name || !part.get()) return part;
		return std::move(part).key("#" + name + "-" + std::to_string(times)).brought_to_top();
	}

	desktop::view link(page& p, std::string words, std::string to, std::string icon, std::vector<std::string> keys) {
		for (const auto& k : keys) p.on_key(k, [&p, address = address_of(p, to)] { p.open(address); });
		// One of the web's icons is a quiet button drawn as just that, as on the web.
		if (desktop::has_icon(icon) && !leaves(to) && !to.starts_with("#")) {
			return desktop::icon_button(icon, std::move(words), [&p, address = address_of(p, to)] { p.open(address); });
		}
		if (std::string g = glyph_of(icon); !g.empty()) words = g + " " + words;
		if (leaves(to)) return desktop::link(std::move(words), [to] { open_in_browser(to); });
		if (to.starts_with("#")) {
			return desktop::link(std::move(words), [&p, name = to.substr(1)] {
				auto at = p.kept<anchor>("anchor");
				at.set({name, at.peek().second + 1});
			});
		}
		return desktop::link(std::move(words), [&p, address = address_of(p, to)] { p.open(address); });
	}

	desktop::view hero(std::string title, std::vector<desktop::view> inside) { return hero(section_title{.title = std::move(title)}, std::move(inside)); }

	desktop::view hero(section_title title, std::vector<desktop::view> inside) {
		std::vector<desktop::view> parts;
		if (!title.overline.empty()) parts.push_back(overline(std::move(title.overline)));
		parts.push_back(desktop::text(std::move(title.title), current_theme().heading(32)));
		for (auto& v : inside) {
			if (v.get()) parts.push_back(std::move(v));
		}
		return desktop::column(std::move(parts)).spacing(14).padding(edges(16, 0, 24, 0));
	}

	desktop::view command(page& p, command_button b) {
		auto shown = command_of(p, b);
		if (!shown) return {};
		std::string label_now = shown->label;
		auto run = [&p, name = b.command, fields = shown->fields, busy = shown->busy, then = b.then_open] {
			if (busy) return;
			if (then.empty()) return p.run(name, fields);
			// What it made, opened: its id in the route's last parameter.
			p.run(name, fields, [&p, then](const answer& made) {
				if (made) p.open(then.substr(0, then.rfind(':')) + *made);
			});
		};
		for (const auto& k : b.keys) p.on_key(k, run);
		// In a toolbar's group, a tool: its icon over its words, or its icon alone.
		if (auto small = p.in_group()) {
			return desktop::tool_button(desktop::has_icon(b.icon) ? b.icon : "", *small ? hint_of(label_now, b.keys) : label_now, std::move(run), {.large = !*small, .on = b.pressed});
		}
		if (desktop::has_icon(b.icon)) return desktop::icon_button(b.icon, label_now, std::move(run), b.primary);
		if (std::string g = glyph_of(b.icon); !g.empty()) label_now = g + " " + label_now;
		return desktop::button(label_now, std::move(run), b.primary);
	}

	desktop::view confirm(page& p, std::string command, std::string question) {
		p.ask_before(command, std::move(question));
		return {};
	}

	desktop::view filter_note(page& p) {
		std::string said_now = p.filtered();
		if (said_now.empty()) return {};
		const auto& t = current_theme();
		auto s = t.strong();
		s.color = t.accent;
		return desktop::row(desktop::row(desktop::text(said_now, s), desktop::link("Show all", [&p] { p.clear_filter(); }, t.quiet()))
		                        .spacing(12)
		                        .padding(edges(6, 12))
		                        .background({.fill = t.sunken, .radius = t.corners}));
	}

	desktop::view cards(page& p, cards_spec spec) {
		const view_state& v = *spec.view;
		// Hidden when empty is hidden until there's something to show, not drawn while it loads.
		if (spec.hide_empty && v.status != status::ready) return {};
		if (auto instead = not_shown(v)) return std::move(*instead);
		const auto& t = current_theme();
		std::vector<desktop::list_row> shown;
		std::map<std::string, std::string> opens;
		for (const auto& card : cards_of(p, spec)) {
			opens[card.id] = card.address;
			std::vector<desktop::view> inside;
			if (card.tags.empty()) {
				inside.push_back(desktop::text(card.title, t.heading(18)));
			} else {
				// Its tags beside its title, like Private.
				std::vector<desktop::view> top;
				top.push_back(desktop::text(card.title, t.heading(18)).grow());
				for (const auto& tag : card.tags) top.push_back(desktop::text(tag, t.quiet()).padding(edges(1, 8)).background({.fill = t.sunken, .border = t.line, .border_width = 1, .radius = 9}));
				inside.push_back(desktop::row(std::move(top)).spacing(6));
			}
			for (const auto& d : card.details) inside.push_back(quiet(d));
			if (spec.tally) inside.push_back(quiet(card.tally));
			if (!card.filters.empty()) {
				std::vector<desktop::view> filters;
				for (const auto& f : card.filters) {
					auto look = t.quiet();
					look.color = f.color.empty() ? t.accent : section_color(f.color);
					filters.push_back(desktop::link(f.label, [&p, address = f.address] { p.open(address); }, look));
				}
				inside.push_back(desktop::row(std::move(filters)).spacing(14));
			}
			shown.push_back({card.id, desktop::column(std::move(inside)).spacing(8).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners})});
		}
		if (shown.empty()) return spec.hide_empty ? desktop::view() : quiet("Nothing here yet.");
		return desktop::list(std::move(shown), [&p, opens](const std::string& id) {
			if (auto found = opens.find(id); found != opens.end() && !found->second.empty()) p.open(found->second);
		});
	}

	desktop::view details(page&, state<view_state> from, std::vector<screens::column> fields, std::map<std::string, std::vector<screens::option>> choices,
	                      std::optional<tint_spec> tint, std::vector<std::string> labels,
	                       std::vector<std::string> files) {
		const view_state& v = *from;
		if (auto instead = not_shown(v)) return std::move(*instead);
		const auto& t = current_theme();
		std::vector<desktop::view> shown;
		for (const auto& line : details_of(v, fields, choices, tint)) {
			std::vector<desktop::view> value_parts;
			if (auto tone = tone_color(line.tone)) value_parts.push_back(desktop::edge(*tone));
			if (std::ranges::contains(labels, line.field)) {
				// Labels as the table draws them, each on its own, going on to a new line.
				std::vector<desktop::view> words_shown;
				for (const auto& word : field_of(v.data, line.field).as_array()) {
					if (screens::show(word).empty()) continue;
					auto look = t.quiet();
					look.color = t.accent;
					words_shown.push_back(desktop::text(screens::show(word), look).padding(edges(1, 7)).background({.fill = t.sunken, .radius = 9}));
				}
				value_parts.push_back(desktop::flow(std::move(words_shown)).spacing(4).grow());
			} else if (std::ranges::contains(files, line.field)) {
				value_parts.push_back(desktop::text(file_names(field_of(v.data, line.field)), t.body()).grow());
			} else {
				value_parts.push_back(desktop::text(line.value, t.body()).grow());
			}
			shown.push_back(desktop::row(quiet(line.label).width(108), desktop::row(std::move(value_parts)).spacing(8).grow()).spacing(8));
		}
		return desktop::column(std::move(shown)).spacing(8).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
	}

}  // namespace one::desktop::runtime
