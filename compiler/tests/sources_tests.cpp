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
            // A last line without a newline, as an image's is, ends it.
            if (end == std::string::npos) break;
            at = end + 1;
            ++n;
        }
        return found;
    }

} // namespace

TEST_CASE("every generated line knows where it came from") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/library", "/examples/tracker", "/examples/print_shop"}) {
        auto generated = generate(project);
        REQUIRE(generated.errors.empty());
        for (const auto& f : generated.files) {
            CAPTURE(project);
            for (const auto& gap : gaps(f)) FAIL_CHECK(gap);
        }
    }
}

TEST_CASE("each agent is a program of its own, whose rules lead back to the lines saying them") {
    auto generated = generate("/examples/print_shop");
    REQUIRE(generated.errors.empty());
    auto found = std::ranges::find_if(generated.files, [](const auto& f) { return f.path == "agents/press/main.cpp"; });
    REQUIRE(found != generated.files.end());
    const auto& main = found->content;
    CAPTURE(main);
    CHECK(main.find(R"(press.rules.push_back({.queue = "shop::plate_work", .entity = "shop::plate", .along = "stage", .stage = "etching", )"
                    R"(.program = "tools/etch {plate.name}", .then = "etched", .otherwise = "failed", .lease_minutes = 1});)") != std::string::npos);
    CHECK(main.find(R"(.then = "printed", .otherwise = "failed", .lease_minutes = 10});)") != std::string::npos);
    CHECK(main.find("return one::client::work(app, loop, press);") != std::string::npos);
    // The line of the order's rule is the .one line saying it.
    auto source = *platform::read_file(root + "/examples/print_shop/main.one");
    int said = 0, at = 0;
    for (std::size_t i = 0, line = 1; i < source.size(); ++i) {
        if (source.compare(i, 22, "on order in printing  ") == 0) said = static_cast<int>(line);
        if (source[i] == '\n') ++line;
    }
    for (std::size_t i = 0, line = 0; i < main.size(); ++i) {
        if (main.compare(i, 40, R"(press.rules.push_back({.queue = "shop::o)") == 0) at = static_cast<int>(line);
        if (main[i] == '\n') ++line;
    }
    REQUIRE(said > 0);
    REQUIRE(found->sources[static_cast<std::size_t>(at)]);
    CHECK(found->sources[static_cast<std::size_t>(at)]->line == said);
    CHECK(std::ranges::any_of(generated.files, [](const auto& f) { return f.path == "agents/press/CMakeLists.txt"; }));
}

TEST_CASE("a board over stages written in the code has them as its columns, its workflow's steps a person takes, and chimes where it says") {
    language::diagnostics out;
    std::vector<language::file> files;
    for (const auto& path : platform::find_one_files({root + "/examples/print_shop"})) files.push_back(language::parse(path, *platform::read_file(path), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_project(files, root + "/examples/print_shop", root + "/examples/print_shop/build", language::targets_of(files));
    REQUIRE(generated.errors.empty());
    auto content = [&](const std::string& path) {
        auto found = std::ranges::find_if(generated.files, [&](const auto& f) { return f.path == path; });
        REQUIRE(found != generated.files.end());
        return found->content;
    };
    auto web = content("web/src/screens/main.tsx");
    CAPTURE(web);
    CHECK(web.find(R"(over={{ status: "live", data: { stages: [{ id: "to_do", title: "To do" }, { id: "etching", title: "Etching" }, { id: "etched", title: "Etched" }, )"
                   R"({ id: "failed", title: "Failed" }] } }} overList="stages" shown="title" statuses={{ etched: "done", failed: "failed" }} notify={["failed"]})") != std::string::npos);
    // Etching is the press's to move on, so a person's steps are only into it and back from Failed.
    CHECK(web.find(R"(steps: { status: "live", data: { steps: [{ from: "to_do", to: "etching" }, { from: "failed", to: "to_do" }] } }, list: "steps")") != std::string::npos);
    for (const char* target : {"desktop", "terminal"}) {
        auto native = content(std::string(target) + "/src/screens/main.cpp");
        CAPTURE(native);
        CHECK(native.find(R"(.over = state<view_state>(view_state{status::ready, ember::parse_json("{\"stages\":[{\"id\":\"to_do\",\"title\":\"To do\"},)") != std::string::npos);
        CHECK(native.find(R"(.notify = {"printed"}, .statuses = {{"printed", "done"}, {"failed", "failed"}})") != std::string::npos);
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
    int lending = 1 + static_cast<int>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(source.find("dispatch book::update { id = book  status = status::lent }")), '\n'));
    std::vector<std::string> from;
    std::size_t at = 0, n = 0;
    while (at < go->content.size()) {
        std::size_t end = go->content.find('\n', at);
        const auto& s = go->sources[n];
        if (s && !s->fixed() && s->path.ends_with("main.one") && s->line == lending) from.push_back(go->content.substr(at, end - at));
        at = end + 1;
        ++n;
    }
    CHECK(from == std::vector<std::string>{"\t\tif err := one.DispatchUpdate(c, l.Book, func(dispatched *Book) {", "\t\t\tdispatched.Status = StatusLent",
                                           "\t\t}, nil, nil); err != nil {", "\t\t\treturn err", "\t\t}"});
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
