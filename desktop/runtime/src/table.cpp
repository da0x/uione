// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Tables: a list's rows, a column each for the fields named, in tabs by a choice,
// found by what's typed, sorted and in pages, each row opening its address and
// running its actions, some asking first with a form under the row.

#include <algorithm>
#include <format>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		std::string lower(std::string s) {
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		text_style quieter() {
			auto s = current_theme().strong();
			s.color = current_theme().muted;
			s.size -= 2;
			return s;
		}

		// A choice as it's shown, or the value itself.
		std::string chosen_as(const table_spec& spec, const std::string& column, const value& v) {
			std::string said = show(v);
			if (auto found = spec.choices.find(column); found != spec.choices.end()) {
				for (const auto& o : found->second) {
					if (o.value == said) return o.label;
				}
			}
			return said;
		}

		// A row's value as a cell shows it: people by name beside their pictures' color,
		// words each on their own, a choice as it's shown, and anything else as text.
		desktop::view cell(const table_spec& spec, const value& row, const std::string& column) {
			const auto& t = current_theme();
			const value& v = field_of(row, column);
			if (spec.people.contains(column)) {
				// Their pictures together, then their names, which wrap within the column.
				std::vector<std::string> names;
				if (v.is_array()) {
					for (const auto& name : v.items)
						if (!show(name).empty()) names.push_back(show(name));
				} else if (!show(v).empty()) {
					names.push_back(show(v));
				}
				if (names.empty()) return desktop::text("", t.body());
				std::vector<desktop::view> pictures;
				std::string joined;
				for (const auto& name : names) {
					pictures.push_back(desktop::avatar(name, 20));
					joined += (joined.empty() ? "" : ", ") + name;
				}
				return desktop::row(desktop::row(std::move(pictures)).spacing(2), desktop::text(joined, t.body()).grow()).spacing(6).align(align::center);
			}
			if (std::ranges::contains(spec.labels, column)) {
				std::vector<desktop::view> words;
				for (const auto& word : v.as_array()) {
					auto look = t.quiet();
					look.color = t.accent;
					words.push_back(desktop::text(show(word), look).padding(edges(1, 7)).background({.fill = t.sunken, .radius = 9}));
				}
				return desktop::row(std::move(words)).spacing(4);
			}
			return desktop::text(chosen_as(spec, column, v), t.body());
		}

		// How much of a row a column takes: a number little, a title more.
		desktop::view sized(desktop::view v, const std::string& column) {
			if (column == "number" || column.ends_with(".number")) return std::move(v).width(56);
			return std::move(v).grow(column == "title" || column.ends_with(".title") ? 2.f : 1.f);
		}

		// Orders by a field, largest or latest first with a -.
		void sorted(std::vector<value>& rows, const std::string& by) {
			if (by.empty()) return;
			bool down = by.starts_with('-');
			std::string field = down ? by.substr(1) : by;
			std::ranges::stable_sort(rows, [&](const value& a, const value& b) {
				const value &x = field_of(a, field), &y = field_of(b, field);
				bool less = (x.is_integer() || x.is_double()) && (y.is_integer() || y.is_double()) ? x.as_double() < y.as_double()
				            : x.is_time() && y.is_time()                                       ? x.as_time() < y.as_time()
				                                                                                : show(x) < show(y);
				bool more = (x.is_integer() || x.is_double()) && (y.is_integer() || y.is_double()) ? y.as_double() < x.as_double()
				            : x.is_time() && y.is_time()                                       ? y.as_time() < x.as_time()
				                                                                                : show(y) < show(x);
				return down ? more : less;
			});
		}

	}  // namespace

	desktop::view row_action_form(page& p, const std::string& list, const row_action& action, const value& row) {
		// The row, as a view's document, for the form to start from.
		std::string id = row["id"].as_string();
		auto started = p.kept<view_state>("table:" + list + ":" + action.command + ":" + id + ":row");
		if (started.peek().status != status::ready || ember::to_json(started.peek().data) != ember::to_json(row)) started.set(view_state{status::ready, row, ""});
		auto acting = p.kept<std::string>("table:" + list + ":acting");
		auto shown = form(p, {.command = action.command, .fields = action.form, .from = started, .id = id, .submit = action.submit.empty() ? action.label : action.submit,
		                      .on_done = [acting] { acting.set(""); }});
		return desktop::column(std::move(shown), desktop::row(desktop::button("Cancel", [acting] { acting.set(""); }))).spacing(8);
	}

	desktop::view table(page& p, table_spec spec) {
		const auto& t = current_theme();
		const view_state& v = *spec.view;
		if (v.status == status::loading) return desktop::text("Loading…", t.quiet());
		if (v.status == status::failed) {
			auto s = t.body();
			s.color = t.danger;
			return desktop::text(v.error.empty() ? "This couldn't be read." : v.error, s);
		}
		std::string key = "table:" + (spec.list.empty() ? std::string("rows") : spec.list);
		auto query = p.kept<std::string>(key + ":search");
		auto tab = p.kept<std::string>(key + ":tab");
		auto at = p.kept<int>(key + ":page", 0);
		auto acting = p.kept<std::string>(key + ":acting");  // the row and action whose form is open
		std::string me = p.reader() ? p.reader()->uid : "";
		std::vector<value> rows;
		for (const auto& row : rows_of(v, spec.list)) {
			if (p.keeps(row) && kept(row, spec.only, me)) rows.push_back(row);
		}
		if (rows.empty() && spec.hide_empty) return {};
		sorted(rows, spec.sort);
		rows = ordered(p, key, spec.reorder, std::move(rows));
		auto tracking = track(p, key, spec.track, rows, spec.list.empty() ? std::string("rows") : spec.list);
		if (spec.track) std::erase_if(rows, [&](const value& row) { return !tracking.keeps(row, spec.track->field); });
		// What's typed, over the fields it searches, as they're shown.
		std::string wanted = lower(query.get());
		if (!wanted.empty()) {
			std::erase_if(rows, [&](const value& row) {
				return std::ranges::none_of(spec.search, [&](const std::string& f) { return lower(chosen_as(spec, f, field_of(row, f))).find(wanted) != std::string::npos; });
			});
		}
		std::vector<desktop::view> parts;
		if (tracking.shown.get()) parts.push_back(std::move(tracking.shown));
		if (auto note = filter_note(p); note.get()) parts.push_back(std::move(note));
		std::vector<desktop::view> tools;
		if (!spec.search.empty()) tools.push_back(desktop::field(query, {.placeholder = "Search"}).width(280));
		if (spec.tools)
			if (auto made = spec.tools(); made.get()) tools.push_back(std::move(made));
		if (!tools.empty()) parts.push_back(desktop::row(std::move(tools)).spacing(10).align(align::center));
		// Tabs, one for each choice of the column it's by, with how many each has.
		if (!spec.by.empty()) {
			auto found = spec.choices.find(spec.by);
			std::vector<desktop::option> choices = found != spec.choices.end() ? found->second : std::vector<desktop::option>{};
			std::string open = tab.get().empty() && !choices.empty() ? choices.front().value : tab.get();
			std::vector<desktop::view> tabs;
			for (const auto& o : choices) {
				auto count = std::ranges::count_if(rows, [&](const value& row) { return show(field_of(row, spec.by)) == o.value; });
				std::string value_of = o.value;
				tabs.push_back(desktop::button(std::format("{} {}", o.label, count), [tab, at, value_of] {
					tab.set(value_of);
					at.set(0);
				}, o.value == open));
			}
			parts.push_back(desktop::row(std::move(tabs)).spacing(6));
			std::erase_if(rows, [&](const value& row) { return show(field_of(row, spec.by)) != open; });
		}
		if (rows.empty()) {
			parts.push_back(desktop::text(spec.empty, t.quiet()));
			return desktop::column(std::move(parts)).spacing(10);
		}
		// A page of them, when it says how many a page has.
		std::size_t total = rows.size(), first = 0, last = total;
		if (spec.page > 0) {
			std::size_t pages = (total + static_cast<std::size_t>(spec.page) - 1) / static_cast<std::size_t>(spec.page);
			std::size_t now = std::min(static_cast<std::size_t>(std::max(0, at.get())), pages - 1);
			first = now * static_cast<std::size_t>(spec.page);
			last = std::min(total, first + static_cast<std::size_t>(spec.page));
		}
		// Each row keeps the same room for its actions, so its columns line up with the others'.
		float acting_width = 0;
		for (const auto& a : spec.actions) {
			if (a.allowed) acting_width += 24 + 9 * static_cast<float>(a.label.size());
		}
		std::vector<desktop::view> labels;
		if (spec.tint) labels.push_back(desktop::spacer().grow(0).width(3));
		for (const auto& c : spec.columns) labels.push_back(sized(desktop::text(c.label, quieter()), c.field));
		if (acting_width > 0) labels.push_back(desktop::spacer().grow(0).width(acting_width));
		parts.push_back(desktop::row(std::move(labels)).spacing(12).padding(edges(0, 8)));
		std::vector<desktop::list_row> shown;
		std::map<std::string, value> by_id;
		for (std::size_t i = first; i < last; ++i) {
			const value& row = rows[i];
			std::string id = row["id"].as_string();
			by_id.emplace(id, row);
			std::vector<desktop::view> cells;
			if (spec.tint) {
				auto tone = tint_of(row, spec.tint);
				cells.push_back(tone ? desktop::edge(*tone) : desktop::spacer().grow(0).width(3));
			}
			for (std::size_t c = 0; c < spec.columns.size(); ++c) {
				// What a row opens is said in the table's color, when it has one, by its first column.
				if (c == 0 && !spec.color.empty() && !spec.link.empty() && !spec.people.contains(spec.columns[c].field) && !std::ranges::contains(spec.labels, spec.columns[c].field)) {
					auto look = t.body();
					look.color = section_color(spec.color);
					cells.push_back(sized(desktop::text(chosen_as(spec, spec.columns[c].field, field_of(row, spec.columns[c].field)), look), spec.columns[c].field));
					continue;
				}
				cells.push_back(sized(cell(spec, row, spec.columns[c].field), spec.columns[c].field));
			}
			std::vector<desktop::view> buttons;
			for (const auto& a : spec.actions) {
				if (!a.allowed || (a.when && !a.when(row))) continue;
				std::string command = a.command;
				std::string opens = id + "|" + command;
				bool asks = !a.form.empty();
				buttons.push_back(desktop::button(p.running(command) ? a.label + "…" : a.label, [&p, command, id, asks, acting, opens] {
					if (asks) acting.set(acting.peek() == opens ? "" : opens);
					else p.run(command, value(std::map<std::string, value>{{"id", id}}));
				}));
			}
			if (acting_width > 0) {
				buttons.insert(buttons.begin(), desktop::spacer());
				cells.push_back(desktop::row(std::move(buttons)).spacing(6).width(acting_width));
			}
			std::vector<desktop::view> inside;
			inside.push_back(desktop::row(std::move(cells)).spacing(12).align(align::center));
			for (const auto& a : spec.actions) {
				if (acting.get() == id + "|" + a.command && !a.form.empty()) inside.push_back(row_action_form(p, spec.list, a, row));
			}
			shown.push_back({id, desktop::column(std::move(inside)).spacing(10)});
		}
		std::function<void(const std::string&)> opening;
		if (!spec.link.empty()) {
			opening = [&p, by_id, link = spec.link, keyed = spec.keyed, named = spec.named](const std::string& id) {
				if (auto found = by_id.find(id); found != by_id.end()) p.open(row_link(link, found->second, p.params(), keyed, named));
			};
		}
		desktop::list_options options{.on_open = std::move(opening)};
		// Put in order while nothing typed hides some of them.
		if (spec.reorder && spec.reorder->allowed && wanted.empty()) {
			std::vector<value> around(rows.begin() + static_cast<std::ptrdiff_t>(first), rows.begin() + static_cast<std::ptrdiff_t>(last));
			options.on_row_key = [&p, key, reorder = *spec.reorder, around](const std::string& id, const key_event& e) {
				if (!e.alt || (!e.is(keys::up) && !e.is(keys::down))) return false;
				move_row(p, key, reorder, around, id, e.is(keys::up) ? -1 : 1);
				return true;
			};
		}
		parts.push_back(desktop::list(std::move(shown), std::move(options)));
		if (spec.page > 0 && total > static_cast<std::size_t>(spec.page)) {
			std::vector<desktop::view> paging;
			if (first > 0) paging.push_back(desktop::button("Previous", [at] { at.set(at.peek() - 1); }));
			paging.push_back(desktop::text(std::format("{}–{} of {}", first + 1, last, total), t.quiet()));
			if (last < total) paging.push_back(desktop::button("Next", [at] { at.set(at.peek() + 1); }));
			parts.push_back(desktop::row(std::move(paging)).spacing(10).align(align::center));
		}
		return desktop::column(std::move(parts)).spacing(8);
	}

}  // namespace one::desktop::runtime
