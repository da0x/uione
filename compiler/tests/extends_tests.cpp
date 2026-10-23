// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A project that extends another: an open-source app, and a hosted edition built on
// it from another folder, checked and built as one project. What it adds is its own;
// what the other says, it can't change.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "driver/check.hpp"
#include "platform/files.hpp"
#include "version.hpp"

using namespace one;
using driver::check_project;

namespace {

    namespace fs = std::filesystem;

    const fs::path place = fs::temp_directory_path() / "uione-extends";

    // A project's folder beside the others, with its main.one and any other files.
    fs::path folder(const std::string& name, const std::string& main, const std::vector<std::pair<std::string, std::string>>& more = {}) {
        fs::path dir = place / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        platform::write_file((dir / "main.one").string(), main);
        for (const auto& [path, text] : more) {
            fs::create_directories((dir / path).parent_path());
            platform::write_file((dir / path).string(), text);
        }
        return dir;
    }

    std::string pinned() { return "\tone  \"" + std::string(version) + "\"\n"; }

    // The open-source app: a widget, its screen, an icon and where it runs.
    fs::path base(const std::string& extra = "") {
        return folder("base",
                      "import one\nproject base {\n" + pinned() + extra +
                          "\ttarget  web\n\ticon    \"assets/icon.svg\"\n"
                          "\tenvironment production {\n\t\tdomain    \"base.example\"\n\t\tfirebase  \"base\"\n\t\tregion    \"us-east4\"\n\t}\n}\n"
                          "namespace base {\n\tentity widget {\n\t\tname  text  required\n\t}\n\tcommand widget::create\n"
                          "\tscreen \"Widgets\" /widgets {\n\t\ttext \"Every widget\"\n\t}\n}\n",
                      {{"assets/icon.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"/>"}});
    }

    std::string errors_of(const language::diagnostics& found) {
        std::string out;
        for (const auto& d : found) out += language::format(d) + "\n";
        return out;
    }

    const language::project_declaration* project_of(const std::vector<language::file>& files, int* count = nullptr) {
        const language::project_declaration* found = nullptr;
        int n = 0;
        for (const auto& f : files) {
            for (const auto& d : f.declarations) {
                if (auto* p = std::get_if<language::project_declaration>(&d.node)) found = p, ++n;
            }
        }
        if (count) *count = n;
        return found;
    }

    const language::setting* setting_of(const language::project_declaration& p, const std::string& key) {
        auto it = std::find_if(p.settings.begin(), p.settings.end(), [&](const auto& s) { return s.key == key; });
        return it == p.settings.end() ? nullptr : &*it;
    }

} // namespace

TEST_CASE("a project that extends another is checked with the other's files, and takes the settings it doesn't say") {
    base();
    auto cloud = folder("cloud", "import one\nproject cloud {\n" + pinned() +
                                     "\textends base\n}\n"
                                     "namespace cloud {\n\tentity plan {\n\t\tname  text  required\n\t}\n"
                                     "\tscreen \"Plans\" /plans {\n\t\ttext \"Every plan\"\n\t}\n}\n"
                                     // It adds to a namespace the other declares, too.
                                     "namespace base {\n\tscreen \"Billing\" /billing {\n\t\ttext \"What it costs\"\n\t}\n}\n");
    language::diagnostics found;
    std::vector<language::file> files;
    check_project(cloud.string(), found, &files);
    CAPTURE(errors_of(found));
    CHECK(found.empty());
    int projects = 0;
    const auto* p = project_of(files, &projects);
    REQUIRE(p != nullptr);
    CHECK(projects == 1);  // the other's project block is folded into this one's
    CHECK(p->name == "cloud");
    REQUIRE(setting_of(*p, "target"));
    CHECK(setting_of(*p, "target")->value == "web");
    // A file the other names is found where it is, beside the other's .one files.
    REQUIRE(setting_of(*p, "icon"));
    CHECK(fs::exists(cloud / setting_of(*p, "icon")->value));
    CHECK(setting_of(*p, "icon")->value == "../base/assets/icon.svg");
    REQUIRE(p->environments.size() == 1);
    CHECK(p->environments[0].name == "production");
}

TEST_CASE("a project that extends another says its own settings, and the other's give way") {
    base();
    auto cloud = folder("cloud", "import one\nproject cloud {\n" + pinned() + "\textends \"../base\"\n\ttarget  web desktop\n}\n");
    language::diagnostics found;
    std::vector<language::file> files;
    check_project(cloud.string(), found, &files);
    CAPTURE(errors_of(found));
    CHECK(found.empty());
    const auto* p = project_of(files);
    REQUIRE(p != nullptr);
    // A list is a setting for each of its values: its own two, and none of the other's.
    std::vector<std::string> targets;
    for (const auto& s : p->settings) {
        if (s.key == "target") targets.push_back(s.value);
    }
    CHECK(targets == std::vector<std::string>{"web", "desktop"});
}

TEST_CASE("a project that extends another can't say again what the other says") {
    base();
    auto cloud = folder("cloud", "import one\nproject cloud {\n" + pinned() +
                                     "\textends base\n}\n"
                                     "namespace base {\n\tentity widget {\n\t\tname  text  required\n\t\tprice  number\n\t}\n}\n");
    language::diagnostics found;
    check_project(cloud.string(), found);
    CAPTURE(errors_of(found));
    CHECK(errors_of(found).find("widget") != std::string::npos);
}

TEST_CASE("a project extends one that's there, and never itself") {
    auto lonely = folder("lonely", "import one\nproject lonely {\n" + pinned() + "\textends nowhere\n}\n");
    language::diagnostics found;
    check_project(lonely.string(), found);
    CHECK(errors_of(found).find("there's no project nowhere to extend") != std::string::npos);

    folder("first", "import one\nproject first {\n" + pinned() + "\textends second\n}\n");
    auto second = folder("second", "import one\nproject second {\n" + pinned() + "\textends first\n}\n");
    found.clear();
    check_project(second.string(), found);
    CAPTURE(errors_of(found));
    CHECK(errors_of(found).find("extends itself") != std::string::npos);
}

TEST_CASE("a project that extends another adds commands to the roles the other defines") {
    folder("base", "import one\nproject base {\n" + pinned() +
                       "\tenvironment production {\n\t\tdomain    \"base.example\"\n\t\tfirebase  \"base\"\n\t\tregion    \"us-east4\"\n\t}\n}\n"
                       "namespace organizations {\n\tentity organization {\n\t\treaders member\n\t\tslug  text  required  unique  key\n\t}\n"
                       "\tcommand organization::create {\n\t\tby anyone signed in\n\t}\n\tcommand organization::update\n"
                       "\tdefine role owner \"Owner\" in organization {\n\t\torganization::update\n\t}\n}\n");
    auto cloud = [](const std::string& role) {
        return folder("cloud", "import one\nproject cloud {\n" + pinned() +
                                   "\textends base\n}\n"
                                   "namespace billing {\n\tentity account {\n\t\torganization  organizations::organization  required  key\n\t\tplan  text\n\t}\n"
                                   "\tcommand account::subscribe\n"
                                   "\textend role " + role + " {\n\t\taccount::subscribe\n\t}\n}\n");
    };
    language::diagnostics found;
    std::vector<language::file> files;
    check_project(cloud("organizations::owner").string(), found, &files);
    CAPTURE(errors_of(found));
    CHECK(found.empty());
    // The owner allows the other namespace's command, named in full.
    const language::roles_declaration* roles = nullptr;
    for (const auto& f : files) {
        for (const auto& d : f.declarations) {
            auto* n = std::get_if<language::namespace_declaration>(&d.node);
            if (!n || n->name != "organizations") continue;
            for (const auto& inner : n->declarations) {
                if (auto* r = std::get_if<language::roles_declaration>(&inner.node)) roles = r;
            }
        }
    }
    REQUIRE(roles != nullptr);
    REQUIRE(roles->defaults.size() == 1);
    std::vector<std::string> allows;
    for (const auto& p : roles->defaults[0].permissions) allows.push_back(p.text());
    CHECK(allows == std::vector<std::string>{"organization::update", "billing::account::subscribe"});

    language::diagnostics wrong;
    check_project(cloud("organizations::ownr").string(), wrong);
    CHECK(errors_of(wrong).find("there's no define role organizations::ownr to extend; name it with its namespace, like organizations::owner") != std::string::npos);
}
