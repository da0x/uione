// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Finding things across a page's lists, copying what a view holds as Markdown, and
// what goes between two of a list's things, as a grid and as a list of moves.

#include <algorithm>
#include <format>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		std::string lower(std::string s) {
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		std::string trimmed(const std::string& s) {
			std::size_t from = s.find_first_not_of(" \n\t"), to = s.find_last_not_of(" \n\t");
			return from == std::string::npos ? "" : s.substr(from, to - from + 1);
		}

	}  // namespace

	desktop::view find(page& p, std::string label, std::vector<find_source> sources) {
		const auto& t = current_theme();
		auto query = p.kept<std::string>("find");
		std::string wanted = lower(trimmed(query.get()));
		// #12, or 12, finds by number too.
		std::string number = wanted;
		if (number.starts_with('#')) number.erase(0, 1);
		bool numeric = !number.empty() && std::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; });
		std::vector<desktop::view> parts;
		parts.push_back(desktop::field(query, {.placeholder = label}));
		if (!wanted.empty()) {
			bool any = false;
			for (const auto& source : sources) {
				std::string numbered, named;
				for (const auto& f : source.by) {
					if (numbered.empty() && (f == "number" || f.ends_with(".number"))) numbered = f;
					else if (named.empty()) named = f;
				}
				std::vector<const value*> found;
				for (const auto& row : rows_of(source.view.get(), source.list)) {
					bool by_number = numeric && !numbered.empty() && show(field_of(row, numbered)) == number;
					bool by_words = std::ranges::any_of(source.by, [&](const std::string& f) { return f != numbered && lower(show(field_of(row, f))).find(wanted) != std::string::npos; });
					if (by_number || by_words || (!numeric && !numbered.empty() && lower(show(field_of(row, numbered))).find(wanted) != std::string::npos)) found.push_back(&row);
				}
				if (found.empty()) continue;
				any = true;
				std::vector<desktop::view> results;
				results.push_back(desktop::text(source.label, t.strong()));
				for (std::size_t i = 0; i < found.size() && i < 8; ++i) {
					const value& row = *found[i];
					std::string said = (numbered.empty() ? "" : "#" + show(field_of(row, numbered)) + " ") + (named.empty() ? "" : show(field_of(row, named)));
					if (trimmed(said).empty() || said == "#") said = row["id"].as_string();
					std::string to = source.link.empty() ? "" : row_link(source.link, row, p.params(), source.keyed);
					if (to.empty()) results.push_back(desktop::text(said, t.body()));
					else results.push_back(desktop::link(said, [&p, to] { p.open(to); }));
				}
				if (found.size() > 8) results.push_back(desktop::text(std::format("and {} more", found.size() - 8), t.quiet()));
				parts.push_back(desktop::column(std::move(results)).spacing(6));
			}
			if (!any) parts.push_back(desktop::text("Nothing found.", t.quiet()));
		}
		return desktop::column(std::move(parts)).spacing(10);
	}

	std::string markdown_of(const std::string& title, const value& data, const std::vector<copied_field>& fields, const std::vector<copied_list>& lists,
	                        const std::map<std::string, std::vector<desktop::option>>& choices) {
		std::vector<std::string> out;
		if (!title.empty()) out.insert(out.end(), {"# " + title, ""});
		auto value_of = [&](const std::string& name) {
			const value& stored = field_of(data, name);
			if (auto found = choices.find(name); found != choices.end() && stored.is_string()) {
				for (const auto& o : found->second) {
					if (o.value == stored.text) return o.label;
				}
			}
			return show(stored);
		};
		bool said = false;
		for (const auto& f : fields) {
			if (f.markdown || value_of(f.name).empty()) continue;
			out.push_back("**" + f.label + ":** " + value_of(f.name) + "  ");
			said = true;
		}
		if (said) out.push_back("");
		for (const auto& f : fields) {
			std::string body = trimmed(field_of(data, f.name).as_string());
			if (f.markdown && !body.empty()) out.insert(out.end(), {"## " + f.label, "", body, ""});
		}
		for (const auto& l : lists) {
			const auto& rows = data[l.name].as_array();
			if (rows.empty()) continue;
			out.insert(out.end(), {"## " + l.label, ""});
			for (const auto& row : rows) {
				if (l.kind == "thread") {
					out.insert(out.end(), {"**" + show(row["author.name"]) + "** · " + when(row["created_at"]), "", trimmed(row["body"].as_string()), ""});
				} else if (l.kind == "changes") {
					std::string who = show(row["created_by.name"]);
					out.push_back("- " + (who.empty() ? std::string("System") : who) + " " + changed(row["field"], row["before"], row["after"], row["action"]) + " · " + when(row["created_at"]));
				} else {
					std::string line;
					for (const auto& c : l.columns) {
						if (std::string v = show(field_of(row, c)); !v.empty()) line += (line.empty() ? "" : " · ") + v;
					}
					out.push_back("- " + line);
				}
			}
			if (l.kind != "thread") out.push_back("");
		}
		std::string joined;
		for (const auto& line : out) joined += line + "\n";
		return trimmed(joined) + "\n";
	}

	desktop::view copy(page& p, state<view_state> from, std::string label, std::vector<copied_field> fields, std::vector<copied_list> lists,
	                   std::map<std::string, std::vector<desktop::option>> choices) {
		if (from->status != status::ready) return {};
		auto copied = p.kept<bool>("copy:" + label, false);
		return desktop::button(copied.get() ? "Copied" : label, [&p, from, fields, lists, choices, copied] {
			desktop::copy_text(markdown_of(p.title(), from.peek().data, fields, lists, choices));
			copied.set(true);
		});
	}

	namespace {

		struct betweens {
			std::vector<value> things, entries;
			std::string name(const between_spec& spec, const value& thing) const {
				std::string said = show(field_of(thing, spec.shown));
				return said.empty() ? thing["id"].as_string() : said;
			}
			const value* between(const between_spec& spec, const value& a, const value& b) const {
				for (const auto& e : entries) {
					if (field_of(e, spec.from).as_string() == a["id"].as_string() && field_of(e, spec.to).as_string() == b["id"].as_string()) return &e;
				}
				return nullptr;
			}
		};

		// What's open between two, and the forms and buttons for it.
		desktop::view asking(page& p, const between_spec& spec, const betweens& b, state<std::string> open) {
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
			auto done = [open](const answer& got) {
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
				std::string id = (*entry)["id"].as_string(), command = spec.remove->command;
				buttons.push_back(desktop::button(spec.remove->submit.empty() ? "Remove" : spec.remove->submit,
				                                  [&p, command, id, done] { p.run(command, value(std::map<std::string, value>{{"id", id}}), done); }));
			}
			buttons.push_back(desktop::button("Close", [open] { open.set(""); }));
			parts.push_back(desktop::row(std::move(buttons)).spacing(8));
			return desktop::column(std::move(parts)).spacing(10).padding(16).background({.fill = t.surface, .border = t.accent, .border_width = 1.5f, .radius = t.corners});
		}

		betweens gathered(const between_spec& spec) {
			return {rows_of(spec.over.get(), spec.over_list), rows_of(spec.view.get(), spec.list)};
		}

		bool may(const between_spec& spec, bool entry) {
			auto allowed = [](const std::optional<between_command>& c) { return c && c->allowed; };
			return entry ? allowed(spec.update) || allowed(spec.remove) : allowed(spec.create);
		}

	}  // namespace

	desktop::view grid(page& p, between_spec spec) {
		const auto& t = current_theme();
		if (spec.over->status == status::loading || spec.view->status == status::loading) return desktop::text("Loading…", t.quiet());
		auto b = gathered(spec);
		auto open = p.kept<std::string>("grid:" + spec.list);
		std::vector<desktop::view> rows;
		std::vector<desktop::view> header;
		header.push_back(desktop::text(label(spec.from) + ", " + [&] { std::string s = label(spec.to); s[0] = static_cast<char>(std::tolower(s[0])); return s; }(), t.quiet()).width(140));
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
				std::string said = entry ? (spec.cell.empty() ? "" : show(field_of(*entry, spec.cell))) : "";
				if (entry && said.empty()) said = "✓";
				std::string key = a["id"].as_string() + "|" + c["id"].as_string();
				if (may(spec, entry != nullptr)) cells.push_back(desktop::button(entry ? said : "+", [open, key] { open.set(open.peek() == key ? "" : key); }).grow());
				else cells.push_back(desktop::text(said, t.body()).grow().padding(8));
			}
			rows.push_back(desktop::row(std::move(cells)).spacing(6).align(align::center));
		}
		if (auto asked = asking(p, spec, b, open); asked.get()) rows.push_back(std::move(asked));
		return desktop::column(std::move(rows)).spacing(6);
	}

	desktop::view diagram(page& p, between_spec spec) {
		const auto& t = current_theme();
		if (spec.over->status == status::loading || spec.view->status == status::loading) return desktop::text("Loading…", t.quiet());
		auto b = gathered(spec);
		auto open = p.kept<std::string>("diagram:" + spec.list);
		std::vector<desktop::list_row> moves;
		for (const auto& a : b.things) {
			for (const auto& c : b.things) {
				const value* entry = b.between(spec, a, c);
				if (!entry) continue;
				std::string said = spec.cell.empty() ? "" : show(field_of(*entry, spec.cell));
				moves.push_back({a["id"].as_string() + "|" + c["id"].as_string(),
				                 desktop::row(desktop::text(b.name(spec, a), t.strong()), desktop::text("→", t.quiet()), desktop::text(b.name(spec, c), t.strong()),
				                              desktop::text(said, t.quiet()).grow())
				                     .spacing(10)});
			}
		}
		std::vector<desktop::view> parts;
		if (moves.empty()) parts.push_back(desktop::text("Nothing goes anywhere yet.", t.quiet()));
		else parts.push_back(desktop::list(std::move(moves), [open, spec](const std::string& key) {
			if (may(spec, true)) open.set(key);
		}));
		if (auto asked = asking(p, spec, b, open); asked.get()) parts.push_back(std::move(asked));
		return desktop::column(std::move(parts)).spacing(8);
	}

}  // namespace one::desktop::runtime
