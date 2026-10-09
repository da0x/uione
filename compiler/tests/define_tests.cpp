// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a name means where it's written, for an editor's hover and its going to a
// definition: a field, through a reference or with its entity's name, an entity,
// a type, a choice, and the language's own words.

#include <doctest/doctest.h>

#include <string>

#include "driver.hpp"

using namespace one;

namespace {

    const driver::sources tracker{
        {"projects.one", "namespace tracker {\n"
                         "\tentity project {\n"
                         "\t\tname           text     required\n"
                         "\t\ttakes_reports  boolean  = false\n"
                         "\t}\n"
                         "}\n"},
        {"reports.one", "namespace tracker {\n"
                        "\tentity report {\n"
                        "\t\tproject  project  required\n"
                        "\t\tstatus   enum     open | resolved = status::open\n"
                        "\t}\n"
                        "\tcommand report::create {\n"
                        "\t\tby anyone signed in\n"
                        "\t\trequire report.project.takes_reports  \"this project doesn't take reports\"\n"
                        "\t}\n"
                        "\tcommand report::resolve {\n"
                        "\t\trequire status == status::open  \"that report is already resolved\"\n"
                        "\t\tstatus = status::resolved\n"
                        "\t}\n"
                        "}\n"},
    };

    // The column of the nth (from 0) `word` on a line of reports.one, counted from 1.
    int column_of(int line, const std::string& word, int nth = 0) {
        const std::string& text = tracker.at("reports.one");
        std::size_t start = 0;
        for (int i = 1; i < line; ++i) start = text.find('\n', start) + 1;
        std::string row = text.substr(start, text.find('\n', start) - start);
        std::size_t at = row.find(word);
        while (nth-- > 0) at = row.find(word, at + 1);
        return static_cast<int>(at) + 1;
    }

    driver::definition at(int line, const std::string& word, int nth = 0) {
        return driver::define(tracker, "reports.one", line, column_of(line, word, nth));
    }

} // namespace

TEST_CASE("a field's type names the entity, which is declared in another file") {
    auto d = at(3, "project", 1);
    REQUIRE(d.found);
    CHECK(d.says == "entity project");
    CHECK(d.path == "projects.one");
    CHECK(d.line == 2);
}

TEST_CASE("a command's own name names its entity") {
    auto d = at(6, "report");
    REQUIRE(d.found);
    CHECK(d.says == "entity report");
    CHECK(d.path == "reports.one");
}

TEST_CASE("the entity's name, its field, and a field read through it, each mean what they are") {
    auto entity = at(8, "report");
    REQUIRE(entity.found);
    CHECK(entity.says == "entity report");
    auto own = at(8, "project");
    REQUIRE(own.found);
    CHECK(own.says == "field project of report, a project");
    CHECK(own.path == "reports.one");
    CHECK(own.line == 3);
    auto through = at(8, "takes_reports");
    REQUIRE(through.found);
    CHECK(through.says == "field takes_reports of project, a boolean");
    CHECK(through.path == "projects.one");
    CHECK(through.line == 4);
    // Anywhere in the name, not only its first letter.
    CHECK(driver::define(tracker, "reports.one", 8, column_of(8, "takes_reports") + 5).says == through.says);
}

TEST_CASE("the language's own words say what they are, and where the reference says so") {
    auto permission = at(7, "anyone");
    REQUIRE(permission.found);
    CHECK(permission.says == "by anyone signed in: anyone signed in, any way the project offers");
    CHECK(permission.path.empty());
    CHECK(permission.section == "command");
    auto type = at(3, "required");
    CHECK_FALSE(type.found);  // a rule, which isn't a name
}

TEST_CASE("a field, and a choice of its enum, by name") {
    auto status = at(11, "status");
    REQUIRE(status.found);
    CHECK(status.says == "field status of report, status::open or status::resolved");
    auto choice = at(11, "status::open");
    REQUIRE(choice.found);
    CHECK(choice.says == "choice open of status, in report");
    CHECK(choice.line == 4);
}

TEST_CASE("nothing is said where there's no name, or the project doesn't parse") {
    CHECK_FALSE(driver::define(tracker, "reports.one", 1, 1).found);  // namespace, a keyword
    driver::sources broken = tracker;
    broken["reports.one"] += "entity {\n";
    CHECK_FALSE(driver::define(broken, "reports.one", 8, column_of(8, "project")).found);
}
