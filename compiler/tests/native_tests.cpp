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
#include <filesystem>

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
                                 "src/screens/releases.cpp", "src/screens/mission.cpp", "src/screens/install.cpp", "src/screens/about.cpp", "src/screens/themes.cpp"}) {
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

TEST_CASE("a monitor and charts are written for the desktop and the terminal alike") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-native-monitor";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work at / {\n"
                         "entity project {\n\tslug  text  required  unique  key\n\tname  text\n}\n"
                         "enum agent_status {\n\tworking \"Working\"\n\tidle \"Idle\"\n}\n"
                         "entity agent {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n\tstatus  agent_status\n\tsince  date\n"
                         "\tdoing  text\n\tstep  number\n\tsteps  number\n\tseen_at  date\n\tstats  data\n}\n"
                         "view agents per project {\n\tname\n\tagents = each agent in project {\n\t\ttitle  status  since  doing  step  steps  seen_at  stats\n\t}\n}\n"
                         "view agent_page per agent {\n\ttitle\n\tstats\n}\n"
                         "screen \"Agents\" /:project/agents {\n\tmonitor agents.agents link /:project/agents/:agent {\n\t\tstatus status\n\t\tsince since\n"
                         "\t\tdoing doing\n\t\tprogress step of steps\n\t\tseen seen_at 30\n\t\tchart stats.cpu \"CPU\" percent\n\t}\n}\n"
                         "screen \"Agent\" /:project/agents/:agent {\n\tchart agent_page.stats.cpu \"CPU\" percent\n"
                         "\tcharts agent_page {\n\t\tstats.memory \"Memory\" bytes\n\t\tstats.sent \"Out\" rate\n\t}\n}\n"
                         "}\n");
    language::diagnostics out;
    std::vector<language::file> parsed;
    for (const auto& path : platform::find_one_files({dir.string()})) parsed.push_back(language::parse(path, *platform::read_file(path), out));
    language::check(parsed, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto files = generators::generate_native(parsed, dir.string(), "terminal");
    const auto* screens = find(files, "src/screens/main.cpp");
    REQUIRE(screens != nullptr);
    const auto& cpp = screens->content;
    CHECK(cpp.find(R"(monitor(p, {.view = agents, .list = "agents", .title = "title", .status = "status", .doing = "doing", .since = "since", .step = "step", )"
                   R"(.steps = "steps", .seen = "seen_at", .seen_seconds = 30, .charts = {chart_line{"stats.cpu", "CPU", "percent"}}, .link = "/:project/agents/:agent")") !=
          std::string::npos);
    CHECK(cpp.find(R"(chart(p, {.view = agent_page, .series = {chart_line{"stats.memory", "Memory", "bytes"}, chart_line{"stats.sent", "Out", "rate"}}}))") != std::string::npos);
    fs::remove_all(dir);
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

// Installing a desktop app lists it among the system's apps, with its icon, and its
// project's license beside it, found above the project too; without one, it isn't
// installed or packaged.
TEST_CASE("a desktop app is installed and packaged with its .desktop file, icon and license") {
    namespace fs = std::filesystem;
    fs::path repository = fs::temp_directory_path() / "uione-packaged";
    fs::remove_all(repository);
    fs::create_directories(repository / ".git");
    fs::create_directories(repository / "app" / "assets");
    platform::write_file((repository / "LICENSE").string(), "The license.\n");
    platform::write_file((repository / "app" / "assets" / "icon.svg").string(), "<svg/>\n");
    platform::write_file((repository / "app" / "main.one").string(), "project notes {\n\ttarget desktop\n\ticon \"assets/icon.svg\"\n}\nnamespace notes {\n\tscreen \"Notes\" / {\n\t\ttext \"Hello\"\n\t}\n}\n");
    std::vector<language::file> files;
    language::diagnostics out;
    files.push_back(language::parse((repository / "app" / "main.one").string(), *platform::read_file((repository / "app" / "main.one").string()), out));
    auto generated = generators::generate_native(files, (repository / "app").string(), "desktop");
    const auto* license = find(generated, "packaging/LICENSE");
    const auto* entry = find(generated, "packaging/org.uione.notes.desktop");
    const auto* cmake = find(generated, "CMakeLists.txt");
    REQUIRE(license != nullptr);
    REQUIRE(entry != nullptr);
    REQUIRE(cmake != nullptr);
    CHECK(license->content == "The license.\n");
    CHECK(find(generated, "packaging/org.uione.notes.svg") != nullptr);
    CHECK(entry->content.find("Exec=notes-desktop %u") != std::string::npos);
    CHECK(entry->content.find("Icon=org.uione.notes") != std::string::npos);
    CHECK(cmake->content.find("install(PROGRAMS $<TARGET_FILE:notes_desktop> DESTINATION bin RENAME notes-desktop)") != std::string::npos);
    CHECK(cmake->content.find("install(FILES packaging/LICENSE DESTINATION share/licenses/notes-desktop)") != std::string::npos);
    CHECK(cmake->content.find("set(CPACK_PACKAGE_FILE_NAME \"notes-desktop-${VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}\")") != std::string::npos);
    fs::remove(repository / "LICENSE");
    auto unlicensed = generators::generate_native(files, (repository / "app").string(), "desktop");
    CHECK(find(unlicensed, "packaging/LICENSE") == nullptr);
    fs::remove_all(repository);
}

TEST_CASE("a native component's fonts come along with it, byte for byte, to be embedded, and nothing else does") {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(root) / "compiler" / "build" / "font-component-project";
    fs::remove_all(dir);
    fs::create_directories(dir / "components" / "fonts");
    REQUIRE(platform::write_file((dir / "main.one").string(), "screen \"Editor\" /edit {\n\tcomponent workbench\n}\n"));
    REQUIRE(platform::write_file((dir / "components" / "workbench.cpp").string(), "// draws in the font beside it\n"));
    const std::string font("\0\1\0\0 a font\n\xff", 13);
    REQUIRE(platform::write_file((dir / "components" / "fonts" / "Logo.ttf").string(), font));
    REQUIRE(platform::write_file((dir / "components" / "notes.txt").string(), "not code\n"));
    language::diagnostics out;
    std::vector<language::file> files;
    for (const auto& path : platform::find_one_files({dir.string()})) files.push_back(language::parse(path, *platform::read_file(path), out));
    language::check(files, out);
    auto generated = generators::generate_native(files, dir.string(), "desktop");
    fs::remove_all(dir);
    const auto* copied = find(generated, "src/components/fonts/Logo.ttf");
    REQUIRE(copied != nullptr);
    CHECK(copied->content == font);
    CHECK(find(generated, "src/components/notes.txt") == nullptr);
    const auto* cmake = find(generated, "CMakeLists.txt");
    REQUIRE(cmake != nullptr);
    CHECK(cmake->content.find("Logo.ttf") == std::string::npos);
}
