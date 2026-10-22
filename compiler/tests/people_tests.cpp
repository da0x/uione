// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A view of everyone who has signed up, for the app's own people: read a page at a
// time by a role given user::view, showing what's known of each person and how many
// of something they keep.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/web.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    const std::string office = R"(import one

namespace projects {
	entity project {
		slug  slug  required  unique  key
	}
	command project::create {
		by anyone signed in
	}
}

namespace staff {
	role clerk  user::view

	view people {
		each user {
			order by active descending
			limit 50
			name  email  joined  active  commands
			projects = keeps(projects::project)
		}
	}

	screen "People" /people {
		table people {
			name "Name"
			email "Email"
			joined "Joined"
			projects "Projects"
		}
	}
}
)";

    std::vector<language::file> checked(const std::string& source, language::diagnostics& out) {
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        return files;
    }

    std::string wrong(const std::string& from, const std::string& to) {
        std::string source = office;
        auto at = source.find(from);
        REQUIRE(at != std::string::npos);
        source.replace(at, from.size(), to);
        language::diagnostics out;
        checked(source, out);
        std::string said;
        for (const auto& d : out) said += d.message + "\n";
        return said;
    }

    bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

TEST_CASE("a clerk's list of everyone becomes the runtime's people, asked for a page at a time") {
    language::diagnostics out;
    auto files = checked(office, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));

    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto go = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "staff/staff.go"; });
    REQUIRE(go != generated.files.end());
    CAPTURE(go->content);
    CHECK(has(go->content, "var People = one.People(\"people\").Order(\"-active\").Fields(\"name\", \"email\", \"joined\", \"active\", \"commands\").\n"
                           "\tKeeps(\"projects\", one.Entity[projects.Project]()).\n"
                           "\tLimit(50)\n"));
    CHECK(has(go->content, "one.Role(\"clerk\", \"user:view\")"));

    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return has(f.content, "usePeople("); });
    REQUIRE(screen != web.end());
    CAPTURE(screen->content);
    CHECK(has(screen->content, "const people = usePeople(\"staff::people\");"));
    CHECK(has(screen->content, "<Table view={people} more={people.more}"));
    CHECK(!has(screen->content, "useView("));
}

TEST_CASE("a list of everyone is read by a role given user::view, and holds nothing else") {
    CHECK(has(wrong("\tview people {", "\tview people public {"),
              "view people lists everyone who has signed up, so it isn't public; a role given user::view reads it, like role staff  user::view"));
    CHECK(has(wrong("\t\teach user {\n", "\t\ttotal = count(projects::project)\n\t\teach user {\n"),
              "view people lists everyone who has signed up, and holds nothing else: view people { each user { ... } }"));
    CHECK(has(wrong("\t\teach user {\n", "\t\teach user where name == \"Ada\" {\n"), "each user lists everyone, so it doesn't pick some out with where or in"));
    CHECK(has(wrong("order by active descending", "order by name"),
              "each user is ordered by when they were last active or joined, like order by active descending"));
    CHECK(has(wrong("name  email  joined", "name  email  password  joined"),
              "a person's row shows name, picture, username, service, email, joined, active, commands, writes or bytes, "
              "or how many of something they keep, like projects = keeps(projects::project), not password"));
    CHECK(has(wrong("keeps(projects::project)", "keeps(projects::plan)"),
              "keeps counts the records of an entity each person made and still has, like keeps(projects::project)"));
    CHECK(has(wrong("command project::create {\n\t\tby anyone signed in", "command project::create {\n\t\tpermission user::view"),
              "user::view reads the lists of everyone who has signed up, so it's given to a role, like role staff  user::view"));
}
