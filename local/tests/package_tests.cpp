// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::local's packages, held to what the Go library does for the cloud, on the
// language's own library example: ids made of keys and counted per book, a unique key
// refused, defaults and choices, a body that reads and changes another entity in the
// same step, a date after another, and views ordered through a function, with a
// value worked out for each row and a page per book.

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <source_location>

#include "check.hpp"
#include "one/local/package.hpp"

namespace {

	using namespace one::local;
	using fields = std::map<std::string, value>;

	struct scratch {
		std::filesystem::path at;
		scratch() {
			static int count = 0;
			at = std::filesystem::temp_directory_path() / ("one-package-" + std::to_string(::getpid()) + "-" + std::to_string(count++));
			std::filesystem::remove_all(at);
		}
		~scratch() { std::filesystem::remove_all(at); }
	};

	value sort_title(const value& title) {
		const std::string& t = title.as_string();
		for (std::string article : {"The ", "An ", "A "})
			if (t.starts_with(article)) return t.substr(article.size());
		return t;
	}

	// examples/library/main.one, as the compiler writes it for storage local.
	package library() {
		auto book = entity("library::book", {
		    {.name = "title", .required = true},
		    {.name = "author", .required = true},
		    {.name = "shelfmark", .required = true, .key = true, .unique = true},
		    {.name = "status", .choices = {"on_shelf", "lent", "withdrawn"}, .initial = "on_shelf"},
		    {.name = "summary"},
		});
		auto loan = entity("library::loan", {
		    {.name = "book", .required = true, .key = true, .refers = "library::book"},
		    {.name = "number", .key = true, .serial = "book"},
		    {.name = "member", .required = true},
		    {.name = "lent_at", .now = true},
		    {.name = "due_at", .required = true, .after = "lent_at"},
		    {.name = "returned_at"},
		});
		auto book_create = command("library::book::create").creates("library::book");
		auto update = command("library::book::update").updates("library::book").fields({"title", "author", "summary"});
		auto withdraw = command("library::book::withdraw").on("library::book").does([](context& c, value& b) {
			if (b["status"].as_string() != "on_shelf") return c.fail("only a book on the shelf can be withdrawn");
			b.fields["status"] = "withdrawn";
			return c.done();
		});
		auto loan_create = command("library::loan::create").creates("library::loan").does([](context& c, value& l) -> std::expected<void, std::string> {
			auto b = c.read("library::book", l["book"].as_string());
			if (!b || (*b)["status"].as_string() != "on_shelf") return c.fail("that book is not on the shelf");
			return c.update("library::book", l["book"].as_string(), value(fields{{"status", "lent"}}));
		});
		auto checkin = command("library::loan::checkin").on("library::loan").does([](context& c, value& l) -> std::expected<void, std::string> {
			if (!nothing(l["returned_at"])) return c.fail("that book is already back");
			l.fields["returned_at"] = c.now();
			return c.update("library::book", l["book"].as_string(), value(fields{{"status", "on_shelf"}}));
		});
		auto shelf = view("library::shelf")
		                 .each(all("library::book").except("status", "withdrawn"))
		                 .order(by("title", sort_title))
		                 .fields({"shelfmark", "title", "author", "status"})
		                 .value("lent_to", first("library::loan").where("book", row).where("returned_at", none), "member");
		// For each book: how many times it's been lent, how many are out, their numbers
		// added up, and who had it last and first.
		auto lending = view("library::lending")
		                   .each(all("library::book"))
		                   .order("title")
		                   .fields({"title"})
		                   .counted("times_lent", where("library::loan", "book", row))
		                   .counted("out", where("library::loan", "book", row).where("returned_at", none))
		                   .summed("numbers", where("library::loan", "book", row), "number")
		                   .value("last_member", where("library::loan", "book", row), "member", {by("-number")})
		                   .value("first_member", where("library::loan", "book", row), "member");
		auto desk = view("library::desk").each(where("library::loan", "returned_at", none)).order("due_at").fields({"book.title", "member", "due_at"});
		auto book_page = view("library::book_page")
		                     .per("library::book")
		                     .copy("title", "title")
		                     .copy("author", "author")
		                     .count("times_lent", where("library::loan", "book", subject))
		                     .tally("listed", "loans")
		                     .tally("latest", "loans", {{"number", ">=", "times_lent"}})
		                     .total("numbers", "loans", "number")
		                     .list("loans", where("library::loan", "book", subject))
		                     .order("-number")
		                     .fields({"number", "member", "returned_at"});
		// A summary written again by itself, kept, but never a step undo takes back.
		auto summarize = command("library::book::summarize").on("library::book").fields({"summary"}).apart();
		return package("library", {book, loan}, {book_create, update, withdraw, loan_create, checkin, summarize}, {shelf, lending, desk, book_page});
	}

	// A source on a folder of its own, the library installed, and every command's answer.
	struct app {
		scratch s;
		package m = library();
		std::unique_ptr<local_source> source;
		app() {
			source = std::move(*local_source::open(*folder::open_at(s.at)));
			m.install(*source);
		}
		one::screens::answer run(const std::string& command, value sent) {
			one::screens::answer got = std::unexpected(std::string("never answered"));
			source->run(command, sent, [&](const one::screens::answer& a) { got = a; });
			return got;
		}
		// A command a test relies on, which has to be done.
		void ok(const std::string& command, value sent, std::source_location at = std::source_location::current()) {
			auto got = run(command, std::move(sent));
			check::verify(got.has_value(), "it was done", command + (got ? "" : ": " + got.error()), at);
		}
		value view(const std::string& name, const std::string& subject = "") {
			value shown;
			auto listening = source->listen(name, subject, [&](const one::screens::view_state& v) { shown = v.status == one::screens::status::ready ? v.data : value(); });
			return shown;
		}
	};

	value book(std::string title, std::string shelfmark) {
		return value(fields{{"title", std::move(title)}, {"author", "Someone"}, {"shelfmark", std::move(shelfmark)}});
	}

}  // namespace

TEST("a book is made with its defaults, named by its key, and its key refused twice") {
	app a;
	auto made = a.run("library::book::create", book("The Hobbit", "HIS-0142"));
	CHECK_SAYING(made.has_value(), made ? "" : made.error());
	CHECK(*made == "HIS-0142");
	auto kept = a.source->entities().find("library::book", "HIS-0142");
	CHECK(kept && (*kept)["status"].as_string() == "on_shelf");
	CHECK(kept && !(*kept)["created_at"].as_string().empty());
	auto again = a.run("library::book::create", book("Another", "HIS-0142"));
	CHECK(!again.has_value() && again.error() == "Shelfmark is already taken");
	auto missing = a.run("library::book::create", value(fields{{"title", "No author"}, {"shelfmark", "X-1"}}));
	CHECK(!missing.has_value() && missing.error() == "Author is required");
	auto wrong = a.run("library::book::create", value(fields{{"title", "T"}, {"author", "A"}, {"shelfmark", "Y-1"}, {"status", "lost"}}));
	CHECK(!wrong.has_value() && wrong.error() == "Status has to be one of on_shelf, lent, withdrawn");
}

TEST("loans are counted per book, and lending takes the book off the shelf in the same step") {
	app a;
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	a.ok("library::book::create", book("Emma", "LIT-0002"));
	auto first = a.run("library::loan::create", value(fields{{"book", "SCI-0001"}, {"member", "maya"}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
	CHECK_SAYING(first.has_value(), first ? "" : first.error());
	CHECK(*first == "SCI-0001-1");
	CHECK((*a.source->entities().find("library::book", "SCI-0001"))["status"].as_string() == "lent");
	auto refused = a.run("library::loan::create", value(fields{{"book", "SCI-0001"}, {"member", "omar"}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
	CHECK(!refused.has_value() && refused.error() == "that book is not on the shelf");
	CHECK(a.run("library::loan::checkin", value(fields{{"id", "SCI-0001-1"}})).has_value());
	auto back = a.run("library::loan::checkin", value(fields{{"id", "SCI-0001-1"}}));
	CHECK(!back.has_value() && back.error() == "that book is already back");
	auto second = a.run("library::loan::create", value(fields{{"book", "SCI-0001"}, {"member", "omar"}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
	CHECK(second.has_value() && *second == "SCI-0001-2");
	auto other = a.run("library::loan::create", value(fields{{"book", "LIT-0002"}, {"member", "omar"}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
	CHECK(other.has_value() && *other == "LIT-0002-1");
	auto early = a.run("library::loan::create", value(fields{{"book", "LIT-0002"}, {"member", "omar"}, {"due_at", "2000-01-01T00:00:00.000000000Z"}}));
	CHECK(!early.has_value());
	auto gone = a.run("library::loan::checkin", value(fields{{"id", "nothing-9"}}));
	CHECK(!gone.has_value() && gone.error() == "there's no such loan");
}

TEST("an update changes only what its forms ask for, and a body's refusal keeps nothing") {
	app a;
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	CHECK(a.run("library::book::update", value(fields{{"id", "SCI-0001"}, {"title", "Dune Messiah"}, {"status", "lent"}, {"shelfmark", "Z-9"}})).has_value());
	auto kept = *a.source->entities().find("library::book", "SCI-0001");
	CHECK(kept["title"].as_string() == "Dune Messiah");
	CHECK(kept["status"].as_string() == "on_shelf");
	CHECK(kept["shelfmark"].as_string() == "SCI-0001");
	CHECK(a.run("library::book::withdraw", value(fields{{"id", "SCI-0001"}})).has_value());
	auto twice = a.run("library::book::withdraw", value(fields{{"id", "SCI-0001"}}));
	CHECK(!twice.has_value() && twice.error() == "only a book on the shelf can be withdrawn");
}

TEST("the shelf leaves out what's withdrawn, orders by title as it's said, and says who has each") {
	app a;
	a.ok("library::book::create", book("The Hobbit", "FAN-0001"));
	a.ok("library::book::create", book("Emma", "LIT-0002"));
	a.ok("library::book::create", book("A Wrinkle in Time", "SCI-0003"));
	a.ok("library::book::create", book("Gone", "OUT-0004"));
	a.ok("library::book::withdraw", value(fields{{"id", "OUT-0004"}}));
	a.ok("library::loan::create", value(fields{{"book", "LIT-0002"}, {"member", "maya"}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
	value shelf = a.view("library::shelf");
	const auto& rows = shelf["rows"].as_array();
	CHECK(rows.size() == 3);
	CHECK(rows[0]["title"].as_string() == "Emma");
	CHECK(rows[1]["title"].as_string() == "The Hobbit");
	CHECK(rows[2]["title"].as_string() == "A Wrinkle in Time");
	CHECK(rows[0]["id"].as_string() == "LIT-0002");
	CHECK(rows[0]["lent_to"].as_string() == "maya");
	CHECK(rows[1]["lent_to"].is_null());
	CHECK(rows[0].find("summary") == nullptr);  // a row holds what its view lists, and its id
	value desk = a.view("library::desk");
	CHECK(desk["rows"].as_array().size() == 1);
	CHECK(desk["rows"].as_array()[0]["book.title"].as_string() == "Emma");
}

TEST("a page per book copies the book, counts and lists its loans, counts and adds up the loans it lists, and is missing for no book") {
	app a;
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	for (int i = 0; i < 3; ++i) {
		a.ok("library::loan::create", value(fields{{"book", "SCI-0001"}, {"member", "maya"}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
		a.ok("library::loan::checkin", value(fields{{"id", "SCI-0001-" + std::to_string(i + 1)}}));
	}
	value page = a.view("library::book_page", "SCI-0001");
	CHECK(page["title"].as_string() == "Dune");
	CHECK(page["times_lent"].as_integer() == 3);
	// Of those it lists: all of them, those numbered from how many there are, and their numbers added up.
	CHECK(page["listed"].as_integer() == 3);
	CHECK(page["latest"].as_integer() == 1);
	CHECK(page["numbers"].as_double() == 6);
	const auto& loans = page["loans"].as_array();
	CHECK(loans.size() == 3);
	CHECK(loans[0]["number"].as_integer() == 3);
	CHECK(loans[2]["number"].as_integer() == 1);
	CHECK(loans[0]["returned_at"].is_time());  // a time, handed to screens as one
	CHECK(a.view("library::book_page", "NOT-0000").is_null());
}

TEST("a view says for each row how many of another name it, what a number of theirs adds up to, and a field of the first in an order, kept as they come and go") {
	app a;
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	a.ok("library::book::create", book("Emma", "LIT-0002"));
	for (std::string member : {"maya", "omar", "lena"}) {
		a.ok("library::loan::create", value(fields{{"book", "SCI-0001"}, {"member", member}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
		if (member != "lena") a.ok("library::loan::checkin", value(fields{{"id", "SCI-0001-" + std::to_string(member == "maya" ? 1 : 2)}}));
	}
	std::vector<value> seen;
	auto listening = a.source->listen("library::lending", "", [&](const one::screens::view_state& v) { seen.push_back(v.data); });
	const auto& rows = seen.back()["rows"].as_array();
	CHECK(rows.size() == 2);
	CHECK(rows[0]["title"].as_string() == "Dune");
	CHECK(rows[0]["times_lent"].as_integer() == 3);
	CHECK(rows[0]["out"].as_integer() == 1);
	CHECK(rows[0]["numbers"].as_double() == 6);
	CHECK(rows[0]["last_member"].as_string() == "lena");
	CHECK(rows[0]["first_member"].as_string() == "maya");
	CHECK(rows[1]["times_lent"].as_integer() == 0);
	CHECK(rows[1]["numbers"].as_double() == 0);
	CHECK(rows[1]["last_member"].is_null());
	// Lent again, it's told at once.
	a.ok("library::loan::create", value(fields{{"book", "LIT-0002"}, {"member", "maya"}, {"due_at", "2999-01-01T00:00:00.000000000Z"}}));
	CHECK(seen.back()["rows"].as_array()[1]["times_lent"].as_integer() == 1);
	CHECK(seen.back()["rows"].as_array()[1]["last_member"].as_string() == "maya");
}

TEST("a value for each row reads the rows it counts once, not once a row, so a view of many over many more is quick") {
	store kept;
	std::vector<change> made;
	for (int b = 0; b < 50; ++b) made.push_back({.entity = "x::shelf", .id = "s" + std::to_string(b), .fields = value(fields{{"name", "s" + std::to_string(b)}})});
	for (int i = 0; i < 20000; ++i) {
		made.push_back({.entity = "x::clipping", .id = "c" + std::to_string(i),
		                .fields = value(fields{{"shelf", "s" + std::to_string(i % 50)}, {"bytes", i}})});
	}
	kept.apply({.command = "", .at = "", .changes = std::move(made)});
	package m("x", {entity("x::shelf", {{.name = "name"}}), entity("x::clipping", {{.name = "shelf", .refers = "x::shelf"}, {.name = "bytes"}})}, {}, {});
	auto v = view("x::shelves")
	             .each(all("x::shelf"))
	             .fields({"name"})
	             .counted("total", where("x::clipping", "shelf", row))
	             .summed("size", where("x::clipping", "shelf", row), "bytes")
	             .value("newest", where("x::clipping", "shelf", row), "bytes", {by("-bytes")});
	auto started = std::chrono::steady_clock::now();
	auto shown = m.build(v, kept, "");
	auto took = std::chrono::steady_clock::now() - started;
	CHECK(shown.has_value());
	if (!shown) return;
	const auto& rows = (*shown)["rows"].as_array();
	CHECK(rows.size() == 50);
	long long all = 0;
	for (const auto& r : rows) all += r["total"].as_integer();
	CHECK(all == 20000);
	for (const auto& r : rows) {
		if (r["name"].as_string() != "s7") continue;
		CHECK(r["total"].as_integer() == 400);
		CHECK(r["newest"].as_integer() == 19957);
		CHECK(r["size"].as_double() == 400.0 * 7 + 50.0 * 399 * 400 / 2);
	}
	CHECK_SAYING(took < std::chrono::seconds(2), std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(took).count()) + " ms");
}

TEST("views are told as commands change them, through the source the screens read") {
	app a;
	std::vector<std::size_t> sizes;
	auto listening = a.source->listen("library::shelf", "", [&](const one::screens::view_state& v) { sizes.push_back(v.data["rows"].as_array().size()); });
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	a.ok("library::book::create", book("Emma", "LIT-0002"));
	a.ok("library::book::withdraw", value(fields{{"id", "SCI-0001"}}));
	CHECK((sizes == std::vector<std::size_t>{0, 1, 2, 1}));
}

TEST("a query takes either way, and a list held") {
	store kept;
	kept.apply({.command = "", .at = "", .changes = {
	    {.entity = "x::card", .id = "a", .fields = value(fields{{"color", "red"}, {"tags", value(std::vector<value>{"one", "two"})}})},
	    {.entity = "x::card", .id = "b", .fields = value(fields{{"color", "blue"}, {"tags", value(std::vector<value>{})}})},
	    {.entity = "x::card", .id = "c", .fields = value(fields{{"color", "green"}})},
	}});
	package m("x", {entity("x::card", {{.name = "color"}, {.name = "tags", .list = true}})}, {}, {});
	auto r = package::from(kept);
	CHECK(m.picked(where("x::card", "color", "red").either(where("x::card", "color", "blue")), r, nullptr, "").size() == 2);
	CHECK(m.picked(all("x::card").has("tags", "two"), r, nullptr, "").size() == 1);
	CHECK(m.picked(where("x::card", "tags", none), r, nullptr, "").size() == 2);
	CHECK(m.picked(all("x::card").except("color", "red"), r, nullptr, "").size() == 2);
}

TEST("a number in a range is checked as its line says") {
	scratch s;
	auto source = std::move(*local_source::open(*folder::open_at(s.at)));
	package m("x", {entity("x::dial", {{.name = "volume", .range = field::numbers{0, 100, 5}}})}, {command("x::dial::create").creates("x::dial")}, {});
	m.install(*source);
	one::screens::answer got;
	source->run("x::dial::create", value(fields{{"volume", 120}}), [&](const one::screens::answer& a) { got = a; });
	CHECK(!got.has_value() && got.error() == "Volume is from 0 to 100");
	source->run("x::dial::create", value(fields{{"volume", 7}}), [&](const one::screens::answer& a) { got = a; });
	CHECK(!got.has_value() && got.error() == "Volume goes in steps of 5 from 0");
	source->run("x::dial::create", value(fields{{"volume", 35}}), [&](const one::screens::answer& a) { got = a; });
	CHECK(got.has_value());
}

TEST("undo takes back each step, redo takes it again, and both are kept, so the folder opened again agrees") {
	app a;
	auto title = [&] { return a.source->entities().find("library::book", "SCI-0001") ? (*a.source->entities().find("library::book", "SCI-0001"))["title"].as_string() : "gone"; };
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	a.ok("library::book::update", value(fields{{"id", "SCI-0001"}, {"title", "Dune Messiah"}}));
	CHECK(title() == "Dune Messiah");
	// Its own view says what undo and redo would take back, so their buttons can be off.
	value history;
	auto listening = a.source->listen("one::history", "", [&](const one::screens::view_state& v) { history = v.data; });
	CHECK(history["undo"].as_string() == "library::book::update");
	CHECK(history["redo"].as_string() == "");
	CHECK(a.run("one::undo", value::map()) == one::screens::answer("library::book::update"));
	CHECK(title() == "Dune");
	CHECK(history["redo"].as_string() == "library::book::update");
	CHECK(a.run("one::undo", value::map()) == one::screens::answer("library::book::create"));
	CHECK(title() == "gone");
	CHECK(history["undo"].as_string() == "");
	CHECK(!a.run("one::undo", value::map()));  // nothing left to take back
	a.ok("one::redo", value::map());
	a.ok("one::redo", value::map());
	CHECK(title() == "Dune Messiah");
	CHECK(!a.run("one::redo", value::map()));
	a.ok("one::undo", value::map());
	a.source.reset();  // which lets go of the folder
	auto reopened = local_source::open(*folder::open_at(a.s.at));
	CHECK(reopened && (*(*reopened)->entities().find("library::book", "SCI-0001"))["title"].as_string() == "Dune");
}

TEST("a step joined to the one before is undone with it, one apart is never undone, and what it changed stays") {
	app a;
	auto field = [&](const char* id, const char* name) { return (*a.source->entities().find("library::book", id))[name].as_string(); };
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	a.ok("library::book::create", book("Emma", "LIT-0002"));
	a.ok("library::book::update", value(fields{{"id", "SCI-0001"}, {"title", "Dune Messiah"}}));
	a.ok("library::book::update", value(fields{{"id", "LIT-0002"}, {"title", "Persuasion"}, {"one:history", "joined"}}));
	a.ok("library::book::update", value(fields{{"id", "SCI-0001"}, {"summary", "Spice"}, {"one:history", "apart"}}));
	CHECK(a.run("one::undo", value::map()) == one::screens::answer("library::book::update"));
	CHECK(field("SCI-0001", "title") == "Dune");
	CHECK(field("LIT-0002", "title") == "Emma");
	CHECK(field("SCI-0001", "summary") == "Spice");  // changed apart, so it stays
	CHECK(a.run("one::undo", value::map()) == one::screens::answer("library::book::create"));
	CHECK(!a.source->entities().find("library::book", "LIT-0002"));
}

TEST("a command apart from undo is kept, but never a step undo takes back") {
	app a;
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	a.ok("library::book::summarize", value(fields{{"id", "SCI-0001"}, {"summary", "Spice"}}));
	CHECK(a.run("one::undo", value::map()) == one::screens::answer("library::book::create"));
	CHECK(!a.source->entities().find("library::book", "SCI-0001"));
}

TEST("a step taken after an undo means there's nothing to redo") {
	app a;
	a.ok("library::book::create", book("Dune", "SCI-0001"));
	a.ok("one::undo", value::map());
	a.ok("library::book::create", book("Emma", "LIT-0002"));
	CHECK(!a.run("one::redo", value::map()));
}

TEST("a value divided, or what's left over, is worked out as on every target: as a clock counts, and either by 0 is 0") {
	CHECK(one::local::divided(6, 4) == 1.5);
	CHECK(one::local::divided(3, 0) == 0);
	CHECK(one::local::remainder(-1, 4) == 3);
	CHECK(one::local::remainder(5, 4) == 1);
	CHECK(one::local::remainder(3, 0) == 0);
}
