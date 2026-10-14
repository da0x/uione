// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::terminal::runtime's tests: screens drawn in cells as the app draws them,
// compared as text with tests/golden, and keys pressed on them, all on one::screens'
// in-memory source. ONE_TERMINAL_GOLDEN=write draws the text again, to be read
// before it's committed.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "check.hpp"
#include "one/terminal/runtime/components.hpp"

namespace {

	using namespace one::terminal::runtime;
	using fields = std::map<std::string, value>;
	using list = std::vector<value>;
	namespace terminal = one::terminal;

	const std::filesystem::path golden_folder = ONE_TERMINAL_TESTS "/golden";

	// The terminal as a navigator's site draws it, at a size.
	struct screen_at {
		terminal::view_host host;
		int width, height;
		screen_at(navigator& nav, int w = 80, int h = 24) : host([&nav] { return drawn(nav); }), width(w), height(h) {}
		std::string text() {
			host.bring_up_to_date(width, height);
			terminal::canvas c(width, height);
			host.draw(c);
			return c.text();
		}
		bool press(terminal::key_press e) {
			host.bring_up_to_date(width, height);
			return host.deliver(e);
		}
		bool type(const std::string& words) {
			bool all = true;
			for (char c : words) all = press({.text = std::string(1, c)}) && all;
			return all;
		}
	};

	void matches_golden(const std::string& name, const std::string& drawn) {
		auto file = golden_folder / (name + ".txt");
		const char* mode = std::getenv("ONE_TERMINAL_GOLDEN");
		if (mode && std::string(mode) == "write") {
			std::filesystem::create_directories(golden_folder);
			std::ofstream(file) << drawn;
			return;
		}
		std::ifstream in(file);
		CHECK_SAYING(in.good(), "no golden text " + file.string() + "; draw it with ONE_TERMINAL_GOLDEN=write and read it");
		std::stringstream golden;
		golden << in.rdbuf();
		CHECK_SAYING(golden.str() == drawn, "what's drawn differs from " + file.string() + ":\n" + drawn);
	}

	terminal::key_press key(int code) { return {.code = code}; }
	terminal::key_press letter(char c) { return {.text = std::string(1, c)}; }

	site tracker() {
		site s{.name = "tracker"};
		s.screens.push_back({"Projects", "/projects", "Projects", [](page& p) {
			                     return items(table(p, {.view = p.view("tracker::projects"), .columns = {{"name", "Name"}, {"slug", "Project"}}, .link = "/projects/:project"}));
		                     }});
		s.screens.push_back({"", "/projects/:project", "", [](page& p) {
			                     auto project = p.view("tracker::project_page", p.param("project"));
			                     p.title({live(project, "name")});
			                     crumbs(p, {{"/projects", {words("Projects")}}});
			                     subtitle(p, {words("Kept at "), live(project, "repository")});
			                     p.beside_title(command(p, {.command = "tracker::project::archive", .label = "Archive"}));
			                     return items(details(p, project, {{"visibility", "Visibility"}, {"owner.name", "Owner"}}),
			                                  section({"Issues"}, table(p, {.view = project,
			                                                                .list = "issues",
			                                                                .columns = {{"number", "#"}, {"title", "Title"}, {"status", "Status"}},
			                                                                .link = "/issues/:issue",
			                                                                .actions = {{.command = "tracker::issue::close", .label = "Close",
			                                                                             .when = [](const value& row) { return row["status"].as_string() == "open"; }}}})),
			                                  form(p, {.command = "tracker::issue::create", .fields = {{"title"}, {.name = "body", .label = "Description", .type = "markdown"}},
			                                           .given = {{"project", p.param("project")}}, .submit = "Open issue"}));
		                     }});
		s.screens.push_back({"", "/issues/:issue", "", [](page& p) {
			                     auto issue = p.view("tracker::issue_page", p.param("issue"));
			                     p.title({live(issue, "title")});
			                     auto doing = [&p](std::string label) { return command(p, {.command = "tracker::issue::" + label, .label = label}); };
			                     return items(layout("two_columns",
			                                         region{"main", items(markdown(p, issue, "body"))},
			                                         region{"side", items(actions(doing("Edit"), doing("Link an issue"), doing("Transfer to another board"), doing("Copy issue")),
			                                                              details(p, issue, {{"status", "Status"}, {"labels", "Labels"}, {"milestone", "Milestone"}}, {},
			                                                                      std::nullopt, {"labels"}))}));
		                     }});
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
		data.set("tracker::issue_page", "atlas-12",
		         value(fields{{"title", "Copy an issue whole"},
		                      {"body", "Copy its title, body and labels, so a new one starts where this one is, with its links and comments left behind."},
		                      {"status", "open"},
		                      {"labels", list{"issues", "", "copy"}},
		                      {"milestone", ""}}));
		data.sign_in(person{"maya", "Maya Garcia"});
	}

}  // namespace

TEST("a project's page in a terminal: the bar, the trail above, its title, details, issues and a form") {
	memory_source data;
	fill(data);
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects");
	nav.open("/projects/atlas");
	screen_at terminal_now(nav, 80, 40);
	matches_golden("project-page", terminal_now.text());
}

TEST("an issue in two columns where the terminal is wide, one after the other where it's narrow, its buttons going on to new lines") {
	memory_source data;
	fill(data);
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/issues/atlas-12");
	screen_at wide(nav, 120, 20);
	matches_golden("issue-wide", wide.text());
	screen_at narrow(nav, 60, 30);
	matches_golden("issue-narrow", narrow.text());
}

TEST("keys open a row, type into a form and send it, and Esc goes back") {
	memory_source data;
	fill(data);
	data.on("tracker::issue::create", [](const value& f, memory_source&) -> answer { return "atlas-" + f["title"].as_string(); });
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects");
	screen_at t(nav, 80, 40);
	t.text();
	CHECK(t.press(key(terminal::keys::newline)));  // the projects' list has the keys; Enter opens Atlas
	CHECK(nav.current().param("project") == "atlas");
	t.text();
	// Tab past the bar's links, the trail, Archive, the issues and their Close, to the form's title.
	for (int tabs = 0; tabs < 20 && !t.host.cursor; ++tabs) {
		t.press(key(terminal::keys::tab));
		t.text();
	}
	CHECK(t.host.cursor.has_value());  // a field has the keyboard, and its cursor shows
	t.type("Draw in cells");
	t.press(key(terminal::keys::tab));  // to the description
	t.press(key(terminal::keys::tab));  // to Open issue
	t.press(key(terminal::keys::newline));
	CHECK(!data.runs().empty());
	if (!data.runs().empty()) {
		CHECK(data.runs().back().command == "tracker::issue::create");
		CHECK(data.runs().back().fields["title"].as_string() == "Draw in cells");
		CHECK(data.runs().back().fields["project"].as_string() == "atlas");
	}
	CHECK(nav.depth() == 2);
	nav.back();
	CHECK(nav.depth() == 1);
}

namespace {

	site with_board() {
		site s{.name = "neotrac"};
		s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
			                     auto board_page = p.view("projects::board_page", "atlas-main");
			                     auto roles = p.view("projects::member_roles");
			                     return items(board(p, {.view = board_page,
			                                            .list = "issues",
			                                            .by = "stage",
			                                            .over = board_page,
			                                            .over_list = "stages",
			                                            .columns = {{"title", "Title"}, {"number", "#"}, {"labels", "Labels"}, {"assignees.name", "Assigned to"}},
			                                            .people = {{"assignees.name", "assignees.picture"}},
			                                            .labels = {"labels"},
			                                            .move = board_move{.command = "projects::issue::move", .steps = board_page, .list = "steps", .held = "roles", .roles = roles, .within = "atlas"},
			                                            .tint = tint_spec{"priority", {"critical", "high", "normal", "low", "trivial"}}}));
		                     }});
		return s;
	}

	void fill_board(memory_source& data) {
		auto issue = [](std::string id, int n, std::string title, std::string stage, std::string priority, list labels = {}, list people = {}) {
			return value(fields{{"id", id}, {"number", n}, {"title", title}, {"stage", stage}, {"priority", priority}, {"labels", labels}, {"assignees.name", people}});
		};
		data.set("projects::board_page", "atlas-main",
		         value(fields{{"stages", list{value(fields{{"id", "s1"}, {"title", "To do"}}), value(fields{{"id", "s2"}, {"title", "Doing"}}), value(fields{{"id", "s3"}, {"title", "Done"}})}},
		                      {"steps", list{value(fields{{"from", "s1"}, {"to", "s2"}, {"roles", list{"atlas-contributor"}}}), value(fields{{"from", "s2"}, {"to", "s3"}, {"roles", list{"atlas-maintainer"}}})}},
		                      {"issues", list{issue("atlas-12", 12, "Copy an issue whole", "s1", "high", list{"ui"}, list{"Maya Garcia"}),
		                                      issue("atlas-14", 14, "Draw boards natively", "s1", "normal"),
		                                      issue("atlas-9", 9, "Search by label", "s2", "critical", list{"search"}, list{"Ade Bello"})}}}));
		data.set("projects::member_roles", "", value(fields{{"rows", list{value(fields{{"project", "atlas"}, {"role", "atlas-contributor"}})}}}));
	}

	site with_canvas() {
		site s{.name = "neotrac"};
		s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
			                     auto board_page = p.view("projects::board_page", "atlas-main");
			                     return items(canvas_board(p, {.view = board_page,
			                                                   .list = "issues",
			                                                   .columns = {{"title", "Title"}, {"number", "#"}},
			                                                   .place = "projects::issue::place",
			                                                   .grid = 160,
			                                                   .origin_x = 0,
			                                                   .origin_y = 0,
			                                                   .by = "stage",
			                                                   .over = board_page,
			                                                   .over_list = "stages"}));
		                     }});
		return s;
	}

}  // namespace

TEST("a board in a terminal: its columns one after another, and m moving a card along a step the reader may take") {
	memory_source data;
	fill_board(data);
	data.on("projects::issue::move", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	screen_at t(nav, 80, 30);
	matches_golden("board", t.text());
	CHECK(t.press(letter('m')));  // on the first card, #12, which the To do list has chosen
	matches_golden("board-moving", t.text());
	CHECK(t.press(key(terminal::keys::newline)));  // the one step it may take: Doing
	CHECK(data.runs().back().fields["stage"].as_string() == "s2");
}

TEST("a canvas in a terminal: its cards as boxes where they're laid out, n choosing one and each arrow placing it a grid step over") {
	memory_source data;
	fill_board(data);
	data.on("projects::issue::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	auto s = with_canvas();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	screen_at t(nav, 80, 30);
	matches_golden("canvas", t.text());
	CHECK(t.press(key(terminal::keys::tab)));  // to the canvas
	CHECK(t.press(letter('n')));               // the first card, #12, in To do
	CHECK(t.press(key(terminal::keys::down)));
	CHECK(t.press(key(terminal::keys::down)));  // past #14, below it in To do
	CHECK(!data.runs().empty());
	if (data.runs().empty()) return;
	CHECK(data.runs().back().command == "projects::issue::place");
	CHECK(data.runs().back().fields["y"].as_double() == 320);
	CHECK(data.runs().back().fields["x"].as_double() == 0);
	matches_golden("canvas-placed", t.text());
}

int main() {
	use_time_zone("UTC");
	return check::run();
}
