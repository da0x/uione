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
    CHECK(says(said, "7: nobody signs in to a project that says storage local"));
    CHECK(says(said, "me, who's running a command, needs someone signed in"));
    CHECK(says(said, "a role needs someone signed in"));
    CHECK(!says(said, "web"));  // its web app keeps its data in the browser
    CHECK(said.size() == 3);
}

TEST_CASE("a local project says which app it's built for") {
    auto said = messages(with(board, "\ttarget   desktop\n", ""));
    CHECK(says(said, "a project that says storage local is built for this machine, so it says which app, like target desktop"));
}

TEST_CASE("a local project is served from a Firebase project only as its web app") {
    const std::string hosted = "\ttarget   desktop\n\tfirebase \"pinned-1\"\n\tregion   \"us-east4\"\n\tdomain   \"pinned.example.com\"\n";
    auto said = messages(with(board, "\ttarget   desktop\n", hosted));
    CHECK(says(said, "is served from a Firebase project only as its web app, so it says target web"));
    CHECK(messages(with(board, "\ttarget   desktop\n", with(hosted, "desktop", "desktop web"))).empty());
}

TEST_CASE("one build serves a local project's web app from Hosting, with nothing else") {
    language::diagnostics out;
    auto files = parsed(with(with(board, "\ttarget   desktop\n", "\ttarget   web\n\tfirebase \"pinned-1\"\n\tregion   \"us-east4\"\n\tdomain   \"pinned.example.com\"\n"),
                             "\t\tw      number  = 400\n", "\t\tw      number  = 400\n\t\tshot   file\n"),
                        out);
    REQUIRE(out.empty());
    auto dir = std::filesystem::temp_directory_path() / "one-local-hosted";
    auto whole = generators::generate_project(files, dir.string(), (dir / "build").string(), {"web"});
    for (const auto& e : whole.errors) FAIL_CHECK(language::format(e));
    auto found = std::ranges::find_if(whole.files, [](const auto& f) { return f.path == "infrastructure/main.go"; });
    REQUIRE(found != whole.files.end());
    CHECK(found->content.find("Static:   true") != std::string::npos);
    CHECK(found->content.find("Files:") == std::string::npos);  // kept in the browser
    CHECK(std::ranges::none_of(whole.files, [](const auto& f) { return f.path.starts_with("api/") || f.path.ends_with(".rules"); }));
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
#include <one/screens/screens.hpp>

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

TEST_CASE("a local view counts its own list's rows, those its where keeps, and adds up a number of theirs, in both") {
    const std::string counted = with(with(board, "\ttarget   desktop\n", "\ttarget   desktop web\n"), "\t\t\tx  w  kind\n\t\t}\n",
                                     "\t\t\tx  w  kind\n\t\t}\n\t\tplaced = count(layers)\n\t\twide = count(layers where w > pictures)\n\t\twidth = sum(layers).w\n");
    language::diagnostics out;
    auto files = parsed(counted, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    CHECK(generated.files[0].content.find("\t\t\t\t\t.tally(\"placed\", \"layers\")\n\t\t\t\t\t.tally(\"wide\", \"layers\", {{\"w\", \">\", \"pictures\"}})\n\t\t\t\t\t.total(\"width\", \"layers\", \"w\")\n") != std::string::npos);
    CHECK(generated.web[0].content.find("        .tally(\"placed\", \"layers\")\n        .tally(\"wide\", \"layers\", [[\"w\", \">\", \"pictures\"]])\n        .total(\"width\", \"layers\", \"w\")\n") != std::string::npos);
    // What's added up is a number its rows hold.
    CHECK(messages(with(counted, "sum(layers).w", "sum(layers).kind")) == std::vector<std::string>{"52: sum adds up a number, and layer holds no number kind"});
    CHECK(messages(with(with(counted, "\t\t\tx  w  kind\n", "\t\t\tw  kind\n"), "sum(layers).w", "sum(layers).x")) == std::vector<std::string>{"52: layers's rows don't hold x; list it in its block to add it up"});
}

TEST_CASE("a local list's row counts, adds up and takes the first of another entity naming it, the first in an order, in both") {
    const std::string per_row = with(with(with(board, "\ttarget   desktop\n", "\ttarget   desktop web\n"), "\t\tx      number  = 0\n", "\t\tx      number  = 0\n\t\tpicture  file\n"),
                                     "\t\t\torder by name\n\t\t\tname\n",
                                     "\t\t\torder by name\n\t\t\tname\n\t\t\tplaced = count(layer where layer.board == board.id)\n"
                                     "\t\t\twidth = sum(layer where layer.board == board.id).w\n"
                                     "\t\t\tcover = first(layer where layer.board == board.id order by layer.x descending).picture\n"
                                     "\t\t\toldest = first(layer where layer.board == board.id).picture\n");
    language::diagnostics out;
    auto files = parsed(with(per_row, "\t\ttable boards {\n\t\t\tname\n\t\t}\n", "\t\tcards boards {\n\t\t\tname\n\t\t\tpicture cover fill\n\t\t}\n"), out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    CHECK(generated.files[0].content.find("\t\t\t\t\t.counted(\"placed\", all(\"pinned::layer\").where(\"board\", row))\n"
                                          "\t\t\t\t\t.summed(\"width\", all(\"pinned::layer\").where(\"board\", row), \"w\")\n"
                                          "\t\t\t\t\t.value(\"cover\", all(\"pinned::layer\").where(\"board\", row), \"picture\", {by(\"-x\")})\n"
                                          "\t\t\t\t\t.value(\"oldest\", all(\"pinned::layer\").where(\"board\", row), \"picture\"),\n") != std::string::npos);
    CHECK(generated.web[0].content.find("        .counted(\"placed\", all(\"pinned::layer\").where(\"board\", row))\n"
                                        "        .summed(\"width\", all(\"pinned::layer\").where(\"board\", row), \"w\")\n"
                                        "        .value(\"cover\", all(\"pinned::layer\").where(\"board\", row), \"picture\", [by(\"-x\")])\n") != std::string::npos);
    // Only first is ordered, by a field of what it takes; only a number is added up; and
    // what's taken through first is shown as what it is.
    CHECK(messages(with(per_row, "count(layer where layer.board == board.id)", "count(layer where layer.board == board.id order by x)")) ==
          std::vector<std::string>{"41: only first takes an order, the one it takes first, like first(loan where ... order by due descending)"});
    CHECK(messages(with(per_row, "order by layer.x descending", "order by height descending")) ==
          std::vector<std::string>{"43: entity layer has no field height"});
    CHECK(messages(with(per_row, "sum(layer where layer.board == board.id).w", "sum(layer where layer.board == board.id).kind")) ==
          std::vector<std::string>{"42: sum adds up a number, and layer holds no number kind"});
    CHECK(says(messages(with(with(per_row, "\t\ttable boards {\n\t\t\tname\n\t\t}\n", "\t\tcards boards {\n\t\t\tname\n\t\t\tpicture placed fill\n\t\t}\n"), "", "")),
               "board.placed is shown as a picture, so it's a file"));
}

TEST_CASE("what local storage can't do yet is an error at its line, and nothing's written") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tscreen \"Boards\"", "\tfunction layers() {\n\t\treturn count(layer)\n\t}\n\n\tscreen \"Boards\""), out);
    REQUIRE(out.empty());
    auto generated = generators::generate_local(files);
    CHECK(generated.files.empty());
    REQUIRE(generated.errors.size() == 1);
    CHECK(generated.errors[0].message == "not supported yet in a project that says storage local: this call");
}

TEST_CASE("a function is written for each target on values as rows keep them, called from a command, itself too") {
    const std::string counted = with(board, "\tscreen \"Boards\"", R"(	enum pace {
		slow
		fast
	}

	function steps(n number, way pace) {
		if n <= 0 { return 0 }
		return match way {
			slow 1 + steps(n - 1, way)
			fast 2 + steps(n - 1, way)
		}
	}

	command layer::step {
		x = steps(w, pace::fast)
	}

	screen "Boards")");
    language::diagnostics out;
    auto files = parsed(counted, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CAPTURE(cpp);
    CAPTURE(ts);
    // Said first, so it may call itself, and only the arm chosen worked out.
    CHECK(cpp.find("\tstatic value pinned_steps(value, value);\n") != std::string::npos);
    CHECK(cpp.find("\tstatic value pinned_steps(value n, value way) {\n\t\tif (compare(n, value(0)) <= 0) {\n\t\t\treturn value(0);\n\t\t}\n") != std::string::npos);
    CHECK(cpp.find("\t\tif (way.as_string() == \"slow\") return value(value(1).as_double() + pinned_steps(value(n.as_double() - value(1).as_double()), way).as_double());\n") != std::string::npos);
    CHECK(cpp.find("e.fields[\"x\"] = pinned_steps(e[\"w\"], value(\"fast\"));") != std::string::npos);
    CHECK(ts.find("type Value, view } from \"@uione/react/local\";") != std::string::npos);
    CHECK(ts.find("function pinned_steps(n: Value, way: Value): Value {\n  if (compare(n, 0) <= 0) {\n    return 0;\n  }\n") != std::string::npos);
    CHECK(ts.find("  if (text(way) === \"fast\") return (number(2) + number(pinned_steps((number(n) - number(1)), way)));\n  return null;\n}") != std::string::npos);
    CHECK(ts.find("e[\"x\"] = pinned_steps(e[\"w\"], \"fast\");") != std::string::npos);
}

TEST_CASE("a list is read, joined and drawn from at random on each target, and an enum's input is one of its choices") {
    const std::string stacked = with(with(board, "\t\tw      number  = 400\n", "\t\tw      number  = 400\n\t\tseen   list of layer\n"), "\tscreen \"Boards\"", R"(	enum way {
		top
		bottom
	}

	function cut(items list of layer, at number) {
		return drop(items, at) + first(items, at)
	}

	command layer::look {
		input from  way
		seen = cut(seen, random(1, count(seen))) + []
	}

	screen "Boards")");
    language::diagnostics out;
    auto files = parsed(stacked, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CAPTURE(cpp);
    CAPTURE(ts);
    CHECK(cpp.find("return one::local::joined(one::local::drop_of(items, at.as_double()), one::local::first_of(items, at.as_double()));") != std::string::npos);
    CHECK(cpp.find("if (!std::ranges::contains(std::vector<std::string>{\"top\", \"bottom\"}, c.input(\"from\").as_string())) return c.fail(\"from is one of top or bottom\");") != std::string::npos);
    CHECK(cpp.find("e.fields[\"seen\"] = one::local::joined(pinned_cut(e[\"seen\"], value(one::local::random(value(1).as_double(), value(one::local::counted(e[\"seen\"])).as_double()))), value(std::vector<value>{}));") != std::string::npos);
    CHECK(ts.find("counted, divided, dropOf, entity, failed, firstOf, joined, keyOf, localPackage, number, random, remainder,") != std::string::npos);
    CHECK(ts.find("return joined(dropOf(items, number(at)), firstOf(items, number(at)));") != std::string::npos);
    CHECK(ts.find("if (![\"top\", \"bottom\"].includes(text(c.input(\"from\")))) return c.fail(\"from is one of top or bottom\");") != std::string::npos);
    CHECK(ts.find("e[\"seen\"] = joined(pinned_cut(e[\"seen\"], (random(number(1), number((counted(e[\"seen\"])))))), []);") != std::string::npos);
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
    // A second launch hands its address to the copy holding the folder.
    CHECK(main->content.find("if (one::desktop::runtime::hand_over(\"org.uione.pinned\", count > 1 ? given[1] : \"/\")) return 0;") != std::string::npos);
    CHECK(main->content.find("one::client") == std::string::npos);
}

TEST_CASE("a local view lists what the store keeps of every row, like when it last changed") {
    language::diagnostics out;
    auto files = parsed(with(board, "\t\t\torder by name\n\t\t\tname\n", "\t\t\torder by name\n\t\t\tname  created_at  updated_at\n"), out);
    REQUIRE(out.empty());
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    CHECK(generated.files[0].content.find(R"(.fields({"name", "created_at", "updated_at"}))") != std::string::npos);
}

TEST_CASE("a local project's web app is given the same commands and views, on @uione/react/local") {
    language::diagnostics out;
    auto files = parsed(with(board, "\ttarget   desktop\n", "\ttarget   desktop web\n"), out);
    REQUIRE(out.empty());
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.web.size() == 1);
    CHECK(generated.web[0].path == "src/local.generated.ts");
    CHECK(generated.web[0].content == R"(// Generated from main.one by one. Do not edit.

import { all, by, command, compare, divided, entity, failed, keyOf, localPackage, number, remainder, row, same, subject, subjectField, text, truthy, view } from "@uione/react/local";

// Each namespace's entities, commands and views, kept in this browser.
export const packages = [
  localPackage("pinned",
    [
      entity("pinned::board", [
        { name: "name", required: true },
      ]),
      entity("pinned::layer", [
        { name: "board", required: true, refers: "pinned::board" },
        { name: "order", serial: "board" },
        { name: "kind", choices: ["picture", "words"], initial: "picture" },
        { name: "x", initial: 0 },
        { name: "w", initial: 400 },
      ])
    ],
    [
      command("pinned::board::create").creates("pinned::board"),
      command("pinned::layer::create").creates("pinned::layer"),
      command("pinned::layer::place").on("pinned::layer").fields(["x", "w"]).does((c, e) => {
        if (!(compare(e["w"], 0) > 0)) return c.fail("a layer has a size");
        return c.done();
      }),
      command("pinned::layer::raise").on("pinned::layer").does((c, e) => {
        if (!(!same(e["kind"], "words") || compare(e["w"], 0) > 0)) return c.fail("a layer has a size");
        e["x"] = (number(e["x"]) + number(1));
        return c.done();
      })
    ],
    [
      view("pinned::boards")
        .each(all("pinned::board"))
        .order([by("name")])
        .fields(["name"]),
      view("pinned::board_page")
        .per("pinned::board")
        .copy("name", "name")
        .count("pictures", all("pinned::layer").where("board", subject).where("kind", "picture"))
        .list("layers", all("pinned::layer").where("board", subject))
        .order([by("-order")])
        .fields(["x", "w", "kind"])
    ]),
];
)");
}

TEST_CASE("a local body that changes other rows is written the same way in both") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tcommand board::create\n", R"(	command board::create

	command board::clear {
		each layer where kind == kind::words {
			dispatch layer::update { id = id  x = 0 }
		}
		dispatch board::update { id = id  name = "cleared" }
	}

	command board::update
	command layer::update
)"),
                        out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CHECK(cpp.find(R"(for (value row : c.each(all("pinned::layer"))) {)") != std::string::npos);
    CHECK(ts.find(R"(for (const row of c.each(all("pinned::layer"))) {)") != std::string::npos);
    CHECK(cpp.find(R"(if (!same(row["kind"], value("words"))) continue;)") != std::string::npos);
    CHECK(ts.find(R"(if (!same(row["kind"], "words")) continue;)") != std::string::npos);
    CHECK(cpp.find(R"(if (auto done = c.update("pinned::layer", row["id"].as_string(), value(fields{{"x", value(0)}})); !done) return done;)") != std::string::npos);
    CHECK(ts.find(R"({ const done = c.update("pinned::layer", text(row["id"]), { "x": 0 }); if (done) return done; })") != std::string::npos);
    CHECK(cpp.find(R"(if (auto done = c.update("pinned::board", e["id"].as_string(), value(fields{{"name", value("cleared")}})); !done) return done;)") != std::string::npos);
    CHECK(ts.find(R"({ const done = c.update("pinned::board", text(e["id"]), { "name": "cleared" }); if (done) return done; })") != std::string::npos);
}

TEST_CASE("a local body makes and deletes rows by their plain commands, and names a row keyed by its own, in both") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tcommand board::create\n", R"(	entity shelf {
		board  board  required  key
		name   text   required  key
	}

	command board::create {
		dispatch shelf::create { board = id  name = "top" }
		dispatch layer::create { board = id }
	}

	command board::clear {
		each layer where board == id {
			dispatch layer::delete
		}
	}

	command layer::delete
	command shelf::create
	command shelf::delete {
		require name != "top"  "the top shelf stays"
	}

	command board::trim {
		each shelf where board == id {
			dispatch shelf::delete
		}
	}
)"),
                        out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    // A delete that does more than delete isn't run from another yet.
    REQUIRE(generated.errors.size() == 1);
    CHECK(generated.errors[0].message == "not supported yet in a project that says storage local: running a delete that does more than delete from another");
    auto plain = parsed(with(board, "\tcommand board::create\n", R"(	entity shelf {
		board  board  required  key
		name   text   required  key
	}

	entity board_note {
		board  board  required
		shelf  shelf
	}

	command board::create {
		dispatch shelf::create { board = id  name = "top" }
	}
	command board::clear {
		each layer where board == id {
			dispatch layer::delete
		}
	}
	command board_note::create
	command layer::delete
	command shelf::create
)"),
                        out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    generated = generators::generate_local(plain);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CHECK(cpp.find(R"(if (auto made = c.create("pinned::shelf", value(fields{{"board", e["id"]}, {"name", value("top")}})); !made) return c.fail(made.error());)") != std::string::npos);
    CHECK(ts.find(R"({ const made = c.create("pinned::shelf", { "board": e["id"], "name": "top" }); if (failed(made)) return made; })") != std::string::npos);
    CHECK(cpp.find(R"(if (auto done = c.remove("pinned::layer", row["id"].as_string()); !done) return done;)") != std::string::npos);
    CHECK(ts.find(R"({ const done = c.remove("pinned::layer", text(row["id"])); if (done) return done; })") != std::string::npos);
}

TEST_CASE("a / between two values divides, while a / alone after a screen's name is still its address") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tcommand layer::raise {", "\tcommand layer::halve {\n\t\tchanges x w\n\t\tx = x + w / 4\n\t\tw = w / 2\n\t}\n\n\tcommand layer::raise {"), out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CAPTURE(cpp);
    CAPTURE(ts);
    // Tighter than +, as * is.
    CHECK(cpp.find(R"(e.fields["x"] = value(e["x"].as_double() + value(one::local::divided(e["w"].as_double(), value(4).as_double())).as_double());)") != std::string::npos);
    CHECK(ts.find(R"(e["x"] = (number(e["x"]) + number((divided(number(e["w"]), number(4)))));)") != std::string::npos);
    CHECK(cpp.find(R"(e.fields["w"] = value(one::local::divided(e["w"].as_double(), value(2).as_double()));)") != std::string::npos);
}

TEST_CASE("min and max are the smaller and the larger of two numbers, in a command as on every target") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tcommand layer::raise {", "\tcommand layer::fit {\n\t\tinput room number\n\t\tw = min(room, w * 2)\n\t\tx = max(0, x)\n\t}\n\n\tcommand layer::raise {"), out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CAPTURE(cpp);
    CAPTURE(ts);
    CHECK(cpp.find("std::min<double>(") != std::string::npos);
    CHECK(cpp.find("std::max<double>(") != std::string::npos);
    CHECK(ts.find("Math.min(") != std::string::npos);
    CHECK(ts.find("Math.max(") != std::string::npos);
    language::diagnostics wrong;
    auto three = parsed(with(board, "\tcommand layer::raise {", "\tcommand layer::fit {\n\t\tw = min(w, 1, 2)\n\t}\n\n\tcommand layer::raise {"), wrong);
    language::check(three, wrong);
    CHECK(std::ranges::any_of(wrong, [](const auto& d) { return d.message == "min takes 2 arguments, not 3"; }));
}

TEST_CASE("a % leaves what's over as a clock counts, and was reads what the command's entity held before it") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tcommand layer::raise {", "\tcommand layer::turn {\n\t\tx = (was x + 3) % 4\n\t\tw = was x\n\t}\n\n\tcommand layer::raise {"), out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CAPTURE(cpp);
    CAPTURE(ts);
    // -1 % 4 is 3, so a turn left three times over is a turn right.
    CHECK(cpp.find(R"(e.fields["x"] = value(one::local::remainder(value(c.at("pinned::layer", e["id"])["x"].as_double() + value(3).as_double()).as_double(), value(4).as_double()));)") != std::string::npos);
    CHECK(ts.find(R"(e["x"] = (remainder(number((number(c.at("pinned::layer", e["id"])["x"]) + number(3))), number(4)));)") != std::string::npos);
    // What it held, not what the line before made it.
    CHECK(cpp.find(R"(e.fields["w"] = c.at("pinned::layer", e["id"])["x"];)") != std::string::npos);
    CHECK(ts.find(R"(e["w"] = c.at("pinned::layer", e["id"])["x"];)") != std::string::npos);
}

TEST_CASE("a local each reads its where's right side as the command's, and leaves its rows as its dispatches left them") {
    language::diagnostics out;
    auto files = parsed(with(board, "\tcommand board::create\n", R"(	command board::create

	command board::reset {
		each layer where board == id || x == id {
			dispatch layer::update { w = 400 }
		}
	}

	command board::update
	command layer::update
)"),
                        out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_local(files);
    for (const auto& e : generated.errors) FAIL_CHECK(language::format(e));
    REQUIRE(generated.files.size() == 1);
    REQUIRE(generated.web.size() == 1);
    const std::string& cpp = generated.files[0].content;
    const std::string& ts = generated.web[0].content;
    CAPTURE(cpp);
    CAPTURE(ts);
    // The board's id each time, not the layer's own.
    CHECK(cpp.find(R"(if (!(same(row["board"], e["id"]) || same(row["x"], e["id"]))) continue;)") != std::string::npos);
    CHECK(ts.find(R"(if (!(same(row["board"], e["id"]) || same(row["x"], e["id"]))) continue;)") != std::string::npos);
    // A dispatch with no id acts on the row, and the row as it was read isn't written back over it.
    CHECK(cpp.find(R"(if (auto done = c.update("pinned::layer", row["id"].as_string(), value(fields{{"w", value(400)}})); !done) return done;)") != std::string::npos);
    CHECK(ts.find(R"({ const done = c.update("pinned::layer", text(row["id"]), { "w": 400 }); if (done) return done; })") != std::string::npos);
    CHECK(cpp.find(R"(c.update("pinned::layer", row["id"].as_string(), row))") == std::string::npos);
    CHECK(ts.find(R"(c.update("pinned::layer", text(row["id"]), row))") == std::string::npos);
}

TEST_CASE("one build writes a local project's web app on localSource, with no Firebase, service or sign-in") {
    language::diagnostics out;
    auto files = parsed(with(board, "\ttarget   desktop\n", "\ttarget   desktop web\n"), out);
    REQUIRE(out.empty());
    auto dir = std::filesystem::temp_directory_path() / "one-local-project";
    auto whole = generators::generate_project(files, dir.string(), (dir / "build").string(), {"web", "desktop"});
    for (const auto& e : whole.errors) FAIL_CHECK(language::format(e));
    for (const auto& f : whole.files) {
        CAPTURE(f.path);
        CHECK((f.path.starts_with("web/") || f.path.starts_with("desktop/")));
    }
    auto content = [&](const std::string& path) {
        auto found = std::ranges::find_if(whole.files, [&](const auto& f) { return f.path == path; });
        REQUIRE(found != whole.files.end());
        return found->content;
    };
    CHECK(content("web/src/local.generated.ts").find("localPackage(\"pinned\",") != std::string::npos);
    CHECK(content("desktop/src/local/pinned.cpp").find("pinned.install(source);") != std::string::npos);
    std::string app = content("web/src/app.tsx");
    CHECK(app.find("import { localSource } from \"@uione/react/local\";") != std::string::npos);
    CHECK(app.find("import { packages } from \"./local.generated\";") != std::string::npos);
    CHECK(app.find("const data = localSource({ name: \"pinned\", packages });") != std::string::npos);
    CHECK(app.find("authentication: false") != std::string::npos);
    CHECK(app.find("firebase") == std::string::npos);
    CHECK(content("web/package.json").find("firebase") == std::string::npos);
    CHECK(content("web/vite.config.ts").find("/api") == std::string::npos);
}
