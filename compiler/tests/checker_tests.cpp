// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one::language;

namespace {

    // Parses each file, which has to succeed, then checks them together as one project.
    diagnostics check_files(const std::vector<std::pair<std::string, std::string>>& sources) {
        diagnostics parse_errors;
        std::vector<file> files;
        for (const auto& [path, source] : sources) files.push_back(parse(path, source, parse_errors));
        for (const auto& d : parse_errors) FAIL_CHECK(format(d));
        diagnostics out;
        check(files, out);
        return out;
    }

    diagnostics check_source(const std::string& source) {
        return check_files({{"test.one", source}});
    }

    // The one error a source should produce.
    diagnostic only_error(const std::string& source) {
        auto out = check_source(source);
        if (out.size() != 1) {
            for (const auto& d : out) MESSAGE(format(d));
        }
        REQUIRE(out.size() == 1);
        return out[0];
    }

    const std::string library = R"(
namespace library {

format shelfmark AAA-9999

entity book {
	title      text       required
	shelfmark  shelfmark  unique
	status     enum on_shelf | lent = status::on_shelf
}

entity loan {
	book       book  required
	member     user
	lent_at    date  = now
	due_at     date  after lent_at
}

command book::create
command book::update
command loan::checkin {
	require book.status == status::lent  "that book is on the shelf"
	dispatch book::update { id = book  status = status::on_shelf }
	clear due_at
}

view shelf {
	each book where status != status::lent {
		order by title
		title  shelfmark
	}
}

role librarian  book::view  loan::checkin

screen "Shelf" /shelf {
	table shelf
	book::create
	form book::create {
		title  shelfmark
	}
	confirm loan::checkin "Check it in?"
}

picker book from shelf

} // namespace library
)";

} // namespace

TEST_CASE("a project that makes sense has no errors") {
    CHECK(check_source(library).empty());
}

TEST_CASE("names that aren't snake_case, with the fix") {
    auto e = only_error("entity book {\n\tdueAt date\n}\n");
    CHECK(e.message == "'dueAt' isn't snake_case; write it as due_at");
    CHECK(e.where.line == 2);
    CHECK(e.where.column == 2);

    CHECK(only_error("entity book {\n\tstatus enum OnShelf | lent = status::lent\n}\n").message ==
          "'OnShelf' isn't snake_case; write it as on_shelf");
    CHECK(only_error("function sortTitle(title) {\n\treturn title\n}\n").message ==
          "'sortTitle' isn't snake_case; write it as sort_title");
}

TEST_CASE("names inside expressions are snake_case too") {
    auto e = only_error("entity book {\n\tdue date\n}\nview v {\n\teach book where dueAt == none\n}\n");
    CHECK(e.message == "'dueAt' isn't snake_case; write it as due_at");
    CHECK(e.where.line == 5);
}

TEST_CASE("a command's entity has to exist") {
    auto e = only_error("namespace library {\ncommand boook::create\n}\n");
    CHECK(e.message == "command boook::create is on entity boook, which isn't declared in namespace library");
    CHECK(e.where.line == 2);
}

TEST_CASE("a command is named entity::command") {
    CHECK(only_error("command create\n").message == "a command is named after its entity, like book::create");
}

TEST_CASE("a field's type has to exist") {
    auto e = only_error("entity book {\n\tshelf shelfmark\n}\n");
    CHECK(e.message.starts_with("'shelfmark' isn't a type"));
}

TEST_CASE("after names a field of the same entity") {
    CHECK(only_error("entity loan {\n\tdue_at date after lent_on\n}\n").message ==
          "'after lent_on' names a field entity loan doesn't have");
}

TEST_CASE("a field with choices starts on one of them") {
    CHECK(only_error("entity book {\n\tstatus enum on_shelf | lent = status::lost\n}\n").message ==
          "lost isn't one of status's choices, status::on_shelf, status::lent; did you mean lent?");
}

TEST_CASE("each field is declared once") {
    CHECK(only_error("entity book {\n\ttitle text\n\ttitle text\n}\n").message ==
          "entity book has two fields called title");
}

TEST_CASE("clear names a field of the command's entity") {
    CHECK(only_error("entity loan {\n\tdue_at date\n}\ncommand loan::checkin {\n\tclear returned_at\n}\n").message ==
          "'clear returned_at' names a field entity loan doesn't have");
}

TEST_CASE("a view's each and per name entities") {
    CHECK(only_error("view v {\n\teach boook\n}\n").message == "'each boook' needs an entity called boook");
    CHECK(only_error("view v per member {\n\ttotal = 1\n}\n").message ==
          "view v is per member, which is neither user nor an entity at the top level");
    CHECK(check_source("view v per user {\n\ttotal = 1\n}\n").empty());
}

TEST_CASE("a table shows a view that exists") {
    CHECK(only_error("screen \"S\" / {\n\ttable shelf\n}\n").message ==
          "there's no view shelf for this table at the top level");
}

TEST_CASE("forms, confirms and buttons name commands that exist") {
    std::string entity = "entity book {\n\ttitle text\n}\ncommand book::create\n";
    CHECK(only_error(entity + "screen \"S\" / {\n\tbook::delete\n}\n").message.starts_with("there's no command book::delete"));
    CHECK(only_error(entity + "screen \"S\" / {\n\tconfirm book::delete \"Sure?\"\n}\n").message.starts_with("there's no command book::delete"));
    CHECK(only_error(entity + "screen \"S\" / {\n\tform book::create {\n\t\ttitle  author\n\t}\n}\n").message ==
          "the form asks for author, which isn't a field of entity book");
}

TEST_CASE("permissions") {
    std::string entity = "entity task {\n\ttitle text\n}\n";
    CHECK(only_error(entity + "command task::create {\n\tpermission everyone\n}\n").message.starts_with("'everyone' isn't a permission"));
    CHECK(only_error(entity + "command task::create {\n\tby owner\n}\n").message ==
          "'owner' needs entity task to have a field 'owner user'");
    CHECK(check_source("entity task {\n\towner user = me\n}\ncommand task::delete {\n\tby owner\n}\n").empty());
    CHECK(only_error("role editor  post::edit\n").message ==
          "permission post::edit is on entity post, which isn't declared at the top level");
}

TEST_CASE("a picker names an entity and a view") {
    auto out = check_source("picker book from shelf\n");
    REQUIRE(out.size() == 2);
    CHECK(out[0].message == "picker book needs an entity called book");
    CHECK(out[1].message == "picker book picks from view shelf, which isn't declared at the top level");
}

TEST_CASE("nothing is declared twice") {
    auto e = only_error("entity book {\n\ttitle text\n}\nentity book {\n\tauthor text\n}\n");
    CHECK(e.message == "entity book is declared twice at the top level; the first is at test.one:1");
    CHECK(e.where.line == 4);

    CHECK(only_error("entity book {\n\ttitle text\n}\ncommand book::create\ncommand book::create\n").message.starts_with(
        "command book::create is declared twice"));
}

TEST_CASE("two screens can't share a route, counting the namespace") {
    auto e = only_error("namespace docs {\nscreen \"A\" /language {\n\ttext \"a\"\n}\n}\nscreen \"B\" /docs/language {\n\ttext \"b\"\n}\n");
    CHECK(e.message == "two screens are at /docs/language; the other is at test.one:2");
    CHECK(check_source("namespace studio {\nscreen \"S\" / {\n\ttext \"a\"\n}\n}\nscreen \"H\" / {\n\ttext \"b\"\n}\n").empty());
}

TEST_CASE("one project block, with known settings") {
    CHECK(only_error("import one\nproject a {\n\tcolour \"red\"\n}\n").message ==
          "'colour' isn't a setting of a project; it says one, title, domain, firebase, region, ui, signin, icon, color, theme, appearance, corners, layout, serve, redirect or analytics");
    CHECK(check_source("import one\nproject p {\n\tanalytics google\n}\n").empty());
    CHECK(only_error("import one\nproject p {\n\tanalytics plausible\n}\n").message == "analytics is google, written plainly, like analytics google");
    CHECK(only_error("import one\nproject a {\n\tui radix\n}\nproject b {\n\tui radix\n}\n").message.starts_with(
        "a project has one project block"));
}

TEST_CASE("one file can use what another declares") {
    auto out = check_files({
        {"waitlist.one", "namespace waitlist {\nentity signup {\n\temail email\n}\ncommand signup::create\n"
                         "view signups public {\n\ttotal = count(signup)\n}\n}\n"},
        {"home.one", "screen \"Home\" / {\n\tform waitlist::signup::create {\n\t\temail\n\t}\n"
                     "\ttext \"{waitlist::signups.total} waiting\"\n}\n"},
    });
    for (const auto& d : out) FAIL_CHECK(format(d));
}

TEST_CASE("a name is looked up in its own namespace, then at the top level") {
    CHECK(check_source("entity book {\n\ttitle text\n}\nnamespace library {\ncommand book::create\n}\n").empty());
    CHECK(only_error("namespace a {\nentity book {\n\ttitle text\n}\n}\nnamespace b {\ncommand book::create\n}\n").message ==
          "command book::create is on entity book, which isn't declared in namespace b");
}

TEST_CASE("where a project runs is checked, so it can't break the code it goes into") {
    CHECK(check_source("import one\nproject p {\n\tfirebase \"ui-one\"\n\tregion \"us-east4\"\n\tdomain \"uione.io\"\n}\n").empty());
    CHECK(only_error("import one\nproject p {\n\tfirebase \"ui-one; rm -rf ~\"\n\tregion \"us-east4\"\n\tdomain \"uione.io\"\n}\n").message ==
          "firebase has to be lowercase letters, digits and dashes, like ui-one or us-east4");
    CHECK(only_error("import one\nproject p {\n\tfirebase \"ui-one\"\n\tregion \"us-east4\"\n\tdomain \"$(whoami).io\"\n}\n").message ==
          "domain has to be a domain name, like uione.io");
    CHECK(only_error("import one\nproject p {\n\tfirebase \"ui-one\"\n\tregion \"us-east4\"\n\tdomain \"localhost\"\n}\n").message ==
          "domain has to be a domain name, like uione.io");
}

TEST_CASE("a project names all of where it runs, or none of it") {
    CHECK(only_error("import one\nproject p {\n\tfirebase \"ui-one\"\n\tdomain \"uione.io\"\n}\n").message ==
          "a project that says where it runs needs firebase, region and domain; this one has no region");
    CHECK(check_source("import one\nproject p {\n\tui radix\n}\n").empty());
}


TEST_CASE("every field says what it holds") {
    auto e = only_error("entity loan {\n\tdue_at  date\n\treturned_at\n}\n");
    CHECK(e.message == "returned_at needs a type, like date or text");
    CHECK(e.where.line == 3);
    CHECK(check_source("entity book {\n\tstatus  enum on_shelf | lent\n}\n").empty());
}

TEST_CASE("a table shows only what its view's rows hold, or commands on the row") {
    const std::string project = R"(
entity book {
	title   text
	author  text
}
command book::withdraw
view shelf {
	each book {
		title
	}
}
screen "Shelf" /shelf {
	table shelf {
		title
		withdraw
		author
	}
}
)";
    auto e = only_error(project);
    CHECK(e.message == "view shelf has no author in its rows; add it to the list's block");
    CHECK(e.where.line == 16);
}

TEST_CASE("a row's button may say which rows it's on, reading the row's own fields") {
    const std::string shelf = R"(
entity book {
	title   text
	lender  user
}
command book::withdraw
view shelf {
	each book {
		title  lender
	}
}
screen "Shelf" /shelf {
	table shelf {
		title
		withdraw "Withdraw" when lender != me && title != none
	}
}
)";
    CHECK(check_source(shelf).size() == 0);

    std::string unread = shelf;
    unread.replace(unread.find("when lender"), 11, "when author");
    auto e = only_error(unread);
    CHECK(e.message == "view shelf has no author in its rows for the row's when to read; add it to the list's block");
    CHECK(e.where.line == 15);

    std::string shown = shelf;
    shown.replace(shown.find("\t\ttitle\n\t\twithdraw"), 7, "\t\ttitle when lender != me\n");
    CHECK(only_error(shown).message == "only a row's button has a when, like delete \"Remove\" when person != me");
}

TEST_CASE("a table's rows are dragged into order by a number field they're ordered by, which an update sets") {
    const std::string board = R"(namespace board {
entity column {
	title     text
	position  number
}
command column::update
view columns {
	each column {
		order by position
		title  position
	}
}
screen "Columns" /columns {
	table columns {
		reorder position
		title
	}
}
}
)";
    CHECK(check_source(board).size() == 0);

    std::string text = board;
    text.replace(text.find("reorder position"), 16, "reorder title");
    CHECK(only_error(text).message == "a table's rows are put in order by a number field of theirs, like reorder position");

    std::string unordered = board;
    unordered.replace(unordered.find("\t\torder by position\n"), 20, "");
    CHECK(only_error(unordered).message == "the list is put in order by position, so it's ordered by it first, like order by position");

    std::string updated = board;
    updated.replace(updated.find("command column::update\n"), 23, "");
    CHECK(only_error(updated).message == "rows are put in order by column::update, which isn't declared in namespace board");
}

TEST_CASE("a key can be made from another field when it isn't given, like a phase's name from its title") {
    const std::string lane = "entity lane {\n\tname   text  required  key = slug(title)\n\ttitle  text  required\n\tsize   number\n}\n";
    CHECK(check_source(lane).size() == 0);

    std::string number = lane;
    number.replace(number.find("slug(title)"), 11, "slug(size)");
    CHECK(only_error(number).message == "slug makes a name from a text field of entity lane, like slug(title)");

    std::string other = lane;
    other.replace(other.find("slug(title)"), 11, "upper(title)");
    CHECK(only_error(other).message == "a field starts as a value, me, me.username, now, a name made from another field, like slug(title), or the people a field mentions, like mentions(body)");
}

TEST_CASE("a command's inputs, and the entities it changes or deletes with it, are checked where they're named") {
    const std::string board = R"(namespace board {
entity column {
	title  text
}
entity card {
	column  column
}
command card::update
command card::delete
command column::delete {
	input into column
	each card where column == id {
		dispatch card::update { column = into }
	}
	each card where column == into {
		dispatch card::delete
	}
}
}
)";
    CHECK(check_source(board).size() == 0);

    std::string field = board;
    field.replace(field.find("input into column"), 17, "input into column\n\tinput title text");
    CHECK(only_error(field).message == "title is a field of entity column already; an input is something else it's sent");

    std::string picked = board;
    picked.replace(picked.find("where column == id"), 18, "where colour == id");
    CHECK(only_error(picked).message.starts_with("entity card has no field colour"));

    std::string shape = board;
    shape.replace(shape.find("where column == into"), 20, "where column != into");
    CHECK(only_error(shape).message == "what's picked is by a field's value, like where phase == id, or by any of several, like from == id || to == id");

    std::string unknown = board;
    unknown.replace(unknown.find("each card where"), 9, "each cart");
    CHECK(only_error(unknown).message == "there's no entity cart in namespace board");
}

TEST_CASE("a once's steps are checked as updates of what they go through, and may name what's stored") {
    const std::string crew = R"(namespace crew {
entity crew {
	slug     slug  required  unique  key
	newcomer role
}
define role captain "Captain" in crew {
	crew::update
	role::update
}
command crew::update
command role::update
once "2026-10-07 mates" {
	each role where name == "mate" {
		dispatch role::update { title = "First mate"  may = [crew::update] }
	}
	each crew {
		dispatch crew::update { newcomer = role::bosun }
	}
}
}
)";
    CHECK(check_source(crew).size() == 0);

    std::string where = crew;
    where.replace(where.find("where name == \"mate\""), 20, "where title != \"mate\"");
    CHECK(only_error(where).message == "a once's where picks by a field's value, like where name == \"developer\"");

    std::string entity = crew;
    entity.replace(entity.find("each role where"), 9, "each rang");
    CHECK(only_error(entity).message == "there's no entity rang in namespace crew");

    std::string field = crew;
    field.replace(field.find("title = \"First mate\""), 5, "titel");
    CHECK(only_error(field).message.starts_with("entity role has no field titel"));

    CHECK(only_error("once \"x\" {\n\teach crew {\n\t}\n}\n").message ==
          "once \"x\" changes a namespace's entities, so it goes inside a namespace");
}

TEST_CASE("roles a project defines for itself are role records, given by member records, which the language declares") {
    const std::string crew = R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
entity job {
	crew   crew  required
	title  text  required
}
define role captain "Captain" in crew {
	member::create
	job::create
}
define role deckhand "Deckhand" in crew {
	job::create
}
command crew::create {
	by anyone signed in
	dispatch member::create {
		crew = id  person = me  role = role::captain
	}
}
command member::create
command job::create
view crew_page per crew {
	readers member
	slug = crew.slug
}
}
)";
    CHECK(check_source(crew).size() == 0);

    std::string unknown = crew;
    unknown.replace(unknown.find("role = role::captain"), 20, "role = role::admiral");
    CHECK(only_error(unknown).message == "role::admiral isn't a role this command makes or every project starts with; those are role::captain, role::deckhand");

    std::string allowing = crew;
    allowing.replace(allowing.find("in crew {\n\tjob::create"), 22, "in crew {\n\tjob::sink");
    CHECK(only_error(allowing).message == "deckhand allows job::sink, which isn't a command in namespace crew");

    // Every command is one a role allows, or says who runs it.
    std::string ungranted = crew;
    ungranted.replace(ungranted.find("command job::create"), 19, "command job::create\ncommand job::update");
    auto e = only_error(ungranted);
    CHECK(e.message == "job::update isn't allowed by any role or service; add it to a define role, or say by anyone signed in in the command");

    std::string both = crew;
    both.replace(both.find("\tmember::create\n\tjob::create"), 15, "\tmember::create\n\tcrew::create");
    CHECK(only_error(both).message == "crew::create says who runs it, by anyone or by anyone signed in, so no role or service allows it");

    std::string own = crew;
    own.replace(own.find("entity job {"), 12, "entity member {\n\tname  text\n}\nentity job {");
    auto errors = check_source(own);
    REQUIRE(errors.size() >= 1);
    CHECK(errors[0].message == "define role keeps roles as the language's own member records; take out entity member, which it declares");

    // Roles were once declared by the entities that keep them.
    CHECK(only_error("namespace crew {\nentity crew {\n\tslug  slug  required  key\n}\nroles rank per crew from hand {\n}\n}\n").message ==
          "a project's roles are each declared on their own, like define role maintainer \"Maintainer\" in crew { project::update }, "
          "and the language keeps them as role and member records");
}

TEST_CASE("a theme and corners are ones the component set has") {
    CHECK(only_error("import one\nproject p {\n\ttheme  solarized\n}\n").message == "theme is papercolor, written plainly, like theme papercolor");
    CHECK(only_error("import one\nproject p {\n\tcorners  sharp\n}\n").message == "corners is square or round, written plainly, like corners square");
    CHECK(check_source("import one\nproject p {\n\ttheme  papercolor\n\tcorners  square\n}\n").size() == 0);
}

TEST_CASE("an enum declared on its own gives any field of its type its choices, named with the enum") {
    const std::string project = R"(
enum status {
	open         "Open"
	in_progress  "In progress"
}
entity issue {
	status  status = status::open
	stage   status = status::in_progress
	kind    enum {
		bug
		feature  "Feature"
	} = kind::bug
}
command issue::start {
	require status == status::open  "it isn't open"
	status = status::in_progress
}
)";
    auto files = std::vector<std::pair<std::string, std::string>>{{"test.one", project}};
    diagnostics parse_errors;
    std::vector<file> parsed;
    parsed.push_back(parse("test.one", project, parse_errors));
    REQUIRE(parse_errors.size() == 0);
    diagnostics out;
    check(parsed, out);
    for (const auto& d : out) CAPTURE(format(d));
    REQUIRE(out.size() == 0);
    const auto& issue = std::get<entity_declaration>(parsed[0].declarations[1].node);
    CHECK(issue.fields[0].choices == std::vector<std::string>{"open", "in_progress"});
    CHECK(issue.fields[0].choice_labels == std::vector<std::string>{"Open", "In progress"});
    CHECK(issue.fields[0].enum_name == "status");
    CHECK_FALSE(issue.fields[0].type);
    CHECK(issue.fields[2].choices == std::vector<std::string>{"bug", "feature"});

    std::string wrong = project;
    wrong.replace(wrong.find("status = status::open"), 21, "status = status::shut");
    CHECK(only_error(wrong).message == "shut isn't one of status's choices, status::open, status::in_progress");

    CHECK(only_error("enum status {\n\topen\n\topen\n}\n").message == "open is one of status's choices already");
}

TEST_CASE("names inside expressions are looked up where they're written, with the nearest match suggested") {
    const std::string project = R"(namespace library {
entity book {
	title   text
	status  enum on_shelf | lent | withdrawn
}
entity loan {
	book         book
	returned_at  date
}
command book::withdraw {
	require statuss == on_shelf  "only a book on the shelf"
	status = lnt
}
command loan::checkin {
	require book.statuss == lent  "that book isn't out"
	returned_at = now
}
view shelf {
	each book where statu != lent {
		order by sort_title(titel)
		title  pages
		lent = first(loan where loan.book == book.id && returned_at == none).member
	}
}
view mine per user {
	each loan where book == user.name {
		returned_at
	}
}
function sort_title(title) {
	if starts_with(title) { return drop(title, 4) }
	return titel
}
}
)";
    auto out = check_source(project);
    std::vector<std::string> messages;
    for (const auto& d : out) messages.push_back(std::to_string(d.where.line) + ": " + d.message);
    CHECK(messages == std::vector<std::string>{
                          "11: entity book has no field statuss; did you mean status?",
                          "12: lnt isn't one of status's choices, status::on_shelf, status::lent, status::withdrawn; did you mean lent?",
                          "15: entity book has no field statuss; did you mean status?",
                          "19: entity book has no field statu; did you mean status?",
                          "20: entity book has no field titel; did you mean title?",
                          "21: entity book has no field pages",
                          "22: entity loan has no field member",
                          "26: the person reading a view is only known by me",
                          "31: starts_with takes 2 arguments, not 1",
                          "32: there's no titel here; did you mean title?",
                      });
}

TEST_CASE("an unknown function is reported, with the nearest one suggested") {
    auto e = only_error("entity book {\n\ttitle  text\n}\nview shelf {\n\teach book {\n\t\torder by sort_titel(title)\n\t\ttitle\n\t}\n}\n"
                        "function sort_title(title) {\n\treturn title\n}\n");
    CHECK(e.message == "there's no function sort_titel; did you mean sort_title?");
    CHECK(e.where.line == 6);
}

TEST_CASE("a wrong name is reported once, not again in what it's compared with or given") {
    auto e = only_error("entity book {\n\tstatus  enum on_shelf | lent\n}\ncommand book::lend {\n\tstatuss = status::lent\n}\n");
    CHECK(e.message == "entity book has no field statuss; did you mean status?");
}

TEST_CASE("the old clipped words are pointed at the whole ones") {
    CHECK(only_error("entity task {\n\tdone  bool\n}\n").message == "the type is the whole word: boolean, not bool");
    diagnostics out;
    parse("test.one", "fn sort_title(title) {\n\treturn title\n}\n", out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "functions are declared with the whole word: function, not fn");
}

TEST_CASE("a project's icon is an .svg file that's there") {
    CHECK(only_error("import one\nproject p {\n\ticon \"assets/icon.png\"\n}\n").message == "icon has to be an .svg file, like \"assets/icon.svg\"");
    CHECK(only_error("import one\nproject p {\n\ticon \"assets/nowhere.svg\"\n}\n").message ==
          "there's no icon file at assets/nowhere.svg; it's looked for next to this .one file");
}

TEST_CASE("an update form needs its entity in the address, and a view holding what it asks for") {
    const std::string start = "entity book {\n\ttitle  text\n\tauthor  text\n}\ncommand book::update\n";
    auto e = only_error(start + "screen \"Books\" /books {\n\tform book::update {\n\t\ttitle\n\t}\n}\n");
    CHECK(e.message == "form book::update changes one book, so its screen needs :book in its route, like /books/:book");
    e = only_error(start + "view page per book {\n\ttitle = book.title\n}\n" +
                   "screen \"Book\" /books/:book {\n\tform book::update {\n\t\ttitle  author\n\t}\n}\n");
    CHECK(e.message == "form book::update starts from what's stored, so it needs a view per book holding title author, like title = book.title");
    CHECK(check_source(start + "view page per book {\n\ttitle = book.title\n\tauthor = book.author\n}\n" +
                       "screen \"Book\" /books/:book {\n\tform book::update {\n\t\ttitle  author\n\t}\n}\n")
              .empty());
}


TEST_CASE("a serial is counted per a field that points at another entity, and is never set by hand") {
    const std::string book = "entity book {\n\ttitle  text\n}\n";
    auto e = only_error(book + "entity loan {\n\tnumber  serial  per shelf\n}\n");
    CHECK(e.message == "number is counted per shelf, so entity loan needs a field shelf that points at another entity, like shelf  shelf  required");
    e = only_error(book + "entity loan {\n\tbook  book\n\tnumber  serial  per book\n}\n");
    CHECK(e.message == "number is counted per book, so book has to be required");
    e = only_error(book + "entity loan {\n\tnumber  serial = 1\n}\n");
    CHECK(e.message == "number is counted, so it doesn't start with a value");

    const std::string loan = book + "entity loan {\n\tbook  book  required  key\n\tnumber  serial  per book  key\n\tnote  text\n}\n";
    CHECK(check_source(loan).empty());
    e = only_error(loan + "command loan::create\nscreen \"Loans\" /loans {\n\tform loan::create {\n\t\tbook  number\n\t}\n}\n");
    CHECK(e.message == "number is counted when it's made, so it can't be set");
    e = only_error(loan + "command loan::renumber {\n\tnumber = 2\n}\n");
    CHECK(e.message == "number is counted when it's made, so it can't be set");
    e = only_error(loan + "command loan::move {\n\tclear book\n}\n");
    CHECK(e.message == "book is part of what names it, its key, so it can't be changed");
}

TEST_CASE("a create form asks for a key, and an update form can't") {
    const std::string start = "entity book {\n\tshelfmark  text  required  unique  key\n\ttitle  text\n}\n"
                              "command book::create\ncommand book::update\n"
                              "view page per book {\n\tshelfmark = book.shelfmark\n\ttitle = book.title\n}\n";
    CHECK(check_source(start + "screen \"Shelf\" /shelf {\n\tform book::create {\n\t\tshelfmark  title\n\t}\n}\n").empty());
    auto e = only_error(start + "screen \"Book\" /books/:book {\n\tform book::update {\n\t\tshelfmark  title\n\t}\n}\n");
    CHECK(e.message == "shelfmark is part of what names it, its key, so it can't be changed");
}

TEST_CASE("a view's lists have names of their own, and a table names the one it shows") {
    const std::string start = "entity book {\n\ttitle  text\n}\nentity loan {\n\tbook  book  required\n\tmember  user\n}\n";
    auto e = only_error(start + "view page per book {\n\teach loan in book\n\teach loan\n}\n");
    CHECK(e.message == "view page already has a list without a name; give this one a name, like comments = each ...");
    e = only_error(start + "view page per book {\n\ttitle = book.title\n\ttitle = each loan\n}\n");
    CHECK(e.message == "view page already has something called title");
    e = only_error(start + "view page per book {\n\trows = each loan\n}\n");
    CHECK(e.message == "rows is what a view's list without a name is called; name this list something else");

    const std::string page = start + "view page per book {\n\ttitle = book.title\n\tloans = each loan in book {\n\t\tmember\n\t}\n}\n";
    CHECK(check_source(page + "screen \"Book\" /books/:book {\n\ttable page.loans {\n\t\tmember\n\t}\n}\n").empty());
    e = only_error(page + "screen \"Book\" /books/:book {\n\ttable page.lends {\n\t\tmember\n\t}\n}\n");
    CHECK(e.message == "view page has no list called lends");
    e = only_error(page + "screen \"Book\" /books/:book {\n\ttable page {\n\t\tmember\n\t}\n}\n");
    CHECK(e.message == "view page's lists all have names; say which one, like table page.loans");
    e = only_error(page + "screen \"Book\" /books/:book {\n\ttable page.loans {\n\t\tbook.title\n\t}\n}\n");
    CHECK(e.message == "view page has no book.title in its loans; add it to the list's block");
}

TEST_CASE("a view shows a person's name, picture and username, and nothing else about them") {
    const std::string start = "entity post {\n\ttext  text\n\tauthor  user = me\n}\n";
    CHECK(check_source(start + "view posts {\n\teach post {\n\t\ttext  author.name  author.picture  author.username\n\t}\n}\n").empty());
    auto e = only_error(start + "view posts {\n\teach post {\n\t\tauthor.email\n\t}\n}\n");
    CHECK(e.message == "a view can show a person's name, picture and username, not author.email");
    e = only_error(start + "view posts {\n\teach post {\n\t\tauthor.nme\n\t}\n}\n");
    CHECK(e.message == "a view can show a person's name, picture and username, not author.nme; did you mean name?");
    e = only_error(start + "command post::sign {\n\ttext = author.name\n}\n");
    CHECK(e.message == "a person's name, picture and username are shown in views; here author is only who they are");
}

TEST_CASE("a role held in a project allows commands on what's in one") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity issue {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "entity comment {\n\tissue  issue  required\n}\n"
                              "entity note {\n\ttext  text\n}\n"
                              "command issue::create\ncommand comment::create\ncommand note::create {\n\tby anyone signed in\n}\n";
    CHECK(check_source(start + "define role maintainer \"Maintainer\" in project {\n\tissue::create\n\tcomment::create\n}\n").empty());
    auto e = only_error(start + "command note::update\n"
                                "define role maintainer \"Maintainer\" in project {\n\tissue::create\n\tcomment::create\n\tnote::update\n}\n");
    CHECK(e.message == "role maintainer is held in a project, but note doesn't point at one, so there's no project to look in for note::update");
    e = only_error(start + "entity member {\n\tproject  project  required\n\tperson  user  required\n\trole  enum maintainer | reporter\n}\n"
                           "role maintainer per project from member  issue::create\n");
    CHECK(e.message == "a role held in a project is declared with define role, like define role maintainer \"Maintainer\" in project { ... }, "
                       "and the language keeps it as role and member records");
}

TEST_CASE("a command dispatches another's create, giving what it makes what it needs") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity member {\n\tproject  project  required  key\n\tperson  user  required  key\n"
                              "\trole  enum maintainer | reporter = role::reporter\n\tseat  serial  per project\n}\n"
                              "command member::create\n";
    CHECK(check_source(start + "command project::create {\n\tdispatch member::create {\n\t\tproject = id  person = me  role = role::maintainer\n\t}\n}\n").empty());
    auto e = only_error(start + "command project::create {\n\tdispatch member::create {\n\t\tproject = id\n\t}\n}\n");
    CHECK(e.message == "dispatch member::create needs a value for person, which is required");
    e = only_error(start + "command project::create {\n\tdispatch member::create {\n\t\tproject = id  person = me  role = owner\n\t}\n}\n");
    CHECK(e.message.starts_with("owner isn't one of role's choices"));
    e = only_error(start + "command project::create {\n\tdispatch member::create {\n\t\tproject = id  person = me  seat = 1\n\t}\n}\n");
    CHECK(e.message == "seat is counted when it's made, so it can't be set");
    e = only_error(start + "command project::create {\n\tdispatch member::create {\n\t\tproject = id  persn = me\n\t}\n}\n");
    CHECK(e.message == "entity member has no field persn; did you mean person?");
    e = only_error(start + "command project::create {\n\tdispatch seat::create {\n\t}\n}\n");
    CHECK(e.message == "there's no command seat::create at the top level; commands are named after their entity, like book::create");
    e = only_error(start + "command project::create {\n\tdispatch member::create {\n\t\tid = \"x\"  project = id  person = me\n\t}\n}\n");
    CHECK(e.message == "what's made gets an id of its own; dispatch member::create gives it its fields");
}

TEST_CASE("only commands change records: what changed them otherwise is fixed to a dispatch") {
    const std::string start = "entity board {\n\ttitle  text\n\tstart  column\n}\nentity column {\n\tboard  board  required\n\ttitle  text\n}\n"
                              "entity card {\n\tcolumn  column\n}\ncommand card::create\ncommand card::update\ncommand card::delete\ncommand board::update\n";
    auto e = only_error(start + "command column::delete {\n\tcreate card { column = id }\n}\n");
    CHECK(e.message == "a record is made by its create command: dispatch card::create { ... }");
    REQUIRE(e.fix);
    CHECK(e.fix->text == "dispatch card::create");
    CHECK(e.fix->length == std::string("create card").size());
    e = only_error(start + "command column::delete {\n\tcard::create { column = id }\n}\n");
    CHECK(e.message == "a command is run with dispatch, like dispatch card::create { ... }");
    e = only_error(start + "command column::delete {\n\teach card where column == id {\n\t\tcolumn = none\n\t}\n}\n");
    CHECK(e.message == "a record is changed by its commands: dispatch card::update { column = none }");
    REQUIRE(e.fix);
    CHECK(e.fix->text == "dispatch card::update { column = none }");
    e = only_error(start + "command column::delete {\n\tdelete each card where column == id\n}\n");
    CHECK(e.message == "a record is deleted by its delete command: each card where column == id { dispatch card::delete }");
    REQUIRE(e.fix);
    CHECK(e.fix->text == "each card where column == id { dispatch card::delete }");
    e = only_error(start + "command column::delete {\n\tboard.start = none\n}\n");
    CHECK(e.message == "a record is changed by its commands: dispatch board::update { id = board  start = none }");
    e = only_error(start + "command column::update {\n\tdispatch card::update { column = id }\n}\n");
    CHECK(e.message == "dispatch card::update says which card it acts on, like id = card, or goes in an each over them");
    e = only_error(start + "command column::delete {\n\teach card where column == id {\n\t\tdispatch column::update { title = \"x\" }\n\t}\n}\n");
    CHECK(e.message == "there's no command column::update at the top level; commands are named after their entity, like book::create");
}

TEST_CASE("a view per entity can say who reads each document: the people of its project, and everyone when it's public") {
    const std::string start = "entity project {\n\tslug  text  required  key\n\tvisibility  enum public | private = visibility::public\n}\n"
                              "entity issue {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "entity note {\n\ttext  text\n}\n"
                              "define role maintainer \"Maintainer\" in project {\n}\n";
    CHECK(check_source(start + "view page per project {\n\treaders member\n\tpublic when project.visibility == visibility::public\n\tslug = project.slug\n}\n"
                               "view issue_page per issue {\n\treaders member\n\tpublic when issue.project.visibility == visibility::public\n}\n")
              .empty());
    auto e = only_error(start + "view notes {\n\treaders member\n}\n");
    CHECK(e.message == "view notes says who may read it, so it needs a document per entity, like per project");
    e = only_error(start + "view page per note {\n\treaders member\n}\n");
    CHECK(e.message == "readers member are people in a project, but a note isn't held within one");
    e = only_error(start + "view page per project {\n\treaders project\n}\n");
    CHECK(e.message == "readers project are the people whose project records give them a role, so it's readers member, with roles each declared like define role maintainer \"Maintainer\" in project { ... }");
    e = only_error(start + "view page per project public {\n\tpublic when project.visibility == visibility::public\n}\n");
    CHECK(e.message == "view page is public, so it can't also be public when something holds");
    e = only_error(start + "view page per issue {\n\tpublic when issue.project.visibilty == public\n}\n");
    CHECK(e.message == "entity project has no field visibilty; did you mean visibility?");
    e = only_error(start + "view page per project {\n\tpublic when true\n}\n");
    CHECK(e.message == "public when compares a field with a value, like project.visibility == public");
    e = only_error(start + "view page per project {\n\treaders = project.slug\n}\n");
    CHECK(e.message == "readers says who may read a view's document, so it can't name a value");
}

TEST_CASE("a view's readers can be the people a field of its entity names") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity report {\n\tproject  project  required\n\ttitle  text\n\tauthor  user = me\n\twatchers  list of user\n}\n"
                              "define role maintainer \"Maintainer\" in project {\n}\n";
    CHECK(check_source(start + "view page per report {\n\treaders member\n\treaders report.author\n\treaders report.watchers\n\ttitle = report.title\n}\n"
                               "view card per report {\n\treaders report.author\n}\n")
              .empty());
    auto e = only_error(start + "view page per report {\n\treaders report.title\n}\n");
    CHECK(e.message == "readers report.title needs a person, but a report's title isn't one");
    e = only_error(start + "view page per report {\n\treaders report.writer\n}\n");
    CHECK(e.message == "a report has no field writer for readers to name");
    e = only_error(start + "view page per report {\n\treaders project.slug\n}\n");
    CHECK(e.message == "readers names an entity, like readers member, or the people in a field of the report, like readers report.author");
    e = only_error(start + "view reports {\n\treaders report.author\n}\n");
    CHECK(e.message == "view reports says who may read it, so it needs a document per entity, like per project");
}

TEST_CASE("a list holds text, people or entities, changes by add and remove, and is asked with has") {
    const std::string start = "entity label {\n\tname  text  required  key\n}\n"
                              "entity issue {\n\ttitle  text\n\tlabels  list of label\n\ttags  list of text\n\tassignees  list of user\n}\n";
    CHECK(check_source(start + "command issue::take {\n\tadd me to assignees\n}\ncommand issue::drop {\n\tremove me from assignees\n}\n"
                               "view mine per user {\n\teach issue where assignees has me {\n\t\ttitle  assignees.name  labels.name\n\t}\n}\n")
              .empty());
    auto e = only_error(start + "entity bad {\n\tsizes  list of number\n}\n");
    CHECK(e.message == "sizes is a list, which holds text, people or entities, like list of label or list of user");
    e = only_error(start + "entity bad {\n\tnames  list of text  key\n}\n");
    CHECK(e.message == "names is a list, so it can't be a key, unique, after a field, or start with a value");
    e = only_error(start + "command issue::take {\n\tadd me to title\n}\n");
    CHECK(e.message == "title isn't a list, so nothing can be added to it");
    e = only_error(start + "command issue::take {\n\tadd me to assignes\n}\n");
    CHECK(e.message == "entity issue has no field assignes; did you mean assignees?");
    e = only_error(start + "command issue::take {\n\tassignees = me\n}\n");
    CHECK(e.message == "assignees is a list; add to it or remove from it, like add me to assignees, or give it a whole list, like [a, b]");
    e = only_error(start + "view mine per user {\n\teach issue where title has me\n}\n");
    CHECK(e.message == "has asks a list, and title isn't one");
}

TEST_CASE("a view lists the changes an entity keeps, by the entity or by what it points at") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity issue history {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "entity note {\n\ttext  text\n}\n";
    CHECK(check_source(start + "view issue_page per issue {\n\tchanges = each change in issue {\n"
                               "\t\torder by created_at\n\t\tlimit 20\n\t\tfield  before  after  action  created_by.name  created_at\n\t}\n}\n"
                               "view project_page per project {\n\ttimeline = each change in issue in project {\n"
                               "\t\tissue  field  after\n\t}\n}\n")
              .empty());
    auto e = only_error(start + "view notes {\n\teach change in note\n}\n");
    CHECK(e.message == "'each change in note' needs note to keep its history: entity note history { ... }");
    e = only_error(start + "view page per issue {\n\tchanges = each change in issue {\n\t\ttitle\n\t}\n}\n");
    CHECK(e.message == "entity change has no field title");
}

TEST_CASE("a github webhook finds a project by its repository, and runs as its service, which allows what it dispatches") {
    const std::string start = "entity project {\n\tslug  text  required  key\n\trepository  text  unique\n}\n"
                              "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n}\n"
                              "entity mention {\n\tissue  issue  required  key\n\turl  text  required  key\n"
                              "\tkind  enum commit | pull_request\n\ttitle  text\n}\n"
                              "command mention::create\ncommand mention::delete\n"
                              "define service github \"GitHub\" in project {\n\tmention::create\n\tmention::delete\n}\n";
    const std::string hook = "webhook github /hooks/github as github {\n\tfor project by repository\n";
    CHECK(check_source(start + hook + "\ton commit {\n\t\tdispatch mention::create {\n\t\t\tissue = mentioned  url = url  kind = kind::commit  title = message\n"
                                      "\t\t}\n\t}\n\ton pull_request {\n\t\tdispatch mention::create {\n\t\t\tissue = mentioned  url = url  title = title\n\t\t}\n\t}\n}\n")
              .empty());

    auto e = only_error(start + "webhook gitlab /hooks/gitlab {\n}\n");
    CHECK(e.message == "'gitlab' isn't a webhook uione knows; it knows github");
    e = only_error(start + "webhook github /github as github {\n\tfor project by repository\n}\n");
    CHECK(e.message == "a webhook is received under /hooks/, like /hooks/github");
    e = only_error(start + "webhook github /hooks/github as github {\n\tfor project by slugg\n}\n");
    CHECK(e.message == "webhook github finds a project by slugg, so project needs a text field slugg, like slugg  text  unique");
    e = only_error(start + hook + "\ton push {\n\t}\n}\n");
    CHECK(e.message == "github sends commit and pull_request, not push");
    e = only_error(start + hook + "\ton commit {\n\t\tdispatch mention::create {\n\t\t\tissue = mentioned  url = url  title = title\n\t\t}\n\t}\n}\n");
    CHECK(e.message.starts_with("there's no title here"));
    e = only_error(start + hook + "\ton commit {\n\t\tslug = message\n\t}\n}\n");
    CHECK(e.message == "a webhook makes things with dispatch; it changes nothing else");
    auto errors = check_source("entity project {\n\tslug  text  required  key\n\trepository  text\n}\n" + hook + "}\n");
    CHECK(std::any_of(errors.begin(), errors.end(), [](const diagnostic& d) { return d.message.starts_with("there's no service github"); }));
    CHECK(std::any_of(errors.begin(), errors.end(), [](const diagnostic& d) {
        return d.message == "webhook github reads #12 as a project's issue 12, so an entity needs keys project and a serial per project, like issue";
    }));

    // It runs as a service, which says what it may do, and nothing else.
    e = only_error(start + "webhook github /hooks/github {\n\tfor project by repository\n}\n");
    CHECK(e.message == "webhook github runs as a service, which allows what it does, like webhook github /hooks/github as github, "
                       "with define service github \"GitHub\" in project { ... }");
    std::string narrow = start;
    narrow.replace(narrow.find("\tmention::create\n\tmention::delete\n}"), 35, "\tmention::delete\n}");
    narrow += "define service gitlab \"GitLab\" in project {\n\tmention::create\n}\n";
    e = only_error(narrow + hook + "\ton commit {\n\t\tdispatch mention::create {\n\t\t\tissue = mentioned  url = url\n\t\t}\n\t}\n}\n");
    CHECK(e.message == "service github doesn't run mention::create; add it to define service github");
    e = only_error(start + hook + "\ton commit {\n\t\tdispatch mention::delete {\n\t\t\tid = mentioned\n\t\t}\n\t}\n}\n");
    CHECK(e.message == "a webhook makes things, so it dispatches a create command, like mention::create");
    e = only_error(start + "define service github_app \"GitHub\" {\n\tmention::create\n}\n"
                           "webhook github /hooks/github as github_app {\n\tfor project by repository\n}\n");
    CHECK(e.message == "webhook github is told about each project's repository, so service github_app is connected in each project, "
                       "like define service github_app \"GitHub\" in project { ... }");
    e = only_error(start + hook + "\ton commit {\n\t\tcreate mention {\n\t\t\tissue = mentioned  url = url\n\t\t}\n\t}\n}\n");
    CHECK(e.message == "a record is made by its create command: dispatch mention::create { ... }");
    REQUIRE(e.fix);
    CHECK(e.fix->text == "dispatch mention::create");

    // A service runs commands on what's in its project, as a role's are.
    e = only_error(start + "entity note {\n\ttext  text\n}\ncommand note::create\n"
                           "define service notes \"Notes\" in project {\n\tnote::create\n}\n");
    CHECK(e.message == "service notes is held in a project, but note doesn't point at one, so there's no project to look in for note::create");
    e = only_error(start + "define service other \"Other\" in project {\n\tmention::sink\n}\n");
    CHECK(e.message == "service other runs mention::sink, which isn't a command at the top level");
}

TEST_CASE("a project's webhook secret is shown only to its own people") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "define role maintainer \"Maintainer\" in project {\n}\n";
    CHECK(check_source(start + "view settings per project {\n\treaders member\n\tsecret = github_secret(project.id)\n}\n").empty());
    auto e = only_error(start + "view settings per project public {\n\tsecret = github_secret(project.id)\n}\n");
    CHECK(e.message == "view settings shows a webhook secret, so only the project's people may read it: give it readers, and don't make it public");
    e = only_error(start + "view settings per project {\n\treaders member\n\tsecret = github_secret(project.slug)\n}\n");
    CHECK(e.message == "github_secret takes the id of the project a view is per, like github_secret(project.id)");
}

TEST_CASE("a component is a file beside the .one that draws it") {
    namespace fs = std::filesystem;
    namespace platform = one::platform;
    fs::path dir = fs::temp_directory_path() / "uione-component";
    fs::remove_all(dir);
    fs::create_directories(dir / "components");
    REQUIRE(platform::write_file((dir / "components" / "workbench.tsx").string(), "export default function Workbench() { return null; }\n"));
    std::string main = (dir / "main.one").string();
    CHECK(check_files({{main, "screen \"Editor\" /edit {\n\tcomponent workbench\n}\n"}}).empty());
    auto out = check_files({{main, "screen \"Editor\" /edit {\n\tcomponent sidebar\n}\n"}});
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "component sidebar is drawn by components/sidebar.tsx beside this file, which isn't there");
    CHECK(out[0].where.line == 2);
    fs::remove_all(dir);
}

TEST_CASE("a table's link may name the screen's own parameters before the row's") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity issue {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "view issues per project {\n\teach issue in project {\n\t\ttitle\n\t}\n}\n"
                              "view issue_page per issue {\n\ttitle = issue.title\n}\n"
                              "screen \"Issue\" /projects/:project/issues/:issue {\n\ttext \"{issue_page.title}\"\n}\n";
    CHECK(check_source(start + "screen \"Project\" /projects/:project {\n\ttable issues link /projects/:project/issues/:issue {\n\t\ttitle\n\t}\n}\n")
              .empty());
    auto e = only_error(start + "view all {\n\teach issue {\n\t\ttitle\n\t}\n}\n"
                                "screen \"All\" /issues {\n\ttable all link /projects/:project/issues/:issue {\n\t\ttitle\n\t}\n}\n");
    CHECK(e.message == "this table's link needs :project, which neither this screen's address nor its rows have");
    // A row holding the project fills it.
    CHECK(check_source(start + "view all {\n\teach issue {\n\t\tproject  title\n\t}\n}\n"
                               "screen \"All\" /issues {\n\ttable all link /projects/:project/issues/:issue {\n\t\ttitle\n\t}\n}\n")
              .empty());
}

TEST_CASE("a link opens a screen that's there") {
    const std::string projects = "namespace projects {\nscreen \"All\" / {\n\ttext \"a\"\n}\n"
                                 "namespace archive {\nscreen \"Old\" / {\n\ttext \"b\"\n}\n}\n"
                                 "screen \"Project\" /:project {\n\ttext \"c\"\n}\n}\n"
                                 "namespace empty {\n}\n";
    auto home = [&](const std::string& link) { return projects + "screen \"Home\" / {\n\tlink " + link + " \"Go\"\n}\n"; };
    CHECK(check_source(home("namespace projects")).empty());
    CHECK(check_source(home("namespace projects::archive")).empty());
    CHECK(check_source(home("/projects/archive")).empty());
    CHECK(check_source(home("/projects/neotrac")).empty());  // :project stands for any one part
    CHECK(check_source(home("#top")).empty());
    // A screen of markdown pages is at its namespace's address too.
    CHECK(check_source("namespace guide {\nscreen guide /:page {\n\tmarkdown \"guide/*.md\"\n}\n}\n"
                       "screen \"Home\" / {\n\tlink namespace guide \"Read the guide\"\n\tlink /guide/start \"Start\"\n}\n")
              .empty());

    CHECK(only_error(home("namespace project")).message == "there's no namespace project for this link to open");
    CHECK(only_error(home("namespace empty")).message ==
          "namespace empty has no screen at its own address, /empty, for this link to open");
    auto e = only_error(home("/project"));
    CHECK(e.message == "there's no screen at /project for this link to open");
    CHECK(e.where.line == 17);
    CHECK(only_error(home("/projects/neotrac/people")).message ==
          "there's no screen at /projects/neotrac/people for this link to open");
}

TEST_CASE("a menu holds links to screens that are there") {
    const std::string screens = "screen \"General\" /settings {\n\ttext \"g\"\n}\n";
    CHECK(check_source(screens + "screen \"Home\" / {\n\tmenu {\n\t\tlink /settings \"General\"\n\t}\n}\n").empty());
    CHECK(only_error(screens + "screen \"Home\" / {\n\tmenu {\n\t\ttext \"no\"\n\t}\n}\n").message ==
          "a menu holds links, like link /settings \"General\"");
    CHECK(only_error(screens + "screen \"Home\" / {\n\tmenu {\n\t\tlink /nowhere \"Lost\"\n\t}\n}\n").message ==
          "there's no screen at /nowhere for this link to open");
}

TEST_CASE("an address's last parameter can take the rest of it") {
    const std::string code = "screen \"Code\" /code/:file* {\n\ttext \"x\"\n}\n";
    CHECK(check_source(code).empty());
    CHECK(check_source(code + "screen \"Home\" / {\n\tlink /code/components/chart.tsx \"The chart\"\n}\n").empty());
    CHECK(only_error(code + "screen \"Home\" / {\n\tlink /code \"Nothing\"\n}\n").message == "there's no screen at /code for this link to open");
    for (const char* route : {"/code/*", "/code/:file*/more", "/:a*b"}) {
        CHECK(only_error(std::string("screen \"Code\" ") + route + " {\n\ttext \"x\"\n}\n").message ==
              "a * ends the last parameter of an address, taking the rest of it, like /code/:file*");
    }
}

TEST_CASE("a role is declared once") {
    const std::string book = "entity book {\n\ttitle  text\n}\ncommand book::create\ncommand book::update\n";
    CHECK(check_source(book + "role librarian {\n\tbook::create\n\tbook::update\n}\n").empty());
    auto e = only_error(book + "role librarian  book::create\nrole librarian  book::update\n");
    CHECK(e.message.starts_with("role librarian is declared twice"));
    CHECK(e.message.ends_with("lists them in a block, like role librarian { ... }"));
}

TEST_CASE("an enum says so, and its choices are named with it") {
    const std::string issue = "entity issue {\n\tstatus  enum open | closed = status::open\n\tsize  enum small | large\n}\n";
    CHECK(check_source(issue + "command issue::close {\n\trequire status == status::open  \"closed already\"\n\tstatus = status::closed\n}\n").empty());
    CHECK(only_error(issue + "command issue::close {\n\tstatus = closed\n}\n").message == "write status::closed; an enum's choices are named with it");
    CHECK(only_error("entity issue {\n\tstatus  enum open | closed = open\n}\n").message == "write status::open; an enum's choices are named with it");
    CHECK(only_error(issue + "command issue::close {\n\tstatus = size::large\n}\n").message ==
          "size::large isn't one of status's choices; they're written status::open and so on");
    CHECK(only_error(issue + "command issue::close {\n\tstatus = status::shut\n}\n").message ==
          "shut isn't one of status's choices, status::open, status::closed");
}

TEST_CASE("serve names a folder next to the project") {
    CHECK(only_error("import one\nproject a {\n\tserve \"no-such-folder\"\n}\n").message ==
          "there's no folder no-such-folder to serve; it's looked for next to this .one file");
}

TEST_CASE("a link is inside its namespace, its parameters from the page it's on") {
    const std::string screens = "namespace projects {\nscreen \"Project\" /:project {\n\tlink /:project/reports \"Reports\"\n}\n"
                                "screen \"Reports\" /:project/reports {\n\ttext \"r\"\n}\n";
    CHECK(check_source(screens + "}\n").empty());
    auto e = only_error(screens + "screen \"All\" /all {\n\tlink /:project/reports \"Reports\"\n}\n}\n");
    CHECK(e.message == "this link needs :project, which this screen's address doesn't have");
    e = only_error(screens + "screen \"All\" /all {\n\tlink /all/reports/old \"Reports\"\n}\n}\n");
    CHECK(e.message == "there's no screen at /projects/all/reports/old for this link to open");
    // :projects isn't :project.
    e = only_error("namespace projects {\nscreen \"Reports\" /:project/reports {\n\ttext \"r\"\n}\n"
                   "screen \"All\" /:projects {\n\tlink /:project/reports \"Reports\"\n}\n}\n");
    CHECK(e.message == "this link needs :project, which this screen's address doesn't have");
}

TEST_CASE("a backend is a Go file of the namespace's package, beside the .one file") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-backend-check";
    fs::remove_all(dir);
    fs::create_directories(dir / "backend");
    one::platform::write_file((dir / "backend" / "deploy.go").string(), "// Written by hand.\npackage desk\n");
    one::platform::write_file((dir / "backend" / "other.go").string(), "package main\n");
    auto errors = [&](const std::string& source) {
        diagnostics out;
        std::vector<file> files;
        files.push_back(parse((dir / "main.one").string(), source, out));
        check(files, out);
        return out;
    };
    CHECK(errors("namespace desk {\nbackend deploy\n}\n").empty());
    auto out = errors("namespace desk {\nbackend missing\n}\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "backend missing is written in backend/missing.go beside this file, which isn't there");
    out = errors("namespace desk {\nbackend other\n}\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "backend/other.go has to start with package desk, the package of namespace desk");
    out = errors("namespace desk {\nbackend desk\n}\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].message.starts_with("backend desk would be the same file as the code generated"));
    out = errors("backend deploy\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "backend deploy is Go in a namespace's package, so it goes inside a namespace");
    fs::remove_all(dir);
}

TEST_CASE("a project can redirect an address that moved, and a link can go to another site") {
    CHECK(check_source("import one\nproject p {\n\tredirect \"/install.sh\" \"https://www.uione.io/install.sh\"\n}\n").empty());
    CHECK(only_error("import one\nproject p {\n\tredirect \"install.sh\" \"https://www.uione.io/install.sh\"\n}\n").message.starts_with("redirect takes an address"));
    CHECK(only_error("import one\nproject p {\n\tredirect \"/install.sh\"\n}\n").message.starts_with("redirect takes an address"));
    CHECK(only_error("import one\nproject p {\n\tdomain \"a.io\" \"b.io\"\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n}\n").message == "domain takes one value");
    CHECK(check_source("screen \"Home\" / {\n\tlink \"https://uione.io/studio\" \"Open the studio\"\n}\n").empty());
    CHECK(only_error("screen \"Home\" / {\n\tlink \"http://uione.io\" \"Open\"\n}\n").message ==
          "a link to another site is an https:// address, like \"https://uione.io/studio\"");
    CHECK(only_error("screen \"Home\" / {\n\tlink \"uione.io\" \"Open\"\n}\n").message.starts_with("a link goes to an address"));
}

TEST_CASE("people sign in with Google, GitHub or Microsoft, each named once") {
    CHECK(check_source("import one\nproject p {\n\tsignin google\n}\n").empty());
    CHECK(check_source("import one\nproject p {\n\tsignin github\n\tsignin google\n\tsignin microsoft\n}\n").empty());
    CHECK(only_error("import one\nproject p {\n\tsignin twitter\n}\n").message == "signin is google, github or microsoft, written plainly, like signin google");
    CHECK(only_error("import one\nproject p {\n\tsignin github\n\tsignin github\n}\n").message == "signin github is named twice");
    // What they were called for a while, each with its fix.
    auto signin = only_error("import one\nproject p {\n\tauthentication github\n}\n");
    CHECK(signin.message == "authentication is called signin, like signin github");
    REQUIRE(signin.fix);
    CHECK(signin.fix->text == "signin");
    CHECK(signin.fix->length == 14);
    auto signed_in = only_error("entity task {\n\ttitle text\n}\ncommand task::create {\n\tpermission authenticated\n}\n");
    CHECK(signed_in.message == "who runs a command is said as it reads: by anyone signed in");
    REQUIRE(signed_in.fix);
    CHECK(signed_in.fix->text == "by anyone signed in");
    CHECK(signed_in.fix->length == std::string("permission authenticated").size());
    CHECK(check_source("entity task {\n\ttitle text\n}\ncommand task::create {\n\tby anyone signed in\n}\n").empty());
}

TEST_CASE("a field can start as the person's username") {
    CHECK(check_source("namespace a {\nentity project {\n\towner  text  key  = me.username\n\tslug  text  key\n}\n}\n").empty());
    CHECK(only_error("namespace a {\nentity project {\n\towner  user  = me.username\n}\n}\n").message == "owner starts as me.username, so it's text");
    CHECK(only_error("namespace a {\nentity project {\n\towner  text  = me.name\n}\n}\n").message ==
          "a field starts as a value, me, me.username or now, not me.name");
}

TEST_CASE("a namespace's screens can be at another address") {
    CHECK(check_source("namespace studio at / {\nscreen \"Projects\" / {\n\ttext \"a\"\n}\n}\n"
                       "screen \"Home\" /home {\n\tlink namespace studio \"Projects\"\n}\n").empty());
    auto e = only_error("namespace studio at / {\nscreen \"Projects\" / {\n\ttext \"a\"\n}\n}\nscreen \"Home\" / {\n\ttext \"b\"\n}\n");
    CHECK(e.message.starts_with("two screens are at /;"));
}

TEST_CASE("a namespace says where it is once, in any of its files") {
    // The file that says at comes last, and the screens before it are there too.
    CHECK(check_files({{"a.one", "namespace studio {\nscreen \"Projects\" / {\n\tlink /settings \"Settings\"\n}\n}\n"},
                       {"b.one", "namespace studio at / {\nscreen \"Settings\" /settings {\n\ttext \"a\"\n}\n}\n"}})
              .empty());
    auto out = check_files({{"a.one", "namespace studio at /docs {\n}\n"}, {"b.one", "namespace studio at / {\n}\n"}});
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "namespace studio is at /docs in a.one:1 but at / here; say where it is once");
}

TEST_CASE("a screen's title can show a view the screen can read") {
    CHECK(check_source("namespace a {\nentity issue {\n\ttitle  text\n}\nview issue_page per issue {\n\ttitle = issue.title\n}\n"
                       "screen \"{issue_page.title}\" /issues/:issue {\n\ttext \"hi\"\n}\n}\n")
              .empty());
    CHECK(only_error("namespace a {\nentity issue {\n\ttitle  text\n}\nview issue_page per issue {\n\ttitle = issue.title\n}\n"
                     "screen \"{issue_page.title}\" /issues {\n\ttext \"hi\"\n}\n}\n")
              .message.starts_with("view issue_page has one document per issue, so the screen showing it needs :issue"));
}

TEST_CASE("a button's when reads the views its screen shows") {
    const std::string start = "namespace a {\nentity issue {\n\tstatus  enum  open | closed = status::open\n}\n"
                              "command issue::close {\n\tstatus = status::closed\n}\nview issue_page per issue {\n\tstatus = issue.status\n}\n";
    CHECK(check_source(start + "screen \"Issue\" /issues/:issue {\n\tissue::close \"Close issue\" when issue_page.status == status::open\n}\n}\n").empty());
    CHECK(only_error(start + "screen \"Issue\" /issues/:issue {\n\tissue::close when issue_page.title == \"x\"\n}\n}\n").message ==
          "view issue_page has no title for the button to read");
    CHECK(only_error(start + "screen \"Issue\" /issues/:issue {\n\tissue::close when count(issue) > 1\n}\n}\n").message ==
          "a button's when compares a view's fields with values, like issue_page.status == status::open");
}

TEST_CASE("a thread shows who wrote what, and a timeline an entity's changes") {
    const std::string start = "namespace a {\nentity issue history {\n\ttitle  text\n}\nentity comment {\n\tissue  issue  required\n\tbody  text\n\tauthor  user  = me\n}\n";
    const std::string view = "view issue_page per issue {\n\tcomments = each comment in issue {\n\t\tauthor.name  body  created_at\n\t}\n"
                             "\thistory = each change in issue {\n\t\tfield  before  after  created_at\n\t}\n}\n";
    CHECK(check_source(start + view + "screen \"Issue\" /issues/:issue {\n\tthread issue_page.comments\n\ttimeline issue_page.history\n}\n}\n").empty());
    CHECK(only_error(start + view + "screen \"Issue\" /issues/:issue {\n\ttimeline issue_page.comments\n}\n}\n").message ==
          "a timeline shows an entity's changes, so comments is each change in an entity, like each change in issue");
    auto wrong = check_source(start + view + "screen \"Issue\" /issues/:issue {\n\tthread issue_page.history\n}\n}\n");
    REQUIRE(wrong.size() == 2);
    CHECK(wrong[0].message == "a thread needs body in each of history's rows");
    CHECK(wrong[1].message == "a thread needs author.name in each of history's rows");
    CHECK(only_error(start + view + "screen \"Issue\" /issues/:issue {\n\tthread issue_page.replies\n}\n}\n").message ==
          "view issue_page has no list called replies");
}

TEST_CASE("a table's tabs are by a choice it shows") {
    const std::string start = "namespace a {\nentity issue {\n\ttitle  text\n\tstatus  enum  open | closed = status::open\n}\n"
                              "view issues {\n\teach issue {\n\t\ttitle  status\n\t}\n}\n";
    CHECK(check_source(start + "screen \"Issues\" /issues {\n\ttable issues by status {\n\t\ttitle\n\t\tstatus\n\t}\n}\n}\n").empty());
    CHECK(only_error(start + "screen \"Issues\" /issues {\n\ttable issues by status {\n\t\ttitle\n\t}\n}\n}\n").message ==
          "the table's tabs are by status, which it needs as a column too");
    CHECK(only_error(start + "screen \"Issues\" /issues {\n\ttable issues by title {\n\t\ttitle\n\t\tstatus\n\t}\n}\n}\n").message ==
          "a table's tabs are by a field with choices, like status, and title isn't one");
}

TEST_CASE("a project's color is dark enough to read on a white page") {
    CHECK(check_source("import one\nproject p {\n\tcolor  \"#0f766e\"\n}\n").empty());
    CHECK(only_error("import one\nproject p {\n\tcolor  \"teal\"\n}\n").message == "color is written #rrggbb, like color \"#0f766e\"");
    CHECK(only_error("import one\nproject p {\n\tcolor  \"#fde047\"\n}\n").message ==
          "color #fde047 is too light to read as a link on a white page (1.3:1, and it needs 4.5:1); choose a darker one");
}

TEST_CASE("details show values the view has") {
    const std::string start = "namespace a {\nentity issue {\n\ttitle  text\n\towner  user\n}\nview issue_page per issue {\n\ttitle = issue.title\n\towner_name = issue.owner.name\n}\n";
    CHECK(check_source(start + "screen \"Issue\" /issues/:issue {\n\tdetails issue_page {\n\t\towner_name \"Owner\"\n\t}\n}\n}\n").empty());
    CHECK(only_error(start + "screen \"Issue\" /issues/:issue {\n\tdetails issue_page {\n\t\tstatus\n\t}\n}\n}\n").message == "view issue_page has no status to show");
}

TEST_CASE("a screen's regions are its layout's, and hold everything once there are any") {
    const std::string start = "namespace a {\n";
    CHECK(check_source(start + "screen \"S\" /s layout two_columns {\n\tmain {\n\t\ttext \"a\"\n\t}\n\tside {\n\t\ttext \"b\"\n\t}\n}\n}\n").empty());
    CHECK(only_error(start + "screen \"S\" /s layout three_columns {\n\ttext \"a\"\n}\n}\n").message == "layout is single or two_columns");
    CHECK(only_error(start + "screen \"S\" /s {\n\tside {\n\t\ttext \"b\"\n\t}\n}\n}\n").message == "layout single has no region side; its regions are main");
    CHECK(only_error(start + "screen \"S\" /s layout two_columns {\n\tmain {\n\t\ttext \"a\"\n\t}\n\ttext \"b\"\n}\n}\n").message ==
          "this screen puts its items in regions, so this goes in one too, like main { ... }");
    CHECK(check_source("import one\nproject p {\n\tlayout  two_columns\n}\nnamespace a {\nscreen \"S\" /s {\n\tside {\n\t\ttext \"b\"\n\t}\n}\n}\n").empty());
}

TEST_CASE("a table searches and sorts by what its rows have") {
    const std::string start = "namespace a {\nentity issue {\n\ttitle  text\n\tnumber  number\n}\nview issues {\n\teach issue {\n\t\ttitle  number\n\t}\n}\n";
    CHECK(check_source(start + "screen \"I\" /i {\n\ttable issues {\n\t\tsearch title\n\t\tsort by number descending\n\t\tpage 10\n\t\ttitle\n\t}\n}\n}\n").empty());
    CHECK(only_error(start + "screen \"I\" /i {\n\ttable issues {\n\t\tsearch body\n\t\ttitle\n\t}\n}\n}\n").message == "the table searches body, which its rows don't have");
    CHECK(only_error(start + "screen \"I\" /i {\n\ttable issues {\n\t\tsort by created_at descending\n\t\ttitle\n\t}\n}\n}\n").message == "the table is sorted by created_at, which its rows don't have");
}

TEST_CASE("a card's filter keeps what changed since a time counted from now") {
    std::string code = "namespace work at / {\n"
                       "entity board {\n\ttitle  text\n}\n"
                       "entity issue {\n\tboard  board\n\tdue  date\n}\n"
                       "view board_list {\n\tboards = each board {\n\t\ttitle\n\t}\n}\n"
                       "screen \"Board\" /boards/:board {\n\ttext \"hi\"\n}\n"
                       "screen \"Boards\" / {\n\tcards board_list.boards link /boards/:board {\n\t\ttitle\n";
    CHECK(check_source(code + "\t\tfilter \"Changed this week\" updated_at > 7 days ago\n\t\tfilter \"Due soon\" due <= 2 weeks from now\n\t}\n}\n}\n").empty());
    CHECK(only_error(code + "\t\tfilter \"Recent\" updated_at > 7\n\t}\n}\n}\n").message ==
          "a filter compares a time with one counted from now, like updated_at > 7 days ago");
    CHECK(only_error(code + "\t\tfilter \"Recent\" updated_at == 7 days ago\n\t}\n}\n}\n").message ==
          "a filter keeps a time before or after one, like updated_at > 7 days ago");
}

TEST_CASE("a view counts the rows of its own list that pass, and a header's badge shows it") {
    auto views = [](const std::string& value) {
        return "namespace work {\n"
               "entity issue history {\n\ttitle  text\n\tassignees  list of user\n}\n"
               "entity reader {\n\tperson  user  key  = me\n\tseen_at  date\n}\n"
               "view news per user {\n\tchanges = each change in issue where assignees has me {\n\t\tfield  created_at\n\t}\n"
               "\tseen = first(reader where person == me).seen_at\n\t" + value + "\n}\n}\n";
    };
    std::string counted = views("unread = count(changes where created_at > seen)");
    CHECK(check_source("import one\nproject tracker {\n\tui  radix\n}\nheader {\n\tbadge news.unread\n}\n" + counted).empty());
    CHECK(check_source(views("listed = count(changes)")).empty());
    CHECK(only_error(views("unread = count(changes where field > seen && created_at > looked)")).message ==
          "view news has no value looked to compare with, like seen = first(...).seen_at");
    CHECK(only_error(views("unread = count(changes where before > seen)")).message ==
          "changes's rows don't hold before; list it in its block to count by it");
    CHECK(only_error(views("unread = count(changes where created_at > 3)")).message ==
          "counting changes compares a field of its rows with another value of the view, like created_at > seen, joined by &&");
    CHECK(only_error("header {\n\tbadge news.fresh\n}\n" + counted).message ==
          "view news has no value fresh to show; give it one, like fresh = count(...)");
    CHECK(only_error("header {\n\tbadge feed.unread\n}\n" + counted).message == "a badge shows a value of a view, and there's no view feed");
    CHECK(only_error("import one\nproject tracker {\n\tunread  news.changes since news.seen\n}\n" + views("")).message ==
          "what's new is counted in a view and shown in the header: unread = count(changes where created_at > seen) in view news, "
          "and header { badge news.unread }; one upgrade moves it there");
}

TEST_CASE("buttons, links and a toolbar's buttons are drawn as icons that exist") {
    std::string code = "namespace work {\nentity issue {\n\ttitle  text\n}\ncommand issue::create\nview issues {\n\teach issue {\n\t\ttitle\n\t}\n}\n"
                       "screen \"Workflow\" /workflow {\n\ttext \"a\"\n}\n";
    CHECK(check_source(code + "screen \"Issues\" /issues {\n\tlink /workflow \"Workflow\" icon workflow\n\ttable issues {\n\t\ttitle\n\t\tissue::create \"New issue\" icon add\n\t}\n"
                              "\tform issue::create {\n\t\ttitle\n\t}\n}\n}\n")
              .empty());
    CHECK(only_error(code + "screen \"Issues\" /issues {\n\tlink /workflow \"Workflow\" icon flow\n}\n}\n").message ==
          "there's no icon flow; there are add, edit, follow, following and workflow");
}

TEST_CASE("a section is drawn in one of the library's colors") {
    std::string code = "namespace work {\nscreen \"Home\" / layout two_columns {\n\tside {\n\t\tsection \"Wiki\" color ";
    CHECK(check_source(code + "violet {\n\t\t\ttext \"a\"\n\t\t}\n\t}\n}\n}\n").empty());
    CHECK(only_error(code + "purple {\n\t\t\ttext \"a\"\n\t\t}\n\t}\n}\n}\n").message ==
          "there's no color purple; there are blue, teal, green, amber, red and violet");
}

TEST_CASE("a subtitle sits under the title of a screen laid out in regions") {
    CHECK(check_source("namespace work {\nscreen \"Home\" / layout two_columns {\n\tsubtitle \"Everything at a glance\"\n\tmain {\n\t\ttext \"a\"\n\t}\n"
                       "\tside {\n\t\ttext \"b\"\n\t}\n}\n}\n")
              .empty());
}

TEST_CASE("only, tint by and find name what their rows hold") {
    std::string code = "namespace work {\n"
                       "entity issue {\n\ttitle  text\n\tdue  date\n\tpriority  enum { high  low }\n}\n"
                       "view issues {\n\tall = each issue {\n\t\ttitle  due  priority\n\t}\n}\n"
                       "screen \"Issue\" /issues/:issue {\n\ttext \"a\"\n}\n";
    CHECK(check_source(code + "screen \"Issues\" /issues {\n\ttable issues.all {\n\t\tonly due <= 2 weeks from now\n\t\ttint by priority\n\t\ttitle\n\t}\n"
                              "\tfind \"Find\" {\n\t\tissues.all \"Issues\" link /issues/:issue by title\n\t}\n}\n}\n")
              .empty());
    CHECK(only_error(code + "screen \"Issues\" /issues {\n\ttable issues.all {\n\t\ttint by title\n\t\ttitle\n\t}\n}\n}\n").message ==
          "a tint is by a field with choices, like priority, the first the most urgent, and title isn't one");
    CHECK(only_error(code + "screen \"Issues\" /issues {\n\tfind \"Find\" {\n\t\tissues.all \"Issues\" by body\n\t}\n}\n}\n").message ==
          "view issues has no body in its all to find by; add it to the list's block");
}

TEST_CASE("a comment is kept in its issue's history, and mentions the people its body names") {
    std::string issue = "namespace work {\nentity issue history {\n\ttitle  text\n}\n";
    CHECK(check_source(issue + "entity comment history of issue {\n\tissue  issue  required\n\tbody  markdown\n\tmentioned  list of user = mentions(body)\n}\n}\n").empty());
    CHECK(only_error("namespace work {\nentity issue {\n\ttitle  text\n}\nentity comment history of issue {\n\tissue  issue  required\n}\n}\n").message ==
          "entity issue keeps no history to keep comment's in; say entity issue history");
    CHECK(only_error(issue + "entity comment {\n\tissue  issue\n\tbody  markdown\n\tmentioned  user = mentions(body)\n}\n}\n").message ==
          "mentioned holds the people body mentions, so it's a list of user");
}

TEST_CASE("an invitation invites to be something with one person, and has what it needs") {
    std::string code = "namespace work {\n"
                       "entity team {\n\tslug  text  required  key\n}\n"
                       "entity seat {\n\tteam  team  required  key\n\tperson  user  required  key\n\trole  enum { lead  helper }\n}\n";
    CHECK(check_source(code + "entity invitation invites seat {\n\tteam  team  required  key\n\temail  email  required  key\n}\n}\n").empty());
    CHECK(only_error(code + "entity invitation invites seat {\n\temail  email  required  key\n}\n}\n").message ==
          "a seat needs its team, so invitation has it too, like team  team");
    CHECK(only_error(code + "entity invitation invites seat {\n\tteam  team  required  key\n}\n}\n").message ==
          "an invitation is to one email, so invitation has one email field, like email  email  required");
    CHECK(only_error(code + "entity invitation invites seat {\n\tteam  team  required  key\n\temail  email  required\n\tperson  user\n}\n}\n").message ==
          "invitation doesn't say who person is; whoever signs in with its email is");
    CHECK(only_error(code + "entity invitation invites chair {\n\temail  email\n}\n}\n").message ==
          "there's no entity chair in namespace work for invitation to invite to be");
}

TEST_CASE("on signin does each step as the person signing in, with me.email theirs") {
    CHECK(check_source("namespace work {\nentity note {\n\temail  email\n\ttext  text\n}\n"
                       "command note::update\n"
                       "on signin {\n\teach note where email == me.email {\n\t\tdispatch note::update { text = \"seen\" }\n\t}\n\tdelete each note where email == me.email\n}\n}\n")
              .empty());
}

TEST_CASE("a footer says who a site is by, and what it was built from, with a screen's items") {
    std::string code = "import one\nproject shop {\n\tui  radix\n}\n";
    CHECK(check_source(code + "footer {\n\ttext \"© {year} [Ada Lovelace](https://www.linkedin.com/in/ada)\"\n"
                              "\tlink build.release \"uione {build.version}\"\n\tlink build.source \"{build.commit}\"\n}\n")
              .empty());
    CHECK(check_source(code + "footer columns {\n\tsection \"Project\" {\n\t\tlink \"https://github.com/da0x/neotrac\" \"Source\"\n\t}\n}\n").empty());
    CHECK(only_error(code + "footer {\n\tlink build.tag \"uione\"\n}\n").message ==
          "a link to what the site was built from is build.release, its uione release's notes, or build.source, its commit");
    CHECK(only_error(code + "footer {\n\ttext \"{build.date}\"\n}\n").message ==
          "{build.date} isn't something a site is built from; it's {build.version} or {build.commit}");
    CHECK(only_error(code + "footer grid {\n\ttext \"a\"\n}\n").message ==
          "a footer is a bar, one line, or columns, each a section, like footer columns { ... }");
    CHECK(only_error(code + "footer {\n\ttext \"a\"\n}\nfooter {\n\ttext \"b\"\n}\n").message == "a site has one footer, at the foot of every page");
    CHECK(only_error("namespace shop {\nview stock {\n\teach item {\n\t\tname\n\t}\n}\nentity item {\n\tname  text\n}\n}\nfooter {\n\ttable shop::stock {\n\t\tname\n\t}\n}\n").message ==
          "a footer holds text, links and sections of them, like text \"© {year} Ada Lovelace\"");
    // Who a site is by was a setting for a while.
    CHECK(only_error("import one\nproject shop {\n\tcopyright \"Ada Lovelace\" \"https://www.linkedin.com/in/ada\"\n}\n").message ==
          "who a site is by is said in its footer, like footer { text \"© {year} Ada Lovelace\" }; one upgrade moves it there");
}

TEST_CASE("each issue in project lists the rows whose one field pointing at the view's project holds it") {
    const std::string start = "entity project {\n\tname  text\n}\nentity issue history {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "entity link {\n\tfrom  issue\n\tto  issue\n}\n";
    CHECK(check_source(start + "view page per project {\n\tissues = each issue in project {\n\t\ttitle\n\t}\n"
                               "\ttimeline = each change in issue in project {\n\t\tfield\n\t}\n}\n").empty());
    auto e = only_error(start + "view page per issue {\n\teach link in issue\n}\n");
    CHECK(e.message == "link points at an issue by from and to; say which with where, like where from == issue.id");
    e = only_error(start + "view page per project {\n\teach link in project\n}\n");
    CHECK(e.message == "link has no field pointing at a project, so no link is in one");
    e = only_error(start + "view page per issue {\n\teach issue in project\n}\n");
    CHECK(e.message == "view page is per issue, so it has no project for its rows to be in");
}

TEST_CASE("a view says where in and the person reading as me, and each is fixed to it") {
    const std::string start = "entity project {\n\tname  text\n}\nentity issue {\n\tproject  project  required\n\towner  user\n}\n";
    auto e = only_error(start + "view page per project {\n\teach issue where project == project.id\n}\n");
    CHECK(e.message == "write in project for the rows in the view's project");
    REQUIRE(e.fix);
    CHECK(e.fix->text == "in project");
    CHECK(e.fix->length == std::string("where project == project.id").size());
    e = only_error(start + "view mine per user {\n\teach issue where owner == user.id\n}\n");
    CHECK(e.message == "write me for the person reading");
    REQUIRE(e.fix);
    CHECK(e.fix->where.column == 28);
    CHECK(check_source(start + "view mine per user {\n\teach issue where owner == me\n}\n").empty());
}
