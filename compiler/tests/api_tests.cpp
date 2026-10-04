// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The backend and rules generators are held to site/target, like the web generator.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/rules.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    std::vector<language::file> read(const std::string& project) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({root + project})) {
            files.push_back(language::parse(path, *platform::read_file(path), out));
        }
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return files;
    }

    generators::generated_api api(const std::string& project) {
        return generators::generate_api(read(project), root + project, root + project + "/build/api");
    }

    const generators::output_file* find(const std::vector<generators::output_file>& files, const std::string& path) {
        auto it = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.path == path; });
        return it == files.end() ? nullptr : &*it;
    }

    std::string without_license(const std::string& text) {
        if (!text.starts_with("// Copyright")) return text;
        auto end = text.find("\n\n");
        return end == std::string::npos ? text : text.substr(end + 2);
    }

} // namespace

TEST_CASE("the site's backend is generated exactly as its targets say") {
    auto generated = api("/site");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    for (const char* path : {"go.mod", "main.go", "waitlist/waitlist.go", "studio/studio.go"}) {
        CAPTURE(path);
        auto target = platform::read_file(root + "/site/target/api/" + path);
        REQUIRE(target);
        const auto* file = find(generated.files, path);
        REQUIRE(file != nullptr);
        CHECK(file->content == without_license(*target));
    }
}

TEST_CASE("the site's rules are generated exactly as their target says") {
    auto target = platform::read_file(root + "/site/target/firestore.rules");
    REQUIRE(target);
    CHECK(generators::generate_rules(root + "/site") == without_license(*target));
}

TEST_CASE("a command's body becomes Go that reads like it") {
    auto generated = api("/examples/tasks");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* tasks = find(generated.files, "tasks/tasks.go");
    REQUIRE(tasks != nullptr);
    CHECK(tasks->content.find(R"(var Complete = one.Command[Task]("task::complete").Allow(one.Owner).
	Do(func(c *one.Ctx, t *Task) error {
		if t.Done {
			return c.Fail("that task is already done")
		}
		t.Done = true
		return nil
	})
)") != std::string::npos);
    CHECK(tasks->content.find(R"(var List = one.View("list").PerUser().
	Each(one.Where[Task]("owner", one.Viewer)).
	Order("done", "-created_at").
	Fields("title", "done")
)") != std::string::npos);
}

TEST_CASE("the library example's backend is generated exactly as its target says") {
    auto generated = api("/examples/library");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto target = platform::read_file(root + "/site/target/api/library/library.go");
    REQUIRE(target);
    const auto* file = find(generated.files, "library/library.go");
    REQUIRE(file != nullptr);
    CHECK(file->content == without_license(*target));
}

TEST_CASE("what the library can't do yet stops the build, and says where") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace shop {
entity order {
	total  number
}
view orders {
	newest = first(order).total
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    CHECK(generated.files.empty());
    REQUIRE(generated.errors.size() == 1);
    CHECK(generated.errors[0].message == "not supported yet: a view value other than count(...), or a field of the entity a view per entity is for");
    CHECK(generated.errors[0].where.line == 6);
}

TEST_CASE("generated Go is about as long as the .one it came from") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/library", "/examples/tracker"}) {
        std::size_t source = 0;
        for (const auto& path : platform::find_one_files({root + project})) {
            auto text = platform::read_file(path);
            source = std::max<std::size_t>(source, std::count(text->begin(), text->end(), '\n'));
        }
        for (const auto& f : api(project).files) {
            CAPTURE(project);
            CAPTURE(f.path);
            CHECK(static_cast<std::size_t>(std::count(f.content.begin(), f.content.end(), '\n')) <= std::max<std::size_t>(source, 60));
        }
    }
}

TEST_CASE("Go names") {
    CHECK(generators::api_detail::go_name("created_at") == "CreatedAt");
    CHECK(generators::api_detail::go_name("owner_id") == "OwnerID");
    CHECK(generators::api_detail::go_name("email") == "Email");
    CHECK(generators::api_detail::go_name("on_shelf") == "OnShelf");
}

TEST_CASE("an entity that keeps its history has changes a view can list, by itself or by what it points at") {
    auto generated = api("/examples/tracker");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find("\tone.Record\n\tone.History\n") != std::string::npos);
    CHECK(file->content.find(R"(List("changes", one.Where[one.ChangeOf[Issue]]("issue", one.Subject)).)") != std::string::npos);
    CHECK(file->content.find(R"(List("timeline", one.Where[one.ChangeOf[Issue]]("project", one.Subject)).)") != std::string::npos);
    CHECK(file->content.find("\tLimit(50).\n") != std::string::npos);
}

TEST_CASE("a github webhook makes what each event's handler creates, and is part of its module") {
    auto generated = api("/examples/tracker");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find(R"(var WebhookGithub = one.GitHub("/hooks/github").For(one.Entity[Project](), "repository").Mentions(one.Entity[Issue]()).)") !=
          std::string::npos);
    CHECK(file->content.find(R"(one.Create(c, &Mention{Issue: m.Issue, URL: m.URL, Kind: KindCommit, Title: m.Message, Author: m.Author}))") !=
          std::string::npos);
    CHECK(file->content.find(", WebhookGithub,") != std::string::npos);
}

TEST_CASE("a view shows a project's webhook secret through the library") {
    auto generated = api("/examples/tracker");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find("\tGitHubSecret(\"webhook_secret\")") != std::string::npos);
}

TEST_CASE("two entities whose fields share a name and a choice get constants of their own") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-shared-choices";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace desk {\n"
                         "entity issue {\n\ttitle  text\n\tstatus  enum open | closed = status::open\n\tsize  enum small | large\n}\n"
                         "entity report {\n\ttitle  text\n\tstatus  enum open | resolved = status::open\n}\n"
                         "command issue::close {\n\trequire status == status::open  \"already closed\"\n\tstatus = status::closed\n}\n"
                         "command report::resolve {\n\tstatus = status::resolved\n}\n"
                         "}\n");
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse((dir / "main.one").string(), *platform::read_file((dir / "main.one").string()), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    const auto* desk = find(generated.files, "desk/desk.go");
    REQUIRE(desk != nullptr);
    const auto& go = desk->content;
    CHECK(go.find("IssueStatusOpen") != std::string::npos);
    CHECK(go.find("ReportStatusOpen") != std::string::npos);
    CHECK(go.find("ReportStatusResolved") != std::string::npos);
    CHECK(go.find("if i.Status != IssueStatusOpen {") != std::string::npos);
    CHECK(go.find("i.Status = IssueStatusClosed") != std::string::npos);
    CHECK(go.find("r.Status = ReportStatusResolved") != std::string::npos);
    // A field no other entity shares keeps its short names.
    CHECK(go.find("SizeSmall") != std::string::npos);
    CHECK(go.find("IssueSizeSmall") == std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("Go written by hand beside a .one file is built into its namespace's package") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-backend";
    fs::remove_all(dir);
    fs::create_directories(dir / "backend");
    const std::string go = "// Written by hand.\n\npackage desk\n\nfunc init() {}\n";
    platform::write_file((dir / "backend" / "deploy.go").string(), go);
    platform::write_file((dir / "main.one").string(),
                         "namespace desk {\nentity ticket {\n\ttitle  text\n}\ncommand ticket::create\nbackend deploy\n}\n");
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse((dir / "main.one").string(), *platform::read_file((dir / "main.one").string()), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    const auto* copied = find(generated.files, "desk/deploy.go");
    REQUIRE(copied != nullptr);
    CHECK(copied->content == go);
    fs::remove_all(dir);
}

TEST_CASE("a field starting as the person's username tells the library so") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-username";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace studio {\nentity project {\n\towner  text  key  = me.username\n\tname  slug  key\n}\ncommand project::create\n}\n");
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse((dir / "main.one").string(), *platform::read_file((dir / "main.one").string()), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    const auto* go = find(generated.files, "studio/studio.go");
    REQUIRE(go != nullptr);
    CHECK(go->content.find(R"(one:"key,default=me.username")") != std::string::npos);
    CHECK(go->content.find(R"(one:"key,slug")") != std::string::npos);
    fs::remove_all(dir);
}
