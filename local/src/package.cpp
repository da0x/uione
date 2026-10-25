// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/local/package.hpp"

#include <one/screens/screens.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <stdexcept>

namespace one::local {

	namespace {

		// Now, as RFC 3339 in UTC, always with nine digits of the second, so two times
		// are in order as text.
		std::string now_text() {
			auto since = std::chrono::system_clock::now().time_since_epoch();
			auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since);
			auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(since - seconds).count();
			std::time_t whole = static_cast<std::time_t>(seconds.count());
			std::tm parts{};
			gmtime_r(&whole, &parts);
			char text[64];
			std::size_t length = std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &parts);
			return std::string(text, length) + std::format(".{:09}Z", nanos);
		}

		bool numeric(const value& v) { return v.is_integer() || v.is_double(); }

		// A number as a .one file writes it, 5 or 0.5, and anything else as its text.
		std::string written(const value& v) {
			if (v.is_integer()) return std::to_string(v.integer);
			if (v.is_double()) return std::format("{}", v.floating);
			return v.as_string();
		}

		bool alike(const value& a, const value& b) {
			if (numeric(a) && numeric(b)) return a.as_double() == b.as_double();
			if (a.type != b.type) return false;
			if (a.is_array() || a.is_map()) return ember::to_json(a) == ember::to_json(b);
			return compare(a, b) == 0;
		}

		// The last part of an entity's name, as a person reads it: library::book is book.
		std::string called(const std::string& entity) {
			std::size_t at = entity.rfind("::");
			std::string last = at == std::string::npos ? entity : entity.substr(at + 2);
			std::string words = label(last);
			if (!words.empty()) words[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(words[0])));
			return words;
		}

		std::string no_such(const std::string& entity) { return "there's no such " + called(entity); }

		// Rows in the order they were made, oldest first, ties by id.
		void by_age(std::vector<value>& rows, bool oldest_first) {
			std::ranges::stable_sort(rows, [&](const value& a, const value& b) {
				int c = compare(a["created_at"], b["created_at"]);
				if (c != 0) return (c < 0) == oldest_first;
				return a["id"].as_string() < b["id"].as_string();
			});
		}

	}  // namespace

	bool nothing(const value& v) {
		switch (v.type) {
			case value::kind::null: return true;
			case value::kind::string: return v.text.empty();
			case value::kind::array: return v.items.empty();
			default: return false;
		}
	}

	int compare(const value& a, const value& b) {
		if (a.is_null()) return b.is_null() ? 0 : -1;
		if (b.is_null()) return 1;
		if (a.is_bool() && b.is_bool()) return a.boolean == b.boolean ? 0 : (a.boolean ? 1 : -1);
		if (numeric(a) && numeric(b)) {
			double x = a.as_double(), y = b.as_double();
			return x < y ? -1 : (y < x ? 1 : 0);
		}
		if (a.is_string() && b.is_string()) return a.text.compare(b.text) < 0 ? -1 : (a.text == b.text ? 0 : 1);
		return 0;
	}

	std::string label(const std::string& name) {
		std::string words = name;
		std::ranges::replace(words, '_', ' ');
		if (!words.empty()) words[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(words[0])));
		return words;
	}

	const field* entity_description::find(const std::string& name) const {
		for (const auto& f : fields)
			if (f.name == name) return &f;
		return nullptr;
	}

	entity_description entity(std::string name, std::vector<local::field> fields) { return {std::move(name), std::move(fields)}; }

	query query::where(std::string field, compared wanted) const {
		query q = *this;
		q.conditions.push_back({std::move(field), std::move(wanted), condition::is});
		return q;
	}

	query query::except(std::string field, compared wanted) const {
		query q = *this;
		q.conditions.push_back({std::move(field), std::move(wanted), condition::is_not});
		return q;
	}

	query query::has(std::string field, compared wanted) const {
		query q = *this;
		q.conditions.push_back({std::move(field), std::move(wanted), condition::holds});
		return q;
	}

	query query::either(query other) const {
		query q = *this;
		q.others.push_back(std::move(other));
		return q;
	}

	query all(std::string entity) { return query{.entity = std::move(entity), .conditions = {}, .others = {}}; }
	query where(std::string entity, std::string field, compared wanted) { return all(std::move(entity)).where(std::move(field), std::move(wanted)); }
	query first(std::string entity) { return all(std::move(entity)); }

	by::by(std::string name) {
		descending = name.starts_with("-");
		field = descending ? name.substr(1) : std::move(name);
	}

	namespace {

		view_description::listing& last(view_description& v, const char* what) {
			if (v.lists.empty()) throw std::logic_error("view " + v.name + " has " + what + " before any each or list");
			return v.lists.back();
		}

	}  // namespace

	view_description& view_description::list(std::string list_name, query rows) {
		lists.push_back({.name = std::move(list_name), .rows = std::move(rows), .order = {}, .limit = 0, .fields = {}, .values = {}});
		return *this;
	}

	view_description& view_description::per(std::string entity) {
		per_kind = std::move(entity);
		return *this;
	}

	view_description& view_description::copy(std::string copy_name, std::string field) {
		copies.emplace_back(std::move(copy_name), std::move(field));
		return *this;
	}

	view_description& view_description::count(std::string count_name, query rows) {
		counts.emplace_back(std::move(count_name), std::move(rows));
		return *this;
	}

	view_description& view_description::order(std::vector<by> keys) {
		last(*this, "an order").order = std::move(keys);
		return *this;
	}

	view_description& view_description::limit(int rows) {
		last(*this, "a limit").limit = rows;
		return *this;
	}

	view_description& view_description::fields(std::vector<std::string> names) {
		last(*this, "fields").fields = std::move(names);
		return *this;
	}

	view_description& view_description::value(std::string value_name, query from, std::string field) {
		last(*this, "a value").values.push_back({std::move(value_name), std::move(from), std::move(field)});
		return *this;
	}

	view_description view(std::string name) { return view_description{.name = std::move(name), .per_kind = {}, .copies = {}, .counts = {}, .lists = {}}; }

	command_description& command_description::creates(std::string on) {
		entity = std::move(on);
		does_what = kind::creates;
		return *this;
	}

	command_description& command_description::updates(std::string on) {
		entity = std::move(on);
		does_what = kind::updates;
		return *this;
	}

	command_description& command_description::deletes(std::string on) {
		entity = std::move(on);
		does_what = kind::deletes;
		return *this;
	}

	command_description& command_description::on(std::string on) {
		entity = std::move(on);
		does_what = kind::acts;
		return *this;
	}

	command_description& command_description::fields(std::vector<std::string> names) {
		allowed = std::move(names);
		return *this;
	}

	command_description& command_description::takes(std::vector<std::string> names) {
		inputs = std::move(names);
		return *this;
	}

	command_description& command_description::does(local::body body) {
		run = std::move(body);
		return *this;
	}

	command_description command(std::string name) {
		command_description c;
		c.name = std::move(name);
		return c;
	}

	package::package(std::string name, std::vector<entity_description> entities, std::vector<command_description> commands, std::vector<view_description> views)
	    : name_(std::move(name)), commands_(std::move(commands)), views_(std::move(views)) {
		for (auto& e : entities) {
			std::string kind = e.name;
			entities_.emplace(std::move(kind), std::move(e));
		}
	}

	const entity_description* package::entity(const std::string& name) const {
		auto found = entities_.find(name);
		return found == entities_.end() ? nullptr : &found->second;
	}

	void package::install(local_source& source) const {
		for (const auto& c : commands_) source.on(c.name, [this, &c](const value& sent, step& changes) { return run(c, sent, changes); });
		for (const auto& v : views_)
			source.define(v.name, [this, &v](const store& entities, const std::string& subject) { return build(v, entities, subject); });
	}

	package::reading package::from(const store& entities) {
		return {[&entities](const std::string& kind) {
			        std::vector<value> rows;
			        for (const value* row : entities.each(kind)) rows.push_back(*row);
			        return rows;
		        },
		        [&entities](const std::string& kind, const std::string& id) -> std::optional<value> {
			        const value* row = entities.find(kind, id);
			        return row ? std::optional<value>(*row) : std::nullopt;
		        }};
	}

	package::reading package::from(const step& changes) {
		return {[&changes](const std::string& kind) { return changes.each(kind); },
		        [&changes](const std::string& kind, const std::string& id) { return changes.find(kind, id); }};
	}

	namespace {

		// A row's field, or one read through what it points at, like book.title: the
		// title of the book its book field names, or of each in a list of them.
		value field_of(const value& row, const std::string& name, const entity_description* kind, const package::reading& r) {
			std::size_t dot = name.find('.');
			if (dot == std::string::npos) return row[name];
			std::string through = name.substr(0, dot), inner = name.substr(dot + 1);
			const field* pointer = kind ? kind->find(through) : nullptr;
			if (!pointer || pointer->refers.empty()) return value();
			auto read = [&](const value& id) -> value {
				if (!id.is_string()) return value();
				auto found = r.find(pointer->refers, id.text);
				return found ? (*found)[inner] : value();
			};
			const value& ids = row[through];
			if (ids.is_array()) {
				std::vector<value> each;
				for (const auto& id : ids.items) each.push_back(read(id));
				return value(std::move(each));
			}
			return read(ids);
		}

		value wanted_of(const compared& c, const value* subject, const std::string& row) {
			return std::visit(
			    [&](const auto& w) -> value {
				    using kind = std::decay_t<decltype(w)>;
				    if constexpr (std::is_same_v<kind, value>) return w;
				    else if constexpr (std::is_same_v<kind, subject_marker>) return subject ? (*subject)["id"] : value();
				    else if constexpr (std::is_same_v<kind, subject_field>) return subject ? (*subject)[w.field] : value();
				    else if constexpr (std::is_same_v<kind, row_marker>) return value(row);
				    else return value();
			    },
			    c);
		}

	}  // namespace

	std::vector<value> package::picked(const query& q, const reading& r, const value* subject, const std::string& row) const {
		std::vector<value> out;
		std::vector<std::string> seen;
		std::vector<const query*> ways{&q};
		for (const auto& other : q.others) ways.push_back(&other);
		for (const query* way : ways) {
			const entity_description* kind = entity(way->entity);
			for (const value& candidate : r.each(way->entity)) {
				bool kept = true;
				for (const auto& c : way->conditions) {
					value got = field_of(candidate, c.field, kind, r);
					value wanted = wanted_of(c.wanted, subject, row);
					bool holds = false;
					if (c.test == query::condition::holds) {
						for (const auto& item : got.as_array()) holds = holds || alike(item, wanted);
					} else {
						holds = nothing(wanted) ? nothing(got) : alike(got, wanted);
						if (c.test == query::condition::is_not) holds = !holds;
					}
					if (!holds) {
						kept = false;
						break;
					}
				}
				std::string id = candidate["id"].as_string();
				if (kept && std::ranges::find(seen, id) == seen.end()) {
					seen.push_back(id);
					out.push_back(candidate);
				}
			}
		}
		return out;
	}

	std::optional<value> package::build(const view_description& v, const store& entities, const std::string& subject) const {
		reading r = from(entities);
		std::optional<value> per;
		if (!v.per_kind.empty()) {
			per = r.find(v.per_kind, subject);
			if (!per) return std::nullopt;
		}
		const value* about = per ? &*per : nullptr;
		value document = value::map();
		for (const auto& [name, field] : v.copies) document.fields[name] = about ? field_of(*about, field, entity(v.per_kind), r) : value();
		for (const auto& [name, rows] : v.counts) document.fields[name] = static_cast<long long>(picked(rows, r, about, "").size());
		for (const auto& l : v.lists) {
			std::vector<value> rows = picked(l.rows, r, about, "");
			const entity_description* kind = entity(l.rows.entity);
			// Newest first, unless the list has an order: then the ones that tie keep
			// the order they were made in, so a new one goes after the others.
			by_age(rows, !l.order.empty());
			if (!l.order.empty()) {
				auto key = [&](const value& row, const by& o) { return o.through ? o.through(field_of(row, o.field, kind, r)) : field_of(row, o.field, kind, r); };
				std::ranges::stable_sort(rows, [&](const value& a, const value& b) {
					for (const auto& o : l.order) {
						int c = compare(key(a, o), key(b, o));
						if (c != 0) return (c < 0) != o.descending;
					}
					return false;
				});
			}
			if (l.limit > 0 && rows.size() > static_cast<std::size_t>(l.limit)) rows.resize(static_cast<std::size_t>(l.limit));
			std::vector<value> shown;
			for (const value& row : rows) {
				value out = value::map();
				out.fields["id"] = row["id"];
				for (const auto& name : l.fields) out.fields[name] = field_of(row, name, kind, r);
				for (const auto& val : l.values) {
					std::vector<value> found = picked(val.from, r, about, row["id"].as_string());
					by_age(found, true);  // the earliest made
					out.fields[val.name] = found.empty() ? value() : found.front()[val.field];
				}
				shown.push_back(std::move(out));
			}
			document.fields[l.name] = value(std::move(shown));
		}
		return document;
	}

	namespace {

		std::expected<void, std::string> validate(const entity_description& e, const value& v) {
			for (const auto& f : e.fields) {
				const value& held = v[f.name];
				if (f.required && nothing(held)) return std::unexpected(label(f.name) + " is required");
				if (!f.choices.empty() && held.is_string() && !held.text.empty() && std::ranges::find(f.choices, held.text) == f.choices.end()) {
					std::string all;
					for (const auto& c : f.choices) all += (all.empty() ? "" : ", ") + c;
					return std::unexpected(label(f.name) + " has to be one of " + all);
				}
				if (f.range && numeric(held)) {
					double n = held.as_double();
					if (n < f.range->from || n > f.range->to)
						return std::unexpected(std::format("{} is from {} to {}", label(f.name), f.range->from, f.range->to));
					double steps = (n - f.range->from) / f.range->by;
					if (std::abs(steps - std::round(steps)) > 1e-9)
						return std::unexpected(std::format("{} goes in steps of {} from {}", label(f.name), f.range->by, f.range->from));
				}
				if (!f.after.empty()) {
					const value& before = v[f.after];
					if (held.is_string() && before.is_string() && !held.text.empty() && !before.text.empty() && !(held.text > before.text)) {
						std::string other = label(f.after);
						std::ranges::transform(other, other.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						return std::unexpected(label(f.name) + " has to be after " + other);
					}
				}
			}
			return {};
		}

		// What's kept of an entity once made: its keys and serials never change.
		void keep_fixed(const entity_description& e, const value& before, value& after) {
			for (const auto& f : e.fields)
				if (f.key || f.serial) after.fields[f.name] = before[f.name];
			after.fields["id"] = before["id"];
			after.fields["created_at"] = before["created_at"];
		}

	}  // namespace

	std::expected<std::string, std::string> package::make(const entity_description& e, value given, step& changes, const std::string& now,
	                                                     const std::function<std::expected<void, std::string>(value&)>& body) const {
		value v = value::map();
		for (const auto& f : e.fields) {
			if (const value* sent = given.find(f.name)) v.fields[f.name] = *sent;
			if (!nothing(v[f.name])) continue;
			if (f.initial) v.fields[f.name] = *f.initial;
			else if (f.now) v.fields[f.name] = now;
		}
		for (const auto& f : e.fields) {
			if (!f.serial) continue;
			std::string within = f.serial->empty() ? "" : written(v[*f.serial]);
			v.fields[f.name] = static_cast<long long>(changes.next_serial(e.name, f.name, *f.serial, within));
		}
		// The id is made from its keys, in order, when it has them all, before the body
		// runs, so the body can name it.
		std::vector<std::string> parts;
		bool keyed = false, whole = true;
		for (const auto& f : e.fields) {
			if (!f.key) continue;
			keyed = true;
			std::string part = written(v[f.name]);
			while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
			while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
			if (part.empty()) whole = false;
			parts.push_back(std::move(part));
		}
		std::string id = keyed && whole ? screens::key_of(parts) : made_up_id();
		v.fields["id"] = id;
		if (body) {
			if (auto done = body(v); !done) return std::unexpected(done.error());
		}
		if (auto checked = validate(e, v); !checked) return std::unexpected(checked.error());
		v.fields["created_at"] = now;
		// The same keys are the same entity: made again, it keeps when it was first
		// made and the numbers it was given, unless a key is unique and refuses.
		if (std::optional<value> before = changes.find(e.name, id)) {
			for (const auto& f : e.fields)
				if (f.key && f.unique) return std::unexpected(label(f.name) + " is already taken");
			keep_fixed(e, *before, v);
		}
		v.fields["updated_at"] = now;
		changes.create(e.name, std::move(v));
		return id;
	}

	screens::answer package::run(const command_description& c, const value& sent, step& changes) const {
		const entity_description* e = entity(c.entity);
		if (!e) return std::unexpected("there's no entity " + c.entity);
		std::string now = now_text();
		// What it's sent besides its entity's fields is kept apart, for its body.
		value inputs = value::map(), given = value::map();
		for (const auto& [name, held] : sent.as_map()) (std::ranges::find(c.inputs, name) != c.inputs.end() ? inputs : given).fields[name] = held;
		context ctx(*this, changes, now, inputs);
		auto body = [&](value& v) -> std::expected<void, std::string> { return c.run ? c.run(ctx, v) : std::expected<void, std::string>{}; };

		if (c.does_what == command_description::kind::creates) {
			if (c.allowed) {
				value kept = value::map();
				for (const auto& name : *c.allowed)
					if (const value* held = given.find(name)) kept.fields[name] = *held;
				given = std::move(kept);
			}
			return make(*e, std::move(given), changes, now, body);
		}

		std::string id = given["id"].as_string();
		std::optional<value> before = changes.find(e->name, id);
		if (!before) return std::unexpected(no_such(e->name));
		value v = *before;
		// An update may change the fields it names, or all but its keys; another
		// command only those it names; a delete none.
		std::vector<std::string> may;
		if (c.allowed) may = *c.allowed;
		else if (c.does_what == command_description::kind::updates)
			for (const auto& f : e->fields)
				if (!f.key && !f.serial) may.push_back(f.name);
		if (c.does_what != command_description::kind::deletes)
			for (const auto& name : may)
				if (const value* held = given.find(name)) v.fields[name] = *held;
		if (auto done = body(v); !done) return std::unexpected(done.error());
		if (c.does_what == command_description::kind::deletes) {
			changes.remove(e->name, id);
			return id;
		}
		keep_fixed(*e, *before, v);
		if (auto checked = validate(*e, v); !checked) return std::unexpected(checked.error());
		v.fields["updated_at"] = now;
		changes.update(e->name, id, v);
		return id;
	}

	std::optional<value> context::read(const std::string& entity, const std::string& id) const { return changes_.find(entity, id); }

	std::vector<value> context::each(const query& rows) const { return package_.picked(rows, package::from(changes_), nullptr, ""); }

	std::expected<std::string, std::string> context::create(const std::string& entity, local::value fields) {
		const entity_description* e = package_.entity(entity);
		if (!e) return std::unexpected("there's no entity " + entity);
		return package_.make(*e, std::move(fields), changes_, now_, nullptr);
	}

	std::expected<void, std::string> context::update(const std::string& entity, const std::string& id, const local::value& fields) {
		const entity_description* e = package_.entity(entity);
		if (!e) return std::unexpected("there's no entity " + entity);
		std::optional<local::value> before = changes_.find(entity, id);
		if (!before) return std::unexpected(no_such(entity));
		local::value v = *before;
		for (const auto& [name, held] : fields.as_map()) v.fields[name] = held;
		keep_fixed(*e, *before, v);
		if (auto checked = validate(*e, v); !checked) return std::unexpected(checked.error());
		v.fields["updated_at"] = now_;
		changes_.update(entity, id, v);
		return {};
	}

	std::expected<void, std::string> context::remove(const std::string& entity, const std::string& id) {
		if (!changes_.remove(entity, id)) return std::unexpected(no_such(entity));
		return {};
	}

}  // namespace one::local
