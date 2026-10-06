// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Every generated line says which .one line it came from, or that it's the same
// for every project, so `one show` can show what any line of uione turns into.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/project.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    generators::generated_project generate(const std::string& project) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({root + project})) {
            files.push_back(language::parse(path, *platform::read_file(path), out));
        }
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return generators::generate_project(files, root + project, root + project + "/build");
    }

    // The lines of a file that have text but no source.
    std::vector<std::string> gaps(const generators::output_file& f) {
        std::vector<std::string> found;
        std::size_t at = 0, n = 0;
        while (at < f.content.size()) {
            std::size_t end = f.content.find('\n', at);
            std::string text = f.content.substr(at, end - at);
            if (!text.empty() && (n >= f.sources.size() || !f.sources[n])) {
                found.push_back(f.path + ":" + std::to_string(n + 1) + ": " + text);
            }
            at = end + 1;
            ++n;
        }
        return found;
    }

} // namespace

TEST_CASE("every generated line knows where it came from") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/library", "/examples/tracker"}) {
        auto generated = generate(project);
        REQUIRE(generated.errors.empty());
        for (const auto& f : generated.files) {
            CAPTURE(project);
            for (const auto& gap : gaps(f)) FAIL_CHECK(gap);
        }
    }
}

TEST_CASE("a command's lines lead to the Go they became") {
    auto generated = generate("/examples/library");
    const generators::output_file* go = nullptr;
    for (const auto& f : generated.files) {
        if (f.path == "api/library/library.go") go = &f;
    }
    REQUIRE(go != nullptr);
    // loan::create in main.one: lending takes the book off the shelf.
    // The line of main.one that says it, found rather than counted, so editing the
    // example elsewhere doesn't move it.
    auto source = *platform::read_file(root + "/examples/library/main.one");
    int lending = 1 + static_cast<int>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(source.find("book.status = status::lent")), '\n'));
    std::vector<std::string> from;
    std::size_t at = 0, n = 0;
    while (at < go->content.size()) {
        std::size_t end = go->content.find('\n', at);
        const auto& s = go->sources[n];
        if (s && !s->fixed() && s->path.ends_with("main.one") && s->line == lending) from.push_back(go->content.substr(at, end - at));
        at = end + 1;
        ++n;
    }
    CHECK(from == std::vector<std::string>{"\t\tbook.Status = StatusLent"});
}

TEST_CASE("a project whose roles are held within something gets a view per person of where they hold them") {
    auto built = generate("/examples/tracker");
    const generators::output_file* app = nullptr;
    const generators::output_file* api = nullptr;
    for (const auto& f : built.files) {
        if (f.path == "web/src/app.tsx") app = &f;
        if (f.path == "api/tracker/tracker.go") api = &f;
    }
    REQUIRE(app != nullptr);
    REQUIRE(api != nullptr);
    CHECK(app->content.find(R"(personal: ["tracker::mine", "tracker::member_roles"])") != std::string::npos);
    CHECK(api->content.find(R"(one.View("member_roles").PerUser().)") != std::string::npos);
}
