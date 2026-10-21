// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A text field marked searchable keeps its words on its record, and a find box finds
// a list's rows by them, asking the server, though the rows don't hold the field.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/indexes.hpp"
#include "generators/web.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    const std::string wiki = R"(import one

namespace projects {
	entity project {
		slug        slug  required  unique  key
		visibility  text
	}
	entity page {
		project  project   required  key
		name     text      required  key = slug(title)
		title    text      required
		body     markdown  searchable
		closed   date
	}
	command project::create {
		by anyone signed in
	}
	command page::create {
		by anyone signed in
	}
	view project_page per project public {
		pages = each page in project where closed == none {
			name  title
		}
	}
	screen "Wiki" /:project {
		find "Search this project" {
			project_page.pages "Wiki" link /:project/:page by title body
		}
	}
	screen "Page" /:project/:page {
		text "A page."
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
        std::string source = wiki;
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

TEST_CASE("a page's body is searchable, and the find box searches it without the rows holding it") {
    language::diagnostics out;
    auto files = checked(wiki, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));

    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto go = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "projects/projects.go"; });
    REQUIRE(go != generated.files.end());
    CAPTURE(go->content);
    CHECK(has(go->content, "`firestore:\"body\" one:\"searchable\"`"));

    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return has(f.content, "<Find"); });
    REQUIRE(screen != web.end());
    CAPTURE(screen->content);
    CHECK(has(screen->content, R"(search: { view: "projects::project_page", subject: projectId }, by: ["title", "body"] })"));

    // The server asks for a project's pages holding a word, the newest first, but not
    // whether they're closed, which it checks on what comes back.
    auto indexes = generators::indexes_file(files).content;
    CAPTURE(indexes);
    CHECK(has(indexes, R"({ "collection": "projects_page", "fields": [{ "field": "project", "order": "ASCENDING" }, { "field": "search_words", "array": "CONTAINS" }, { "field": "created_at", "order": "DESCENDING" }] })"));
}

TEST_CASE("only text is searchable, and a find box finds by what its rows hold or what's searchable") {
    CHECK(has(wrong("title    text      required\n", "title    text      required  searchable\n\t\tsize     number    searchable\n"),
              "size holds no words to search; only a text or markdown field is searchable"));
    CHECK(has(wrong("body     markdown  searchable", "body     markdown"),
              "view project_page has no body in its pages to find by; add it to the list's block, or mark it searchable, like body  markdown  searchable"));
    CHECK(has(wrong("closed   date\n", "closed   date\n\t\tsearch_words  text\n"), "a page keeps the words of body in search_words, so no field of its own is called that"));
    CHECK(has(wrong("body     markdown  searchable", "body     markdown  searchble"), "'searchble' isn't a field rule; expected required, unique, key, searchable, after or from"));
}

TEST_CASE("a page that isn't found by its words needs no index") {
    std::string source = wiki;
    std::string from = "by title body";
    source.replace(source.find(from), from.size(), "by title");
    language::diagnostics out;
    auto files = checked(source, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    CHECK(!has(generators::indexes_file(files).content, "search_words"));
    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return has(f.content, "<Find"); });
    REQUIRE(screen != web.end());
    CHECK(!has(screen->content, "search:"));
}
