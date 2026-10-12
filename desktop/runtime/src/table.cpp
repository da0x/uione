// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Tables, drawn: a column each, in tabs by a choice, with search and pages, each
// row opening its address and running its actions, some asking first with a form
// under the row; and the track of records above a list.

#include <algorithm>
#include <format>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		text_style quieter() {
			auto s = current_theme().strong();
			s.color = current_theme().muted;
			s.size -= 2;
			return s;
		}

		// A row's value as a cell shows it: people by their pictures and names, words
		// each on their own, a choice as it's shown, and anything else as text.
		desktop::view cell(const table_spec& spec, const value& row, const std::string& column) {
			const auto& t = current_theme();
			if (spec.people.contains(column)) {
				auto names = people_in(row, column);
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
				std::vector<desktop::view> words_shown;
				for (const auto& word : field_of(row, column).as_array()) {
					auto look = t.quiet();
					look.color = t.accent;
					words_shown.push_back(desktop::text(screens::show(word), look).padding(edges(1, 7)).background({.fill = t.sunken, .radius = 9}));
				}
				return desktop::row(std::move(words_shown)).spacing(4);
			}
			return desktop::text(cell_text(spec.choices, row, column), t.body());
		}

		// How much of a row a column takes: a number little, a title more.
		desktop::view sized(desktop::view v, const std::string& column) {
			if (column == "number" || column.ends_with(".number")) return std::move(v).width(56);
			return std::move(v).grow(column == "title" || column.ends_with(".title") ? 2.f : 1.f);
		}

		desktop::view row_form(page& p, const std::string& list, const row_action& action, const value& row, state<std::string> acting) {
			auto shown = form(p, {.command = action.command, .fields = action.form, .from = acted_on(p, list, action.command, row), .id = row["id"].as_string(),
			                      .submit = action.submit.empty() ? action.label : action.submit, .on_done = [acting] { acting.set(""); }});
			return desktop::column(std::move(shown), desktop::row(desktop::button("Cancel", [acting] { acting.set(""); }))).spacing(8);
		}

	}  // namespace

	desktop::view track(const std::optional<track_model>& model) {
		if (!model) return {};
		const auto& t = current_theme();
		std::vector<desktop::view> cards_shown;
		for (const auto& record : model->records) {
			std::vector<desktop::view> inside;
			auto heading_style = t.strong();
			if (record.picked) heading_style.color = t.accent;
			inside.push_back(desktop::link(record.title, [model = *model, id = record.id] { model.pick(id); }, heading_style));
			for (const auto& d : record.details) inside.push_back(desktop::text(d, t.quiet()));
			cards_shown.push_back(desktop::column(std::move(inside))
			                          .spacing(4)
			                          .padding(edges(10, 14))
			                          .width(200)
			                          .background({.fill = record.picked ? t.sunken : t.surface, .border = record.picked ? t.accent : t.line,
			                                       .border_width = record.picked ? 1.5f : 1, .radius = t.corners}));
		}
		return desktop::column(desktop::text(model->label, t.quiet()), desktop::scroll_across(desktop::row(std::move(cards_shown)).spacing(10).align(align::start))).spacing(6);
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
		auto model = table_of(p, spec);
		if (model.hidden) return {};
		std::vector<desktop::view> parts;
		if (auto shown = track(model.track); shown.get()) parts.push_back(std::move(shown));
		if (auto note = filter_note(p); note.get()) parts.push_back(std::move(note));
		std::vector<desktop::view> tools;
		if (!spec.search.empty()) tools.push_back(desktop::field(model.query, {.placeholder = "Search"}).width(280));
		if (spec.tools)
			if (auto made = spec.tools(); made.get()) tools.push_back(std::move(made));
		if (!tools.empty()) parts.push_back(desktop::row(std::move(tools)).spacing(10).align(align::center));
		// Tabs, one for each choice of the column it's by, with how many each has.
		if (!model.tabs.empty()) {
			std::vector<desktop::view> tabs;
			for (const auto& tab : model.tabs) {
				tabs.push_back(desktop::button(std::format("{} {}", tab.label, tab.count), [model, value_of = tab.value] {
					model.tab.set(value_of);
					model.at.set(0);
				}, tab.open));
			}
			parts.push_back(desktop::row(std::move(tabs)).spacing(6));
		}
		if (model.rows.empty()) {
			parts.push_back(desktop::text(spec.empty, t.quiet()));
			return desktop::column(std::move(parts)).spacing(10);
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
		for (const auto& row : model.rows) {
			std::string id = row["id"].as_string();
			by_id.emplace(id, row);
			std::vector<desktop::view> cells;
			if (spec.tint) {
				auto tone = tone_color(tone_of(row, spec.tint));
				cells.push_back(tone ? desktop::edge(*tone) : desktop::spacer().grow(0).width(3));
			}
			for (std::size_t c = 0; c < spec.columns.size(); ++c) {
				const auto& column = spec.columns[c].field;
				// What a row opens is said in the table's color, when it has one, by its first column.
				if (c == 0 && !spec.color.empty() && !spec.link.empty() && !spec.people.contains(column) && !std::ranges::contains(spec.labels, column)) {
					auto look = t.body();
					look.color = section_color(spec.color);
					cells.push_back(sized(desktop::text(cell_text(spec.choices, row, column), look), column));
					continue;
				}
				cells.push_back(sized(cell(spec, row, column), column));
			}
			std::vector<desktop::view> buttons;
			for (const auto& a : spec.actions) {
				if (!a.allowed || (a.when && !a.when(row))) continue;
				std::string opens = id + "|" + a.command;
				bool asks = !a.form.empty();
				buttons.push_back(desktop::button(p.running(a.command) ? a.label + "…" : a.label, [&p, command = a.command, id, asks, acting = model.acting, opens] {
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
				if (model.acting.get() == id + "|" + a.command && !a.form.empty()) inside.push_back(row_form(p, spec.list, a, row, model.acting));
			}
			shown.push_back({id, desktop::column(std::move(inside)).spacing(10)});
		}
		desktop::list_options options;
		if (!spec.link.empty()) {
			options.on_open = [&p, by_id, link = spec.link, keyed = spec.keyed, named = spec.named](const std::string& id) {
				if (auto found = by_id.find(id); found != by_id.end()) p.open(row_link(link, found->second, p.params(), keyed, named));
			};
		}
		// Put in order with Alt and the arrows while nothing typed hides some of them.
		if (model.arranging) {
			options.on_row_key = [&p, key = model.key, reorder = *spec.reorder, around = model.rows](const std::string& id, const key_event& e) {
				if (!e.alt || (!e.is(keys::up) && !e.is(keys::down))) return false;
				move_row(p, key, reorder, around, id, e.is(keys::up) ? -1 : 1);
				return true;
			};
		}
		parts.push_back(desktop::list(std::move(shown), std::move(options)));
		if (spec.page > 0 && model.total > static_cast<std::size_t>(spec.page)) {
			std::vector<desktop::view> paging;
			if (model.first > 0) paging.push_back(desktop::button("Previous", [at = model.at] { at.set(at.peek() - 1); }));
			paging.push_back(desktop::text(std::format("{}–{} of {}", model.first + 1, model.last, model.total), t.quiet()));
			if (model.last < model.total) paging.push_back(desktop::button("Next", [at = model.at] { at.set(at.peek() + 1); }));
			parts.push_back(desktop::row(std::move(paging)).spacing(10).align(align::center));
		}
		return desktop::column(std::move(parts)).spacing(8);
	}

}  // namespace one::desktop::runtime
