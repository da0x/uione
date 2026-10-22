// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// changes slug: a command that changes a key given as it is, like a project's
// address, moving it and everything named by it to the new one.

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

    const std::string source = R"(import one

namespace projects {
	entity project {
		slug  text  required  unique  key
		name  text  required
	}
	command project::create {
		by anyone signed in
	}
	command project::move {
		changes slug
		require slug != "about"  "that address is one of the site's own pages"
	}

	define role maintainer "Maintainer" in project {
		project::move
		board::create
	}

	entity board {
		project  project  required  key
		name     text     required  key = slug(title)
		title    text     required
		number   serial   per project
	}
	command board::create

	view project_page per project {
		slug  name
	}

	screen "Settings" /:project/settings {
		form project::move "Change address" {
			slug "New address"
		}
	}
}
)";

    std::vector<language::file> checked(const std::string& text, language::diagnostics& out) {
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", text, out));
        language::check(files, out);
        return files;
    }

    std::string wrong(const std::string& from, const std::string& to) {
        std::string text = source;
        auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        language::diagnostics out;
        checked(text, out);
        std::string said;
        for (const auto& d : out) said += d.message + "\n";
        return said;
    }

    bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

TEST_CASE("a command that changes a project's address moves it, and its form asks for the new one") {
    language::diagnostics out;
    auto files = checked(source, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto go = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "projects/projects.go"; });
    REQUIRE(go != generated.files.end());
    CAPTURE(go->content);
    CHECK(has(go->content, "one.Command[Project](\"project::move\").Fields(\"slug\")."));
}

TEST_CASE("changes names a key only when it's given as it is") {
    CHECK(has(wrong("\tcommand board::create\n", "\tcommand board::create\n\tcommand board::shift {\n\t\tchanges project\n\t}\n"),
              "changes may name a key that's given as it is, like a project's slug, which moves it; project points at what it's within"));
    CHECK(has(wrong("\tcommand board::create\n", "\tcommand board::create\n\tcommand board::renumber {\n\t\tchanges number\n\t}\n"),
              "number is counted when it's made"));
    CHECK(has(wrong("\tcommand board::create\n", "\tcommand board::create\n\tcommand board::rename {\n\t\tchanges name\n\t}\n"),
              "name follows what it's made from"));
    // Without changes, a form can't ask for a key.
    CHECK(has(wrong("\t\tchanges slug\n", ""), "slug is part of what names it, its key, so it can't be changed"));
}
