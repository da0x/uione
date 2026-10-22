// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// extend command: requires a project adds to a command another namespace declares,
// as a hosted edition limits what a free organization has in an app that says
// nothing about plans. They run wherever the command runs, and are checked where
// they're written.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    // The app, which knows nothing of plans.
    const std::string app = R"(import one

namespace projects {
	entity organization {
		slug  slug  required  unique  key
	}
	command organization::create {
		by anyone signed in
	}

	entity project {
		slug          slug  required  unique  key
		organization  organization
	}
	command project::create {
		by anyone signed in
		require slug != "about"  "that address is taken"
		dispatch board::create { project = id  name = "main" }
	}

	entity board {
		project  project  required  key
		name     slug     required  key
	}
	command board::create
}
)";

    // What the hosted edition adds.
    const std::string hosted = R"(namespace billing {
	entity account {
		organization  projects::organization  required  unique  key
		plan          text
	}
	command account::create

	extend command projects::project::create {
		require "a free organization has 3 projects" {
			organization == none || exists(billing::account where organization == project.organization && plan != none) || count(projects::project where organization == project.organization) < 3
		}
	}

	extend command projects::board::create {
		require project.organization == none || count(projects::board where project == board.project) < 5  "a free organization's projects have 5 boards each"
	}
}
)";

    std::vector<language::file> checked(const std::string& hosted_source, language::diagnostics& out) {
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", app, out));
        files.push_back(language::parse("hosted.one", hosted_source, out));
        language::check(files, out);
        return files;
    }

    std::string wrong(const std::string& from, const std::string& to) {
        std::string source = hosted;
        auto at = source.find(from);
        REQUIRE(at != std::string::npos);
        source.replace(at, from.size(), to);
        language::diagnostics out;
        checked(source, out);
        std::string said;
        for (const auto& d : out) said += d.path + ": " + d.message + "\n";
        return said;
    }

    bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

TEST_CASE("extend command adds requires to another namespace's command, after its own, wherever it runs") {
    language::diagnostics out;
    auto files = checked(hosted, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));

    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto go = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "projects/projects.go"; });
    REQUIRE(go != generated.files.end());
    CAPTURE(go->content);
    const std::string& code = go->content;
    // Asked of another namespace's entity, and counted as stored.
    CHECK(has(code, "\"app.local/api/billing\""));
    CHECK(has(code, "found, err := one.Exists[billing.Account](c, one.Where[billing.Account](\"organization\", p.Organization).Except(\"plan\", nil), nil)"));
    CHECK(has(code, "counted2, err := one.Count[Project](c, one.Where[Project](\"organization\", p.Organization))"));
    CHECK(has(code, "return c.Fail(\"a free organization has 3 projects\")"));
    // After the command's own require, before what it does.
    auto own = code.find("that address is taken");
    auto added = code.find("a free organization has 3 projects");
    auto dispatched = code.find("DispatchCreate(c, made");
    CHECK(own < added);
    CHECK(added < dispatched);
    // A board made by another command checks it too: it's in the body both run, and
    // reads the project it's in.
    CHECK(has(code, "func createBoard(c *one.Ctx, b *Board) error {"));
    CHECK(has(code, "project, err := one.Read[Project](c, b.Project)"));
    CHECK(has(code, "one.Count[Board](c, one.Where[Board](\"project\", b.Project))"));
    CHECK(has(code, "a free organization's projects have 5 boards each"));
}

TEST_CASE("extend command names a command that's there, adds only requires, and is checked where it's written") {
    CHECK(has(wrong("extend command projects::project::create", "extend command projects::project::launch"),
              "hosted.one: there's no command projects::project::launch to extend; name it with its namespace, like projects::project::create"));
    CHECK(has(wrong("\trequire project.organization == none", "\tdispatch account::create { organization = project.organization }\n\t\trequire project.organization == none"),
              "hosted.one: extend command adds requires to a command"));
    // Read as if written in the command, so its entity's fields are named plainly.
    CHECK(has(wrong("plan != none", "price != none"), "hosted.one: "));
    CHECK(!has(wrong("plan != none", "price != none"), "main.one: "));
}
