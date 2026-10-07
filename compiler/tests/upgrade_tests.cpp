// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Upgrading a project to this compiler, and the version it records: the fixes a
// change to the language comes with, applied until the project is clean, and
// nothing changed when it can't be.

#include <filesystem>

#include <doctest/doctest.h>
#include "driver.hpp"
#include "platform/files.hpp"
#include "toolchain.hpp"

using namespace one;

namespace {

    std::filesystem::path project(const std::string& name, const std::string& source) {
        auto dir = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        platform::write_file((dir / "main.one").string(), source);
        return dir;
    }

} // namespace

TEST_CASE("an upgrade renames signin and signed_in, and keeps the settings lined up") {
    auto dir = project("uione-upgrade-authentication",
                       "project shop {\n\tone     \"0.5.10\"\n\tregion  \"us-east4\"\n\tsignin  google\n\tenvironment production {\n\t\tdomain    \"shop.example\"\n\t\tfirebase  \"shop-example\"\n\t}\n}\n"
                       "entity order {\n\ttitle text\n}\ncommand order::create {\n\tpermission signed_in\n}\n");
    auto done = driver::upgrade(dir.string(), "9.9.9");
    REQUIRE(done.problems.empty());
    CHECK(done.fixes == 2);
    const auto& text = done.changed.begin()->second;
    CHECK(text.find("\tone             \"9.9.9\"\n\tregion          \"us-east4\"\n\tauthentication  google\n") != std::string::npos);
    CHECK(text.find("\t\tdomain    \"shop.example\"") != std::string::npos);  // an environment's settings are its own
    CHECK(text.find("permission authenticated") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("an upgrade applies the fixes, records the version, and checks clean") {
    auto dir = project("uione-upgrade", "project shop {\n\tsignin  google\n}\n"
                                        "entity order {\n\tstatus  open | shipped = open\n}\n"
                                        "command order::ship {\n\trequire status == open  \"shipped already\"\n\tstatus = shipped\n}\n");
    auto done = driver::upgrade(dir.string(), "9.9.9");
    REQUIRE(done.problems.empty());
    CHECK(done.fixes == 5);
    CHECK(done.recorded);
    REQUIRE(done.changed.size() == 1);
    const auto& text = done.changed.begin()->second;
    CHECK(text.find("\tone             \"9.9.9\"\n\tauthentication  google") != std::string::npos);  // lined up with authentication
    CHECK(text.find("status  enum  open | shipped = status::open") != std::string::npos);
    CHECK(text.find("require status == status::open") != std::string::npos);
    CHECK(text.find("status = status::shipped") != std::string::npos);
    // The upgrade didn't write anything; the command line does.
    CHECK(platform::read_file((dir / "main.one").string())->find("enum") == std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("an upgrade lines an enum's choices up with the other fields' rules") {
    auto dir = project("uione-upgrade-align", "namespace tracker {\n"
                                              "\tentity project {\n\t\tname  text  required\n\t}\n"
                                              "\tentity member {\n"
                                              "\t\tproject  project  required  key\n"
                                              "\t\tperson   user     required  key\n"
                                              "\t\trole     maintainer | reporter = reporter\n"
                                              "\t}\n"
                                              "}\n");
    auto done = driver::upgrade(dir.string(), "9.9.9");
    REQUIRE(done.problems.empty());
    REQUIRE(done.changed.size() == 1);
    const auto& text = done.changed.begin()->second;
    CHECK(text.find("\t\tperson   user     required  key\n"
                    "\t\trole     enum     maintainer | reporter = role::reporter\n") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("an upgrade changes nothing while a mistake has no fix") {
    auto dir = project("uione-upgrade-stuck", "entity order {\n\tstatus  open | shipped = open\n\ttotal  money\n}\n");
    auto done = driver::upgrade(dir.string(), "9.9.9");
    REQUIRE(done.problems.size() == 1);
    CHECK(done.problems[0].message.find("money") != std::string::npos);
    CHECK(done.changed.empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("an upgrade replaces a version already recorded") {
    {
        driver::sources files{{"main.one", "project shop {\n\tone  \"0.4.0\"\n\tui   radix\n}\n"}};
        CHECK(driver::record_version(files, "0.5.0"));
        CHECK(files["main.one"] == "project shop {\n\tone  \"0.5.0\"\n\tui   radix\n}\n");
    }
    driver::sources none{{"main.one", "entity order {\n\ttitle  text\n}\n"}};
    CHECK_FALSE(driver::record_version(none, "0.5.0"));
}

TEST_CASE("a project's version is read from its text, without parsing it") {
    // Written for a compiler whose language this one may not read.
    auto dir = project("uione-pinned", "project shop {\n\tone  \"0.3.0\"\n\tsome  future  syntax {\n}\n");
    CHECK(toolchain::pinned(dir.string()) == "0.3.0");
    platform::write_file((dir / "main.one").string(), "project shop {\n\tui  radix\n}\nentity x {\n\tone  \"1.0.0\"\n}\n");
    CHECK_FALSE(toolchain::pinned(dir.string()).has_value());  // only the project block names it
    std::filesystem::remove_all(dir);
}

TEST_CASE("a project's version says which compiler to hand to, unless it's this one or switching is off") {
    auto a = project("uione-wanted-a", "project a {\n\tone  \"0.3.0\"\n}\n");
    auto b = project("uione-wanted-b", "project b {\n\tone  \"0.4.0\"\n}\n");
    std::string why;
    CHECK(toolchain::wanted({a.string()}, "0.4.0", why) == "0.3.0");
    CHECK_FALSE(toolchain::wanted({b.string()}, "0.4.0", why).has_value());
    CHECK_FALSE(toolchain::wanted({a.string(), b.string()}, "0.4.0", why).has_value());
    CHECK(why == "these projects are for different compilers, 0.3.0 and 0.4.0; check them one at a time");
    setenv("UIONE_TOOLCHAIN", "local", 1);
    why.clear();
    CHECK_FALSE(toolchain::wanted({a.string()}, "0.4.0", why).has_value());
    unsetenv("UIONE_TOOLCHAIN");
    std::filesystem::remove_all(a);
    std::filesystem::remove_all(b);
}

TEST_CASE("a project names its compiler's version as one, like one \"0.4.0\"") {
    driver::sources bad{{"main.one", "project shop {\n\tone  \"latest\"\n}\n"}};
    language::diagnostics found;
    driver::check_sources(bad, found);
    REQUIRE(found.size() == 1);
    CHECK(found[0].message == "one names the compiler's version, like one \"0.4.0\"");
}

TEST_CASE("each screen is outlined by line: its layout, its regions and their items") {
    language::diagnostics found;
    std::vector<language::file> files;
    files.push_back(language::parse("issues.one", R"(namespace tracker {
	screen "Issue" /issues/:issue layout two_columns {
		main {
			markdown "{issue_page.body}"
			thread issue_page.comments
		}
		side {
			issue::close "Close" when issue_page.open
			details issue_page {
				status "Status"
			}
		}
	}
	screen "Issues" /issues {
		table issues
	}
}
)", found));
    REQUIRE(found.empty());
    auto screens = driver::screens_of(files);
    REQUIRE(screens.size() == 2);
    const auto& issue = screens[0];
    CHECK(issue.path == "issues.one");
    CHECK(issue.title == "Issue");
    CHECK(issue.route == "/issues/:issue");
    CHECK(issue.line == 2);
    CHECK(issue.layout == "two_columns");
    CHECK(issue.layout_line == 2);
    REQUIRE(issue.items.size() == 2);
    CHECK(issue.items[0].kind == "region");
    CHECK(issue.items[0].subject == "main");
    CHECK(issue.items[0].line == 3);
    REQUIRE(issue.items[0].items.size() == 2);
    CHECK(issue.items[0].items[0].kind == "markdown");
    CHECK(issue.items[0].items[1].kind == "thread");
    CHECK(issue.items[0].items[1].subject == "issue_page.comments");
    CHECK(issue.items[0].items[1].line == 5);
    const auto& side = issue.items[1];
    CHECK(side.subject == "side");
    REQUIRE(side.items.size() == 2);
    CHECK(side.items[0].kind == "button");
    CHECK(side.items[0].subject == "issue::close");
    CHECK(side.items[0].label == "Close");
    CHECK(side.items[1].kind == "details");
    CHECK(side.items[1].line == 9);
    CHECK(screens[1].layout.empty());
    CHECK(screens[1].layout_line == 0);
    REQUIRE(screens[1].items.size() == 1);
    CHECK(screens[1].items[0].kind == "table");
    CHECK(screens[1].items[0].subject == "issues");
}

TEST_CASE("a table is outlined with its columns, its settings and what its rows hold") {
    language::diagnostics found;
    std::vector<language::file> files;
    files.push_back(language::parse("issues.one", R"(namespace tracker {
	entity issue {
		title   text
		status  enum  open | closed
	}
	view board {
		issues = each issue {
			title  status  author.name
		}
	}
	screen "Board" /board {
		table board.issues by status link /issues/:issue {
			search title
			sort title descending
			page 25
			title "Title"
			status
			close "Close" when status == status::open  // as written
		}
	}
}
)", found));
    REQUIRE(found.empty());
    auto screens = driver::screens_of(files);
    REQUIRE(screens.size() == 1);
    REQUIRE(screens[0].items.size() == 1);
    REQUIRE(screens[0].items[0].table);
    const auto& t = *screens[0].items[0].table;
    REQUIRE(t.columns.size() == 3);
    CHECK(t.columns[0].value == "title");
    CHECK(t.columns[0].label == "Title");
    CHECK(t.columns[0].labeled);
    CHECK(t.columns[0].line == 16);
    CHECK(t.columns[1].value == "status");
    CHECK_FALSE(t.columns[1].labeled);
    CHECK(t.columns[1].when.empty());
    CHECK(t.columns[2].value == "close");
    CHECK(t.columns[2].when == "status == status::open");
    REQUIRE(t.rows.size() == 3);
    CHECK(t.rows[0].name == "title");
    CHECK_FALSE(t.rows[0].choices);
    CHECK(t.rows[1].name == "status");
    CHECK(t.rows[1].choices);
    CHECK(t.rows[2].name == "author.name");
    CHECK(t.by == "status");
    CHECK(t.search == std::vector<std::string>{"title"});
    CHECK(t.sort == "-title");
    CHECK(t.page == 25);
    CHECK(t.link == "/issues/:issue");
}
