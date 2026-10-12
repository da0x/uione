// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// What a list's rows are kept to and put in order by: a track of another list's
// records above it, and an order the person sets, row by row.

#include <algorithm>
#include <format>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	tracked track(page& p, const std::string& key, const std::optional<track_spec>& spec, const std::vector<value>& rows, const std::string& noun) {
		if (!spec) return {};
		const auto& t = current_theme();
		auto picked = p.kept<std::string>(key + ":track");
		const auto& records = rows_of(spec->view.get(), spec->list);
		if (records.empty()) return {};
		std::vector<desktop::view> cards;
		for (const auto& record : records) {
			std::string id = record["id"].as_string();
			bool chosen = picked.get() == id;
			auto count = std::ranges::count_if(rows, [&](const value& row) { return field_of(row, spec->field).as_string() == id; });
			std::vector<desktop::view> inside;
			std::string title = show(field_of(record, spec->shown));
			auto heading = t.strong();
			if (chosen) heading.color = t.accent;
			inside.push_back(desktop::link(title.empty() ? id : title, [picked, id] { picked.set(picked.peek() == id ? "" : id); }, heading));
			for (const auto& c : spec->columns) {
				if (std::string said = show(field_of(record, c.field)); !said.empty()) inside.push_back(desktop::text(c.label + " " + said, t.quiet()));
			}
			// One issue, two issues.
			std::string counted = count == 1 && noun.ends_with('s') ? noun.substr(0, noun.size() - 1) : noun;
			inside.push_back(desktop::text(std::format("{} {}", count, counted), t.quiet()));
			cards.push_back(desktop::column(std::move(inside))
			                    .spacing(4)
			                    .padding(edges(10, 14))
			                    .width(200)
			                    .background({.fill = chosen ? t.sunken : t.surface, .border = chosen ? t.accent : t.line, .border_width = chosen ? 1.5f : 1, .radius = t.corners}));
		}
		return {desktop::column(desktop::text(label(spec->field), t.quiet()), desktop::scroll_across(desktop::row(std::move(cards)).spacing(10).align(align::start))).spacing(6), picked.get()};
	}

	namespace {

		std::optional<double> number_of(const value& v) {
			if (v.is_integer() || v.is_double()) return v.as_double();
			return std::nullopt;
		}

		// The numbers that put a row between two others, as the web works them out:
		// halfway between theirs, one past the last, one before the first, or, when
		// there's no room, every row numbered again in its new order.
		std::vector<std::pair<std::string, double>> placed(const std::vector<value>& list, const std::string& field, const value& row, const value* before, const value* after) {
			auto low = before ? number_of(field_of(*before, field)) : std::nullopt;
			auto high = after ? number_of(field_of(*after, field)) : std::nullopt;
			std::string id = row["id"].as_string();
			if (low && high && *low < *high) return {{id, (*low + *high) / 2}};
			if (low && !high && !after) return {{id, *low + 1}};
			if (high && !low && !before) return {{id, *high - 1}};
			if (!before && !after) return {};
			std::vector<const value*> order;
			for (const auto& other : list) {
				if (other["id"].as_string() != id) order.push_back(&other);
			}
			std::size_t at = 0;
			for (std::size_t i = 0; i < order.size(); ++i) {
				if (before && (*order[i])["id"].as_string() == (*before)["id"].as_string()) at = i + 1;
				if (!before && after && (*order[i])["id"].as_string() == (*after)["id"].as_string()) at = i;
			}
			order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), &row);
			std::vector<std::pair<std::string, double>> out;
			for (std::size_t i = 0; i < order.size(); ++i) {
				auto now = number_of(field_of(*order[i], field));
				if (!now || *now != static_cast<double>(i + 1)) out.emplace_back((*order[i])["id"].as_string(), static_cast<double>(i + 1));
			}
			return out;
		}

	}  // namespace

	std::vector<value> ordered(page& p, const std::string& key, const std::optional<reorder_spec>& spec, std::vector<value> rows) {
		if (!spec) return rows;
		auto placing = p.kept<std::map<std::string, double>>(key + ":placing");
		// A place the view now shows is settled.
		if (!placing.peek().empty()) {
			auto now = placing.peek();
			std::erase_if(now, [&](const auto& placed_row) {
				return std::ranges::any_of(rows, [&](const value& row) {
					auto n = number_of(field_of(row, spec->field));
					return row["id"].as_string() == placed_row.first && n && *n == placed_row.second;
				});
			});
			if (now.size() != placing.peek().size()) placing.set(now);
		}
		for (auto& row : rows) {
			if (auto found = placing.get().find(row["id"].as_string()); found != placing.get().end()) row.fields[spec->field] = value(found->second);
		}
		std::ranges::stable_sort(rows, [&](const value& a, const value& b) {
			auto x = number_of(field_of(a, spec->field)), y = number_of(field_of(b, spec->field));
			return x && y && *x < *y;
		});
		return rows;
	}

	bool move_row(page& p, const std::string& key, const reorder_spec& spec, const std::vector<value>& around, const std::string& id, int by) {
		if (!spec.allowed) return false;
		auto at = std::ranges::find_if(around, [&](const value& row) { return row["id"].as_string() == id; });
		if (at == around.end()) return false;
		std::ptrdiff_t index = at - around.begin(), to = index + by;
		if (to < 0 || to >= static_cast<std::ptrdiff_t>(around.size())) return false;
		std::vector<const value*> rest;
		for (const auto& row : around) {
			if (row["id"].as_string() != id) rest.push_back(&row);
		}
		const value* before = to > 0 ? rest[static_cast<std::size_t>(to - 1)] : nullptr;
		const value* after = static_cast<std::size_t>(to) < rest.size() ? rest[static_cast<std::size_t>(to)] : nullptr;
		auto changes = placed(around, spec.field, *at, before, after);
		auto placing = p.kept<std::map<std::string, double>>(key + ":placing");
		placing.change([&](auto& now) {
			for (const auto& [row, number] : changes) now[row] = number;
		});
		for (const auto& [row, number] : changes) {
			p.run(spec.command, value(std::map<std::string, value>{{"id", row}, {spec.field, number}}), [placing, row](const answer& a) {
				if (!a) placing.change([&](auto& now) { now.erase(row); });
			}, true);
		}
		return true;
	}

}  // namespace one::desktop::runtime
