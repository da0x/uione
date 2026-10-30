// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one deploy: the projects under a folder found, each built before any is deployed.

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "driver/deploy.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    // A folder of projects as a site and the apps it serves are laid out.
    std::string sites() {
        auto dir = std::filesystem::temp_directory_path() / "one-deploy-projects";
        std::filesystem::remove_all(dir);
        platform::write_file((dir / "site/site.one").string(), "import one\n\nproject site {\n\tone  \"0.18.0\"\n}\n");
        platform::write_file((dir / "site/home.one").string(), "screen \"Home\" / {}\n");
        platform::write_file((dir / "apps/notes/notes.one").string(), "project notes {\n\textends \"github.com/example/notes\" \"0.1.0\"\n}\n");
        platform::write_file((dir / "apps/notes/build/web/stray.one").string(), "project stray {}\n");
        platform::write_file((dir / "shared/parts.one").string(), "// a project's block: project x {\nscreen \"Parts\" /parts {}\n");
        return dir.string();
    }

}  // namespace

TEST_CASE("one deploy finds each folder with a project block, and none in a build") {
    auto root = sites();
    std::vector<std::string> expected{root + "/apps/notes", root + "/site"};
    CHECK(driver::projects_under(root) == expected);
    CHECK(driver::projects_under(root + "/site") == std::vector<std::string>{root + "/site"});
}

TEST_CASE("one deploy builds every project before it deploys any, each by its own deploy") {
    auto root = sites();
    auto plan = driver::plan_deploy(root, "one", "", false);
    CHECK(plan.refusal.empty());
    REQUIRE(plan.builds.size() == 2);
    REQUIRE(plan.deploys.size() == 2);
    CHECK(plan.builds[0].command == std::vector<std::string>{"one", "build", root + "/apps/notes"});
    CHECK(plan.deploys[0].command == std::vector<std::string>{root + "/apps/notes/build/deploy"});
    CHECK(plan.deploys[1].command == std::vector<std::string>{root + "/site/build/deploy"});

    auto quiet = driver::plan_deploy(root, "one", "staging", true);
    CHECK(quiet.builds[1].command == std::vector<std::string>{"one", "build", root + "/site", "--for", "staging"});
    CHECK(quiet.deploys[1].command == std::vector<std::string>{root + "/site/build/deploy", "--yes"});
}

TEST_CASE("one deploy says so when a folder holds no project") {
    auto root = sites();
    auto plan = driver::plan_deploy(root + "/shared", "one", "", false);
    CHECK(plan.refusal == "there's no project under " + root + "/shared, no .one file there has a project block");
    CHECK(plan.builds.empty());
}
