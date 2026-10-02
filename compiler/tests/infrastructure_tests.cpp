// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The deploy generator is held to site/target, like the others.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "generators/infrastructure.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    generators::generated_infrastructure generate_at(const std::string& dir) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({dir})) {
            files.push_back(language::parse(path, *platform::read_file(path), out));
        }
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return generators::generate_infrastructure(files, dir, dir + "/build");
    }

    std::vector<generators::output_file> generate(const std::string& project) { return generate_at(root + project).files; }

    // A target without the license header it carries as a file in this repository,
    // in whichever comment style the file uses, and after its shebang if it has one.
    std::string without_license(const std::string& text) {
        auto start = text.find("Copyright 2026 Daher Alfawares");
        if (start == std::string::npos) return text;
        start = text.rfind('\n', start);
        start = start == std::string::npos ? 0 : start + 1;
        auto end = text.find("SPDX-License-Identifier:", start);
        if (end == std::string::npos) return text;
        end = text.find('\n', end);
        if (end == std::string::npos) return text;
        ++end;
        if (end < text.size() && text[end] == '\n') ++end;
        return text.substr(0, start) + text.substr(end);
    }

} // namespace

TEST_CASE("the site's deploy is generated exactly as its targets say") {
    auto files = generate("/site");
    REQUIRE(files.size() == 6);
    for (const auto& f : files) {
        CAPTURE(f.path);
        auto target = platform::read_file(root + "/site/target/" + f.path);
        REQUIRE(target);
        CHECK(f.content == without_license(*target));
    }
}

TEST_CASE("only the deploy script is made executable") {
    for (const auto& f : generate("/site")) {
        CAPTURE(f.path);
        CHECK(f.executable == (f.path == "deploy"));
    }
}

TEST_CASE("a project that doesn't say where it runs gets no deploy") {
    CHECK(generate("/examples/tasks").empty());
    CHECK(generate("/examples/library").empty());
}

TEST_CASE("a project outside this repository is told why it gets no deploy, rather than given one that can't work") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-outside-the-repository";
    fs::create_directories(dir);
    REQUIRE(platform::write_file((dir / "main.one").string(),
                                 "project p {\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n\tdomain \"p.io\"\n}\n"));
    auto generated = generate_at(dir.string());
    CHECK(generated.files.empty());
    CHECK(generated.skipped.find("inside a clone of the uione repository") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a license header is taken off in any comment style") {
    CHECK(without_license("#!/bin/sh\n# Copyright 2026 Daher Alfawares\n# SPDX-License-Identifier: AGPL-3.0-only\n\necho\n") ==
          "#!/bin/sh\necho\n");
    CHECK(without_license("// Copyright 2026 Daher Alfawares\n// SPDX-License-Identifier: AGPL-3.0-only\n\npackage main\n") ==
          "package main\n");
    CHECK(without_license("{}\n") == "{}\n");
}

TEST_CASE("a project that takes GitHub's webhook routes it to the backend and is given its secret") {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(root) / "compiler" / "build" / "webhook-project";
    fs::create_directories(dir);
    REQUIRE(platform::write_file((dir / "main.one").string(),
                                 "project p {\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n\tdomain \"p.io\"\n}\n"
                                 "namespace code {\n"
                                 "\tentity project {\n\t\tslug  text  required  key\n\t\trepository  text  unique\n\t}\n"
                                 "\tentity issue {\n\t\tproject  project  required  key\n\t\tnumber  serial  per project  key\n\t}\n"
                                 "\tentity mention {\n\t\tissue  issue  required  key\n\t\turl  text  required  key\n\t}\n"
                                 "\twebhook github /hooks/github {\n\t\tfor project by repository\n"
                                 "\t\ton commit {\n\t\t\tcreate mention {\n\t\t\t\tissue = mentioned  url = url\n\t\t\t}\n\t\t}\n\t}\n"
                                 "}\n"));
    auto generated = generate_at(dir.string());
    fs::remove_all(dir);
    const generators::output_file* hosting = nullptr;
    const generators::output_file* program = nullptr;
    for (const auto& f : generated.files) {
        if (f.path == "web/firebase.json") hosting = &f;
        if (f.path == "infrastructure/main.go") program = &f;
    }
    REQUIRE(hosting != nullptr);
    REQUIRE(program != nullptr);
    CHECK(hosting->content.find(R"({ "source": "/hooks/**", "run": { "serviceId": "api", "region": "us-east4" } },)") != std::string::npos);
    CHECK(program->content.find("\t\tGitHub:   \"/hooks/github\",\n") != std::string::npos);
}
