// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::terminal::runtime's tests: screens drawn in cells as the app draws them,
// compared as text with tests/golden, and keys pressed on them, all on one::screens'
// in-memory source. ONE_TERMINAL_GOLDEN=write draws the text again, to be read
// before it's committed.

#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <format>
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

	// first: more of the first card's fields, like who holds it.
	void fill_board(memory_source& data, const fields& first = {}) {
		auto issue = [&first](std::string id, int n, std::string title, std::string stage, std::string priority, list labels = {}, list people = {}) {
			fields f{{"id", id}, {"number", n}, {"title", title}, {"stage", stage}, {"priority", priority}, {"labels", labels}, {"assignees.name", people}};
			if (n == 12) f.insert(first.begin(), first.end());
			return value(f);
		};
		data.set("projects::board_page", "atlas-main",
		         value(fields{{"stages", list{value(fields{{"id", "s1"}, {"title", "To do"}}), value(fields{{"id", "s2"}, {"title", "Doing"}}), value(fields{{"id", "s3"}, {"title", "Done"}})}},
		                      {"steps", list{value(fields{{"from", "s1"}, {"to", "s2"}, {"roles", list{"atlas-contributor"}}}), value(fields{{"from", "s2"}, {"to", "s3"}, {"roles", list{"atlas-maintainer"}}})}},
		                      {"issues", list{issue("atlas-12", 12, "Copy an issue whole", "s1", "high", list{"ui"}, list{"Maya Garcia"}),
		                                      issue("atlas-14", 14, "Draw boards natively", "s1", "normal"),
		                                      issue("atlas-9", 9, "Search by label", "s2", "critical", list{"search"}, list{"Ade Bello"})}}}));
		data.set("projects::member_roles", "", value(fields{{"rows", list{value(fields{{"project", "atlas"}, {"role", "atlas-contributor"}})}}}));
	}

	site with_pipeline() {
		site s{.name = "neotrac"};
		s.screens.push_back({"Releases", "/:project/boards/:board", "", [](page& p) {
			                     auto board_page = p.view("projects::board_page", "atlas-releases");
			                     return items(pipeline_board(p, {.view = board_page,
			                                                     .list = "issues",
			                                                     .by = "stage",
			                                                     .over = board_page,
			                                                     .over_list = "stages",
			                                                     .columns = {{"title", "Title"}},
			                                                     .status_by = "status"}));
		                     }});
		return s;
	}

	void fill_releases(memory_source& data) {
		auto stage = [](std::string id, std::string title, list kinds, std::string status) {
			return value(fields{{"id", id}, {"title", title}, {"kinds", value(kinds)}, {"status", status}});
		};
		auto card = [](std::string id, std::string title, std::string stage, std::string kind, fields more = {}) {
			fields f{{"id", id}, {"title", title}, {"stage", stage}, {"labels", value(list{value(kind)})}};
			for (auto& [k, v] : more) f[k] = v;
			return value(f);
		};
		data.set("projects::board_page", "atlas-releases",
		         value(fields{{"stages", list{stage("to_do", "To do", {}, "inactive"), stage("npm", "Publishing to npm", {"packages"}, "working"),
		                                     stage("served", "Waiting for npm", {"packages"}, "waiting"), stage("published", "Published", {"packages"}, "done"),
		                                     stage("checking", "Checking", {"websites"}, "working"), stage("deploying", "Deploying", {"websites"}, "working"),
		                                     stage("live", "Live", {"websites"}, "done"), stage("building", "Building app", {"local apps"}, "working"),
		                                     stage("installed", "Installed", {"local apps"}, "done"), stage("failed", "Failed", {}, "failed")}},
		                      {"issues", list{card("a-1", "@uione/compiler", "published", "packages"), card("a-2", "@uione/react", "served", "packages"),
		                                      card("a-3", "neotrac", "failed", "websites", {{"data", value(fields{{"at", "Deploying"}, {"note", "the deploy failed"}})}}),
		                                      card("a-4", "neotrac desktop app", "to_do", "local apps", {{"data", value(fields{{"status", "waiting"}, {"note", "waiting on neotrac"}})}})}}}));
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

	site with_whiteboard() {
		site s{.name = "neotrac"};
		s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
			                     auto board_page = p.view("projects::board_page", "atlas-main");
			                     return items(canvas_board(p, {.view = board_page,
			                                                   .list = "issues",
			                                                   .columns = {{"title", "Title"}, {"number", "#"}},
			                                                   .place = "projects::issue::place",
			                                                   .grid = 160,
			                                                   .by = "stage",
			                                                   .over = board_page,
			                                                   .over_list = "stages",
			                                                   .regions = "width",
			                                                   .move = "projects::issue::move",
			                                                   .resize = "projects::stage::update"}));
		                     }});
		return s;
	}

}  // namespace

// Started by publish, whose agents stop when it quits: q asks first while one is
// working on a card, naming it, Esc goes on, and q again quits; with nothing worked
// on, or agents that don't stop with it, q quits at once.
TEST("q asks before quitting stops agents working on cards") {
	memory_source data;
	fill_board(data);
	auto s = with_board();
	navigator nav(s, data);
	auto handled = [&nav](terminal::key_press e, bool stops) { return one::terminal::runtime::key(nav, e, stops); };
	nav.open("/atlas/boards/main");
	screen_at t(nav, 80, 30);
	(void)t.text();
	CHECK(!handled(letter('q'), true));  // nothing's held: it quits
	value until;
	until.type = value::kind::time;
	until.time.seconds = std::chrono::duration_cast<std::chrono::seconds>((std::chrono::system_clock::now() + std::chrono::minutes(10)).time_since_epoch()).count();
	fill_board(data, {{"held_by", "npm-agent"}, {"held_by.name", "npm"}, {"held_until", until}});
	(void)t.text();
	CHECK(!handled(letter('q'), false));  // its agents don't stop with it
	CHECK(handled(letter('q'), true));
	std::string asked = t.text();
	matches_golden("board-quitting", asked);
	CHECK(handled(key(terminal::keys::escape), true));
	CHECK(t.text().find("Quit?") == std::string::npos);
	CHECK(handled(letter('q'), true));
	CHECK(!handled(letter('q'), true));  // q again quits
}

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

// A board whose Done chimes, kept to a milestone picked on its track: picking one, or
// none, shows other cards in Done without chiming, and a card that does arrive there
// chimes once, whatever's picked.
TEST("a board chimes for a card that arrives, not for those a pick on its track shows") {
	std::vector<std::string> heard;
	chime_into([&](const std::string& message, bool) { heard.push_back(message); });
	memory_source data;
	auto issue = [](std::string id, std::string title, std::string stage, std::string milestone) {
		return value(fields{{"id", id}, {"title", title}, {"stage", stage}, {"milestone", milestone}});
	};
	auto page_with = [&](std::string site_at) {
		return value(fields{{"stages", list{value(fields{{"id", "s1"}, {"title", "To do"}}), value(fields{{"id", "s3"}, {"title", "Done"}})}},
		                    {"milestones", list{value(fields{{"id", "m13"}, {"title", "0.13.2"}}), value(fields{{"id", "m14"}, {"title", "0.14.0"}})}},
		                    {"issues", list{issue("atlas-1", "compiler", "s3", "m13"), issue("atlas-2", "editor", "s3", "m13"), issue("atlas-3", "site", site_at, "m14")}}});
	};
	data.set("projects::board_page", "atlas-main", page_with("s1"));
	site s{.name = "neotrac"};
	s.screens.push_back({"Board", "/:project/boards/:board", "", [](page& p) {
		                     auto board_page = p.view("projects::board_page", "atlas-main");
		                     return items(board(p, {.view = board_page, .list = "issues", .by = "stage", .over = board_page, .over_list = "stages",
		                                            .columns = {{"title", "Title"}}, .track = track_spec{"milestone", board_page, "milestones"}, .notify = {"s3"}}));
	                     }});
	navigator nav(s, data);
	screen_at t(nav, 80, 30);
	nav.open("/atlas/boards/main");
	t.text();
	auto picked = nav.current().kept<std::string>("board:issues:track");
	for (std::string milestone : {"m14", "m13", "", "m14"}) {
		picked.set(milestone);
		t.text();
	}
	// Typed into its search and cleared, the cards it hid coming back.
	auto searched = nav.current().kept<std::string>("board:issues:search");
	for (std::string typed : {"s", "si", "site", "", "comp", ""}) {
		searched.set(typed);
		t.text();
	}
	CHECK(heard.empty());
	data.set("projects::board_page", "atlas-main", page_with("s3"));
	t.text();
	CHECK(heard == std::vector<std::string>{"site: Done"});
	chime_into({});
}

// A table and a board of the same issues, switched between, with one track of
// milestones above the switch: drawn once, whichever is shown, its pick keeping both
// to it; a milestone whose issues have all succeeded is green, and one where any
// failed red.
TEST("a switch's track is drawn once above it, the same whichever way the list is shown") {
	memory_source data;
	auto issue = [](std::string id, std::string title, std::string stage, std::string milestone) {
		return value(fields{{"id", id}, {"title", title}, {"stage", stage}, {"milestone", milestone}});
	};
	auto stage = [](std::string id, std::string title, std::string status) { return value(fields{{"id", id}, {"title", title}, {"status", status}}); };
	data.set("projects::board_page", "atlas-releases",
	         value(fields{{"stages", list{stage("s1", "To do", "inactive"), stage("s3", "Published", "success"), stage("s9", "Failed", "failed")}},
	                      {"milestones", list{value(fields{{"id", "m13"}, {"title", "0.13.2"}}), value(fields{{"id", "m14"}, {"title", "0.14.0"}}), value(fields{{"id", "m15"}, {"title", "0.14.1"}})}},
	                      {"issues", list{issue("atlas-1", "compiler 0.13.2", "s3", "m13"), issue("atlas-2", "compiler 0.14.0", "s3", "m14"),
	                                      issue("atlas-3", "neotrac 0.14.0", "s9", "m14"), issue("atlas-4", "compiler 0.14.1", "s1", "m15")}}}));
	std::string id = "projects::board_page.issues";
	auto shared = [&](state<view_state> v) {
		auto t = track_spec{"milestone", v, "milestones"};
		t.key = "switched:" + id, t.above = true;
		return t;
	};
	site s{.name = "neotrac"};
	s.screens.push_back({"Releases", "/:project/boards/:board", "", [=](page& p) {
		                     auto board_page = p.view("projects::board_page", "atlas-releases");
		                     auto above = shared(board_page);
		                     above.by = "stage", above.over = board_page, above.over_list = "stages", above.status_by = "status";
		                     return items(tracked(p, above, board_page, "issues",
		                                          switched(p, id, "Show issues as", {"Table", "Board"}, 0,
		                                                   table(p, {.view = board_page, .list = "issues", .columns = {{"title", "Title"}}, .track = shared(board_page)}),
		                                                   board(p, {.view = board_page, .list = "issues", .by = "stage", .over = board_page, .over_list = "stages",
		                                                             .columns = {{"title", "Title"}}, .track = shared(board_page), .status_by = "status"}))));
	                     }});
	navigator nav(s, data);
	screen_at t(nav, 100, 30);
	nav.open("/atlas/boards/releases");
	// The track's milestone, with its dot, once, and above the switch.
	auto once_above = [](const std::string& drawn) {
		auto first = drawn.find("● 0.14.0"), switch_at = drawn.find("Show issues as");
		return first != std::string::npos && drawn.find("● 0.14.0", first + 1) == std::string::npos && first < switch_at;
	};
	CHECK(once_above(t.text()));
	nav.current().kept<int>("switched:" + id).set(1);  // the board
	std::string board_drawn = t.text();
	CHECK(once_above(board_drawn));
	CHECK(board_drawn.find("Published") != std::string::npos);
	// Picked in the board's track, both keep to it.
	nav.current().kept<std::string>("switched:" + id + ":track").set("m15");
	CHECK(t.text().find("compiler 0.13.2") == std::string::npos);
	nav.current().kept<int>("switched:" + id).set(0);
	CHECK(t.text().find("compiler 0.13.2") == std::string::npos);
	CHECK(t.text().find("compiler 0.14.1") != std::string::npos);
	// Each milestone's dot.
	auto above = shared(nav.current().view("projects::board_page", "atlas-releases"));
	above.by = "stage", above.over = nav.current().view("projects::board_page", "atlas-releases"), above.over_list = "stages", above.status_by = "status";
	auto model = track_of(nav.current(), "", above, rows_of(*nav.current().view("projects::board_page", "atlas-releases"), "issues"), "issues");
	CHECK(model.has_value());
	if (!model) return;
	std::map<std::string, std::string> dots;
	for (const auto& r : model->records) dots[r.title] = r.status;
	CHECK(dots["0.13.2"] == "success");
	CHECK(dots["0.14.0"] == "failed");
	CHECK(dots["0.14.1"] == "");
}

// A track that says picked last: the newest milestone is picked until another is;
// picking that one again shows every milestone's cards, and picking none of them
// again goes back to the newest.
TEST("a track that's picked last keeps to its last record until another is picked") {
	memory_source data;
	auto issue = [](std::string id, std::string title, std::string milestone) {
		return value(fields{{"id", id}, {"title", title}, {"stage", "s1"}, {"milestone", milestone}});
	};
	data.set("projects::board_page", "atlas-releases",
	         value(fields{{"stages", list{value(fields{{"id", "s1"}, {"title", "To do"}})}},
	                      {"milestones", list{value(fields{{"id", "m13"}, {"title", "0.13.2"}}), value(fields{{"id", "m14"}, {"title", "0.14.0"}}), value(fields{{"id", "m99"}, {"title", "0.15.0"}})}},
	                      {"issues", list{issue("atlas-1", "compiler 0.13.2", "m13"), issue("atlas-2", "compiler 0.14.0", "m14")}}}));
	auto newest = [](state<view_state> v) {
		auto t = track_spec{"milestone", v, "milestones", "title", {}, true};
		t.picked_last = true;
		return t;
	};
	site s{.name = "neotrac"};
	s.screens.push_back({"Releases", "/:project/boards/:board", "", [=](page& p) {
		                     auto board_page = p.view("projects::board_page", "atlas-releases");
		                     return items(table(p, {.view = board_page, .list = "issues", .columns = {{"title", "Title"}}, .track = newest(board_page)}));
	                     }});
	navigator nav(s, data);
	screen_at t(nav, 100, 30);
	nav.open("/atlas/boards/releases");
	// The newest with cards, as 0.15.0 has none and is hidden while it's empty.
	std::string drawn = t.text();
	CHECK(drawn.find("compiler 0.14.0") != std::string::npos);
	CHECK(drawn.find("compiler 0.13.2") == std::string::npos);
	auto model = [&] { return track_of(nav.current(), "table:issues", newest(nav.current().view("projects::board_page", "atlas-releases")), rows_of(*nav.current().view("projects::board_page", "atlas-releases"), "issues"), "issues"); };
	CHECK(model() && model()->picked == "m14");
	if (!model()) return;
	model()->pick("m13");
	CHECK(t.text().find("compiler 0.14.0") == std::string::npos);
	model()->pick("m14");
	model()->pick("m14");  // picked again: every milestone's
	drawn = t.text();
	CHECK(drawn.find("compiler 0.14.0") != std::string::npos);
	CHECK(drawn.find("compiler 0.13.2") != std::string::npos);
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

TEST("a pipeline in a terminal: a lane for each kind, each card's stages along its row, the one it's at marked") {
	memory_source data;
	fill_releases(data);
	auto s = with_pipeline();
	navigator nav(s, data);
	nav.open("/atlas/boards/releases");
	screen_at t(nav, 110, 30);
	matches_golden("pipeline", t.text());
}

TEST("a whiteboard in a terminal: its stages as columns, a card moved across into the next moving there") {
	memory_source data;
	fill_board(data);
	data.on("projects::issue::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("projects::issue::move", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	auto s = with_whiteboard();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	screen_at t(nav, 110, 30);
	matches_golden("whiteboard", t.text());
	CHECK(t.press(key(terminal::keys::tab)));  // to the whiteboard
	CHECK(t.press(letter('n')));               // #12, in To do
	CHECK(t.press(key(terminal::keys::right)));
	CHECK(t.press(key(terminal::keys::right)));  // two grid steps over: Doing
	bool moved = false;
	for (const auto& run : data.runs()) moved = moved || (run.command == "projects::issue::move" && run.fields["stage"].as_string() == "s2");
	CHECK(moved);
}

TEST("a switch opens on the view it's told to start on, until the person picks another") {
	memory_source data;
	site s{.name = "neotrac"};
	s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
		                     return items(switched(p, "projects::board_page.issues", "Show issues as", {"Table", "Board", "Pipeline"}, 2, terminal::text("the table"),
		                                           terminal::text("the board"), terminal::text("the pipeline")));
	                     }});
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	screen_at t(nav, 80, 12);
	CHECK(t.text().find("the pipeline") != std::string::npos);
	nav.current().kept<int>("switched:projects::board_page.issues").set(0);
	screen_at again(nav, 80, 12);
	CHECK(again.text().find("the table") != std::string::npos);
}

namespace {
	// Two agents: one working, how far and what it uses; one not heard from in minutes.
	void fill_agents(memory_source& data) {
		auto at = [](std::chrono::seconds ago) {
			return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now() - ago));
		};
		auto numbers = [](std::initializer_list<double> each) {
			list out;
			for (double n : each) out.emplace_back(n);
			return value(out);
		};
		value stats(fields{{"cpu", numbers({5, 20, 45, 80, 60, 72, 90, 64})}, {"memory", numbers({1e8, 2e8, 4e8, 4.5e8, 5e8, 5.2e8, 6e8, 6.4e8})}});
		data.set("projects::agents", "atlas",
		         value(fields{{"agents", list{value(fields{{"id", "atlas-npm"}, {"title", "npm: Publishing"}, {"status", "working"}, {"since", at(std::chrono::seconds(95))},
		                                                  {"doing", "#12 @uione/react 0.16.0: Uploading"}, {"step", 3}, {"steps", 5}, {"seen_at", at(std::chrono::seconds(2))}, {"stats", stats}}),
		                                    value(fields{{"id", "atlas-sites"}, {"title", "sites: Deploying"}, {"status", "idle"}, {"seen_at", at(std::chrono::seconds(600))}})}}}));
		data.set("projects::agent_page", "atlas-npm", value(fields{{"title", "npm: Publishing"}, {"stats", stats}}));
	}

	site with_agents() {
		site s{.name = "neotrac"};
		s.screens.push_back({"Agents", "/:project/agents", "", [](page& p) {
			                     auto agents = p.view("projects::agents", "atlas");
			                     return items(monitor(p, {.view = agents, .list = "agents", .status = "status", .doing = "doing", .since = "since", .step = "step", .steps = "steps",
			                                              .seen = "seen_at", .seen_seconds = 30,
			                                              .charts = {{"stats.cpu", "CPU", "percent"}, {"stats.memory", "Memory", "bytes"}}}));
		                     }});
		s.screens.push_back({"npm", "/:project/agents/:agent", "", [](page& p) {
			                     auto agent = p.view("projects::agent_page", "atlas-npm");
			                     return items(chart(p, {.view = agent, .series = {{"stats.cpu", "CPU", "percent"}, {"stats.memory", "Memory", "bytes"}}}));
		                     }});
		return s;
	}
}  // namespace

// A monitor, as btop draws one: each agent's dot and state, what it's doing and how
// far, and its charts in braille; one unheard from for minutes is offline, whatever
// it last said. Its page draws the same numbers large.
TEST("a monitor of agents, working and offline, and an agent's charts in braille") {
	memory_source data;
	fill_agents(data);
	auto s = with_agents();
	navigator nav(s, data);
	nav.open("/atlas/agents");
	screen_at t(nav, 110, 12);
	std::string drawn = t.text();
	CHECK_SAYING(drawn.find("Working 1m") != std::string::npos && drawn.find("3/5") != std::string::npos && drawn.find("90%") == std::string::npos &&
	                 drawn.find("64%") != std::string::npos && drawn.find("610 MiB") != std::string::npos,
	             drawn);
	CHECK_SAYING(drawn.find("Offline") != std::string::npos, drawn);
	CHECK_SAYING(drawn.find("⣷") != std::string::npos, drawn);  // the graph, its last samples near the top
	matches_golden("monitor", drawn);
	nav.open("/atlas/agents/npm");
	screen_at page(nav, 80, 30);
	std::string large = page.text();
	CHECK_SAYING(large.find("of 100%") != std::string::npos && large.find("of 1 GiB") != std::string::npos, large);
}

// A card whose holder says how far it is shows it, in btop's squares.
TEST("a card shows how far it is in its stage, as its holder says") {
	memory_source data;
	fill_board(data, {{"data", value(fields{{"step", 2}, {"steps", 5}})}});
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	screen_at t(nav, 100, 30);
	std::string drawn = t.text();
	CHECK_SAYING(drawn.find("■■□□□ 2/5") != std::string::npos, drawn);
}

// Picked once, a board opens that way the next time the app starts, as on the web.
TEST("a switch's pick is remembered for its page between runs of the app") {
	auto folder = std::filesystem::temp_directory_path() / ("one-remembered-" + std::to_string(::getpid()));
	std::filesystem::remove_all(folder);
	::setenv("XDG_STATE_HOME", folder.c_str(), 1);
	site s{.name = "neotrac"};
	s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
		                     return items(switched(p, "projects::board_page.issues", "Show issues as", {"Table", "Board", "Pipeline"}, 0, terminal::text("the table"),
		                                           terminal::text("the board"), terminal::text("the pipeline")));
	                     }});
	{
		one::screens::remembered::open("org.uione.tests");
		memory_source data;
		navigator nav(s, data);
		nav.open("/atlas/boards/main");
		screen_at t(nav, 80, 12);
		CHECK(t.text().find("the table") != std::string::npos);
		t.press(key(terminal::keys::tab));
		t.press(key(terminal::keys::tab));
		CHECK(t.press(key(terminal::keys::newline)));  // Board
		CHECK(t.text().find("the board") != std::string::npos);
	}
	{
		one::screens::remembered::open("org.uione.tests");  // started again
		memory_source data;
		navigator nav(s, data);
		nav.open("/atlas/boards/main");
		screen_at t(nav, 80, 12);
		CHECK_SAYING(t.text().find("the board") != std::string::npos, t.text());
		nav.open("/atlas/boards/other");  // another board keeps its own
		screen_at other(nav, 80, 12);
		CHECK(other.text().find("the table") != std::string::npos);
	}
	::unsetenv("XDG_STATE_HOME");
	one::screens::remembered::open("org.uione.tests");
	std::filesystem::remove_all(folder);
}

// person.service != true: whoever isn't a service, a person said as one or never said.
TEST("a filter keeps the rows that don't hold a value, by a field read through what they point at") {
	auto row = [](fields f) { return value(f); };
	std::vector<std::pair<std::string, std::string>> people{{"person.service!", "true"}};
	CHECK(one::screens::kept(row({{"person.service", false}}), people, ""));
	CHECK(one::screens::kept(row({{"person.name", "Ade"}}), people, ""));
	CHECK(!one::screens::kept(row({{"person.service", true}}), people, ""));
}

int main() {
	use_time_zone("UTC");
	return check::run();
}

TEST("a meter shares its blocks by how much each part is, and a part that's anything gets one") {
	auto drawn = [](std::vector<terminal::meter_part> parts, int width) {
		terminal::view_host host([parts] { return terminal::meter(parts); });
		host.bring_up_to_date(width, 1);
		terminal::canvas c(width, 1);
		host.draw(c);
		std::string colors;
		for (int x = 0; x < width; ++x) colors += c.look_at(0, x).foreground == terminal::color::blue ? 'a' : c.look_at(0, x).foreground == terminal::color::bright_blue ? 'b' : '.';
		return std::pair{c.text(), colors};
	};
	auto [blocks, shares] = drawn({{3, terminal::meter_look(0)}, {1, terminal::meter_look(1)}}, 8);
	CHECK(blocks.starts_with("■■■■■■■■"));
	CHECK(shares == "aaaaaabb");
	CHECK(drawn({{100, terminal::meter_look(0)}, {1, terminal::meter_look(1)}}, 8).second == "aaaaaaab");
	CHECK(drawn({}, 4).second == "....");
}
