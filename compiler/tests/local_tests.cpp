// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// storage local (decision 0015): what a project that keeps its data on its machine
// may say, the C++ its commands and views become on one::local, and that one build
// writes its native apps alone.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "generators/local.hpp"
#include "generators/project.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "language/storage.hpp"

using namespace one;

namespace {

    const std::string board = R"(import one

project pinned {
	one      "0.18.0"
	storage  local
	target   desktop
}

namespace pinned {

	entity board {
		name  text  required
	}

	entity layer {
		board  board   required
		order  serial  per board
		kind   enum    picture | words = kind::picture
		x      number  = 0
		w      number  = 400
	}

	command board::create
	command layer::create

	command layer::place {
		changes x w
		require w > 0  "a layer has a size"
	}

	command layer::raise {
		require kind != kind::words || w > 0  "a layer has a size"
		x = x + 1
	}

	view boards {
		each board {
			order by name
			name
		}
	}

	view board_page per board {
		name
		pictures = count(layer where board == board.id && kind == kind::picture)
		layers = each layer in board {
			order by order descending
			x  w  kind
		}
	}

	screen "Boards" /boards {
		table boards {
			name
		}
	}

}
)";

    std::vector<language::file> parsed(const std::string& source, language::diagnostics& out) {
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        return files;
    }

    std::vector<std::string> messages(const std::string& source) {
        language::diagnostics out;
        parsed(source, out);
        std::vector<std::string> said;
        for (const auto& d : out) said.push_back(std::to_string(d.where.line) + ": " + d.message);
        return said;
    }

    bool says(const std::vector<std::string>& said, const std::string& part) {
        return std::ranges::any_of(said, [&](const std::string& s) { return s.find(part) != std::string::npos; });
    }

    std::string with(std::string source, const std::string& find, const std::string& put) {
        source.replace(source.find(find), find.size(), put);
        return source;
    }

}  // namespace

TEST_CASE("a project keeps its data on its machine only when it says storage local") {
    language::diagnostics out;
    CHECK(language::stores_locally(parsed(board, out)));
    CHECK(out.empty());
    CHECK(!language::stores_locally(parsed(with(board, "\tstorage  local\n", ""), out)));
}

TEST_CASE("what a local project can't have says why, at its line") {
    auto said = messages(with(with(board, "\ttarget   desktop\n", "\ttarget   desktop web\n\tsignin   google\n"),
                              "\tcommand board::create\n", "\tcommand board::create\n\tcommand board::claim {\n\t\tname = me\n\t}\n\trole editor  board::create\n"));
    CHECK(says(said, "6: the web app of a project that says storage local isn't built yet"));
    CHECK(says(said, "7: nobody signs in to a project that says storage local"));
    CHECK(says(said, "me, who's running a command, needs someone signed in"));
    CHECK(says(said, "a role needs someone signed in"));
    CHECK(said.size() == 4);
}

TEST_CASE("a local project says which app it's built for") {
    auto said = messages(with(board, "\ttarget   desktop\n", ""));
    CHECK(says(said, "a project that says storage local is built for this machine, so it says which app, like target desktop"));
}

TEST_CASE("a project on Firebase is checked as it always was") {
    auto said = messages(with(with(board, "\tstorage  local\n", ""), "\ttarget   desktop\n", "\tsignin   google\n"));
    CHECK(said.empty());
}

TEST_CASE("a local project's commands and views are written on one::local, as its .one says them") {
    language::diagnostics out;
    auto files = parsed(board, out);
    REQUIRE(out.empty());
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    CHECK(generated.files[0].path == "src/local/pinned.cpp");
    CHECK(generated.installs == std::vector<std::string>{"install_pinned"});
    CHECK(generated.files[0].content == R"(// Generated from main.one by one. Do not edit.

#include <one/local/package.hpp>

namespace app {

	using namespace one::local;
	using fields = std::map<std::string, value>;

	// pinned's entities, commands and views, kept on this machine.
	void install_pinned(local_source& source) {
		static const package pinned("pinned",
			{
				entity("pinned::board", {
					{.name = "name", .required = true},
				}),
				entity("pinned::layer", {
					{.name = "board", .required = true, .refers = "pinned::board"},
					{.name = "order", .serial = "board"},
					{.name = "kind", .choices = {"picture", "words"}, .initial = value("picture")},
					{.name = "x", .initial = value(0)},
					{.name = "w", .initial = value(400)},
				})
			},
			{
				command("pinned::board::create").creates("pinned::board"),
				command("pinned::layer::create").creates("pinned::layer"),
				command("pinned::layer::place").on("pinned::layer").fields({"x", "w"}).does([](context& c, value& e) -> std::expected<void, std::string> {
					if (!(compare(e["w"], value(0)) > 0)) return c.fail("a layer has a size");
					return c.done();
				}),
				command("pinned::layer::raise").on("pinned::layer").does([](context& c, value& e) -> std::expected<void, std::string> {
					if (!(!same(e["kind"], value("words")) || compare(e["w"], value(0)) > 0)) return c.fail("a layer has a size");
					e.fields["x"] = value(e["x"].as_double() + value(1).as_double());
					return c.done();
				})
			},
			{
				view("pinned::boards")
					.each(all("pinned::board"))
					.order({by("name")})
					.fields({"name"}),
				view("pinned::board_page")
					.per("pinned::board")
					.copy("name", "name")
					.count("pictures", all("pinned::layer").where("board", subject).where("kind", value("picture")))
					.list("layers", all("pinned::layer").where("board", subject))
					.order({by("-order")})
					.fields({"x", "w", "kind"})
			});
		pinned.install(source);
	}

}  // namespace app
)");
}

TEST_CASE("what local storage can't do yet is an error at its line, and nothing's written") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tscreen \"Boards\"", "\tfunction twice(n) {\n\t\treturn n * 2\n\t}\n\n\tscreen \"Boards\""), out);
    REQUIRE(out.empty());
    auto generated = generators::generate_local(files);
    CHECK(generated.files.empty());
    REQUIRE(generated.errors.size() == 1);
    CHECK(generated.errors[0].message == "not supported yet in a project that says storage local: a function");
}

TEST_CASE("one build writes a local project's native apps alone, with no service, rules or cloud") {
    language::diagnostics out;
    auto files = parsed(board, out);
    REQUIRE(out.empty());
    auto dir = std::filesystem::temp_directory_path() / "one-local-project";
    auto whole = generators::generate_project(files, dir.string(), (dir / "build").string(), {"desktop"});
    for (const auto& e : whole.errors) FAIL_CHECK(language::format(e));
    REQUIRE(!whole.files.empty());
    for (const auto& f : whole.files) {
        CAPTURE(f.path);
        CHECK(f.path.starts_with("desktop/"));
    }
    auto has = [&](const std::string& path) { return std::ranges::any_of(whole.files, [&](const auto& f) { return f.path == path; }); };
    CHECK(has("desktop/src/local/pinned.cpp"));
    auto main = std::ranges::find_if(whole.files, [](const auto& f) { return f.path == "desktop/src/main.cpp"; });
    REQUIRE(main != whole.files.end());
    CHECK(main->content.find("one::local::folder::open(\"pinned\")") != std::string::npos);
    CHECK(main->content.find("app::install_pinned(**source);") != std::string::npos);
    CHECK(main->content.find("one::client") == std::string::npos);
}
