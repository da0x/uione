// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <random>

#include "one/local/local.hpp"

namespace one::local {

	std::string made_up_id() {
		static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
		static std::mt19937 random{std::random_device{}()};
		std::uniform_int_distribution<int> pick(0, sizeof alphabet - 2);
		std::string id;
		for (int i = 0; i < 20; ++i) id += alphabet[pick(random)];
		return id;
	}

	namespace {

		bool counts(const value& row, const std::string& within_field, const std::string& within) {
			return within_field.empty() || row[within_field].as_string() == within;
		}

	}  // namespace

	const value* store::find(const std::string& entity, const std::string& id) const {
		auto kind = rows_.find(entity);
		if (kind == rows_.end()) return nullptr;
		auto row = kind->second.find(id);
		return row == kind->second.end() ? nullptr : &row->second;
	}

	std::vector<const value*> store::each(const std::string& entity) const {
		std::vector<const value*> rows;
		auto kind = rows_.find(entity);
		if (kind == rows_.end()) return rows;
		for (const auto& [_, row] : kind->second) rows.push_back(&row);
		return rows;
	}

	void store::apply(const event& e) {
		for (const auto& c : e.changes) {
			if (c.removed) {
				rows_[c.entity].erase(c.id);
				continue;
			}
			value row = c.fields;
			row.fields["id"] = c.id;
			rows_[c.entity][c.id] = std::move(row);
		}
	}

	const change* step::changed(const std::string& entity, const std::string& id) const {
		for (auto c = changes_.rbegin(); c != changes_.rend(); ++c)
			if (c->entity == entity && c->id == id) return &*c;
		return nullptr;
	}

	void step::record(change c) {
		std::erase_if(changes_, [&](const change& earlier) { return earlier.entity == c.entity && earlier.id == c.id; });
		if (!c.removed) c.fields.fields["id"] = c.id;
		changes_.push_back(std::move(c));
	}

	std::optional<value> step::find(const std::string& entity, const std::string& id) const {
		if (const change* c = changed(entity, id)) return c->removed ? std::nullopt : std::optional<value>(c->fields);
		const value* kept = before_.find(entity, id);
		return kept ? std::optional<value>(*kept) : std::nullopt;
	}

	std::vector<value> step::each(const std::string& entity) const {
		std::map<std::string, value> rows;
		for (const value* row : before_.each(entity)) rows[(*row)["id"].as_string()] = *row;
		for (const auto& c : changes_) {
			if (c.entity != entity) continue;
			if (c.removed) rows.erase(c.id);
			else rows[c.id] = c.fields;
		}
		std::vector<value> out;
		for (auto& [_, row] : rows) out.push_back(std::move(row));
		return out;
	}

	std::string step::create(const std::string& entity, value fields) {
		std::string id = fields["id"].as_string();
		if (id.empty()) id = made_up_id();
		fields.type = value::kind::map;
		record({.entity = entity, .id = id, .fields = std::move(fields)});
		return id;
	}

	bool step::update(const std::string& entity, const std::string& id, const value& fields) {
		std::optional<value> row = find(entity, id);
		if (!row) return false;
		for (const auto& [name, field] : fields.as_map())
			if (name != "id") row->fields[name] = field;
		record({.entity = entity, .id = id, .fields = std::move(*row)});
		return true;
	}

	bool step::remove(const std::string& entity, const std::string& id) {
		if (!find(entity, id)) return false;
		record({.entity = entity, .id = id, .removed = true});
		return true;
	}

	std::int64_t step::next_serial(const std::string& entity, const std::string& field, const std::string& within_field, const std::string& within) const {
		std::int64_t highest = 0;
		for (const value& row : each(entity))
			if (counts(row, within_field, within)) highest = std::max(highest, row[field].as_integer());
		return highest + 1;
	}

}  // namespace one::local
