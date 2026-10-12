// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Threads and timelines: what people wrote, one after another, and what happened
// to something, each change said in words, as the web says them.

#include <algorithm>
#include <format>
#include <set>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		std::string spaced(std::string s) {
			std::ranges::replace(s, '_', ' ');
			return s;
		}

		std::vector<std::string> cut(const std::string& s, const std::string& by) {
			std::vector<std::string> out;
			std::size_t at = 0;
			for (std::size_t next; (next = s.find(by, at)) != std::string::npos; at = next + by.size()) out.push_back(s.substr(at, next - at));
			out.push_back(s.substr(at));
			return out;
		}

		std::string lower(std::string s) {
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		bool nothing(const value& v) { return v.is_null() || (v.is_string() && v.text.empty()) || (v.is_array() && v.items.empty()); }

		std::string action_of(const value& command) {
			auto parts = cut(command.as_string(), "::");
			return parts.back();
		}

		// A list of changes with each command said once: one like close, setting status
		// and closed_at together, keeps only its first change.
		std::vector<const value*> once_each(const std::vector<value>& rows) {
			auto made = [](const value& row) {
				auto parts = cut(row["action"].as_string(), "::");
				return parts.size() >= 2 && parts[parts.size() - 2] == row["field"].as_string();
			};
			std::vector<const value*> kept;
			for (const auto& row : rows) {
				bool said = row["field"].is_string() && !row["field"].text.empty() && row.find("before") && row.find("after") && nothing(row["before"]) && nothing(row["after"]);
				if (made(row) || !said) kept.push_back(&row);
			}
			std::vector<const value*> out;
			for (std::size_t i = 0; i < kept.size(); ++i) {
				const value& row = *kept[i];
				std::string act = action_of(row["action"]);
				bool own = row["action"].is_string() && act != "create" && act != "update" && act != "delete";
				if (own && i > 0 && (*kept[i - 1])["action"].as_string() == row["action"].as_string() && show((*kept[i - 1])["created_at"]) == show(row["created_at"])) continue;
				out.push_back(&row);
			}
			return out;
		}

	}  // namespace

	std::string done(const std::string& action) {
		static const std::map<std::string, std::string> irregular{
		    {"take", "took"}, {"make", "made"}, {"give", "gave"}, {"send", "sent"}, {"leave", "left"}, {"begin", "began"}, {"write", "wrote"},
		    {"set", "set"},   {"put", "put"},   {"run", "ran"},   {"find", "found"}, {"hold", "held"},  {"keep", "kept"},   {"get", "got"},
		    {"bring", "brought"}, {"buy", "bought"}, {"pay", "paid"}, {"lead", "led"}};
		static const std::set<std::string> stressed_last{"transfer", "refer", "prefer", "defer", "confer", "infer", "admit", "commit", "submit",
		                                                  "permit",   "omit",  "compel", "expel", "occur", "recur", "regret", "control"};
		auto words = cut(action, "_");
		std::string verb = words.front(), past;
		auto vowel = [](char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; };
		// One vowel, before a last consonant other than w, x or y, like stop: stopped.
		auto short_closed = [&] {
			std::size_t first = verb.find_first_of("aeiou");
			if (first == std::string::npos || first + 2 != verb.size()) return false;
			char last = verb.back();
			return !vowel(last) && last != 'w' && last != 'x' && last != 'y';
		};
		if (auto found = irregular.find(verb); found != irregular.end()) past = found->second;
		else if (verb.ends_with('e')) past = verb + "d";
		else if (verb.size() >= 2 && verb.back() == 'y' && !vowel(verb[verb.size() - 2])) past = verb.substr(0, verb.size() - 1) + "ied";
		else if (short_closed() || stressed_last.contains(verb)) past = verb + verb.back() + "ed";
		else past = verb + "ed";
		words.front() = past;
		std::string out;
		for (const auto& w : words) out += (out.empty() ? "" : " ") + w;
		return out;
	}

	std::pair<std::string, std::string> phrase(const value& field, const value& before, const value& after, const value& command) {
		auto parts = command.is_string() ? cut(command.text, "::") : std::vector<std::string>{};
		std::string made = parts.size() >= 2 ? parts[parts.size() - 2] : "";
		std::string name = field.as_string();
		// A change kept for something of its own, like a comment on an issue.
		if (!name.empty() && name == made) {
			std::string thing = spaced(made), did = parts.back();
			if (did == "create") return {"added a " + thing + " to", ""};
			if (did == "delete") return {"removed a " + thing + " from", ""};
			return {"changed a " + thing + " on", ""};
		}
		std::string verb = command.is_string() ? parts.back() : "";
		if (!verb.empty() && verb != "create" && verb != "update" && verb != "delete") {
			std::string was = show(before), is = show(after);
			bool says = !name.empty() && !was.empty() && !is.empty() && lower(done(verb)).find(lower(is)) == std::string::npos;
			return {done(verb), says ? "from " + was + " to " + is : ""};
		}
		if (name.empty()) return {"created", ""};
		std::string spoken = spaced(name), was = show(before), is = show(after);
		auto long_text = [](const std::string& t) { return t.size() > 80 || t.find('\n') != std::string::npos; };
		if (long_text(was) || long_text(is)) return {was.empty() ? "wrote " + spoken + " of" : is.empty() ? "cleared " + spoken + " of" : "changed " + spoken + " of", ""};
		if (was.empty()) return {"set " + spoken + " of", "to " + is};
		if (is.empty()) return {"cleared " + spoken + " of", ""};
		return {"changed " + spoken + " of", "from " + was + " to " + is};
	}

	std::string changed(const value& field, const value& before, const value& after, const value& command) {
		auto [head, tail] = phrase(field, before, after, command);
		std::string out = head.ends_with(" of") ? head.substr(0, head.size() - 3) + (tail.empty() ? "" : " " + tail) : head + " this" + (tail.empty() ? "" : " " + tail);
		return out;
	}

	std::string when(const value& at) {
		if (!at.is_time()) return show(at);
		auto t = std::chrono::zoned_time(time_zone(), std::chrono::floor<std::chrono::minutes>(at.as_time())).get_local_time();
		std::string day = std::format("{:%b %e, %Y, %H:%M}", t);
		if (auto doubled = day.find("  "); doubled != std::string::npos) day.erase(doubled, 1);
		return day;
	}

	desktop::view thread(page&, thread_spec spec) {
		const view_state& v = *spec.view;
		const auto& t = current_theme();
		if (v.status == status::loading) return desktop::text("Loading…", t.quiet());
		std::vector<desktop::view> entries;
		for (const auto& row : rows_of(v, spec.list)) {
			std::string author = show(row["author.name"]), body = row["body"].as_string();
			entries.push_back(desktop::row(desktop::avatar(author, 28),
			                               desktop::column(desktop::row(desktop::text(author, t.strong()), desktop::text(when(row["created_at"]), t.quiet())).spacing(8),
			                                               spec.body ? spec.body(body) : desktop::text(body, t.body()))
			                                   .spacing(6)
			                                   .grow())
			                      .spacing(12)
			                      .align(align::start)
			                      .padding(14)
			                      .background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners}));
		}
		if (entries.empty()) return desktop::text("Nothing yet.", t.quiet());
		return desktop::column(std::move(entries)).spacing(10);
	}

	desktop::view timeline(page& p, timeline_spec spec) {
		const view_state& v = *spec.view;
		const auto& t = current_theme();
		auto rows = v.status == status::ready ? once_each(rows_of(v, spec.list)) : std::vector<const value*>{};
		// The views it judges new by, as they were when the page opened.
		auto looked = p.kept<std::optional<std::vector<value>>>("timeline:" + spec.list + ":looked");
		bool ready = std::ranges::all_of(spec.fresh_views, [](const state<view_state>& f) { return f->status != status::loading; });
		if (spec.fresh && ready && !looked.peek()) {
			std::vector<value> read;
			for (const auto& f : spec.fresh_views) read.push_back(f.peek().data);
			looked.set(read);
		}
		auto is_new = [&](const value& row) { return spec.fresh && looked.peek() && spec.fresh(row, *looked.peek()); };
		std::size_t fresh = std::ranges::count_if(rows, [&](const value* row) { return is_new(*row); });
		auto said = p.kept<bool>("timeline:" + spec.list + ":said", false);
		if (!spec.on_open.empty() && fresh > 0 && !said.peek()) {
			said.set(true);
			p.run(spec.on_open, value::map());
		}
		std::vector<desktop::view> entries;
		for (const value* row : rows) {
			std::string who = show((*row)["created_by.name"]);
			if (who.empty()) who = "System";  // a change no one made, like a migration's
			std::vector<desktop::view> line;
			line.push_back(desktop::text(who, t.strong()));
			if (spec.subject.empty()) {
				line.push_back(desktop::text(changed((*row)["field"], (*row)["before"], (*row)["after"], (*row)["action"]), t.body()));
			} else {
				auto [head, tail] = phrase((*row)["field"], (*row)["before"], (*row)["after"], (*row)["action"]);
				std::string subject;
				for (const auto& column : spec.subject) {
					std::string part = column.ends_with(".number") || column == "number" ? "#" + show(field_of(*row, column)) : show(field_of(*row, column));
					if (!part.empty() && part != "#") subject += (subject.empty() ? "" : " ") + part;
				}
				line.push_back(desktop::text(head, t.body()));
				if (!spec.link.empty()) line.push_back(desktop::link(subject, [&p, to = row_link(spec.link, *row, p.params(), spec.keyed, spec.named)] { p.open(to); }));
				else line.push_back(desktop::text(subject, t.strong()));
				if (!tail.empty()) line.push_back(desktop::text(tail, t.body()));
			}
			line.push_back(desktop::spacer());
			line.push_back(desktop::text(when((*row)["created_at"]), t.quiet()));
			std::vector<desktop::view> entry;
			entry.push_back(desktop::edge(is_new(*row) ? t.accent : t.line));
			entry.push_back(desktop::avatar(who, 22));
			entry.push_back(desktop::row(std::move(line)).spacing(6).align(align::center).grow());
			entries.push_back(desktop::row(std::move(entry)).spacing(10).align(align::center).padding(edges(4, 0)));
		}
		if (entries.empty()) return {};  // shown only while it holds something
		std::vector<desktop::view> parts;
		if (!spec.title.empty()) {
			auto heading = t.strong();
			heading.size += 2;
			parts.push_back(desktop::text(fresh > 0 ? std::format("{} ({} new)", spec.title, fresh) : spec.title, heading));
		}
		for (auto& e : entries) parts.push_back(std::move(e));
		return desktop::column(std::move(parts)).spacing(4);
	}

}  // namespace one::desktop::runtime
