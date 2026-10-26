// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What the compiler writes for a project that says storage local: its entities,
// commands and views described in a few lines each, as the Go library one has them
// for the cloud, and run on what local.hpp keeps. A view and a command's defaults are
// data this library works through; only a command's body and a function are code.
//
//     auto book = entity("library::book", {
//         {.name = "title", .required = true},
//         {.name = "shelfmark", .required = true, .key = true, .unique = true},
//         {.name = "status", .choices = {"on_shelf", "lent", "withdrawn"}, .initial = "on_shelf"},
//     });
//     auto withdraw = command("library::book::withdraw").on("library::book").does([](context& c, value& b) {
//         if (b["status"].as_string() != "on_shelf") return c.fail("only a book on the shelf can be withdrawn");
//         b.fields["status"] = "withdrawn";
//         return c.done();
//     });
//     auto shelf = view("library::shelf")
//         .each(all("library::book").except("status", "withdrawn"))
//         .order(by("title", sort_title))
//         .fields({"shelfmark", "title", "author", "status"})
//         .value("lent_to", first("library::loan").where("book", row).where("returned_at", none), "member");
//
//     package("library", {book, loan}, {book_create, withdraw}, {shelf}).install(source);
//
// What it can't do yet is everything about people, who don't sign in to a local app
// (decision 0015): no me, no views per person, no roles or readers.

#pragma once

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "one/local/local.hpp"

namespace one::local {

	// What a condition compares a field with, besides a value: the entity a view per
	// entity is for, a field of it, the row a value is worked out for, or nothing.
	struct subject_marker {};
	struct row_marker {};
	struct none_marker {};
	struct subject_field {
		std::string field;
	};
	inline constexpr subject_marker subject;
	inline constexpr row_marker row;
	inline constexpr none_marker none;
	using compared = std::variant<value, subject_marker, subject_field, row_marker, none_marker>;

	// Whether a stored value holds nothing: it's missing or null, or empty text or an
	// empty list, as a field reads before it's given a value.
	bool nothing(const value& v);
	// Orders two values of the same kind: false before true, numbers and text in
	// their usual order, times as RFC 3339 text. Nothing comes first.
	int compare(const value& a, const value& b);
	// x / y and x % y in a command's body, as every target works them out: what's left
	// over as a clock counts, never below none, so -1 % 4 is 3, and either by 0 is 0.
	double divided(double x, double y);
	double remainder(double x, double y);
	// A field's name as a person reads it: due_at is "Due at".
	std::string label(const std::string& name);
	// == in a command's body: the same value, or both nothing when either is none.
	bool same(const value& a, const value& b);
	// A value as a condition: true, or anything but nothing and false.
	bool truthy(const value& v);

	// One field of an entity, as its .one line says it.
	struct field {
		std::string name;
		bool required = false;
		bool key = false;     // part of the id, in order (decision 0009)
		bool unique = false;  // a key that's taken is refused rather than made again
		std::vector<std::string> choices = {};
		std::optional<value> initial = {};  // = value
		bool now = false;                   // = now
		std::optional<std::string> serial = {};  // serial, counted within this field, or "" for all
		std::string refers = {};                 // what it points at, like library::book
		std::string after = {};                  // a field it has to come after
		bool list = false;
		struct numbers {
			double from = 0, to = 0, by = 1;
		};
		std::optional<numbers> range = {};  // from 0 to 100 by 5
	};

	struct entity_description {
		std::string name;
		std::vector<local::field> fields;
		const local::field* find(const std::string& field) const;
	};
	entity_description entity(std::string name, std::vector<local::field> fields);

	// Picks the rows of an entity a view, a value or a body reads: every condition
	// of one way, or of another given with either.
	struct query {
		struct condition {
			std::string field;  // read through what it points at with a dot: board.name
			compared wanted;
			enum { is, is_not, holds } test = is;
		};
		std::string entity;
		std::vector<condition> conditions;
		std::vector<query> others;

		query where(std::string field, compared wanted) const;   // field holds wanted
		query except(std::string field, compared wanted) const;  // field doesn't
		query has(std::string field, compared wanted) const;     // its list holds wanted
		query either(query other) const;                         // or picked this other way
	};
	query all(std::string entity);
	query where(std::string entity, std::string field, compared wanted);
	query first(std::string entity);  // all, read where only the earliest made is wanted

	// What a list is ordered by: a field, -field for the other way round, or a field
	// passed through a function, like a title without its "The".
	struct by {
		std::string field;
		std::function<value(const value&)> through;
		bool descending = false;
		by(std::string name);
		by(const char* name) : by(std::string(name)) {}
		by(std::string name, std::function<value(const value&)> function, bool reverse = false)
		    : field(std::move(name)), through(std::move(function)), descending(reverse) {}
	};

	struct view_description {
		struct value_of {
			std::string name;
			query from;
			std::string field;
		};
		struct listing {
			std::string name;  // rows, for each
			query rows;
			std::vector<by> order;
			int limit = 0;
			std::vector<std::string> fields;
			std::vector<value_of> values;
		};
		std::string name;
		std::string per_kind;  // the entity it has a document per, if any
		std::vector<std::pair<std::string, std::string>> copies;
		std::vector<std::pair<std::string, query>> counts;
		std::vector<listing> lists;

		view_description& each(query rows) { return this->list("rows", std::move(rows)); }
		view_description& list(std::string name, query rows);
		view_description& per(std::string entity);
		view_description& copy(std::string name, std::string field);
		view_description& count(std::string name, query rows);
		view_description& order(std::vector<by> keys);
		view_description& order(by key) { return order(std::vector<by>{std::move(key)}); }
		view_description& limit(int rows);
		view_description& fields(std::vector<std::string> names);
		view_description& value(std::string name, query from, std::string field);
	};
	view_description view(std::string name);

	class package;

	// What a command's body sees and does besides its own entity: when it runs, the
	// rows it reads, what it was sent besides its entity's fields, and what else it
	// changes in the same step, all kept with it or none of it.
	class context {
	public:
		const std::string& now() const { return now_; }
		std::optional<local::value> read(const std::string& entity, const std::string& id) const;
		// What a field pointing at an entity points at, or nothing: book.status reads
		// c.at("library::book", e["book"])["status"].
		local::value at(const std::string& entity, const local::value& id) const {
			auto found = read(entity, id.as_string());
			return found ? *found : local::value();
		}
		std::vector<local::value> each(const query& rows) const;
		std::expected<std::string, std::string> create(const std::string& entity, local::value fields);
		std::expected<void, std::string> update(const std::string& entity, const std::string& id, const local::value& fields);
		std::expected<void, std::string> remove(const std::string& entity, const std::string& id);
		const local::value& input(const std::string& name) const { return inputs_[name]; }
		std::expected<void, std::string> fail(std::string message) const { return std::unexpected(std::move(message)); }
		std::expected<void, std::string> done() const { return {}; }

	private:
		friend class package;
		context(const package& m, step& changes, std::string now, local::value inputs)
		    : package_(m), changes_(changes), now_(std::move(now)), inputs_(std::move(inputs)) {}
		const package& package_;
		step& changes_;
		std::string now_;
		local::value inputs_;
	};
	using body = std::function<std::expected<void, std::string>(context& c, local::value& entity)>;

	struct command_description {
		enum class kind { creates, updates, deletes, acts };
		std::string name;
		std::string entity;
		command_description::kind does_what = kind::acts;
		std::optional<std::vector<std::string>> allowed;  // the fields it may be sent
		std::vector<std::string> inputs;                  // what it's sent besides them
		local::body run;
		bool stands_apart = false;                        // apart from undo: never a step undo takes back

		command_description& creates(std::string on);  // x::create: makes one from its fields
		command_description& updates(std::string on);  // x::update: changes the fields given
		command_description& deletes(std::string on);  // x::delete
		command_description& on(std::string entity);   // any other: acts on the one its id names
		command_description& fields(std::vector<std::string> names);
		command_description& takes(std::vector<std::string> names);  // input into stage
		command_description& does(local::body body);
		command_description& apart();  // apart from undo
	};
	command_description command(std::string name);

	// A namespace's entities, commands and views, given to a source.
	class package {
	public:
		package(std::string name, std::vector<entity_description> entities, std::vector<command_description> commands,
		       std::vector<view_description> views);
		// Gives the source each command and view. The package has to outlive it.
		void install(local_source& source) const;

		const entity_description* entity(const std::string& name) const;
		// A view's document for its subject, or none when what it's per is gone.
		std::optional<value> build(const view_description& v, const store& entities, const std::string& subject) const;
		screens::answer run(const command_description& c, const value& sent, step& changes) const;

		// What a query reads from: the store, for a view, or a command's step, which
		// sees its own changes.
		struct reading {
			std::function<std::vector<value>(const std::string& entity)> each;
			std::function<std::optional<value>(const std::string& entity, const std::string& id)> find;
		};
		static reading from(const store& entities);
		static reading from(const step& changes);
		// A query's rows, for the subject a view is per and the row a value is for.
		std::vector<value> picked(const query& q, const reading& r, const value* subject, const std::string& row) const;
		// An entity made as a create command makes it: defaults, serials, id, checks.
		std::expected<std::string, std::string> make(const entity_description& e, value fields, step& changes, const std::string& now,
		                                             const std::function<std::expected<void, std::string>(value&)>& body) const;

	private:
		std::string name_;
		std::map<std::string, entity_description> entities_;
		std::vector<command_description> commands_;
		std::vector<view_description> views_;
	};

}  // namespace one::local
