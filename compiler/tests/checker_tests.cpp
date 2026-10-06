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
        for (const auto& d : out) CAPTURE(format(d));
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
command loan::checkin {
	require book.status == status::lent  "that book is on the shelf"
	book.status = status::on_shelf
	clear due_at
}

view shelf {
	each book where status != status::lent {
		order title
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
    CHECK(only_error(entity + "command task::create {\n\tpermission owner\n}\n").message ==
          "'owner' needs entity task to have a field 'owner user'");
    CHECK(check_source("entity task {\n\towner user = me\n}\ncommand task::delete {\n\tpermission owner\n}\n").empty());
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
    CHECK(only_error("project a {\n\tcolor \"red\"\n}\n").message ==
          "'color' isn't a project setting; expected domain, firebase, region, ui, authentication, icon, serve, redirect, title, one or analytics");
    CHECK(check_source("project p {\n\tanalytics google\n}\n").empty());
    CHECK(only_error("project p {\n\tanalytics plausible\n}\n").message == "analytics is google, for Firebase Analytics");
    CHECK(only_error("project a {\n\tui shadcn\n}\nproject b {\n\tui shadcn\n}\n").message.starts_with(
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
    CHECK(check_source("project p {\n\tfirebase \"ui-one\"\n\tregion \"us-east4\"\n\tdomain \"uione.io\"\n}\n").empty());
    CHECK(only_error("project p {\n\tfirebase \"ui-one; rm -rf ~\"\n\tregion \"us-east4\"\n\tdomain \"uione.io\"\n}\n").message ==
          "firebase has to be lowercase letters, digits and dashes, like ui-one or us-east4");
    CHECK(only_error("project p {\n\tfirebase \"ui-one\"\n\tregion \"us-east4\"\n\tdomain \"$(whoami).io\"\n}\n").message ==
          "domain has to be a domain name, like uione.io");
    CHECK(only_error("project p {\n\tfirebase \"ui-one\"\n\tregion \"us-east4\"\n\tdomain \"localhost\"\n}\n").message ==
          "domain has to be a domain name, like uione.io");
}

TEST_CASE("a project names all of where it runs, or none of it") {
    CHECK(only_error("project p {\n\tfirebase \"ui-one\"\n\tdomain \"uione.io\"\n}\n").message ==
          "a project that says where it runs needs firebase, region and domain; this one has no region");
    CHECK(check_source("project p {\n\tui radix\n}\n").empty());
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
		order sort_title(titel)
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
                          "26: the person reading a view is only known by user.id",
                          "31: starts_with takes 2 arguments, not 1",
                          "32: there's no titel here; did you mean title?",
                      });
}

TEST_CASE("an unknown function is reported, with the nearest one suggested") {
    auto e = only_error("entity book {\n\ttitle  text\n}\nview shelf {\n\teach book {\n\t\torder sort_titel(title)\n\t\ttitle\n\t}\n}\n"
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
    CHECK(only_error("project p {\n\ticon \"assets/icon.png\"\n}\n").message == "icon has to be an .svg file, like \"assets/icon.svg\"");
    CHECK(only_error("project p {\n\ticon \"assets/nowhere.svg\"\n}\n").message ==
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
    auto e = only_error(start + "view page per book {\n\teach loan where book == book.id\n\teach loan\n}\n");
    CHECK(e.message == "view page already has a list without a name; give this one a name, like comments = each ...");
    e = only_error(start + "view page per book {\n\ttitle = book.title\n\ttitle = each loan\n}\n");
    CHECK(e.message == "view page already has something called title");
    e = only_error(start + "view page per book {\n\trows = each loan\n}\n");
    CHECK(e.message == "rows is what a view's list without a name is called; name this list something else");

    const std::string page = start + "view page per book {\n\ttitle = book.title\n\tloans = each loan where book == book.id {\n\t\tmember\n\t}\n}\n";
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

TEST_CASE("a role held within something names the entity that grants it, which points at it and a person") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity issue {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "entity comment {\n\tissue  issue  required\n}\n"
                              "entity note {\n\ttext  text\n}\n"
                              "command issue::create\ncommand comment::create\ncommand note::create\n";
    const std::string member = "entity member {\n\tproject  project  required\n\tperson  user  required\n\trole  enum maintainer | reporter\n}\n";
    CHECK(check_source(start + member + "role maintainer per project from member  issue::create  comment::create\n").empty());

    auto e = only_error(start + member + "role maintainer per project from member  note::create\n");
    CHECK(e.message == "role maintainer is held within a project, but note doesn't point at one, so there's no project to look in for note::create");
    e = only_error(start + member + "role owner per project from member  issue::create\n");
    CHECK(e.message == "role owner comes from member, so member needs a field role with owner among its choices, like role  enum owner | reader");
    e = only_error(start + "entity member {\n\tproject  project\n\tperson  user  required\n\trole  enum maintainer | reporter\n}\n" +
                   "role maintainer per project from member  issue::create\n");
    CHECK(e.message == "role maintainer comes from member, so member needs a required field pointing at project, like project  project  required");
    e = only_error(start + "entity member {\n\tproject  project  required\n\trole  enum maintainer | reporter\n}\n" +
                   "role maintainer per project from member  issue::create\n");
    CHECK(e.message == "role maintainer comes from member, so member needs a required field holding the person, like person  user  required");
    e = only_error(start + member + "role maintainer per team from member  issue::create\n");
    CHECK(e.message == "role maintainer is held within team, which isn't an entity at the top level");
}

TEST_CASE("a command can create another entity, giving it what it needs") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity member {\n\tproject  project  required  key\n\tperson  user  required  key\n"
                              "\trole  enum maintainer | reporter = role::reporter\n\tseat  serial  per project\n}\n";
    CHECK(check_source(start + "command project::create {\n\tcreate member {\n\t\tproject = id  person = me  role = role::maintainer\n\t}\n}\n").empty());
    auto e = only_error(start + "command project::create {\n\tcreate member {\n\t\tproject = id\n\t}\n}\n");
    CHECK(e.message == "create member needs a value for person, which is required");
    e = only_error(start + "command project::create {\n\tcreate member {\n\t\tproject = id  person = me  role = owner\n\t}\n}\n");
    CHECK(e.message.starts_with("owner isn't one of role's choices"));
    e = only_error(start + "command project::create {\n\tcreate member {\n\t\tproject = id  person = me  seat = 1\n\t}\n}\n");
    CHECK(e.message == "seat is counted when it's made, so it can't be set");
    e = only_error(start + "command project::create {\n\tcreate member {\n\t\tproject = id  persn = me\n\t}\n}\n");
    CHECK(e.message == "entity member has no field persn; did you mean person?");
    e = only_error(start + "command project::create {\n\tcreate seat {\n\t}\n}\n");
    CHECK(e.message == "there's no entity seat to create at the top level");
}

TEST_CASE("a view per entity can say who reads each document: the people of its project, and everyone when it's public") {
    const std::string start = "entity project {\n\tslug  text  required  key\n\tvisibility  enum public | private = visibility::public\n}\n"
                              "entity member {\n\tproject  project  required  key\n\tperson  user  required  key\n\trole  enum maintainer | reporter\n}\n"
                              "entity issue {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "entity note {\n\ttext  text\n}\n"
                              "role maintainer per project from member\n";
    CHECK(check_source(start + "view page per project {\n\treaders member\n\tpublic when project.visibility == visibility::public\n\tslug = project.slug\n}\n"
                               "view issue_page per issue {\n\treaders member\n\tpublic when issue.project.visibility == visibility::public\n}\n")
              .empty());
    auto e = only_error(start + "view notes {\n\treaders member\n}\n");
    CHECK(e.message == "view notes says who may read it, so it needs a document per entity, like per project");
    e = only_error(start + "view page per note {\n\treaders member\n}\n");
    CHECK(e.message == "readers member are people in a project, but a note isn't held within one");
    e = only_error(start + "view page per project {\n\treaders project\n}\n");
    CHECK(e.message == "readers project needs a role that comes from project, like role maintainer per project from project");
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
                              "entity member {\n\tproject  project  required  key\n\tperson  user  required  key\n\trole  enum maintainer | reporter\n}\n"
                              "entity report {\n\tproject  project  required\n\ttitle  text\n\tauthor  user = me\n\twatchers  list of user\n}\n"
                              "role maintainer per project from member\n";
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
                               "view mine per user {\n\teach issue where assignees has user.id {\n\t\ttitle  assignees.name  labels.name\n\t}\n}\n")
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
    CHECK(e.message == "assignees is a list; add to it or remove from it, like add me to assignees");
    e = only_error(start + "view mine per user {\n\teach issue where title has user.id\n}\n");
    CHECK(e.message == "has asks a list, and title isn't one");
}

TEST_CASE("a view lists the changes an entity keeps, by the entity or by what it points at") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity issue history {\n\tproject  project  required\n\ttitle  text\n}\n"
                              "entity note {\n\ttext  text\n}\n";
    CHECK(check_source(start + "view issue_page per issue {\n\tchanges = each change of issue where issue == issue.id {\n"
                               "\t\torder created_at\n\t\tlimit 20\n\t\tfield  before  after  action  created_by.name  created_at\n\t}\n}\n"
                               "view project_page per project {\n\ttimeline = each change of issue where project == project.id {\n"
                               "\t\tissue  field  after\n\t}\n}\n")
              .empty());
    auto e = only_error(start + "view notes {\n\teach change of note\n}\n");
    CHECK(e.message == "'each change of note' needs note to keep its history: entity note history { ... }");
    e = only_error(start + "view page per issue {\n\tchanges = each change of issue where issue == issue.id {\n\t\ttitle\n\t}\n}\n");
    CHECK(e.message == "entity change has no field title");
}

TEST_CASE("a github webhook finds a project by its repository, and makes things from what each event sends") {
    const std::string start = "entity project {\n\tslug  text  required  key\n\trepository  text  unique\n}\n"
                              "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n}\n"
                              "entity mention {\n\tissue  issue  required  key\n\turl  text  required  key\n"
                              "\tkind  enum commit | pull_request\n\ttitle  text\n}\n";
    const std::string hook = "webhook github /hooks/github {\n\tfor project by repository\n";
    CHECK(check_source(start + hook + "\ton commit {\n\t\tcreate mention {\n\t\t\tissue = mentioned  url = url  kind = kind::commit  title = message\n"
                                      "\t\t}\n\t}\n\ton pull_request {\n\t\tcreate mention {\n\t\t\tissue = mentioned  url = url  title = title\n\t\t}\n\t}\n}\n")
              .empty());

    auto e = only_error(start + "webhook gitlab /hooks/gitlab {\n}\n");
    CHECK(e.message == "'gitlab' isn't a webhook uione knows; it knows github");
    e = only_error(start + "webhook github /github {\n\tfor project by repository\n}\n");
    CHECK(e.message == "a webhook is received under /hooks/, like /hooks/github");
    e = only_error(start + "webhook github /hooks/github {\n\tfor project by slugg\n}\n");
    CHECK(e.message == "webhook github finds a project by slugg, so project needs a text field slugg, like slugg  text  unique");
    e = only_error(start + hook + "\ton push {\n\t}\n}\n");
    CHECK(e.message == "github sends commit and pull_request, not push");
    e = only_error(start + hook + "\ton commit {\n\t\tcreate mention {\n\t\t\tissue = mentioned  url = url  title = title\n\t\t}\n\t}\n}\n");
    CHECK(e.message.starts_with("there's no title here"));
    e = only_error(start + hook + "\ton commit {\n\t\tslug = message\n\t}\n}\n");
    CHECK(e.message == "a webhook makes things with create; it changes nothing else");
    e = only_error("entity project {\n\tslug  text  required  key\n\trepository  text\n}\n" + hook + "}\n");
    CHECK(e.message == "webhook github reads #12 as a project's issue 12, so an entity needs keys project and a serial per project, like issue");
}

TEST_CASE("a project's webhook secret is shown only to its own people") {
    const std::string start = "entity project {\n\tslug  text  required  key\n}\n"
                              "entity member {\n\tproject  project  required  key\n\tperson  user  required  key\n\trole  enum maintainer | reporter\n}\n"
                              "role maintainer per project from member\n";
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
                              "view issues per project {\n\teach issue where project == project.id {\n\t\ttitle\n\t}\n}\n"
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
    CHECK(only_error("project a {\n\tserve \"no-such-folder\"\n}\n").message ==
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
    CHECK(check_source("project p {\n\tredirect \"/install.sh\" \"https://www.uione.io/install.sh\"\n}\n").empty());
    CHECK(only_error("project p {\n\tredirect \"install.sh\" \"https://www.uione.io/install.sh\"\n}\n").message.starts_with("redirect takes an address"));
    CHECK(only_error("project p {\n\tredirect \"/install.sh\"\n}\n").message.starts_with("redirect takes an address"));
    CHECK(only_error("project p {\n\tdomain \"a.io\" \"b.io\"\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n}\n").message == "domain takes one value");
    CHECK(check_source("screen \"Home\" / {\n\tlink \"https://uione.io/studio\" \"Open the studio\"\n}\n").empty());
    CHECK(only_error("screen \"Home\" / {\n\tlink \"http://uione.io\" \"Open\"\n}\n").message ==
          "a link to another site is an https:// address, like \"https://uione.io/studio\"");
    CHECK(only_error("screen \"Home\" / {\n\tlink \"uione.io\" \"Open\"\n}\n").message.starts_with("a link goes to an address"));
}

TEST_CASE("people sign in with Google, GitHub or Microsoft, each named once") {
    CHECK(check_source("project p {\n\tauthentication google\n}\n").empty());
    CHECK(check_source("project p {\n\tauthentication github\n\tauthentication google\n\tauthentication microsoft\n}\n").empty());
    CHECK(only_error("project p {\n\tauthentication twitter\n}\n").message == "authentication is google, github or microsoft, one to a line");
    CHECK(only_error("project p {\n\tauthentication github\n\tauthentication github\n}\n").message == "authentication github is named twice");
    // Earlier versions' words, each with its fix.
    auto signin = only_error("project p {\n\tsignin github\n}\n");
    CHECK(signin.message == "signin is called authentication now, like authentication github");
    REQUIRE(signin.fix);
    CHECK(signin.fix->text == "authentication");
    CHECK(signin.fix->length == 6);
    auto signed_in = only_error("entity task {\n\ttitle text\n}\ncommand task::create {\n\tpermission signed_in\n}\n");
    CHECK(signed_in.message == "signed_in is called authenticated now");
    REQUIRE(signed_in.fix);
    CHECK(signed_in.fix->text == "authenticated");
    CHECK(check_source("entity task {\n\ttitle text\n}\ncommand task::create {\n\tpermission authenticated\n}\n").empty());
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

TEST_CASE("a screen's title can show a view the screen can read") {
    CHECK(check_source("namespace a {\nentity issue {\n\ttitle  text\n}\nview issue_page per issue {\n\ttitle = issue.title\n}\n"
                       "screen \"{issue_page.title}\" /issues/:issue {\n\ttext \"hi\"\n}\n}\n")
              .empty());
    CHECK(only_error("namespace a {\nentity issue {\n\ttitle  text\n}\nview issue_page per issue {\n\ttitle = issue.title\n}\n"
                     "screen \"{issue_page.title}\" /issues {\n\ttext \"hi\"\n}\n}\n")
              .message.starts_with("view issue_page has one document per issue, so the screen showing it needs :issue"));
}
