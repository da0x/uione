// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The screen model: what a project's screens mean, which the web, desktop and
// terminal generators each write in their own way. Held here to the tracker example
// and to a small shop, as plain data; that the web app's output is unchanged by it
// is held by site/target/web.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/screens.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;
using generators::screens::model;

namespace {

    std::vector<language::file> tracker() {
        const std::string root = UIONE_ROOT;
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({root + "/examples/tracker"})) files.push_back(language::parse(path, *platform::read_file(path), out));
        language::check(files, out);  // which makes what define role says
        REQUIRE(out.empty());
        return files;
    }

    const std::string shop = R"(import one
project shop {
	one  "0.14.2"
}
namespace shop {
	entity shelf {
		owner  text  required  key
		name   text  required  key
	}
	entity book {
		shelf  shelf  required
		title  text   required
	}
	view shelf_page per shelf {
		name
	}
	view book_page per book {
		title  shelf  shelf.name
		owner = book.shelf.owner
	}
	screen "Shelves" /shelves {
		text "Every shelf"
	}
	screen "{shelf_page.name}" /shelves/:owner/:shelf {
		text "A shelf"
	}
	screen "#{book_page.title}" /books/:book {
		under /shelves/:owner/:shelf "{book_page.shelf.name}"
		text "A book"
	}
}
)";

    std::vector<language::file> parsed(const std::string& source) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        CAPTURE(out.empty() ? std::string() : out.front().message);
        REQUIRE(out.empty());
        return files;
    }

    const language::screen_declaration& screen_at(const model& m, const std::string& route) {
        auto found = std::find_if(m.every_screen.begin(), m.every_screen.end(), [&](const auto& s) { return m.full_route(s.ns, s.screen->route) == route; });
        REQUIRE(found != m.every_screen.end());
        return *found->screen;
    }

}  // namespace

TEST_CASE("the model knows the project, every screen, and where each namespace's screens are") {
    auto files = tracker();
    model m(files);
    CHECK(m.project.name == "app");  // it has no project block
    CHECK(!m.project.has_project);
    CHECK(m.prefix_of("tracker") == "/tracker");
    CHECK(m.full_route("tracker", "/projects/:project") == "/tracker/projects/:project");
    CHECK(m.every_screen.size() == 5);
    CHECK(m.screens_in(files[0]).size() == 5);
    CHECK(m.per_entity("tracker::project_page") == "project");
    CHECK(!m.per_entity("tracker::mine").has_value());  // one per person, which no address names
    const auto* issue = m.entity_of_command("tracker::issue::create");
    CHECK((issue && issue->name == "issue"));
}

TEST_CASE("a title's live values are read from views, and the rest is as written") {
    auto files = tracker();
    model m(files);
    auto parts = m.title_parts("tracker", "#{issue_page.number} {issue_page.title} {plain}");
    REQUIRE(parts.size() == 6);
    CHECK(parts[0].text == "#");
    CHECK((parts[1].view == "tracker::issue_page" && parts[1].field == "number"));
    CHECK(parts[2].text == " ");
    CHECK(parts[3].field == "title");
    CHECK(parts[4].text == " ");
    CHECK((!parts[5].live() && parts[5].text == "{plain}"));
}

TEST_CASE("a screen's trail is the pages its address goes on from, and the page it says it's under") {
    auto files = tracker();
    model m(files);
    auto trail = m.crumbs("tracker", screen_at(m, "/tracker/projects/:project/settings"), "/tracker/projects/:project/settings");
    REQUIRE(trail.size() == 2);
    CHECK(trail[0].to == "/tracker/projects");
    CHECK(trail[1].to == "/tracker/projects/:project");
    CHECK(trail[1].title.front().text == "Project");

    auto shop_files = parsed(shop);
    model s(shop_files);
    auto book = s.crumbs("shop", screen_at(s, "/shop/books/:book"), "/shop/books/:book");
    REQUIRE(book.size() == 2);
    CHECK(book[0].to == "/shop/shelves");
    // The shelf, which the book's address doesn't name: called by the book's view,
    // and found by it, by the shelf's own part, since a shelf is keyed by two.
    CHECK(book[1].to == "/shop/shelves/:owner/:shelf");
    CHECK(book[1].depth >= 1000);
    REQUIRE(book[1].title.size() == 1);
    CHECK((book[1].title[0].view == "shop::book_page" && book[1].title[0].field == "shelf.name"));
    REQUIRE(book[1].fill.size() == 2);
    CHECK(book[1].fill[0].parameter == "owner");
    CHECK(book[1].fill[0].keys == 1);
    CHECK(book[1].fill[1].parameter == "shelf");
    CHECK(book[1].fill[1].view == "shop::book_page");
    CHECK(book[1].fill[1].keys == 2);
}

TEST_CASE("an address names what's keyed by several parts by them, and a link fills the others from its row") {
    auto files = parsed(shop);
    model m(files);
    CHECK(m.keyed_by("shop", "shelf", "/shop/shelves/:owner/:shelf") == std::vector<std::string>{"owner"});
    CHECK(m.keyed_by("shop", "shelf", "/shop/shelves/:shelf").empty());  // by its whole id
    CHECK(m.keyed_by("shop", "book", "/shop/books/:book").empty());
    auto named = m.named_by("shop", "/shop/shelves/:shelf/books/:book");
    REQUIRE(named.size() == 1);
    CHECK(named[0] == std::pair<std::string, int>{"shelf", 2});
}

TEST_CASE("a project's own roles say who may run its commands, within the place the address names") {
    auto files = tracker();
    model m(files);
    auto may = m.allowed("tracker", "/tracker/projects/:project", "tracker::issue::create");
    REQUIRE(may.has_value());
    CHECK(may->by_record());
    CHECK(may->view == "tracker::member_roles");
    CHECK(may->within == "project");
    CHECK(may->command == "issue::create");
    CHECK(may->allows == "role.may");
    // Nowhere to hold them: anyone the command lets in.
    CHECK(!m.allowed("tracker", "/tracker/projects", "tracker::issue::create").has_value());
}

TEST_CASE("columns are shown by a person's name and as the field is called, and actions are the rows' commands") {
    auto files = tracker();
    model m(files);
    const auto* issue = m.listed("tracker", "project_page", "issues");
    REQUIRE(issue != nullptr);
    CHECK(issue->name == "issue");
    auto author = m.shown_as("tracker", "project_page", "issues", issue, "author", std::nullopt);
    CHECK(author.key == "author.name");
    CHECK(author.picture == "author.picture");
    CHECK(author.label == "Author");
    CHECK(m.shown_as("tracker", "project_page", "issues", issue, "number", "#").label == "#");
    CHECK(m.row_command("tracker", "project_page", "issues", "take") == "tracker::issue::take");
    CHECK(!m.row_command("tracker", "project_page", "issues", "title").has_value());
    CHECK(m.listed_columns("tracker", "project_page", "roles") == std::vector<std::string>{"name", "title"});
}

TEST_CASE("a form's fields have their kinds, and their choices from the field or a list on the screen") {
    auto files = tracker();
    model m(files);
    const language::form_item* member = nullptr;
    const language::form_item* project = nullptr;
    for (const auto& item : screen_at(m, "/tracker/projects/:project").items) {
        if (auto* form = std::get_if<language::form_item>(&item.node)) {
            if (form->commands.front().text() == "member::create") member = form;
            if (form->commands.front().text() == "project::update") project = form;
        }
    }
    REQUIRE((member && project));
    auto fields = m.form_fields("tracker", "/tracker/projects/:project", *member);
    REQUIRE(fields.size() == 2);
    CHECK(fields[0].type == "text");  // a person, though nobody's listed to pick from here
    CHECK(fields[1].type == "pick");
    REQUIRE(fields[1].from.has_value());
    CHECK(fields[1].from->view == "tracker::project_page");
    CHECK(fields[1].from->list == "roles");
    CHECK(fields[1].from->shown == "title");
    CHECK(fields[1].from->within == "project");

    auto visibility = m.form_fields("tracker", "/tracker/projects/:project", *project)[2];
    CHECK(visibility.type == "choice");
    REQUIRE(visibility.options.has_value());
    CHECK(visibility.options->front() == std::pair<std::string, std::string>{"public", "Public"});
    CHECK(visibility.start == "public");
    CHECK(!visibility.from.has_value());
}
