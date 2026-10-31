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
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <unordered_set>

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

	double divided(double x, double y) { return y == 0 ? 0 : x / y; }

	double remainder(double x, double y) { return y == 0 ? 0 : x - y * std::floor(x / y); }

	namespace {
		std::size_t at_most(const value& list, double n) { return static_cast<std::size_t>(std::clamp(n, 0.0, static_cast<double>(list.as_array().size()))); }

		std::function<std::int64_t(std::int64_t)>& draw() {
			static std::function<std::int64_t(std::int64_t)> from_the_system = [](std::int64_t n) {
				static std::random_device source;
				return std::uniform_int_distribution<std::int64_t>(0, n - 1)(source);
			};
			return from_the_system;
		}
	}  // namespace

	value first_of(const value& list, double n) {
		const auto& items = list.as_array();
		return value(std::vector<value>(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(at_most(list, n))));
	}

	value drop_of(const value& list, double n) {
		const auto& items = list.as_array();
		return value(std::vector<value>(items.begin() + static_cast<std::ptrdiff_t>(at_most(list, n)), items.end()));
	}

	value joined(const value& a, const value& b) {
		std::vector<value> both = a.as_array();
		both.insert(both.end(), b.as_array().begin(), b.as_array().end());
		return value(std::move(both));
	}

	double counted(const value& list) { return static_cast<double>(list.as_array().size()); }

	value item_of(const value& list, double n) {
		const auto& items = list.as_array();
		if (n < 1 || n >= static_cast<double>(items.size()) + 1) return value();
		return items[static_cast<std::size_t>(n) - 1];
	}

	bool holds(const value& list, const value& wanted) {
		return std::ranges::any_of(list.as_array(), [&](const value& item) { return same(item, wanted); });
	}

	double random(double low, double high) {
		double from = std::ceil(low), to = std::floor(high);
		if (to <= from) return from;
		return from + static_cast<double>(draw()(static_cast<std::int64_t>(to - from) + 1));
	}

	std::shared_ptr<void> draw_with(std::function<std::int64_t(std::int64_t)> given) {
		auto was = std::make_shared<std::function<std::int64_t(std::int64_t)>>(std::exchange(draw(), std::move(given)));
		return std::shared_ptr<void>(nullptr, [was](void*) { draw() = *was; });
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

	bool same(const value& a, const value& b) {
		if (nothing(a) || nothing(b)) return nothing(a) && nothing(b);
		return alike(a, b);
	}

	bool truthy(const value& v) { return v.is_bool() ? v.boolean : !nothing(v); }

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

	view_description& view_description::tally(std::string tally_name, std::string list, std::vector<row_test> tests) {
		tallies.push_back({std::move(tally_name), std::move(list), "", std::move(tests)});
		return *this;
	}

	view_description& view_description::total(std::string total_name, std::string list, std::string field, std::vector<row_test> tests) {
		tallies.push_back({std::move(total_name), std::move(list), std::move(field), std::move(tests)});
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

	view_description& view_description::value(std::string value_name, query from, std::string field, std::vector<by> order, int many) {
		last(*this, "a value").values.push_back({std::move(value_name), std::move(from), std::move(field), value_of::first, std::move(order), many});
		return *this;
	}

	view_description& view_description::counted(std::string value_name, query from) {
		last(*this, "a count").values.push_back({std::move(value_name), std::move(from), "", value_of::counted});
		return *this;
	}

	view_description& view_description::summed(std::string value_name, query from, std::string field) {
		last(*this, "a sum").values.push_back({std::move(value_name), std::move(from), std::move(field), value_of::summed});
		return *this;
	}

	view_description view(std::string name) { return view_description{.name = std::move(name), .per_kind = {}, .copies = {}, .counts = {}, .lists = {}, .tallies = {}}; }

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

	command_description& command_description::apart() {
		stands_apart = true;
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
		for (const auto& c : commands_) source.on(c.name, [this, &c](const value& sent, step& changes) { return run(c, sent, changes); }, c.stands_apart);
		for (const auto& v : views_)
			source.define(v.name, [this, &v](const store& entities, const std::string& subject) { return build(v, entities, subject); });
	}

	package::reading package::from(const store& entities) {
		return {[&entities](const std::string& kind, const std::function<void(const value&)>& row) {
			        for (const value* each : entities.each(kind)) row(*each);
		        },
		        [&entities](const std::string& kind, const std::string& id) -> std::optional<value> {
			        const value* row = entities.find(kind, id);
			        return row ? std::optional<value>(*row) : std::nullopt;
		        }};
	}

	package::reading package::from(const step& changes) {
		return {[&changes](const std::string& kind, const std::function<void(const value&)>& row) {
			        for (const value& each : changes.each(kind)) row(each);
		        },
		        [&changes](const std::string& kind, const std::string& id) { return changes.find(kind, id); }};
	}

	package::reading package::defaulted(reading r) const {
		// Each entity's fields with a default, found once.
		auto defaults = std::make_shared<std::map<std::string, std::vector<const field*>>>();
		for (const auto& [name, e] : entities_) {
			for (const auto& f : e.fields)
				if (f.initial) (*defaults)[name].push_back(&f);
		}
		if (defaults->empty()) return r;
		// A copy only of a record that lacks one.
		auto filled = [defaults](const std::string& kind, const value& row) -> std::optional<value> {
			auto found = defaults->find(kind);
			if (found == defaults->end()) return std::nullopt;
			std::optional<value> out;
			for (const field* f : found->second) {
				if (!nothing(row[f->name])) continue;
				if (!out) out = row;
				out->fields[f->name] = *f->initial;
			}
			return out;
		};
		// Kept while it's read, as what's read may be held on to, as the first of a list is.
		auto kept = std::make_shared<std::deque<value>>();
		return {[each = r.each, filled, kept](const std::string& kind, const std::function<void(const value&)>& row) {
			        each(kind, [&](const value& v) {
				        if (auto with = filled(kind, v)) row(kept->emplace_back(std::move(*with)));
				        else row(v);
			        });
		        },
		        [find = r.find, filled](const std::string& kind, const std::string& id) -> std::optional<value> {
			        auto v = find(kind, id);
			        if (!v) return v;
			        if (auto with = filled(kind, *v)) return with;
			        return v;
		        }};
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

		// Whether a row passes every condition of one way of a query.
		bool passes(const query& way, const value& candidate, const entity_description* kind, const package::reading& r, const value* subject, const std::string& row) {
			for (const auto& c : way.conditions) {
				value got = field_of(candidate, c.field, kind, r);
				value wanted = wanted_of(c.wanted, subject, row);
				bool holds = false;
				if (c.test == query::condition::holds) {
					for (const auto& item : got.as_array()) holds = holds || alike(item, wanted);
				} else {
					holds = nothing(wanted) ? nothing(got) : alike(got, wanted);
					if (c.test == query::condition::is_not) holds = !holds;
				}
				if (!holds) return false;
			}
			return true;
		}

	}  // namespace

	std::vector<value> package::picked(const query& q, const reading& r, const value* subject, const std::string& row) const {
		std::vector<value> out;
		std::unordered_set<std::string> seen;
		std::vector<const query*> ways{&q};
		for (const auto& other : q.others) ways.push_back(&other);
		for (const query* way : ways) {
			const entity_description* kind = entity(way->entity);
			r.each(way->entity, [&](const value& candidate) {
				if (passes(*way, candidate, kind, r, subject, row) && seen.insert(candidate["id"].as_string()).second) out.push_back(candidate);
			});
		}
		return out;
	}

	// Each row's value of one of a list's values: when its query picks those that name the
	// row by one field, its rows are read once, each kept toward the row it names, so a
	// view of many rows over many more reads them once, not once a row; otherwise
	// picked for each.
	class package::row_values {
	public:
		row_values(const view_description::value_of& of, const package& p, const reading& r, const value* about) : of_(of), kind_(p.entity(of.from.entity)), r_(r) {
			const auto& conditions = of.from.conditions;
			auto names_row = [](const query::condition& c) { return std::holds_alternative<row_marker>(c.wanted); };
			auto naming = std::ranges::find_if(conditions, names_row);
			if (!of.from.others.empty() || naming == conditions.end() || naming->test != query::condition::is || naming->field.find('.') != std::string::npos ||
			    std::ranges::count_if(conditions, names_row) != 1) {
				return;
			}
			query rest{of.from.entity, {}, {}};
			for (const auto& c : conditions) {
				if (&c != &*naming) rest.conditions.push_back(c);
			}
			grouped_ = true;
			r.each(of.from.entity, [&](const value& candidate) {
				const value& named = candidate[naming->field];
				if (named.is_string() && passes(rest, candidate, kind_, r, about, "")) take(by_row_[named.text], candidate);
			});
			for (auto& [_, w] : by_row_) settle(w);
		}

		template <class Picking>
		value of(const std::string& row, Picking pick) const {
			if (!grouped_) {
				so_far all;
				std::vector<value> found = pick();
				for (const value& candidate : found) take(all, candidate);
				settle(all);
				return said(all);
			}
			auto found = by_row_.find(row);
			if (found == by_row_.end()) {
				so_far none;
				settle(none);
				return said(none);
			}
			return said(found->second);
		}

	private:
		// How many so far, what they add up to, and the first in its order, or the first
		// so many, while the rows are read, then the field it gives, or theirs.
		struct so_far {
			long long count = 0;
			double sum = 0;
			const value* best = nullptr;
			std::vector<const value*> bests;
			value first;
		};

		void take(so_far& w, const value& candidate) const {
			++w.count;
			if (of_.kind == view_description::value_of::summed && numeric(candidate[of_.field])) w.sum += candidate[of_.field].as_double();
			if (of_.kind != view_description::value_of::first) return;
			if (of_.many > 0) {
				auto at = std::ranges::upper_bound(w.bests, &candidate, [this](const value* a, const value* b) { return before(*a, *b); });
				if (at - w.bests.begin() < of_.many) {
					w.bests.insert(at, &candidate);
					if (static_cast<int>(w.bests.size()) > of_.many) w.bests.pop_back();
				}
			} else if (!w.best || before(candidate, *w.best)) {
				w.best = &candidate;
			}
		}

		void settle(so_far& w) const {
			if (of_.many > 0) {
				std::vector<value> theirs;
				for (const value* b : w.bests) theirs.push_back((*b)[of_.field]);
				w.first = value(std::move(theirs));
			} else if (w.best) {
				w.first = (*w.best)[of_.field];
			}
			w.best = nullptr;
			w.bests.clear();
		}

		// First by its order's fields, then when it was made, then its id.
		bool before(const value& a, const value& b) const {
			for (const auto& o : of_.order) {
				value x = field_of(a, o.field, kind_, r_), y = field_of(b, o.field, kind_, r_);
				if (o.through) x = o.through(x), y = o.through(y);
				if (int c = compare(x, y); c != 0) return (c < 0) != o.descending;
			}
			if (int c = compare(a["created_at"], b["created_at"]); c != 0) return c < 0;
			return a["id"].as_string() < b["id"].as_string();
		}

		value said(const so_far& w) const {
			if (of_.kind == view_description::value_of::counted) return value(w.count);
			if (of_.kind == view_description::value_of::summed) return value(w.sum);
			return w.first;
		}

		const view_description::value_of& of_;
		const entity_description* kind_;
		const reading& r_;
		bool grouped_ = false;
		std::map<std::string, so_far> by_row_;
	};

	std::optional<value> package::build(const view_description& v, const store& entities, const std::string& subject) const {
		reading r = defaulted(from(entities));
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
			std::map<const view_description::value_of*, row_values> worked;
			for (const auto& val : l.values) worked.emplace(&val, row_values(val, *this, r, about));
			std::vector<value> shown;
			for (const value& row : rows) {
				value out = value::map();
				out.fields["id"] = row["id"];
				for (const auto& name : l.fields) out.fields[name] = field_of(row, name, kind, r);
				for (const auto& val : l.values) out.fields[val.name] = worked.at(&val).of(row["id"].as_string(), [&] { return picked(val.from, r, about, row["id"].as_string()); });
				shown.push_back(std::move(out));
			}
			document.fields[l.name] = value(std::move(shown));
		}
		for (const auto& t : v.tallies) {
			auto passes = [&](const value& row) {
				return std::ranges::all_of(t.tests, [&](const view_description::row_test& test) {
					int c = compare(row[test.field], document[test.other]);
					return test.op == "==" ? c == 0 : test.op == "!=" ? c != 0 : test.op == "<" ? c < 0 : test.op == "<=" ? c <= 0 : test.op == ">" ? c > 0 : c >= 0;
				});
			};
			long long count = 0;
			double sum = 0;
			for (const value& row : document[t.list].as_array()) {
				if (!passes(row)) continue;
				++count;
				if (numeric(row[t.field])) sum += row[t.field].as_double();
			}
			document.fields[t.name] = t.field.empty() ? value(count) : value(sum);
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
