// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The desktop and terminal generators are held to site/target/desktop and
// site/target/terminal, as the web generator is to site/target/web.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/native.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    std::vector<generators::output_file> generate(const std::string& project, const std::string& target) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({root + project})) {
            files.push_back(language::parse(path, *platform::read_file(path), out));
        }
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return generators::generate_native(files, root + project, target);
    }

    const generators::output_file* find(const std::vector<generators::output_file>& files, const std::string& path) {
        auto it = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.path == path; });
        return it == files.end() ? nullptr : &*it;
    }

    // A target without the license header it carries as a file in this repository.
    std::string without_license(const std::string& text) {
        if (!text.starts_with("// Copyright") && !text.starts_with("# Copyright")) return text;
        auto end = text.find("\n\n");
        return end == std::string::npos ? text : text.substr(end + 2);
    }

    std::size_t lines(const std::string& text) { return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')); }

}  // namespace

TEST_CASE("the site's desktop and terminal apps are generated exactly as their targets say") {
    for (const char* target : {"desktop", "terminal"}) {
        auto files = generate("/site", target);
        for (const char* path : {"CMakeLists.txt", "src/app.hpp", "src/main.cpp", "src/site.cpp", "src/screens/home.cpp", "src/screens/language.cpp",
                                 "src/screens/releases.cpp", "src/screens/mission.cpp", "src/screens/install.cpp", "src/screens/about.cpp"}) {
            CAPTURE(target);
            CAPTURE(path);
            auto expected = platform::read_file(root + "/site/target/" + target + "/" + path);
            REQUIRE(expected);
            const auto* generated = find(files, path);
            REQUIRE(generated != nullptr);
            CHECK(generated->content == without_license(*expected));
        }
    }
}

TEST_CASE("a native app's screens are the same C++ for the desktop and the terminal") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/tracker"}) {
        auto desktop = generate(project, "desktop");
        auto terminal = generate(project, "terminal");
        for (const auto& f : desktop) {
            if (!f.path.starts_with("src/screens/") && f.path != "src/site.cpp") continue;
            CAPTURE(project);
            CAPTURE(f.path);
            const auto* other = find(terminal, f.path);
            REQUIRE(other != nullptr);
            CHECK(other->content == f.content);
        }
    }
}

TEST_CASE("a generated native screen file has about as many lines as the .one it came from") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/tracker"}) {
        for (const auto& f : generate(project, "desktop")) {
            if (!f.path.starts_with("src/screens/")) continue;
            CAPTURE(project);
            CAPTURE(f.path);
            std::string stem = f.path.substr(std::string("src/screens/").size());
            stem = stem.substr(0, stem.rfind('.'));
            auto source = platform::read_file(root + project + "/" + stem + ".one");
            REQUIRE(source);
            CHECK(lines(f.content) <= lines(*source) + 10);
        }
    }
}

TEST_CASE("every line of a native app knows where it came from") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/tracker"}) {
        for (const auto& f : generate(project, "terminal")) {
            std::size_t at = 0, n = 0;
            while (at < f.content.size()) {
                std::size_t end = f.content.find('\n', at);
                std::string text = f.content.substr(at, end - at);
                CAPTURE(project);
                CAPTURE(f.path);
                CAPTURE(text);
                if (!text.empty()) CHECK((n < f.sources.size() && f.sources[n]));
                at = end + 1;
                ++n;
            }
        }
    }
}

TEST_CASE("a row's when and what roles allow are written as C++") {
    auto files = generate("/examples/tracker", "desktop");
    const auto* screens = find(files, "src/screens/main.cpp");
    REQUIRE(screens != nullptr);
    // A row's own when, of the row and the person reading.
    CHECK(screens->content.find(R"(.when = [&p](const value& row) { return !same(field_of(row, "person"), me(p)); })") != std::string::npos);
    // What a project's roles allow, read from the person's roles in the project the address names.
    CHECK(screens->content.find(R"(.allowed = allows(member_roles, "project", project_id, "issue::create", "role.may"))") != std::string::npos);
}
