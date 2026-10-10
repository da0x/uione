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

    const generators::output_file* find(const std::vector<generators::output_file>& files, const std::string& path) {
        auto it = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.path == path; });
        return it == files.end() ? nullptr : &*it;
    }

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

TEST_CASE("a project outside this repository deploys with the released library") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-outside-the-repository";
    fs::create_directories(dir);
    REQUIRE(platform::write_file((dir / "main.one").string(),
                                 "import one\nproject p {\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n\tdomain \"p.io\"\n}\n"));
    auto generated = generate_at(dir.string());
    CHECK(generated.skipped.empty());
    const auto* mod = find(generated.files, "infrastructure/go.mod");
    REQUIRE(mod != nullptr);
    CHECK(mod->content.find("require github.com/da0x/uione/infrastructure v" + std::string(version) + "\n") != std::string::npos);
    CHECK(mod->content.find("replace") == std::string::npos);
    CHECK(find(generated.files, "deploy") != nullptr);
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
                                 "import one\nproject p {\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n\tdomain \"p.io\"\n}\n"
                                 "namespace code {\n"
                                 "\tentity project {\n\t\tslug  text  required  key\n\t\trepository  text  unique\n\t}\n"
                                 "\tentity issue {\n\t\tproject  project  required  key\n\t\tnumber  serial  per project  key\n\t}\n"
                                 "\tentity mention {\n\t\tissue  issue  required  key\n\t\turl  text  required  key\n\t}\n"
                                 "\tcommand mention::create\n"
                                 "\tdefine service github \"GitHub\" in project {\n\t\tmention::create\n\t}\n"
                                 "\twebhook github /hooks/github as github {\n\t\tfor project by repository\n"
                                 "\t\ton commit {\n\t\t\tdispatch mention::create {\n\t\t\t\tissue = mentioned  url = url\n\t\t\t}\n\t\t}\n\t}\n"
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

TEST_CASE("a project with a role for services lets its backend sign their tokens, and one without doesn't") {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(root) / "compiler" / "build" / "services-project";
    fs::create_directories(dir);
    std::string source = "import one\nproject p {\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n\tdomain \"p.io\"\n}\n"
                         "namespace crew {\n"
                         "\tentity crew {\n\t\tslug  slug  required  unique  key\n\t}\n"
                         "\tdefine role captain \"Captain\" in crew {\n\t\tmember::create\n\t}\n"
                         "\tdefine role welder \"Welder\" in crew for services {\n\t\tcrew::update\n\t}\n"
                         "\tcommand crew::create {\n\t\tby anyone signed in\n\t}\n"
                         "\tcommand crew::update\n\tcommand member::create\n"
                         "}\n";
    auto program_of = [&](const std::string& text) {
        REQUIRE(platform::write_file((dir / "main.one").string(), text));
        auto generated = generate_at(dir.string());
        for (const auto& f : generated.files) {
            if (f.path == "infrastructure/main.go") return f.content;
        }
        return std::string();
    };
    CHECK(program_of(source).find("\t\tServices: true,\n") != std::string::npos);
    std::string without = source;
    without.replace(without.find(" for services {"), 15, " {");
    CHECK(program_of(without).find("Services") == std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a redirect goes into Hosting's settings") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-redirect";
    fs::remove_all(dir);
    fs::create_directories(dir);
    REQUIRE(platform::write_file((dir / "main.one").string(),
                                 "import one\nproject p {\n\tfirebase \"p-1\"\n\tregion \"us-east4\"\n\tdomain \"p.io\"\n"
                                 "\tredirect \"/install.sh\" \"https://www.p.io/install.sh\"\n}\n"));
    auto generated = generate_at(dir.string());
    const auto* hosting = find(generated.files, "web/firebase.json");
    REQUIRE(hosting != nullptr);
    CHECK(hosting->content.find(R"({ "source": "/install.sh", "destination": "https://www.p.io/install.sh", "type": 301 })") != std::string::npos);
    fs::remove_all(dir);
}
