// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Targets: the apps one build writes from a project's screens. What's said, what each
// target draws, the errors when a screen uses what one doesn't, and --target.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "driver/build.hpp"
#include "generators/screens.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "language/targets.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string home = "namespace shop {\n\tscreen \"Home\" / {\n\t\ttext \"Hello\"\n\t\tsection \"More\" color violet {\n\t\t\ttext \"Inside\"\n\t\t}\n\t\tcomponent banner\n\t}\n}\n";

    std::string project_with(const std::string& targets) {
        return "import one\nproject shop {\n\tone  \"0.17.0\"\n" + (targets.empty() ? std::string() : "\ttarget  " + targets + "\n") + "}\n" + home;
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
    const std::string source = "import one\nproject shop {\n\tone  \"0.17.0\"\n\tsignin  google github\n}\n" + home;
    CHECK(messages(source).empty());
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", source, out));
    language::check(files, out);
    generators::screens::model m(files);
    CHECK(m.project.authentication == std::vector<std::string>{"google", "github"});
    CHECK(messages("import one\nproject shop {\n\tone  \"0.17.0\"\n\tsignin  google gitlab\n}\n" + home).size() == 1);
}

TEST_CASE("each element a target doesn't draw is one error for it, saying what it lacks and what to do") {
    // Every target draws everything now, so a table where the desktop can't draw a
    // section's color, as a new target can't draw much at first.
    std::vector<language::drawn_by> table(language::support.begin(), language::support.end());
    for (auto& s : table) {
        if (s.name == "section.color") s.by = "web terminal";
    }
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", project_with("web desktop"), out));
    language::check_targets(files, out, table);
    std::vector<std::string> said;
    for (const auto& d : out) said.push_back(std::to_string(d.where.line) + ": " + d.message);
    CAPTURE(said);
    CHECK(said == std::vector<std::string>{
                      "9: a section's color isn't drawn by the desktop app yet; take desktop out of target, or build with --target web",
                  });
    // The targets that do draw it are said as --target takes them, between commas.
    language::diagnostics three;
    files.clear();
    files.push_back(language::parse("main.one", project_with("web desktop terminal"), three));
    language::check_targets(files, three, table);
    REQUIRE(three.size() == 1);
    CHECK(three[0].message == "a section's color isn't drawn by the desktop app yet; take desktop out of target, or build with --target web,terminal");
    CHECK(messages(project_with("web desktop terminal")).empty());
    auto wrong = messages(project_with("web dektop"));
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0] == "4: target is web, desktop or terminal, written plainly, like target web");
}

TEST_CASE("the web draws everything a screen can say, and everything in the repository's screens is in the table") {
    for (const auto& s : language::support) {
        CAPTURE(s.name);
        CHECK(language::draws(s, "web"));
    }
    // Every element and setting used in uione's own projects is one the table knows.
    const std::string root = UIONE_ROOT;
    for (const auto* folder : {"/site", "/examples/gallery", "/examples/library", "/examples/tasks", "/examples/tracker"}) {
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

TEST_CASE("the gallery uses everything a screen can say, in every target, and what one can't draw yet is in its gaps") {
    const std::string folder = std::string(UIONE_ROOT) + "/examples/gallery";
    language::diagnostics out;
    std::vector<language::file> files;
    for (const auto& path : platform::find_one_files({folder})) files.push_back(language::parse(path, *platform::read_file(path), out));
    REQUIRE(!files.empty());
    language::check(files, out);
    // Nothing's wrong with it but what a target can't draw.
    for (const auto& d : out) {
        if (d.message.find(" drawn by the ") == std::string::npos) FAIL_CHECK(language::format(d));
    }
    // It's built for every target there is.
    auto targets = language::targets_of(files);
    for (const auto& t : language::all_targets) CHECK(language::listed(targets, t));
    // Every element, setting and kind of field in the table is in it somewhere.
    std::vector<language::targets_detail::used> used;
    for (const auto& f : files) language::targets_detail::declarations(f.declarations, used);
    for (const auto& s : language::support) {
        CAPTURE(s.name);
        CHECK(std::ranges::any_of(used, [&](const auto& u) { return u.name == s.name; }));
    }
    // What each target can't draw of it is just what its gaps say: a target and an
    // element a line, so the list only ever gets shorter, and a new target starts with
    // everything in it.
    std::set<std::string> lacking, listed;
    for (const auto& u : used) {
        for (const auto& t : language::all_targets) {
            if (const auto* s = language::support_of(u.name); s && !language::draws(*s, t)) lacking.insert(std::string(t) + " " + std::string(u.name));
        }
    }
    std::istringstream gaps(platform::read_file(folder + "/gaps").value_or(""));
    for (std::string line; std::getline(gaps, line);) {
        std::istringstream words(line);
        std::string target, element;
        if (!(words >> target) || target.starts_with("#") || !(words >> element)) continue;
        listed.insert(target + " " + element);
    }
    std::string said;
    for (const auto& l : lacking) said += l + "\n";
    CAPTURE(said);
    CHECK(lacking == listed);
}

TEST_CASE("a number in a range is a slider, a few choices are radio buttons, and themes are tried from a palette, in every app") {
    auto dir = std::filesystem::temp_directory_path() / "uione-controls";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "import one\nproject mixer {\n\tone  \"0.17.0\"\n\ttarget  web desktop terminal\n}\n"
                                                      "namespace mixer {\n\tentity channel {\n\t\tname    text    required\n"
                                                      "\t\tvolume  number  from 0 to 100  by 5 = 50\n\t\tside    enum    left | right\n\t}\n"
                                                      "\tcommand channel::create {\n\t\tby anyone\n\t}\n"
                                                      "\tscreen \"Mixer\" / {\n\t\tform channel::create {\n\t\t\tname  volume  side\n\t\t}\n\t\tthemes\n\t}\n}\n");
    auto built = driver::build(dir.string(), (dir / "build").string());
    for (const auto& d : built.problems) FAIL_CHECK(language::format(d));
    auto holding = [&](const std::string& words) {
        return std::ranges::count_if(built.files, [&](const auto& f) { return f.content.find(words) != std::string::npos; });
    };
    // The web's form, then the desktop's and the terminal's, the same screen in each.
    CHECK(holding("{ name: \"volume\", type: \"slider\", range: [0, 100, 5], start: \"50\" }") == 1);
    CHECK(holding("<Themes />") == 1);
    CHECK(holding("{.name = \"volume\", .type = \"slider\", .start = \"50\", .range = {0, 100, 5}}") == 2);
    CHECK(holding("themes(p)") == 2);
    // The backend holds the number to its range, starting at what it says.
    CHECK(holding("one:\"range=0:100:5,default=50\"") == 1);
    // What the checker says of a range that can't be.
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace a {\n\tentity b {\n\t\tc  number  from 10 to 0\n\t\td  text  from 0 to 5\n"
                                                "\t\te  number  from 0 to 10  by 3\n\t\tf  number  from 0 to 10 = 20\n\t}\n}\n", out));
    language::check(files, out);
    std::vector<std::string> said;
    for (const auto& d : out) said.push_back(d.message);
    CHECK(said == std::vector<std::string>{
                      "c goes from less to more, like from 0 to 10",
                      "d is from one number to another, so it's a number, like d  number  from 0 to 5",
                      "from 0 in steps of 3 doesn't land on 10; make the steps divide e's range",
                      "f starts within its range, from 0 to 10",
                  });
}
