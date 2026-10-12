// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Targets: the apps one build writes from a project's screens. What's said, what each
// target draws, the errors when a screen uses what one doesn't, and --target.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "driver.hpp"
#include "generators/screens.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "language/targets.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string home = "namespace shop {\n\tscreen \"Home\" / {\n\t\ttext \"Hello\"\n\t\tsection \"More\" color violet {\n\t\t\ttext \"Inside\"\n\t\t}\n\t\tcomponent banner\n\t}\n}\n";

    std::string project_with(const std::string& targets) {
        return "import one\nproject shop {\n\tone  \"0.9.8\"\n" + (targets.empty() ? std::string() : "\ttarget  " + targets + "\n") + "}\n" + home;
    }

    std::vector<std::string> messages(const std::string& source) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        std::vector<std::string> said;
        // The screen's component, drawn by React alone, has no file beside it here.
        for (const auto& d : out) {
            if (!d.message.ends_with("which isn't there")) said.push_back(std::to_string(d.where.line) + ": " + d.message);
        }
        return said;
    }

}  // namespace

TEST_CASE("a project builds the web alone unless its targets say more, several to a line") {
    language::diagnostics out;
    auto file = language::parse("main.one", project_with("web desktop terminal"), out);
    REQUIRE(out.empty());
    std::vector<language::file> files;
    files.push_back(std::move(file));
    CHECK(language::targets_of(files) == std::vector<std::string>{"web", "desktop", "terminal"});
    std::vector<language::file> plain;
    plain.push_back(language::parse("main.one", project_with(""), out));
    CHECK(language::targets_of(plain) == std::vector<std::string>{"web"});
    CHECK(messages(project_with("")).empty());
    CHECK(messages(project_with("web")).empty());
}

TEST_CASE("the ways people sign in are said on one line, in their order") {
    const std::string source = "import one\nproject shop {\n\tone  \"0.9.8\"\n\tsignin  google github\n}\n" + home;
    CHECK(messages(source).empty());
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", source, out));
    language::check(files, out);
    generators::screens::model m(files);
    CHECK(m.project.authentication == std::vector<std::string>{"google", "github"});
    CHECK(messages("import one\nproject shop {\n\tone  \"0.9.8\"\n\tsignin  google gitlab\n}\n" + home).size() == 1);
}

TEST_CASE("each element a target doesn't draw is one error for it, saying what it lacks and what to do") {
    auto said = messages(project_with("web desktop"));
    CAPTURE(said);
    CHECK(said == std::vector<std::string>{
                      "12: a component isn't drawn by the desktop app yet; take desktop out of target, or build with --target web",
                  });
    auto wrong = messages(project_with("web dektop"));
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0] == "4: target is web, desktop or terminal, written plainly, like target web");
}

TEST_CASE("the web draws everything a screen can say, and everything in the repository's screens is in the table") {
    for (const auto& s : language::support) {
        CAPTURE(s.name);
        CHECK(s.web);
    }
    // Every element and setting used in uione's own projects is one the table knows.
    const std::string root = UIONE_ROOT;
    for (const auto* folder : {"/site", "/examples/library", "/examples/tasks", "/examples/tracker"}) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({root + folder})) files.push_back(language::parse(path, *platform::read_file(path), out));
        std::vector<language::targets_detail::used> used;
        for (const auto& f : files) language::targets_detail::declarations(f.declarations, used);
        CHECK(!used.empty());
        for (const auto& u : used) {
            CAPTURE(u.name);
            CHECK(language::support_of(u.name) != nullptr);
        }
    }
}

TEST_CASE("one build --target builds fewer of a project's targets, never others") {
    auto dir = std::filesystem::temp_directory_path() / "uione-targets";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    platform::write_file((dir / "main.one").string(), project_with("web desktop"));
    std::filesystem::create_directories(dir / "components");
    platform::write_file((dir / "components" / "banner.tsx").string(), "export default function Banner() { return null; }\n");
    auto all = driver::build(dir.string(), (dir / "build").string());
    CHECK(all.refusal == "nothing was built, because the project has errors");
    auto web = driver::build(dir.string(), (dir / "build").string(), "", std::vector<std::string>{"web"});
    CHECK(web.refusal.empty());
    CHECK(std::ranges::any_of(web.files, [](const auto& f) { return f.path.starts_with("web/"); }));
    auto other = driver::build(dir.string(), (dir / "build").string(), "", std::vector<std::string>{"web", "terminal"});
    CHECK(other.refusal == "--target names terminal, which this project doesn't build; its targets are web desktop");
    // Nothing of --target is left behind for the next build.
    CHECK(!language::building_targets().has_value());
}
