// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <doctest/doctest.h>

#include <string>

#include "language/parser.hpp"

using namespace one::language;

namespace {

    file parse_ok(std::string_view source) {
        diagnostics out;
        file result = parse("test.one", source, out);
        for (const auto& d : out) FAIL_CHECK(format(d));
        return result;
    }

    diagnostics parse_errors(std::string_view source) {
        diagnostics out;
        parse("test.one", source, out);
        return out;
    }

    template <typename T>
    const T& only(const file& f) {
        REQUIRE(f.declarations.size() == 1);
        const T* node = std::get_if<T>(&f.declarations[0].node);
        REQUIRE(node != nullptr);
        return *node;
    }

    std::string name_of(const expression_ptr& e) {
        const auto* name = std::get_if<name_expression>(&e->node);
        return name ? name->name.text() : "";
    }

} // namespace

TEST_CASE("a project and its settings") {
    auto f = parse_ok("project uione {\n\tdomain \"uione.io\"\n\tui shadcn\n}\n");
    const auto& project = only<project_declaration>(f);
    CHECK(project.name == "uione");
    REQUIRE(project.settings.size() == 2);
    CHECK(project.settings[0].key == "domain");
    CHECK(project.settings[0].value == "uione.io");
    CHECK(project.settings[0].is_string);
    CHECK(project.settings[1].value == "shadcn");
    CHECK_FALSE(project.settings[1].is_string);
}

TEST_CASE("an entity's fields: types, choices, rules and starting values") {
    auto f = parse_ok(R"(
entity loan {
	book         book  required
	due_at       date  required  after lent_at
	status       enum on_shelf | lent = status::on_shelf
	returned_at
}
)");
    const auto& entity = only<entity_declaration>(f);
    CHECK(entity.name == "loan");
    REQUIRE(entity.fields.size() == 4);

    CHECK(entity.fields[0].type->text() == "book");
    CHECK(entity.fields[0].required);

    CHECK(entity.fields[1].after == "lent_at");

    CHECK_FALSE(entity.fields[2].type);
    CHECK(entity.fields[2].choices == std::vector<std::string>{"on_shelf", "lent"});
    CHECK(name_of(entity.fields[2].initial) == "status::on_shelf");

    CHECK(entity.fields[3].name == "returned_at");
    CHECK_FALSE(entity.fields[3].type);
    CHECK(entity.fields[3].where.line == 6);
}

TEST_CASE("a format keeps its pattern and example as written") {
    auto f = parse_ok("format shelfmark AAA-9999 {\n\texample HIS-0142\n}\nformat code YYYY-MMM[-A|B]\n");
    REQUIRE(f.declarations.size() == 2);
    const auto& shelfmark = std::get<format_declaration>(f.declarations[0].node);
    CHECK(shelfmark.pattern == "AAA-9999");
    CHECK(shelfmark.example == "HIS-0142");
    const auto& code = std::get<format_declaration>(f.declarations[1].node);
    CHECK(code.pattern == "YYYY-MMM[-A|B]");
}

TEST_CASE("commands, with and without a body") {
    auto f = parse_ok(R"(
command book::create
command loan::checkin {
	require returned_at == none  "that book is already back"
	permission book::edit
	returned_at = now
	book.status = on_shelf
	clear due_at lent_at
}
)");
    REQUIRE(f.declarations.size() == 2);
    const auto& create = std::get<command_declaration>(f.declarations[0].node);
    CHECK(create.name.text() == "book::create");
    CHECK_FALSE(create.has_body);

    const auto& checkin = std::get<command_declaration>(f.declarations[1].node);
    REQUIRE(checkin.body.size() == 5);
    const auto& require = std::get<require_statement>(checkin.body[0].node);
    CHECK(require.message == "that book is already back");
    CHECK(std::holds_alternative<binary_expression>(require.condition->node));
    CHECK(std::get<permission_statement>(checkin.body[1].node).permission.text() == "book::edit");
    const auto& assign = std::get<assign_statement>(checkin.body[3].node);
    CHECK(std::holds_alternative<member_expression>(assign.target->node));
    CHECK(std::get<clear_statement>(checkin.body[4].node).fields == std::vector<std::string>{"due_at", "lent_at"});
}

TEST_CASE("views: per, public, order, each, and computed values") {
    auto f = parse_ok(R"(
view mine per user {
	each loan where member == user.id {
		order done  -created_at
		book.title  due_at
		lent_to = first(loan where returned_at == none).member
	}
}
view signups public {
	total = count(signup)
}
)");
    REQUIRE(f.declarations.size() == 2);
    const auto& mine = std::get<view_declaration>(f.declarations[0].node);
    CHECK(mine.per == "user");
    REQUIRE(mine.each.size() == 1);
    REQUIRE(mine.each[0].order.size() == 2);
    CHECK(std::holds_alternative<unary_expression>(mine.each[0].order[1]->node));
    CHECK(!mine.each[0].name);
    CHECK(name_of(mine.each[0].source) == "loan");
    CHECK(mine.each[0].condition != nullptr);
    REQUIRE(mine.each[0].rows.size() == 3);
    CHECK_FALSE(mine.each[0].rows[0].name);
    CHECK(mine.each[0].rows[2].name == "lent_to");

    const auto& signups = std::get<view_declaration>(f.declarations[1].node);
    CHECK(signups.is_public);
    REQUIRE(signups.values.size() == 1);
    CHECK(signups.values[0].name == "total");
    CHECK(std::holds_alternative<call_expression>(signups.values[0].value->node));
}

TEST_CASE("a role and its permissions") {
    auto f = parse_ok("role librarian  book::view  loan::checkin\n");
    const auto& role = only<role_declaration>(f);
    REQUIRE(role.permissions.size() == 2);
    CHECK(role.permissions[1].text() == "loan::checkin");
}

TEST_CASE("functions, including blocks on one line and else if") {
    auto f = parse_ok(R"(
function sort_title(title, other) {
	if starts_with(title, "The ") { return drop(title, 4) }
	if a { return 1 } else if b { return 2 } else { return 3 }
	return title
}
)");
    const auto& function = only<function_declaration>(f);
    CHECK(function.parameters == std::vector<std::string>{"title", "other"});
    REQUIRE(function.body.size() == 3);
    const auto& chain = std::get<if_statement>(function.body[1].node);
    REQUIRE(chain.else_body.size() == 1);
    const auto& nested = std::get<if_statement>(chain.else_body[0].node);
    CHECK(nested.else_body.size() == 1);
}

TEST_CASE("a screen and everything that can go on it") {
    auto f = parse_ok(R"(
screen "Shelf" /shelf {
	hero "Write it once." {
		text "One file."
		link #waitlist "Join"
		link /docs/language "Docs"
	}
	section "Example" #example {
		code "../examples/library/main.one"
	}
	table shelf {
		shelfmark  title
		lent_to "Lent to"
		update  withdraw
	}
	book::create
	form book::create book::update {
		title  author
		code = suggest(start) {
			hint "made from the start date"
		}
	}
	confirm book::withdraw "Withdraw {title}?"
}
)");
    const auto& screen = only<screen_declaration>(f);
    CHECK(screen.title == "Shelf");
    CHECK(screen.route == "/shelf");
    REQUIRE(screen.items.size() == 6);

    const auto& hero = std::get<content_block>(screen.items[0].node);
    CHECK(hero.type == content_block::kind::hero);
    REQUIRE(hero.items.size() == 3);
    CHECK(std::get<content_link>(hero.items[1].node).target == "#waitlist");
    CHECK(std::get<content_link>(hero.items[2].node).target == "/docs/language");

    const auto& section = std::get<content_block>(screen.items[1].node);
    CHECK(section.anchor == "example");
    CHECK(std::get<content_text>(section.items[0].node).type == content_text::kind::code);

    const auto& table = std::get<table_item>(screen.items[2].node);
    REQUIRE(table.columns.size() == 5);
    CHECK(table.columns[2].label == "Lent to");

    CHECK(std::get<button_item>(screen.items[3].node).command.text() == "book::create");

    const auto& form = std::get<form_item>(screen.items[4].node);
    CHECK(form.commands.size() == 2);
    REQUIRE(form.fields.size() == 3);
    CHECK(form.fields[2].hint == "made from the start date");

    CHECK(std::get<confirm_item>(screen.items[5].node).message == "Withdraw {title}?");
}

TEST_CASE("a screen named rather than titled, at the root") {
    auto f = parse_ok("screen docs /:page {\n\tmarkdown \"docs/*.md\"\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    REQUIRE(f.declarations.size() == 2);
    const auto& docs = std::get<screen_declaration>(f.declarations[0].node);
    CHECK(docs.title_is_name);
    CHECK(docs.route == "/:page");
    CHECK(std::get<screen_declaration>(f.declarations[1].node).route == "/");
}

TEST_CASE("a picker") {
    auto f = parse_ok("picker book from shelf\n");
    const auto& picker = only<picker_declaration>(f);
    CHECK(picker.entity == "book");
    CHECK(picker.view == "shelf");
}

TEST_CASE("namespaces hold declarations") {
    auto f = parse_ok("namespace library {\n\nentity book {\n\ttitle text\n}\n\n} // namespace library\n");
    const auto& ns = only<namespace_declaration>(f);
    CHECK(ns.name == "library");
    REQUIRE(ns.declarations.size() == 1);
    CHECK(std::holds_alternative<entity_declaration>(ns.declarations[0].node));
}

TEST_CASE("expressions follow the usual precedence") {
    auto f = parse_ok("view v {\n\tx = a || b && c == d + e * f\n}\n");
    const auto& view = only<view_declaration>(f);
    const auto& top = std::get<binary_expression>(view.values[0].value->node);
    CHECK(top.op == token_kind::logical_or);
    const auto& right = std::get<binary_expression>(top.right->node);
    CHECK(right.op == token_kind::logical_and);
}

TEST_CASE("errors say what was expected and where") {
    auto out = parse_errors("entity book {\n\ttitle text requird\n}\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].where.line == 2);
    CHECK(out[0].where.column == 13);
    CHECK(out[0].message == "'requird' isn't a field rule; expected required, unique, key or after");
}

TEST_CASE("a word that doesn't start a declaration") {
    auto out = parse_errors("enitty book {\n}\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].message.starts_with("'enitty' doesn't start a declaration"));
}

TEST_CASE("one mistake doesn't hide the next one") {
    auto out = parse_errors(R"(
namespace library {
entity book {
	title text requird
}
command book::create {
	status ==
}
picker book from shelf
}
)");
    REQUIRE(out.size() == 2);
    CHECK(out[0].where.line == 4);
    CHECK(out[1].where.line == 7);
}

TEST_CASE("a block that's never closed is reported") {
    auto out = parse_errors("namespace library {\nentity book {\n\ttitle text\n}\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].message.find("'}' to close namespace library") != std::string::npos);
}

// Found by feeding the parser damaged copies of the examples. Each one used to hang,
// because a block that the file ended inside never saw its closing brace.
TEST_CASE("a file that ends inside a block is an error, not a hang") {
    for (std::string_view source : {
             "screen \"\" / {\n\ttable shelf {\n",
             "screen \"\" / {\n\ttable t {}\n\tform k {\n",
             "view s per r {\n\teach t where r == r {\n",
             "view f {\n\teach k where s != n {\n",
         }) {
        CAPTURE(source);
        auto out = parse_errors(source);
        REQUIRE_FALSE(out.empty());
        CHECK(out[0].message == "expected '}', found the end of the file");
    }
}

TEST_CASE("a view's order goes inside the list it sorts") {
    auto out = parse_errors("view shelf {\n\torder title\n\teach book {\n\t\ttitle\n\t}\n}\n");
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "order goes inside the list it sorts: each book { order title ... }");
    CHECK(out[0].where.line == 2);
}

TEST_CASE("a view can hold lists with names, and a table names the one it shows") {
    auto f = parse_ok(R"(
view issue_page per issue {
	title = issue.title
	comments = each comment where issue == issue.id {
		order created_at
		author  body
	}
}
screen "Issue" /issues/:issue {
	table issue_page.comments {
		author
	}
}
)");
    const auto& page = std::get<view_declaration>(f.declarations[0].node);
    REQUIRE(page.values.size() == 1);
    REQUIRE(page.each.size() == 1);
    CHECK(page.each[0].name == "comments");
    CHECK(page.each[0].order.size() == 1);
    CHECK(page.each[0].rows.size() == 2);
    const auto& screen = std::get<screen_declaration>(f.declarations[1].node);
    const auto& table = std::get<table_item>(screen.items[0].node);
    CHECK(table.view.text() == "issue_page");
    CHECK(table.list == "comments");
}

TEST_CASE("a role held within something, and a command that creates another entity") {
    auto f = parse_ok(R"(
role maintainer per project from member  issue::create  issue::update
command project::create {
	create member {
		project = id  person = me
		role = maintainer
	}
}
)");
    const auto& role = std::get<role_declaration>(f.declarations[0].node);
    CHECK(role.per == "project");
    CHECK(role.from == "member");
    CHECK(role.permissions.size() == 2);
    const auto& command = std::get<command_declaration>(f.declarations[1].node);
    REQUIRE(command.body.size() == 1);
    const auto& made = std::get<create_statement>(command.body[0].node);
    CHECK(made.entity == "member");
    REQUIRE(made.values.size() == 3);
    CHECK(made.values[0].name == "project");
    CHECK(made.values[2].name == "role");
}

TEST_CASE("a view says who reads it") {
    auto f = parse_ok(R"(
view issue_page per issue {
	readers member
	readers issue.author
	readers issue.assignees
	public when issue.project.visibility == public
	title = issue.title
}
)");
    const auto& page = std::get<view_declaration>(f.declarations[0].node);
    CHECK(page.readers == "member");
    REQUIRE(page.reader_people.size() == 2);
    CHECK(std::get<member_expression>(page.reader_people[1]->node).member == "assignees");
    REQUIRE(page.public_when != nullptr);
    CHECK(std::holds_alternative<binary_expression>(page.public_when->node));
    CHECK(page.values.size() == 1);
}

TEST_CASE("lists: a field of several, add and remove, and has") {
    auto f = parse_ok(R"(
entity issue {
	labels  list of label
}
command issue::take {
	add me to assignees
	remove me from watchers
}
view mine per user {
	each issue where assignees has user.id
}
)");
    const auto& issue = std::get<entity_declaration>(f.declarations[0].node);
    CHECK(issue.fields[0].list);
    CHECK(issue.fields[0].type->text() == "label");
    const auto& take = std::get<command_declaration>(f.declarations[1].node);
    const auto& add = std::get<list_statement>(take.body[0].node);
    CHECK(add.adds);
    CHECK(add.list == "assignees");
    CHECK(!std::get<list_statement>(take.body[1].node).adds);
    const auto& mine = std::get<view_declaration>(f.declarations[2].node);
    CHECK(std::get<binary_expression>(mine.each[0].condition->node).op == token_kind::has);
}

TEST_CASE("history: an entity that keeps it, a list of its changes, and a limit") {
    auto f = parse_ok(R"(
entity issue history {
	title  text
}
view page per issue {
	changes = each change of issue where issue == issue.id {
		order -created_at
		limit 20
		field  after
	}
}
)");
    CHECK(std::get<entity_declaration>(f.declarations[0].node).history);
    const auto& changes = std::get<view_declaration>(f.declarations[1].node).each[0];
    CHECK(changes.changes);
    CHECK(changes.limit == 20);
    CHECK(changes.rows.size() == 2);
}

TEST_CASE("a webhook: who sends it, where, whose repository, and what each event makes") {
    auto f = parse_ok(R"(
webhook github /hooks/github {
	for project by repository
	on commit {
		create mention {
			issue = mentioned  url = url
		}
	}
	on pull_request {
	}
}
)");
    const auto& hook = std::get<webhook_declaration>(f.declarations[0].node);
    CHECK(hook.provider == "github");
    CHECK(hook.route == "/hooks/github");
    CHECK(hook.scope == "project");
    CHECK(hook.repository == "repository");
    REQUIRE(hook.handlers.size() == 2);
    CHECK(hook.handlers[0].event == "commit");
    CHECK(std::holds_alternative<create_statement>(hook.handlers[0].body[0].node));
    CHECK(hook.handlers[1].event == "pull_request");
}

TEST_CASE("a screen draws a hand-written component by name") {
    auto f = parse_ok("screen \"Editor\" /edit {\n\tcomponent workbench\n\tcomponent::create\n}\n");
    const auto& screen = std::get<screen_declaration>(f.declarations[0].node);
    REQUIRE(screen.items.size() == 2);
    CHECK(std::get<component_item>(screen.items[0].node).name == "workbench");
    CHECK(std::holds_alternative<button_item>(screen.items[1].node));
}

TEST_CASE("a link can open a namespace by name") {
    auto f = parse_ok("screen \"Home\" / {\n\tlink namespace projects::archive \"See the projects\"\n}\n");
    const auto& screen = std::get<screen_declaration>(f.declarations[0].node);
    const auto& link = std::get<content_link>(screen.items[0].node);
    CHECK(link.label == "See the projects");
    REQUIRE(link.namespace_name.has_value());
    CHECK(link.namespace_name->text() == "projects::archive");
    CHECK(link.target.empty());
}

TEST_CASE("a role's permissions can be a block, as many to a line as reads well") {
    auto f = parse_ok("role maintainer per project from member  project::update {\n\tissue::create  issue::close\n\n\tcomment::create\n}\n"
                      "role reporter per project from member  issue::create\n");
    const auto& maintainer = std::get<role_declaration>(f.declarations[0].node);
    CHECK(maintainer.per == "project");
    CHECK(maintainer.from == "member");
    REQUIRE(maintainer.permissions.size() == 4);
    CHECK(maintainer.permissions[0].text() == "project::update");
    CHECK(maintainer.permissions[3].text() == "comment::create");
    CHECK(std::get<role_declaration>(f.declarations[1].node).permissions.size() == 1);
    auto out = parse_errors("role maintainer {\n\t\"issue::create\"\n}\n");
    REQUIRE(!out.empty());
}

TEST_CASE("a field's choices are an enum, and say so") {
    auto out = parse_errors("entity issue {\n\tstatus  open | closed\n}\n");
    REQUIRE(!out.empty());
    CHECK(out[0].message == "write enum before a field's choices, like status enum open | ...");
    auto f = parse_ok("entity issue {\n\tstatus  enum open | closed = status::open\n\tkind  enum only\n}\n");
    const auto& entity = std::get<entity_declaration>(f.declarations[0].node);
    CHECK(entity.fields[0].choices == std::vector<std::string>{"open", "closed"});
    CHECK(entity.fields[1].choices == std::vector<std::string>{"only"});
}

TEST_CASE("a menu is a block of links") {
    auto f = parse_ok("screen \"Home\" / {\n\tmenu {\n\t\tlink /a \"A\"\n\t\tlink /b \"B\"\n\t}\n}\n");
    const auto& screen = std::get<screen_declaration>(f.declarations[0].node);
    const auto& block = std::get<content_block>(screen.items[0].node);
    CHECK(block.type == content_block::kind::menu);
    REQUIRE(block.items.size() == 2);
    CHECK(std::get<content_link>(block.items[1].node).label == "B");
}

TEST_CASE("a view's readers come from one entity") {
    auto out = parse_errors("view page per issue {\n\treaders member\n\treaders person\n}\n");
    REQUIRE(!out.empty());
    CHECK(out[0].message == "a view's readers come from one entity, like readers member; another readers line names people, like readers report.author");
}
