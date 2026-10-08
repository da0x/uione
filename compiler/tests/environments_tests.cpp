// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Environments: the places a project runs, each with its own domain, Firebase
// project and region, and the settings shared outside them. What's written, what
// the checker holds them to, and what's built for each.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "driver.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string shop = "import one\nproject shop {\n"
                             "\tregion  \"us-east4\"\n"
                             "\tui      radix\n"
                             "\tenvironment production {\n"
                             "\t\tdomain    \"shop.example\"\n"
                             "\t\tfirebase  \"shop-production\"\n"
                             "\t}\n"
                             "\tenvironment staging {\n"
                             "\t\tdomain    \"staging.shop.example\"\n"
                             "\t\tfirebase  \"shop-staging\"\n"
                             "\t\tregion    \"europe-west1\"\n"
                             "\t}\n"
                             "}\n"
                             "namespace shop {\n\tentity order {\n\t\ttotal  number\n\t}\n}\n";

    language::diagnostics check_source(const std::string& source) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        return out;
    }

    std::string only_error(const std::string& source) {
        auto out = check_source(source);
        for (const auto& d : out) CAPTURE(language::format(d));
        REQUIRE(out.size() == 1);
        return out[0].message;
    }

    std::filesystem::path project(const std::string& name, const std::string& source) {
        auto dir = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        platform::write_file((dir / "main.one").string(), source);
        return dir;
    }

    const generators::output_file* find(const std::vector<generators::output_file>& files, const std::string& path) {
        auto it = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.path == path; });
        return it == files.end() ? nullptr : &*it;
    }

} // namespace

TEST_CASE("environments are written in the project block, with what's their own") {
    language::diagnostics out;
    auto file = language::parse("main.one", shop, out);
    REQUIRE(out.empty());
    const auto& p = std::get<language::project_declaration>(file.declarations.at(1).node);  // after import one
    REQUIRE(p.settings.size() == 2);  // region and ui, shared
    REQUIRE(p.environments.size() == 2);
    CHECK(p.environments[0].name == "production");
    CHECK(p.environments[0].settings.size() == 2);
    CHECK(p.environments[1].name == "staging");
    CHECK(p.environments[1].settings[2].key == "region");
    CHECK(p.environments[1].settings[2].value == "europe-west1");
    CHECK(check_source(shop).empty());
}

TEST_CASE("an environment has only its own place, and all of it, its own or shared") {
    CHECK(only_error("import one\nproject shop {\n\tenvironment production {\n\t\tdomain \"shop.example\"\n\t\tfirebase \"shop\"\n\t\tregion \"us-east4\"\n\t\tui radix\n\t}\n}\n") ==
          "ui is the same in every environment, so it goes outside them; an environment has its own domain, firebase and region");
    CHECK(only_error("import one\nproject shop {\n\tenvironment staging {\n\t\tdomain \"shop.example\"\n\t}\n}\n") ==
          "environment staging needs firebase, region and domain, its own or shared; it has no firebase and region");
    CHECK(only_error("import one\nproject shop {\n\tregion \"us-east4\"\n"
                     "\tenvironment staging {\n\t\tdomain \"a.example\"\n\t\tfirebase \"a\"\n\t}\n"
                     "\tenvironment staging {\n\t\tdomain \"b.example\"\n\t\tfirebase \"b\"\n\t}\n}\n") == "there are two environments called staging");
    // Two environments in one Google Cloud project would deploy over each other.
    CHECK(only_error("import one\nproject shop {\n\tregion \"us-east4\"\n\tfirebase \"shop\"\n"
                     "\tenvironment production {\n\t\tdomain \"shop.example\"\n\t}\n"
                     "\tenvironment staging {\n\t\tdomain \"staging.shop.example\"\n\t}\n}\n") ==
          "environments production and staging both run in the Google Cloud project shop; each needs its own, so their data and deploys never mix");
    // What's checked of a setting outside an environment is checked inside one too.
    CHECK(only_error("import one\nproject shop {\n\tregion \"us-east4\"\n\tenvironment staging {\n\t\tdomain \"a.example\"\n\t\tfirebase \"a; rm -rf ~\"\n\t}\n}\n") ==
          "firebase has to be lowercase letters, digits and dashes, like ui-one or us-east4");
}

TEST_CASE("a build is for one environment: the one named, or the first") {
    auto dir = project("uione-environments", shop);
    auto first = driver::build(dir.string(), (dir / "build").string());
    REQUIRE(first.refusal.empty());
    CHECK(first.environment == "production");
    CHECK(first.note.find("built for environment production, the first; --for names another") != std::string::npos);
    const auto* deploy = find(first.files, "deploy");
    REQUIRE(deploy);
    // An environment's stack is its Google Cloud project, so renaming it keeps its stack.
    CHECK(deploy->content.find("--stack shop-production --project shop-production ") != std::string::npos);

    auto staging = driver::build(dir.string(), (dir / "build").string(), "staging");
    REQUIRE(staging.refusal.empty());
    CHECK(staging.environment == "staging");
    CHECK(staging.note.find("--for") == std::string::npos);
    deploy = find(staging.files, "deploy");
    REQUIRE(deploy);
    CHECK(deploy->content.find("--stack shop-staging --project shop-staging ") != std::string::npos);
    // Everything built is for staging's place: its Firebase project and its region,
    // which takes the place of the shared one.
    const auto* program = find(staging.files, "infrastructure/main.go");
    REQUIRE(program);
    CHECK(program->content.find("shop-staging") != std::string::npos);
    CHECK(program->content.find("europe-west1") != std::string::npos);
    CHECK(program->content.find("shop-production") == std::string::npos);
    CHECK(program->content.find("us-east4") == std::string::npos);

    auto missing = driver::build(dir.string(), (dir / "build").string(), "qa");
    CHECK(missing.refusal == "there's no environment qa; this project has production, staging");
    CHECK(missing.files.empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("a project without environments is built as it always was, and --for is refused") {
    auto dir = project("uione-no-environments", "import one\nproject shop {\n\tdomain \"shop.example\"\n\tfirebase \"shop\"\n\tregion \"us-east4\"\n}\n"
                                                "namespace shop {\n\tentity order {\n\t\ttotal  number\n\t}\n}\n");
    auto built = driver::build(dir.string(), (dir / "build").string());
    REQUIRE(built.refusal.empty());
    CHECK(built.environment.empty());
    const auto* deploy = find(built.files, "deploy");
    REQUIRE(deploy);
    CHECK(deploy->content.find("--stack production --project shop ") != std::string::npos);
    CHECK(driver::build(dir.string(), (dir / "build").string(), "staging").refusal == "this project has no environments, so it's built without --for");
    std::filesystem::remove_all(dir);
}

TEST_CASE("a project counts its visitors with Firebase Analytics only when it says so") {
    auto app = [](const std::string& settings) {
        auto dir = project("uione-analytics", "import one\nproject shop {\n\tdomain \"shop.example\"\n\tfirebase \"shop\"\n\tregion \"us-east4\"\n" + settings +
                                                  "}\nnamespace shop {\n\tentity order {\n\t\ttotal  number\n\t}\n}\nscreen \"Shop\" / {\n\ttext \"Welcome\"\n}\n");
        auto built = driver::build(dir.string(), (dir / "build").string());
        for (const auto& d : built.problems) CAPTURE(language::format(d));
        REQUIRE(built.refusal.empty());
        const auto* file = find(built.files, "web/src/app.tsx");
        REQUIRE(file);
        std::string content = file->content;
        std::filesystem::remove_all(dir);
        return content;
    };
    auto counted = app("\tanalytics google\n");
    CHECK(counted.find("import { firebaseAnalytics, firebaseSource } from \"@uione/react/firebase\";") != std::string::npos);
    CHECK(counted.find("measurementId: import.meta.env.VITE_FIREBASE_MEASUREMENT_ID,") != std::string::npos);
    CHECK(counted.find("const analytics = !import.meta.env.DEV && cloud.config.measurementId ? firebaseAnalytics(cloud.config) : undefined;") != std::string::npos);
    CHECK(counted.find(", analytics, footer: {") != std::string::npos);
    auto quiet = app("");
    CHECK(quiet.find("firebaseAnalytics") == std::string::npos);
    CHECK(quiet.find(", analytics") == std::string::npos);
    CHECK(quiet.find("measurementId") == std::string::npos);
}

TEST_CASE("a form asks for a yes or no with a box to tick") {
    auto dir = project("uione-boolean-form", "namespace shop {\n\tentity store {\n\t\tname  text  required\n\t\topen  boolean = false\n\t}\n"
                                              "\tcommand store::create\n}\nscreen \"Shop\" / {\n\tform shop::store::create {\n\t\tname  open\n\t}\n}\n");
    auto built = driver::build(dir.string(), (dir / "build").string());
    for (const auto& d : built.problems) CAPTURE(language::format(d));
    REQUIRE(built.refusal.empty());
    bool found = false;
    for (const auto& f : built.files) {
        if (f.content.find(R"(fields={["name", { name: "open", type: "boolean" }]})") != std::string::npos) found = true;
    }
    CHECK(found);
    std::filesystem::remove_all(dir);
}
