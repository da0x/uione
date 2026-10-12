// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// one::desktop::runtime's tests: addresses and keys made as the web makes them,
// pages opened by address and left, views read live from a source, commands run and
// turned down, and a whole window of a small tracker drawn and compared with its
// golden image, all on the in-memory source.

#include <cstdlib>
#include <filesystem>
#include <format>

#include "check.hpp"
#include "offscreen.hpp"
#include "one/desktop/runtime/components.hpp"
#include "ui_internal.hpp"

namespace {

	using namespace one::desktop::runtime;
	using fields = std::map<std::string, value>;
	using list = std::vector<value>;

	const std::filesystem::path golden_folder = ONE_DESKTOP_TESTS "/golden";

	const one::desktop::theme& tested_theme() {
		static one::desktop::theme t = [] {
			one::desktop::theme made = one::desktop::theme::harbor();
			made.text_family = "Noto Sans";
			made.code_family = "Noto Sans Mono";
			return made;
		}();
		return t;
	}

	void matches_golden(const std::string& name, one::desktop::size size, const std::function<one::desktop::view()>& build) {
		auto drawn = one::desktop::offscreen::draw(size, 1, tested_theme().page, [&](one::desktop::canvas& c) {
			one::desktop::detail::theme_in_use() = &tested_theme();
			auto root = build().take();
			root->place({0, 0, static_cast<float>(size.width), static_cast<float>(size.height)});
			root->paint(c);
		});
		auto file = golden_folder / (name + ".png");
		const char* mode = std::getenv("ONE_DESKTOP_GOLDEN");
		if (mode && std::string(mode) == "write") {
			std::filesystem::create_directories(golden_folder);
			one::desktop::offscreen::write_png(file, drawn);
			return;
		}
		auto golden = one::desktop::offscreen::read_png(file);
		CHECK_SAYING(golden.has_value(), "no golden image " + file.string() + "; draw it with ONE_DESKTOP_GOLDEN=write and look at it");
		if (!golden) return;
		std::size_t different = one::desktop::offscreen::differences(drawn, *golden, 3);
		std::size_t allowed = drawn.rgba.size() / 4 / 2000;
		if (different > allowed) one::desktop::offscreen::write_png(std::filesystem::temp_directory_path() / (name + ".drawn.png"), drawn);
		CHECK_SAYING(different <= allowed, std::format("{} pixels differ from {}", different, file.string()));
	}

	// A small tracker: its projects, and a project's page with its issues.
	site tracker() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Projects", "/projects", "Projects", [](page& p) {
			                     return items(table(p, {.view = p.view("tracker::projects"), .columns = {{"name", "Name"}, {"slug", "Project"}}, .link = "/projects/:project"}));
		                     }});
		s.screens.push_back({"", "/projects/:project", "", [](page& p) {
			                     auto project = p.view("tracker::project_page", p.param("project"));
			                     auto roles = p.view("tracker::member_roles");
			                     p.title({live(project, "name")});
			                     crumbs(p, {{"/projects", {words("Projects")}}});
			                     subtitle(p, {words("Kept at "), live(project, "repository")});
			                     bool may = allows(roles, "project", p.param("project"), "issue::create");
			                     p.beside_title(command(p, {.command = "tracker::project::archive", .label = "Archive", .allowed = may}));
			                     return items(details(p, project, {{"visibility", "Visibility"}, {"owner.name", "Owner"}}),
			                                  section({"Issues"}, table(p, {.view = project,
			                                                               .list = "issues",
			                                                               .columns = {{"number", "#"}, {"title", "Title"}, {"status", "Status"}},
			                                                               .link = "/issues/:issue",
			                                                               .actions = {{.command = "tracker::issue::close", .label = "Close", .allowed = may,
			                                                                            .when = [](const value& row) { return row["status"].as_string() == "open"; }}}})));
		                     }});
		s.screens.push_back({"Issue", "/issues/:issue", "", [](page& p) { return items(text({words("Issue "), words(p.param("issue"))})); }});
		return s;
	}

	void fill(memory_source& data) {
		data.set("tracker::projects", "", value(fields{{"rows", list{value(fields{{"id", "atlas"}, {"name", "Atlas"}, {"slug", "atlas"}})}}}));
		data.set("tracker::project_page", "atlas",
		         value(fields{{"name", "Atlas"},
		                      {"repository", "github.com/example/atlas"},
		                      {"visibility", "public"},
		                      {"owner", value(fields{{"name", "Maya Garcia"}})},
		                      {"issues", list{value(fields{{"id", "atlas-12"}, {"number", 12}, {"title", "Copy an issue whole"}, {"status", "open"}}),
		                                      value(fields{{"id", "atlas-9"}, {"number", 9}, {"title", "Search by label"}, {"status", "closed"}})}}}));
		data.set("tracker::member_roles", "",
		         value(fields{{"rows", list{value(fields{{"project", "atlas"}, {"role", "atlas-maintainer"}, {"role.may", list{"issue::create", "issue::close", "project::archive"}}})}}}));
		data.sign_in(person{"maya", "Maya Garcia"});
	}

}  // namespace

TEST("an id is made of its key's parts and cut back into them, as the web does") {
	CHECK(key_of({"atlas", "in-progress"}) == "atlas-in%2Dprogress");
	auto parts = parts_of("atlas-in%2Dprogress", 2);
	CHECK((parts && *parts == std::vector<std::string>{"atlas", "in-progress"}));
	CHECK(!parts_of("atlas", 2).has_value());
	CHECK(key_of({"a b", "c"}) == "a%20b-c");
}

TEST("a row opens its own address, filled from the screen's and the row's") {
	value issue(fields{{"id", "atlas-12"}, {"project", "atlas"}});
	CHECK(row_link("/issues/:issue", issue, {}) == "/issues/atlas-12");
	CHECK(row_link("/:project/issues/:issue", issue, {}) == "/atlas/issues/atlas-12");
	CHECK(row_link("/:project/issues/:issue", issue, {{"project", "other"}}) == "/other/issues/atlas-12");
	// A project keyed by its owner and its name, at /:owner/:project.
	value project(fields{{"id", "maya-atlas"}});
	CHECK(row_link("/:owner/:project", project, {}, {"owner"}) == "/maya/atlas");
	// A row's board, by its id, fills /:board with the board's own name.
	value card(fields{{"id", "atlas-12"}, {"board", "atlas-main"}});
	CHECK(row_link("/:project/boards/:board/issues/:issue", card, {{"project", "atlas"}}, {}, {{"board", 2}}) == "/atlas/boards/main/issues/atlas-12");
	// A change holds the issue it's of, and links to that.
	value change(fields{{"id", "change-1"}, {"issue", "atlas-12"}});
	CHECK(row_link("/issues/:issue", change, {}) == "/issues/atlas-12");
}

TEST("a page opens by its address, with what its parameters said, and goes back") {
	memory_source data;
	auto s = tracker();
	navigator nav(s, data);
	CHECK(!nav.open("/nowhere"));
	CHECK(nav.open("/projects"));
	CHECK(nav.open("/projects/atlas%20one"));
	CHECK(nav.current().param("project") == "atlas one");
	CHECK(nav.depth() == 2);
	CHECK(nav.back());
	CHECK(nav.current().shown().route == "/projects");
	CHECK(!nav.back());  // the first page stays
}

TEST("a page's views are read live, and its title with them") {
	memory_source data;
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	auto project = p.view("tracker::project_page", "atlas");
	CHECK(project->status == status::missing);
	fill(data);
	CHECK(project->status == status::ready);
	p.title({live(project, "name"), words(" project")});
	CHECK(p.title() == "Atlas project");
	data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas 2"}}));
	CHECK(p.title() == "Atlas 2 project");
	CHECK(&*p.view("tracker::project_page", "atlas") == &*project);  // listened to once
}

TEST("a command runs, and what turns it down is the page's problem until the next") {
	memory_source data;
	data.on("tracker::issue::close", [](const value& f, memory_source&) -> answer {
		if (f["id"].as_string() == "atlas-9") return std::unexpected(std::string("that issue is already closed"));
		return "atlas-12";
	});
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	std::string answered;
	p.run("tracker::issue::close", value(fields{{"id", "atlas-12"}}), [&](const answer& a) { answered = a.value_or(""); });
	CHECK(answered == "atlas-12");
	CHECK(p.problem().peek().empty());
	p.run("tracker::issue::close", value(fields{{"id", "atlas-9"}}));
	CHECK(p.problem().peek() == "that issue is already closed");
	CHECK(data.runs().size() == 2);
	CHECK(!p.running("tracker::issue::close"));
}

TEST("a project's page: the bar, its trail, live title, details, and its issues with what may be done to them") {
	memory_source data;
	fill(data);
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects");
	nav.open("/projects/atlas");
	matches_golden("project-page", {900, 560}, [&] { return nav.show(); });
}

int main() { return check::run(); }
