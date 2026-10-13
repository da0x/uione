// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Finding things across a page's lists, copying what a view holds, and what goes
// between a list's things as a grid and as a list of moves, drawn.

#include <format>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	desktop::view find(page& p, std::string label_text, std::vector<find_source> sources) {
		const auto& t = current_theme();
		auto query = p.kept<std::string>("find");
		std::vector<desktop::view> parts;
		parts.push_back(desktop::field(query, {.placeholder = label_text}));
		if (!query.get().empty()) {
			auto groups = find_in(p, query.get(), sources);
			for (const auto& g : groups) {
				std::vector<desktop::view> results;
				results.push_back(desktop::text(g.label, t.strong()));
				for (const auto& [says, to] : g.results) {
					if (to.empty()) results.push_back(desktop::text(says, t.body()));
					else results.push_back(desktop::link(says, [&p, to] { p.open(to); }));
				}
				if (g.more > 0) results.push_back(desktop::text(std::format("and {} more", g.more), t.quiet()));
				parts.push_back(desktop::column(std::move(results)).spacing(6));
			}
			if (groups.empty()) parts.push_back(desktop::text("Nothing found.", t.quiet()));
		}
		return desktop::column(std::move(parts)).spacing(10);
	}

	desktop::view copy(page& p, state<view_state> from, std::string label_text, std::vector<copied_field> fields, std::vector<copied_list> lists,
	                   std::map<std::string, std::vector<screens::option>> choices) {
		if (from->status != status::ready) return {};
		auto copied = p.kept<bool>("copy:" + label_text, false);
		return desktop::button(copied.get() ? "Copied" : label_text, [&p, from, fields, lists, choices, copied] {
			desktop::copy_text(markdown_of(p.title(), from.peek().data, fields, lists, choices));
			copied.set(true);
		});
	}

	desktop::theme drawn_in(const palette::theme& tried, desktop::theme own) {
		auto color = [&](std::string_view role) { return desktop::color::hex(static_cast<std::uint32_t>(std::stoul(tried[role].light.substr(1), nullptr, 16))); };
		own.page = color("page");
		own.surface = color("surface");
		own.sunken = color("sunken");
		own.ink = color("ink");
		own.muted = color("muted");
		own.line = color("line");
		own.accent = color("accent");
		own.danger = color("danger");
		own.success = color("success");
		own.warning = color("warning");
		return own;
	}

	desktop::view themes(page& p) {
		const auto& t = current_theme();
		auto tried = screens::tried_themes::theme().get();
		std::vector<desktop::option> schemes;
		for (auto s : palette::schemes) schemes.push_back({std::string(s), palette::scheme_title(s)});
		std::vector<desktop::view> top;
		top.push_back(desktop::radio_buttons(screens::tried_themes::scheme(), schemes, [](const std::string& s) { screens::tried_themes::pick(s); }));
		top.push_back(desktop::button("New theme", [] { screens::tried_themes::make_new(); }, true));
		if (tried) {
			top.push_back(desktop::button("Back to the app's theme", [] { screens::tried_themes::put_back(); }));
			top.push_back(desktop::text("Seed " + std::to_string(tried->seed), t.quiet()));
		}
		std::vector<desktop::view> parts;
		parts.push_back(desktop::flow(std::move(top)).spacing(12));
		if (!tried) return desktop::column(std::move(parts)).spacing(16);
		// Each role, its light color over its dark one.
		std::vector<desktop::view> swatches;
		for (std::size_t i = 0; i < palette::roles.size(); ++i) {
			auto fill = [](const std::string& hex) { return desktop::color::hex(static_cast<std::uint32_t>(std::stoul(hex.substr(1), nullptr, 16))); };
			auto half = [&](const std::string& hex) { return desktop::spacer().height(26).background({.fill = fill(hex)}); };
			auto both = desktop::column(half(tried->colors[i].light), half(tried->colors[i].dark))
			                .width(64)
			                .background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = std::min(t.corners, 6.f)});
			swatches.push_back(desktop::column(std::move(both), desktop::text(std::string(palette::roles[i]), t.quiet())).spacing(4));
		}
		parts.push_back(desktop::flow(std::move(swatches)).spacing(8));
		std::string said = palette::written(*tried);
		auto copied = p.kept<std::string>("themes:copied", "");
		parts.push_back(code("one", said));
		parts.push_back(desktop::row(desktop::button(copied.get() == said ? "Copied" : "Copy",
		                                             [said, copied] {
			                                             desktop::copy_text(said);
			                                             copied.set(said);
		                                             }),
		                             desktop::spacer()));
		return desktop::column(std::move(parts)).spacing(16);
	}

	namespace {

		// What's open between two, and the forms and buttons for it.
		desktop::view asking(page& p, const between_spec& spec, const between_model& b, state<std::string> open) {
			const auto& t = current_theme();
			std::string now = open.get();
			if (now.empty()) return {};
			auto cut = now.find('|');
			const value *a = nullptr, *c = nullptr;
			for (const auto& thing : b.things) {
				if (thing["id"].as_string() == now.substr(0, cut)) a = &thing;
				if (thing["id"].as_string() == now.substr(cut + 1)) c = &thing;
			}
			if (!a || !c) return {};
			const value* entry = b.between(spec, *a, *c);
			std::vector<desktop::view> parts;
			parts.push_back(desktop::text(b.name(spec, *a) + " to " + b.name(spec, *c), t.strong()));
			auto done_now = [open](const answer& got) {
				if (got) open.set("");
			};
			if (entry && spec.update && spec.update->allowed) {
				std::string id = (*entry)["id"].as_string();
				auto started = p.kept<view_state>("between:" + now + ":entry");
				if (ember::to_json(started.peek().data) != ember::to_json(*entry)) started.set(view_state{status::ready, *entry, ""});
				parts.push_back(form(p, {.command = spec.update->command, .fields = spec.update->fields, .from = started, .id = id, .submit = spec.update->submit,
				                         .on_done = [open] { open.set(""); }}));
			}
			if (!entry && spec.create && spec.create->allowed) {
				auto given = spec.create->given;
				given[spec.from] = (*a)["id"].as_string();
				given[spec.to] = (*c)["id"].as_string();
				parts.push_back(form(p, {.command = spec.create->command, .fields = spec.create->fields, .given = given, .submit = spec.create->submit,
				                         .on_done = [open] { open.set(""); }, .key = now}));
			}
			std::vector<desktop::view> buttons;
			if (entry && spec.remove && spec.remove->allowed) {
				std::string id = (*entry)["id"].as_string(), command_name = spec.remove->command;
				buttons.push_back(desktop::button(spec.remove->submit.empty() ? "Remove" : spec.remove->submit,
				                                  [&p, command_name, id, done_now] { p.run(command_name, value(std::map<std::string, value>{{"id", id}}), done_now); }));
			}
			buttons.push_back(desktop::button("Close", [open] { open.set(""); }));
			parts.push_back(desktop::row(std::move(buttons)).spacing(8));
			return desktop::column(std::move(parts)).spacing(10).padding(16).background({.fill = t.surface, .border = t.accent, .border_width = 1.5f, .radius = t.corners});
		}

	}  // namespace

	desktop::view grid(page& p, between_spec spec) {
		const auto& t = current_theme();
		if (spec.over->status == status::loading || spec.view->status == status::loading) return desktop::text("Loading…", t.quiet());
		auto b = between_of(spec);
		auto open = p.kept<std::string>("grid:" + spec.list);
		std::vector<desktop::view> rows;
		std::string to_label = label(spec.to);
		if (!to_label.empty()) to_label[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(to_label[0])));
		std::vector<desktop::view> header;
		header.push_back(desktop::text(label(spec.from) + ", " + to_label, t.quiet()).width(140));
		for (const auto& thing : b.things) header.push_back(desktop::text(b.name(spec, thing), t.strong()).grow());
		rows.push_back(desktop::row(std::move(header)).spacing(6));
		for (const auto& a : b.things) {
			std::vector<desktop::view> cells;
			cells.push_back(desktop::text(b.name(spec, a), t.strong()).width(140));
			for (const auto& c : b.things) {
				if (a["id"].as_string() == c["id"].as_string()) {
					cells.push_back(desktop::text("", t.quiet()).grow().padding(8).background({.fill = t.sunken, .radius = 4}));
					continue;
				}
				const value* entry = b.between(spec, a, c);
				std::string says = entry ? b.says(spec, *entry) : "";
				std::string key = a["id"].as_string() + "|" + c["id"].as_string();
				if (b.may(spec, entry != nullptr)) cells.push_back(desktop::button(entry ? says : "+", [open, key] { open.set(open.peek() == key ? "" : key); }).grow());
				else cells.push_back(desktop::text(says, t.body()).grow().padding(8));
			}
			rows.push_back(desktop::row(std::move(cells)).spacing(6).align(align::center));
		}
		if (auto asked = asking(p, spec, b, open); asked.get()) rows.push_back(std::move(asked));
		return desktop::column(std::move(rows)).spacing(6);
	}

	desktop::view diagram(page& p, between_spec spec) {
		const auto& t = current_theme();
		if (spec.over->status == status::loading || spec.view->status == status::loading) return desktop::text("Loading…", t.quiet());
		auto b = between_of(spec);
		auto open = p.kept<std::string>("diagram:" + spec.list);
		std::vector<desktop::list_row> moves;
		for (const auto& a : b.things) {
			for (const auto& c : b.things) {
				const value* entry = b.between(spec, a, c);
				if (!entry) continue;
				std::string says = spec.cell.empty() ? "" : screens::show(field_of(*entry, spec.cell));
				moves.push_back({a["id"].as_string() + "|" + c["id"].as_string(),
				                 desktop::row(desktop::text(b.name(spec, a), t.strong()), desktop::text("→", t.quiet()), desktop::text(b.name(spec, c), t.strong()),
				                              desktop::text(says, t.quiet()).grow())
				                     .spacing(10)});
			}
		}
		std::vector<desktop::view> parts;
		if (moves.empty()) parts.push_back(desktop::text("Nothing goes anywhere yet.", t.quiet()));
		else
			parts.push_back(desktop::list(std::move(moves), [open, spec, b](const std::string& key) {
				if (b.may(spec, true)) open.set(key);
			}));
		if (auto asked = asking(p, spec, b, open); asked.get()) parts.push_back(std::move(asked));
		return desktop::column(std::move(parts)).spacing(8);
	}

}  // namespace one::desktop::runtime
