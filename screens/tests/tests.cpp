// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::screens' tests: values read as people read them, addresses, keys and routes
// as the web makes them, changes said in words, and what a page does: its views read
// live, its commands run, asked about first and turned down, and what it keeps.
// What's drawn is held by each toolkit's own tests. Where a collage's layers go is
// held to collage-cases.json, as @uione/react's is.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "check.hpp"
#include "one/screens/collage.hpp"
#include "one/screens/models.hpp"
#include "one/screens/palette.hpp"

namespace {

	using namespace one::screens;
	using fields = std::map<std::string, value>;
	using list = std::vector<value>;

	struct nothing_drawn {};
	using page = basic_page<nothing_drawn>;
	using site = basic_site<nothing_drawn, nothing_drawn>;
	using navigator = basic_navigator<nothing_drawn, nothing_drawn>;

	site tracker() {
		site s{.name = "tracker"};
		s.screens.push_back({"Projects", "/projects"});
		s.screens.push_back({"", "/projects/:project"});
		s.screens.push_back({"Issue", "/:project/:issue"});
		s.screens.push_back({"Settings", "/:project/settings"});
		s.screens.push_back({"Docs", "/docs/:page?"});
		return s;
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
	CHECK(row_link("/:owner/:project", value(fields{{"id", "maya-atlas"}}), {}, {"owner"}) == "/maya/atlas");
	value card(fields{{"id", "atlas-12"}, {"board", "atlas-main"}});
	CHECK(row_link("/:project/boards/:board/issues/:issue", card, {{"project", "atlas"}}, {}, {{"board", 2}}) == "/atlas/boards/main/issues/atlas-12");
	CHECK(row_link("/issues/:issue", value(fields{{"id", "change-1"}, {"issue", "atlas-12"}}), {}) == "/issues/atlas-12");
	CHECK(filled("/:project/settings", {{"project", "atlas"}}) == "/atlas/settings");
}

TEST("a file's path pasted on a canvas makes a card of it, named from it, uploaded where it landed") {
	auto file = std::filesystem::temp_directory_path() / "uione-paste" / "sketch.png";
	std::filesystem::create_directories(file.parent_path());
	std::ofstream(file) << "png";
	site s{.name = "ideas"};
	s.screens.push_back({"Wall", "/walls/:wall"});
	memory_source data;
	navigator nav(s, data);
	CHECK(nav.open("/walls/w1"));
	canvas_spec spec{.view = nav.current().view("ideas::wall"), .list = "notes", .x = "x", .y = "y", .w = "w", .h = "h", .grid = 160, .paste = "ideas::note::create", .paste_into = "files", .paste_named = "title", .given = {{"wall", "w1"}}};
	CHECK(paste_on_canvas(nav.current(), spec, "file://" + file.string() + "\n", 40, 40));
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	const auto& made = data.runs()[0];
	CHECK(made.command == "ideas::note::create");
	CHECK(made.fields["title"].as_string() == "sketch");
	CHECK(made.fields["wall"].as_string() == "w1");
	CHECK(made.fields["x"].as_double() == 40);
	CHECK(made.fields["w"].as_double() == 160);
	CHECK(data.uploads() == std::vector<std::string>{file.string()});
	CHECK(!paste_on_canvas(nav.current(), canvas_spec{.view = spec.view, .list = "notes", .x = "x", .y = "y", .w = "w", .h = "h"}, file.string(), 0, 0));
	std::filesystem::remove_all(file.parent_path());
}

TEST("the top bar lists the navigation's addresses first, in its order, the rest after") {
	auto s = tracker();
	s.navigation = {"/docs", "/projects"};
	std::vector<std::string> routes;
	for (const auto* screen : in_navigation(s)) routes.push_back(screen->route);
	CHECK((routes == std::vector<std::string>{"/docs/:page?", "/projects", "/projects/:project", "/:project/:issue", "/:project/settings"}));
}

TEST("an address opens the screen most particular to it, a part named outright before a parameter, with its query") {
	auto s = tracker();
	memory_source data;
	navigator nav(s, data);
	std::map<std::string, std::string> params;
	CHECK(nav.match("/atlas/settings", params)->title == "Settings");
	CHECK(nav.match("/atlas/12", params)->title == "Issue");
	CHECK(params.at("issue") == "12");
	CHECK(nav.match("/docs", params)->title == "Docs");
	CHECK(nav.match("/docs/start", params)->title == "Docs");
	CHECK(nav.match("/docs/start/more", params) == nullptr);
	CHECK(!nav.open("/nowhere"));
	CHECK(nav.open("/projects/atlas%20one?is=Mine&assignees=me"));
	CHECK(nav.current().param("project") == "atlas one");
	CHECK(nav.current().filtered() == "Mine");
	CHECK(nav.open("/projects"));
	CHECK(nav.back());
	CHECK(nav.depth() == 1);
	CHECK(!nav.back());
}

TEST("a page reads what moved where it went, as a board renamed is, all the way along") {
	auto s = tracker();
	memory_source data;
	data.set("tracker::issue_page", "atlas-sprint", value(fields{{"moved", "atlas-sprint_2"}}));
	data.set("tracker::issue_page", "atlas-sprint_2", value(fields{{"moved", "atlas-third"}}));
	data.set("tracker::issue_page", "atlas-third", value(fields{{"title", "Third"}}));
	navigator nav(s, data);
	CHECK(nav.open("/atlas/12"));
	auto read = [&] { return nav.current().view("tracker::issue_page", "atlas-sprint").peek(); };
	for (int i = 0; i < 3 && read().status != status::ready; ++i) {}
	CHECK(read().status == status::ready);
	CHECK(read().data["title"].as_string() == "Third");
}

TEST("a time named by the calendar is that day, week or month's start where it's read, at a time of day when it says, the same as the web's") {
	using namespace std::chrono;
	auto at = [](int y, unsigned m, unsigned d, int h = 0, int min = 0) { return sys_days(year(y) / month(m) / day(d)) + hours(h) + minutes(min); };
	use_time_zone("America/Los_Angeles");
	// A Wednesday afternoon there, which is Thursday morning in UTC.
	auto now = at(2026, 10, 29, 0, 30);
	auto local = [&](int y, unsigned m, unsigned d, int h = 0, int min = 0) { return at(y, m, d, h, min) + hours(7); };  // PDT is 7 hours behind
	// A day alone, at midnight UTC so it's that day wherever it's read; a time of day
	// where the reader is.
	CHECK(time_named("d+0", now) == at(2026, 10, 28));
	CHECK(time_named("d+1@09:00", now) == local(2026, 10, 29, 9));
	CHECK(time_named("d-1", now) == at(2026, 10, 27));
	CHECK(time_named("w+0", now) == at(2026, 10, 26));
	CHECK(time_named("w+1", now) == at(2026, 11, 2));
	CHECK(time_named("M+1", now) == at(2026, 11, 1));
	// Read here, a day alone is the start of that day where it's read.
	CHECK(as_read_here(at(2026, 10, 28)) == local(2026, 10, 28));
	CHECK(as_read_here(at(2026, 11, 2)) == at(2026, 11, 2) + hours(8));  // after the clocks go back
	CHECK(as_read_here(local(2026, 10, 29, 9)) == local(2026, 10, 29, 9));
	CHECK(time_named("+10m", now) == now + minutes(10));
	CHECK(!time_named("sometime", now));
	CHECK(!time_named("d+1@25:00", now));
	use_time_zone("UTC");
	CHECK(time_named("d+0", now) == at(2026, 10, 29));
	CHECK(time_value("d+1").is_time());
	CHECK(time_value("never").is_null());
}

TEST("a value reads as the web shows it: a date alone as that day, wherever it's read") {
	value due;
	due.type = value::kind::time;
	due.time.seconds = 1'790'035'200;  // Sep 22, 2026, midnight UTC
	use_time_zone("America/Los_Angeles");
	CHECK(show(due) == "Sep 22, 2026");
	use_time_zone("UTC");
	CHECK(show(value(3)) == "3");
	CHECK(show(value(2.5)) == "2.5");
	CHECK(show(value(std::vector<value>{"ui", "", "desktop"})) == "ui, desktop");
	// What a worker said, as a sentence; other data by its names.
	value said = value::map();
	said.fields["status"] = value("waiting");
	said.fields["at"] = value("Waiting for packages");
	said.fields["runner"] = value("2");
	said.fields["note"] = value("waiting on @uione/react");
	CHECK(show(said) == "Waiting at Waiting for packages, runner 2: waiting on @uione/react");
	value other = value::map();
	other.fields["size"] = value(3);
	CHECK(show(other) == "size 3");
}

TEST("a table's rows saved as comma-separated values, every one it keeps, quoted where they must be") {
	value at;
	at.type = value::kind::time;
	at.time.seconds = 1'792'324'800;  // Oct 18, 2026, noon UTC
	value first = value::map();
	first.fields["command"] = value("projects::issue::create");
	first.fields["person"] = value("Ann, of Lisbon");
	first.fields["at"] = at;
	first.fields["role"] = value("owner");
	first.fields["changed"] = value(std::vector<value>{"title", "stage"});
	value second = value::map();
	second.fields["command"] = value("projects::issue::update");
	second.fields["person"] = value("Ben \"Bo\"");
	second.fields["at"] = at;
	second.fields["role"] = value("member");
	second.fields["changed"] = value(std::vector<value>{});
	std::vector<column> columns{{"command", "Command"}, {"person", "Who"}, {"at", "When"}, {"role", "Role"}, {"changed", "Changed"}};
	std::map<std::string, std::vector<option>> choices{{"role", {{"owner", "Owner"}, {"member", "Member"}}}};
	CHECK(csv_of(columns, choices, {first, second}) ==
	      "Command,Who,When,Role,Changed\r\n"
	      "projects::issue::create,\"Ann, of Lisbon\",2026-10-18T12:00:00.000Z,Owner,\"title, stage\"\r\n"
	      "projects::issue::update,\"Ben \"\"Bo\"\"\",2026-10-18T12:00:00.000Z,Member,\r\n");

	// Saved where downloads go, never in another folder, and beside one already there.
	namespace fs = std::filesystem;
	fs::path folder = fs::temp_directory_path() / "uione-downloads";
	fs::remove_all(folder);
	fs::create_directories(folder);
	setenv("XDG_DOWNLOAD_DIR", folder.c_str(), 1);
	auto saved = save_download("../audit.csv", "a\r\n");
	CHECK(saved.has_value());
	if (!saved) return;
	CHECK(*saved == (folder / "audit.csv").string());
	auto again = save_download("audit.csv", "b\r\n");
	CHECK(again.has_value());
	if (!again) return;
	CHECK(*again == (folder / "audit-2.csv").string());
	std::ifstream read(*again);
	CHECK(std::string(std::istreambuf_iterator<char>(read), {}) == "b\r\n");
	unsetenv("XDG_DOWNLOAD_DIR");
}

TEST("a field with nothing in it is none, as a condition compares it") {
	CHECK(same(value(""), value()));
	CHECK(same(value(std::vector<value>{}), value()));
	CHECK(same(value(), value()));
	CHECK(!same(value("duplicate"), value()));
	CHECK(!same(value(0), value()));
	CHECK(same(value("open"), value("open")));
	CHECK(!same(value(""), value("open")));
}

TEST("a value worked out on a screen leaves what's over as a clock counts, and either by 0 is 0, as on every target") {
	CHECK(same(worked('%', value(5), value(4)), value(1)));
	CHECK(same(worked('%', value(-1), value(4)), value(3)));
	CHECK(same(worked('%', value(2.5), value(1)), value(0.5)));
	CHECK(same(worked('%', value(3), value(0)), value(0.0)));
	CHECK(same(worked('/', value(6), value(4)), value(1.5)));
	CHECK(same(worked('/', value(3), value(0)), value(0.0)));
}

TEST("a change is said in words, a command's action as done") {
	CHECK(done("close") == "closed");
	CHECK(done("take_over") == "took over");
	CHECK(done("stop") == "stopped");
	CHECK(done("copy") == "copied");
	CHECK(done("submit") == "submitted");
	CHECK(done("visit") == "visited");
	CHECK(changed("status", "open", "closed", "tracker::issue::close") == "closed this");
	CHECK(changed("stage", "Reported", "Triaged", "tracker::issue::move") == "moved this from Reported to Triaged");
	CHECK(changed("title", "Old", "New", "tracker::issue::update") == "changed title from Old to New");
	CHECK(changed("comment", value(), value(), "tracker::comment::create") == "added a comment to this");
}

TEST("what a view holds is copied as Markdown, and code without its license header") {
	value data(fields{{"priority", "high"},
	                  {"body", "  Copy it whole.  "},
	                  {"comments", list{value(fields{{"author.name", "Maya Garcia"}, {"body", "Seen."}, {"created_at", "Monday"}})}}});
	CHECK(markdown_of("#12 Copy", data, {{"priority", "Priority"}, {"body", "Body", true}}, {{"comments", "Comments", "thread"}}, {{"priority", {{"high", "High"}}}}) ==
	      "# #12 Copy\n\n**Priority:** High  \n\n## Body\n\nCopy it whole.\n\n## Comments\n\n**Maya Garcia** · Monday\n\nSeen.\n");
	CHECK(without_license("// Copyright 2026 Someone\n// SPDX-License-Identifier: MIT\n\nint main() {}\n") == "int main() {}");
}

TEST("a page reads its views live, runs commands, asks first when it's told to, and says what turned one down") {
	memory_source data;
	data.on("tracker::issue::close", [](const value& f, memory_source&) -> answer {
		if (f["id"].as_string() == "atlas-9") return std::unexpected(std::string("that issue is already closed"));
		return "atlas-12";
	});
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	auto project = p.view("tracker::project_page", "atlas");
	CHECK(project->status == status::missing);
	data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas"}}));
	p.title({live(project, "name"), words(" project")});
	CHECK(p.title() == "Atlas project");
	p.ask_before("tracker::issue::close", "Close it?");
	p.run("tracker::issue::close", value(fields{{"id", "atlas-9"}}));
	CHECK(data.runs().empty());
	p.answer_asked(true);
	CHECK(p.error("tracker::issue::close") == "that issue is already closed");
	CHECK(p.problem().peek() == "that issue is already closed");
	CHECK(p.kept<int>("count", 4).peek() == 4);
	p.kept<int>("count").set(5);
	CHECK(p.kept<int>("count").peek() == 5);
}

TEST("a whiteboard's regions are its stages side by side: a card let go in another moves there, and one is made wider") {
	memory_source data;
	data.on("tracker::issue::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("tracker::issue::move", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("tracker::stage::update", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	data.set("tracker::board_page", "atlas", value(fields{
		{"issues", value(list{value(fields{{"id", "atlas-1"}, {"title", "Laid out"}, {"stage", "todo"}})})},
		{"stages", value(list{value(fields{{"id", "todo"}, {"title", "To do"}, {"width", 400.0}}), value(fields{{"id", "done"}, {"title", "Done"}})})}}));
	auto board = p.view("tracker::board_page", "atlas");
	canvas_spec spec{.view = board, .list = "issues", .columns = {{"title", "Title"}}, .x = "x", .y = "y", .w = "w", .h = "h", .place = "tracker::issue::place", .grid = 160, .origin_x = 40, .origin_y = 80,
	                 .by = "stage", .over = board, .over_list = "stages", .regions = "width", .move = "tracker::issue::move", .resize = "tracker::stage::update"};
	auto c = canvas_of(p, spec);
	CHECK(c.regions.size() == 2);
	if (c.regions.size() != 2) return;
	// To do as wide as it says; Done two grid steps, after it; the card in the middle of To do.
	CHECK((c.regions[0].x == 40 && c.regions[0].width == 400 && c.regions[1].x == 440 && c.regions[1].width == 320));
	CHECK(c.cards[0].x == 40 + (400 - 150) / 2.0);
	CHECK(c.resizing);
	// Let go in Done, it moves there, and is placed.
	place_card(p, spec, "atlas-1", 500, 80, 150, 150);
	bool moved = false;
	for (const auto& run : data.runs()) moved = moved || (run.command == "tracker::issue::move" && run.fields["stage"].as_string() == "done");
	CHECK(moved);
	// To do made wider: shown so at once, Done after it.
	resize_region(p, spec, "todo", 480);
	auto wider = canvas_of(p, spec);
	CHECK((wider.regions[0].width == 480 && wider.regions[1].x == 520));
	CHECK(data.runs().back().command == "tracker::stage::update");
	CHECK(data.runs().back().fields["width"].as_double() == 480);
}

TEST("a pipeline puts each card in its kind's lane, its kind's stages done up to where it is, and red where it failed") {
	memory_source data;
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	auto stage = [](std::string id, std::string title, list kinds, std::string status = "") {
		return value(fields{{"id", id}, {"title", title}, {"kinds", value(kinds)}, {"status", status}});
	};
	data.set("tracker::board_page", "atlas", value(fields{
		{"stages", value(list{stage("to_do", "To do", {}, "inactive"), stage("npm", "Publishing to npm", {"packages"}, "working"), stage("published", "Published", {"packages"}, "done"),
		                      stage("checking", "Checking", {"websites"}, "working"), stage("live", "Live", {"websites"}, "done"), stage("failed", "Failed", {}, "failed")})},
		{"issues", value(list{value(fields{{"id", "a-1"}, {"title", "compiler"}, {"stage", "npm"}, {"labels", value(list{"packages"})}}),
		                      value(fields{{"id", "a-2"}, {"title", "tracker"}, {"stage", "failed"}, {"labels", value(list{"websites"})}, {"data", value(fields{{"at", "Checking"}, {"note", "not pushed"}})}}),
		                      value(fields{{"id", "a-3"}, {"title", "react"}, {"stage", "published"}, {"labels", value(list{"packages"})}})})}}));
	auto board = p.view("tracker::board_page", "atlas");
	auto m = pipeline_of(p, {.view = board, .list = "issues", .by = "stage", .over = board, .over_list = "stages", .columns = {{"title", "Title"}}, .status_by = "status"});
	CHECK(m.lanes.size() == 2);
	if (m.lanes.size() != 2) return;
	CHECK(m.lanes[0].title == "Packages");
	CHECK(m.lanes[1].title == "Websites");
	// Its kind's stages and To do, never Failed: To do done, the one it's at now.
	const auto& compiler = m.lanes[0].rows[0];
	CHECK(compiler.steps.size() == 3);
	CHECK((compiler.steps[0].at == "done" && compiler.steps[1].at == "now" && compiler.steps[2].at == "later"));
	CHECK(compiler.status == "working");
	// At its end, every stage done.
	const auto& react = m.lanes[0].rows[1];
	CHECK((react.steps[2].title == "Published" && react.steps[2].at == "done"));
	// Failed in Checking, as it says: red there, To do done, Live to come.
	const auto& site = m.lanes[1].rows[0];
	CHECK((site.steps[0].at == "done" && site.steps[1].at == "failed" && site.steps[2].at == "later"));
	CHECK(site.note == "not pushed");
}

TEST("a canvas places its cards where they say, lays out those that don't by column, and moves one at once, back if it's turned down") {
	memory_source data;
	data.on("tracker::issue::place", [](const value& f, memory_source&) -> answer {
		if (f["id"].as_string() == "atlas-3") return std::unexpected(std::string("you can't move that one"));
		return f["id"].as_string();
	});
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	data.set("tracker::board_page", "atlas", value(fields{
		{"issues", value(list{value(fields{{"id", "atlas-1"}, {"title", "Placed"}, {"stage", "todo"}, {"x", 400.0}, {"y", 240.0}, {"w", 310.0}, {"h", 42.0}}),
		                      value(fields{{"id", "atlas-2"}, {"title", "Laid out"}, {"stage", "done"}}),
		                      value(fields{{"id", "atlas-3"}, {"title", "Also todo"}, {"stage", "todo"}})})},
		{"stages", value(list{value(fields{{"id", "todo"}}), value(fields{{"id", "done"}})})}}));
	auto board = p.view("tracker::board_page", "atlas");
	canvas_spec spec{.view = board, .list = "issues", .columns = {{"title", "Title"}}, .x = "x", .y = "y", .w = "w", .h = "h", .place = "tracker::issue::place", .grid = 160, .origin_x = 40, .origin_y = 80,
	                 .by = "stage", .over = board, .over_list = "stages"};
	auto c = canvas_of(p, spec);
	CHECK(c.cards.size() == 3);
	if (c.cards.size() != 3) return;
	CHECK(c.placing);
	CHECK((c.cards[0].placed && c.cards[0].x == 400 && c.cards[0].y == 240 && c.cards[0].w == 310 && c.cards[0].h == 42));
	// Not placed: in its stage's column, a grid step across for each, and down as they come.
	CHECK((!c.cards[1].placed && c.cards[1].x == 200 && c.cards[1].y == 80));
	CHECK((c.cards[2].x == 40 && c.cards[2].y == 240 && c.cards[2].w == 150));  // second in its column, after the one placed
	CHECK(snapped(130, 40, 160) == 200);
	CHECK(snapped(130, 40, 0) == 130);
	// Placed, it's there at once; turned down, it's back where it was.
	place_card(p, spec, "atlas-2", 520, 400, 150, 150);
	auto moved = canvas_of(p, spec);
	CHECK((moved.cards[1].placed && moved.cards[1].x == 520 && moved.cards[1].y == 400));
	place_card(p, spec, "atlas-3", 999, 999, 150, 150);
	CHECK(canvas_of(p, spec).cards[2].y == 240);
	CHECK(p.error("tracker::issue::place") == "you can't move that one");
}

TEST("a mosaic's cards each go in their tile by their place alone, two blocks of four for every eight, the second the first mirrored") {
	auto at = [](std::size_t n) {
		auto t = mosaic_tile(n);
		return std::to_string(t.column) + " " + std::to_string(t.row) + " " + std::to_string(t.columns) + " " + std::to_string(t.rows);
	};
	// The first block: a tall tile at the left, a wide one at the top right, two under it.
	CHECK(at(0) == "0 0 1 2");
	CHECK(at(1) == "1 0 2 1");
	CHECK(at(2) == "1 1 1 1");
	CHECK(at(3) == "2 1 1 1");
	// The second, under it, mirrored: the tall one at the right.
	CHECK(at(4) == "2 2 1 2");
	CHECK(at(5) == "0 2 2 1");
	CHECK(at(6) == "0 3 1 1");
	CHECK(at(7) == "1 3 1 1");
	// And again for the next eight, under those.
	CHECK(at(8) == "0 4 1 2");
	CHECK(at(13) == "0 6 2 1");
	// The last block ends with the cards.
	CHECK(mosaic_rows(0) == 0);
	CHECK(mosaic_rows(1) == 2);
	CHECK(mosaic_rows(4) == 2);
	CHECK(mosaic_rows(5) == 4);
	CHECK(mosaic_rows(11) == 6);
}

TEST("a page reads from and runs on its source, which keeps no file ahead of a command unless it keeps files itself") {
	memory_source data;
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	CHECK(&nav.current().source() == &data);
	CHECK(!data.keep(std::filesystem::temp_directory_path() / "anything.png").has_value());
}

TEST("a form's files are uploaded as it's sent and added to what's attached, and one that isn't there turns it down") {
	memory_source data;
	data.on("tracker::issue::attach", [](const value&, memory_source&) -> answer { return "atlas-12"; });
	data.set("tracker::issue_page", "atlas-12", value(fields{{"attachments", list{"files/old.log"}}}));
	auto s = tracker();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	auto shot = std::filesystem::temp_directory_path() / "uione screens shot.png";
	std::ofstream(shot) << "png";
	form_spec spec{.command = "tracker::issue::attach", .fields = {{.name = "attachments", .type = "files"}}, .from = p.view("tracker::issue_page", "atlas-12"), .id = "atlas-12"};
	form_model form(p, spec);
	// As a file manager copies it, its spaces written as %20.
	std::string address = "file://" + shot.string();
	for (std::size_t at; (at = address.find(' ')) != std::string::npos;) address.replace(at, 1, "%20");
	form.written(spec.fields[0]).set(address + "\n");
	form.send();
	CHECK(data.uploads() == std::vector<std::string>{shot.string()});
	CHECK(!data.runs().empty());
	if (!data.runs().empty()) {
		const auto& sent = data.runs().back().fields["attachments"].as_array();
		CHECK(sent.size() == 2 && sent[0].as_string() == "files/old.log" && sent[1].as_string() == "uploads/uione screens shot.png");
	}
	CHECK(form.written(spec.fields[0]).peek().empty());  // sent, so not sent again
	form.written(spec.fields[0]).set("~/no such file for uione.png");
	form.send();
	CHECK(data.runs().size() == 1);
	CHECK(form.error() == "~/no such file for uione.png isn't a file");
	std::filesystem::remove(shot);
}

// The themes some seeds make, as palettes.txt holds them for @uione/react's palette.ts
// too: a seed, a scheme, then each role's light and dark, a line each. With
// ONE_PALETTE_GOLDEN=write it's written from what's made now.
TEST("a notice is said once its row's time has come, each row once at its time, and its button acts on the row") {
	using namespace std::chrono;
	use_time_zone("UTC");
	memory_source data;
	auto at = [](int h, int m = 0) {
		value t;
		t.type = value::kind::time;
		t.time.seconds = sys_seconds(sys_days(year(2026) / October / 28) + hours(h) + minutes(m)).time_since_epoch().count();
		return t;
	};
	auto task = [](std::string id, std::string title, value when) { return value(fields{{"id", std::move(id)}, {"title", std::move(title)}, {"remind_at", std::move(when)}}); };
	data.set("orbit::planner", "", value(fields{{"reminders", list{task("t1", "Call the bank", at(9)), task("t2", "Dentist", at(14))}}}));
	data.on("orbit::task::remind", [](const value&, memory_source&) -> answer { return std::string("t1"); });
	// Nothing's remembered past the run, as no app says which it is.
	notice_watch watch(data, {notice_spec{.view = "orbit::planner",
	                                      .list = "reminders",
	                                      .at = "remind_at",
	                                      .shown = {"title"},
	                                      .buttons = {notice_button{"orbit::task::remind", "In 10 minutes", [] { return value(fields{{"remind_at", value("later")}}); }}}}});
	auto now = sys_days(year(2026) / October / 28) + hours(10);
	auto due = watch.due(now);
	CHECK(due.size() == 1);
	if (due.size() != 1) return;
	CHECK(due[0].title == "Call the bank");
	CHECK(due[0].row == "t1");
	CHECK(watch.due(now).empty());  // said once
	CHECK(watch.due(now + hours(5)).size() == 1);  // the dentist's, once it's time
	// Moved later, it's said again then.
	data.set("orbit::planner", "", value(fields{{"reminders", list{task("t1", "Call the bank", at(11)), task("t2", "Dentist", at(14))}}}));
	CHECK(watch.due(now + hours(5)).size() == 1);
	watch.press(due[0], 0);
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	CHECK(data.runs()[0].command == "orbit::task::remind");
	CHECK(data.runs()[0].fields["id"].as_string() == "t1");
	CHECK(data.runs()[0].fields["remind_at"].as_string() == "later");
}

TEST("a seed and a scheme make one theme, written as palettes.txt says, wherever it's made") {
	std::string made;
	for (std::uint32_t seed : {1u, 2u, 3u, 7u, 42u, 1000u, 48213u, 99991u, 4294967295u}) {
		for (auto scheme : palette::schemes) {
			auto t = palette::generate(seed, scheme);
			made += std::to_string(seed) + " " + std::string(scheme);
			for (const auto& c : t.colors) made += " " + c.light + " " + c.dark;
			made += "\n";
		}
	}
	auto file = std::filesystem::path(__FILE__).parent_path() / "golden" / "palettes.txt";
	if (std::getenv("ONE_PALETTE_GOLDEN")) std::ofstream(file) << made;
	std::ifstream in(file);
	std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(kept == made);
}

TEST("a theme from a palette is written as a project says one, to paste in") {
	auto t = palette::generate(48213, "triadic");
	std::string said = palette::written(t);
	CHECK(said.starts_with("define theme triadic_48213 \"Triadic 48213\" {\n\tpage     #"));
	CHECK(said.find("\twarning  #") != std::string::npos);
	CHECK(said.ends_with("}\n"));
	CHECK(palette::scheme_title("split_complementary") == "Split complementary");
	// Danger stays red and warning amber, whatever the palette's hue.
	for (std::uint32_t seed = 1; seed < 60; ++seed) {
		auto made = palette::generate(seed, "complementary");
		auto [r, g, b] = colors::rgb(made["danger"].light);
		CHECK(r > g && r > b);
		auto [wr, wg, wb] = colors::rgb(made["warning"].dark);
		CHECK(wr >= wb && wg >= wb);
	}
}

TEST("a shift made ready moves a picture's colors as shifted moves each one, to within rounding") {
	namespace c = one::screens::collage;
	c::color_shift toned;
	toned.gain[1] = 0.8, toned.gain[2] = 1.2, toned.offset[1] = 6, toned.offset[2] = -9;
	toned.from = {0, 40, 128, 200, 255}, toned.to = {10, 60, 120, 210, 250};
	c::color_shift plain;
	plain.gain[0] = 1.1, plain.offset[0] = -5, plain.gain[1] = 0.9, plain.offset[2] = 4;
	for (const auto* s : {&toned, &plain}) {
		c::color_shifter ready(*s);
		int worst = 0;
		for (int r = 0; r < 256; r += 5) {
			for (int g = 0; g < 256; g += 7) {
				for (int b = 0; b < 256; b += 11) {
					std::uint8_t pixel[4] = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), 255};
					ready.shift(pixel, 1);
					double rgb[3] = {static_cast<double>(r), static_cast<double>(g), static_cast<double>(b)};
					c::shifted(*s, rgb);
					for (int k = 0; k < 3; ++k) worst = std::max(worst, std::abs(pixel[k] - static_cast<int>(std::clamp(std::lround(rgb[k]), 0L, 255L))));
					CHECK(pixel[3] == 255);
				}
			}
		}
		CHECK(worst <= 1);
	}
}

TEST("a collage's layers go where collage-cases.json says, as they do on the web") {
	namespace c = one::screens::collage;
	std::ifstream in(std::filesystem::path(__FILE__).parent_path() / "collage-cases.json");
	auto all = ember::parse_json(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
	auto n = [](const value& v, std::size_t i) { return v.as_array().at(i).as_double(); };
	auto box_of = [&](const value& v) { return c::box{n(v, 0), n(v, 1), n(v, 2), n(v, 3)}; };
	auto boxes_of = [&](const value& v) {
		std::vector<c::box> out;
		for (const auto& b : v.as_array()) out.push_back(box_of(b));
		return out;
	};
	auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
	auto same = [&](c::box a, c::box b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h); };
	auto said = [](c::box b) { return std::format("{} {} {} {}", b.x, b.y, b.w, b.h); };
	// A gesture's guides, as the case says them, or none.
	auto guided_as = [&](const c::guided& g, const value& k) {
		bool x = k["guide_x"].is_null() ? !g.guide_x : g.guide_x && near(*g.guide_x, k["guide_x"].as_double());
		bool y = k["guide_y"].is_null() ? !g.guide_y : g.guide_y && near(*g.guide_y, k["guide_y"].as_double());
		bool corner = k["guide_corner"].is_null() ? !g.corner
		                                    : g.corner && near(g.corner->first.x, n(k["guide_corner"].as_array()[0], 0)) && near(g.corner->first.y, n(k["guide_corner"].as_array()[0], 1)) &&
		                                          near(g.corner->second.x, n(k["guide_corner"].as_array()[1], 0)) && near(g.corner->second.y, n(k["guide_corner"].as_array()[1], 1));
		return x && y && corner;
	};
	int count = 0;
	for (const auto& k : all["cases"].as_array()) {
		++count;
		const std::string& what = k["what"].as_string();
		std::string note = std::format("case {}, {}", count, what);
		auto size = k["size"];
		double width = size.is_null() ? 0 : n(size, 0), height = size.is_null() ? 0 : n(size, 1);
		if (what == "sitting") {
			auto got = c::sitting(k["sits"].as_string(), n(k["picture"], 0), n(k["picture"], 1), width, height);
			check::verify(same(got, box_of(k["box"])), "sitting", note + ": " + said(got));
		} else if (what == "whole_of") {
			auto got = c::whole_of(box_of(k["shown"]), box_of(k["crop"]));
			check::verify(same(got, box_of(k["box"])), "whole_of", note + ": " + said(got));
		} else if (what == "snap_lines") {
			auto got = c::snap_lines(width, height, boxes_of(k["others"]));
			std::vector<double> across, down;
			for (const auto& v : k["across"].as_array()) across.push_back(v.as_double());
			for (const auto& v : k["down"].as_array()) down.push_back(v.as_double());
			check::verify(got.across == across && got.down == down, "snap_lines", note);
		} else if (what == "moved" || what == "resized") {
			auto lines = c::snap_lines(width, height, boxes_of(k["others"]));
			auto from = box_of(k["from"]);
			double dx = n(k["by"], 0), dy = n(k["by"], 1), close = k["close"].as_double();
			auto got = what == "moved" ? c::moved(from, dx, dy, lines, width, height, close, k["snapping"].as_bool())
			                           : c::resized(from, static_cast<int>(k["corner"].as_integer()), dx, dy, k["keep_shape"].as_bool(), lines, close, k["snapping"].as_bool());
			check::verify(same(got.at, box_of(k["box"])) && guided_as(got, k), "a gesture", note + ": " + said(got.at));
		} else if (what == "cropped") {
			auto got = c::cropped(box_of(k["kept"]), static_cast<int>(k["edges"].as_integer()), n(k["by"], 0), n(k["by"], 1), box_of(k["whole"]));
			check::verify(same(got, box_of(k["box"])), "cropped", note + ": " + said(got));
		} else if (what == "polygon_points") {
			auto got = c::polygon_points(k["sides"].as_double(), k["spoke"].as_double(), k["star"].as_bool());
			check::verify(got == k["points"].as_string(), "polygon_points", note + ": " + got);
		} else if (what == "zoomed") {
			c::view_at was{n(k["view"], 0), n(k["view"], 1), n(k["view"], 2)};
			c::box area = box_of(k["area"]);
			auto got = c::zoomed(was, area, n(k["size"], 0), n(k["size"], 1), k["margin"].as_double(), k["by"].as_double(), {n(k["at"], 0), n(k["at"], 1)});
			auto board = c::board_of(got, area, n(k["size"], 0), n(k["size"], 1), k["margin"].as_double());
			check::verify(near(got.zoom, n(k["zoomed"], 0)) && near(got.pan_x, n(k["zoomed"], 1)) && near(got.pan_y, n(k["zoomed"], 2)) && same(board, box_of(k["board"])), "zoomed",
			              note + ": " + std::format("{} {} {} {}", got.zoom, got.pan_x, got.pan_y, said(board)));
		} else if (what == "around") {
			std::vector<c::box> boxes;
			for (const auto& b : k["boxes"].as_array()) boxes.push_back(box_of(b));
			check::verify(same(c::around(boxes), box_of(k["box"])), "around", note);
		} else if (what == "carried") {
			check::verify(same(c::carried(box_of(k["one"]), box_of(k["from"]), box_of(k["to"])), box_of(k["box"])), "carried", note);
		} else if (what == "path_bounds") {
			auto got = c::path_bounds(k["path"].as_string());
			std::string fractions = c::path_fractions(k["path"].as_string(), got);
			check::verify(same(got, box_of(k["box"])) && fractions == k["points"].as_string(), "path_bounds", note + ": " + said(got) + " " + fractions);
		} else if (what == "pen_path") {
			std::vector<c::pen_node> nodes;
			for (const auto& at : k["pen"].as_array()) nodes.push_back({n(at, 0), n(at, 1), n(at, 2), n(at, 3)});
			auto got = c::pen_path(nodes, k["closed"].as_bool());
			check::verify(got == k["path"].as_string(), "pen_path", note + ": " + got);
		} else if (what == "freehand") {
			std::vector<c::point> drawn;
			for (const auto& at : k["drawn"].as_array()) drawn.push_back({n(at, 0), n(at, 1)});
			auto got = c::freehand(drawn, k["tolerance"].as_double());
			check::verify(got == k["path"].as_string(), "freehand", note + ": " + got);
		} else if (what == "path_nodes") {
			auto got = c::path_nodes(k["path"].as_string());
			bool same_nodes = got.size() == k["nodes"].as_array().size();
			for (std::size_t i = 0; same_nodes && i < got.size(); ++i) same_nodes = near(got[i].x, n(k["nodes"].as_array()[i], 0)) && near(got[i].y, n(k["nodes"].as_array()[i], 1));
			check::verify(same_nodes, "path_nodes", note);
		} else if (what == "path_moved") {
			auto got = c::path_moved(k["path"].as_string(), static_cast<std::size_t>(k["node"].as_integer()), n(k["by"], 0), n(k["by"], 1));
			check::verify(got == k["moved"].as_string(), "path_moved", note + ": " + got);
		} else if (what == "path_handles") {
			auto got = c::path_handles(k["path"].as_string(), static_cast<std::size_t>(k["node"].as_integer()));
			const auto& want = k["handles"].as_array();
			bool same_handles = got.size() == want.size();
			for (std::size_t i = 0; same_handles && i < got.size(); ++i) {
				same_handles = near(got[i].at.x, n(want[i], 0)) && near(got[i].at.y, n(want[i], 1)) && got[i].out == want[i].as_array()[2].as_bool();
			}
			check::verify(same_handles, "path_handles", note);
		} else if (what == "path_handle_moved") {
			auto got = c::path_handle_moved(k["path"].as_string(), static_cast<std::size_t>(k["node"].as_integer()), k["out"].as_bool(), n(k["by"], 0), n(k["by"], 1));
			check::verify(got == k["moved"].as_string(), "path_handle_moved", note + ": " + got);
		} else if (what == "path_reaches") {
			bool got = c::path_reaches(k["path"].as_string(), {n(k["at"], 0), n(k["at"], 1)}, k["reach"].as_double(), k["filled"].as_bool());
			check::verify(got == k["reaches"].as_bool(), "path_reaches", note);
		} else if (what == "drawn_line") {
			auto got = c::drawn_line({n(k["from"], 0), n(k["from"], 1)}, {n(k["to"], 0), n(k["to"], 1)}, k["straight"].as_bool(), k["click"].as_double(), width, height);
			check::verify(same(got.at, box_of(k["box"])) && got.points == k["points"].as_string(), "drawn_line", note + ": " + said(got.at) + " " + got.points);
		} else if (what == "drawn") {
			auto got = c::drawn({n(k["from"], 0), n(k["from"], 1)}, {n(k["to"], 0), n(k["to"], 1)}, k["square"].as_bool(), k["click"].as_double(), width, height);
			check::verify(same(got, box_of(k["box"])), "drawn", note + ": " + said(got));
		} else if (what == "corner_at") {
			auto got = c::corner_at(box_of(k["box"]), {n(k["at"], 0), n(k["at"], 1)}, k["handle"].as_double());
			check::verify(got == k["corner"].as_integer(), "corner_at", note);
		} else if (what == "crop_edges_at") {
			auto got = c::crop_edges_at(box_of(k["box"]), {n(k["at"], 0), n(k["at"], 1)}, k["handle"].as_double());
			check::verify(got == k["edges"].as_integer(), "crop_edges_at", note);
		} else if (what == "harmonic_fill") {
			int w = static_cast<int>(n(k["size"], 0)), h = static_cast<int>(n(k["size"], 1));
			std::vector<double> colors(static_cast<std::size_t>(w) * h * 3, 0);
			std::vector<bool> covered(static_cast<std::size_t>(w) * h, false);
			for (const auto& c : k["covered"].as_array()) {
				std::size_t at = static_cast<std::size_t>(n(c, 1)) * w + static_cast<std::size_t>(n(c, 0));
				covered[at] = true;
				for (std::size_t i = 0; i < 3; ++i) colors[at * 3 + i] = n(c, 2 + i);
			}
			auto got = c::harmonic_fill(w, h, colors, covered);
			for (const auto& e : k["expect"].as_array()) {
				std::size_t at = static_cast<std::size_t>(n(e, 1)) * w + static_cast<std::size_t>(n(e, 0));
				bool close = true;
				for (std::size_t i = 0; i < 3; ++i) close = close && std::abs(got[at * 3 + i] - n(e, 2 + i)) <= k["close"].as_double();
				check::verify(close, "harmonic_fill", std::format("{} at {}, {}: {} {} {}", note, n(e, 0), n(e, 1), got[at * 3], got[at * 3 + 1], got[at * 3 + 2]));
			}
		} else if (what == "halo") {
			int w = static_cast<int>(n(k["size"], 0)), h = static_cast<int>(n(k["size"], 1));
			std::vector<double> colors(static_cast<std::size_t>(w) * h * 3, 0);
			for (const auto& c : k["cells"].as_array()) {
				std::size_t at = static_cast<std::size_t>(n(c, 1)) * w + static_cast<std::size_t>(n(c, 0));
				for (std::size_t i = 0; i < 3; ++i) colors[at * 3 + i] = n(c, 2 + i);
			}
			int reach = static_cast<int>(k["reach"].as_integer());
			auto got = c::halo(w, h, colors, reach, k["inner"].as_double());
			for (const auto& e : k["expect"].as_array()) {
				std::size_t at = static_cast<std::size_t>(n(e, 1)) * (w + 2 * reach) + static_cast<std::size_t>(n(e, 0));
				bool close = true;
				for (std::size_t i = 0; i < 4; ++i) close = close && std::abs(got[at * 4 + i] - n(e, 2 + i)) <= k["close"].as_double();
				check::verify(close, "halo", std::format("{} at {}, {}: {} {} {} {}", note, n(e, 0), n(e, 1), got[at * 4], got[at * 4 + 1], got[at * 4 + 2], got[at * 4 + 3]));
			}
		} else if (what == "stats_of") {
			std::vector<double> colors;
			for (const auto& c : k["cells"].as_array()) {
				for (std::size_t i = 0; i < 3; ++i) colors.push_back(n(c, i));
			}
			auto got = c::stats_of(colors, k["weight"].as_double());
			bool close = got.weight == k["weight"].as_double();
			for (std::size_t i = 0; i < 3; ++i) {
				close = close && std::abs(got.mean[i] - n(k["mean"], i)) <= k["close"].as_double() && std::abs(got.spread[i] - n(k["spread"], i)) <= k["close"].as_double();
			}
			if (!k["tones"].is_null()) {
				close = close && got.tones.size() == k["tones"].as_array().size();
				for (std::size_t i = 0; close && i < got.tones.size(); ++i) close = std::abs(got.tones[i] - n(k["tones"], i)) <= k["close"].as_double();
			}
			check::verify(close, "stats_of", note);
		} else if (what == "matched") {
			std::vector<c::color_stats> pictures;
			for (const auto& p : k["pictures"].as_array()) {
				c::color_stats s;
				s.weight = p["weight"].as_double();
				for (std::size_t i = 0; i < 3; ++i) s.mean[i] = n(p["mean"], i), s.spread[i] = n(p["spread"], i);
				if (!p["tones"].is_null()) {
					for (std::size_t i = 0; i < p["tones"].as_array().size(); ++i) s.tones.push_back(n(p["tones"], i));
				}
				pictures.push_back(s);
			}
			auto got = c::matched(pictures, k["strength"].as_double(), k["cast"].as_double());
			const auto& want = k["shifts"].as_array();
			bool close = got.size() == want.size();
			for (std::size_t j = 0; close && j < got.size(); ++j) {
				for (std::size_t i = 0; i < 3; ++i) {
					close = close && std::abs(got[j].gain[i] - n(want[j]["gain"], i)) <= k["close"].as_double() && std::abs(got[j].offset[i] - n(want[j]["offset"], i)) <= k["close"].as_double();
				}
				for (const char* curve : {"from", "to"}) {
					const auto& said = std::string(curve) == "from" ? got[j].from : got[j].to;
					std::size_t wanted = want[j][curve].is_null() ? 0 : want[j][curve].as_array().size();
					close = close && said.size() == wanted;
					for (std::size_t i = 0; close && i < wanted; ++i) close = std::abs(said[i] - n(want[j][curve], i)) <= k["close"].as_double();
				}
			}
			check::verify(close, "matched", note);
		} else if (what == "shifted") {
			c::color_shift s;
			const auto& said = k["shift"];
			for (std::size_t i = 0; i < 3; ++i) s.gain[i] = n(said["gain"], i), s.offset[i] = n(said["offset"], i);
			for (std::size_t i = 0; !said["from"].is_null() && i < said["from"].as_array().size(); ++i) s.from.push_back(n(said["from"], i)), s.to.push_back(n(said["to"], i));
			double rgb[3] = {n(k["rgb"], 0), n(k["rgb"], 1), n(k["rgb"], 2)};
			c::shifted(s, rgb);
			bool close = true;
			for (std::size_t i = 0; i < 3; ++i) close = close && std::abs(rgb[i] - n(k["expect"], i)) <= k["close"].as_double();
			check::verify(close, "shifted", std::format("{}: {} {} {}", note, rgb[0], rgb[1], rgb[2]));
		} else if (what == "svg_document") {
			std::vector<c::saved_layer> layers;
			for (const auto& l : k["layers"].as_array()) {
				c::saved_layer saved;
				saved.kind = l["kind"].as_string();
				saved.at = box_of(l["box"]);
				if (!l["opacity"].is_null()) saved.opacity = l["opacity"].as_double();
				if (const auto& shape = l["shape"]; !shape.is_null()) {
					saved.drawn = {shape["kind"].as_string(), shape["points"].as_string(), shape["fill"].as_string(), shape["stroke"].as_string(), shape["stroke_width"].as_double(),
					               shape["corners"].as_double()};
				}
				saved.words = l["words"].as_string(), saved.color = l["color"].as_string(), saved.family = l["family"].as_string();
				if (!l["size"].is_null()) saved.size = l["size"].as_double();
				if (!l["weight"].is_null()) saved.weight = static_cast<int>(l["weight"].as_integer());
				saved.italic = !l["italic"].is_null() && l["italic"].as_bool();
				saved.href = l["href"].as_string();
				if (!l["crop"].is_null()) saved.crop = box_of(l["crop"]);
				if (!l["turns"].is_null()) saved.turns = static_cast<int>(l["turns"].as_integer());
				saved.mirrored = !l["mirrored"].is_null() && l["mirrored"].as_bool();
				if (!l["corners"].is_null()) saved.corners = l["corners"].as_double();
				saved.svg = l["svg"].as_string();
				if (!l["natural"].is_null()) saved.natural = box_of(l["natural"]);
				layers.push_back(std::move(saved));
			}
			std::string got = c::svg_document(width, height, layers);
			check::verify(got == k["svg"].as_string(), "svg_document", note + ": " + got);
		} else if (what == "svg") {
			const auto& said = k["shape"];
			c::shape s{said["kind"].as_string(), said["points"].as_string(), said["fill"].as_string(), said["stroke"].as_string(), said["stroke_width"].as_double(),
			           said["corners"].as_double()};
			std::string got = c::svg_element(s, n(k["box"], 0), n(k["box"], 1), n(k["box"], 2), n(k["box"], 3));
			check::verify(got == k["svg"].as_string(), "svg", note + ": " + got);
		} else {
			check::verify(false, "a case of something known", note);
		}
	}
	CHECK(count >= 30);
}

int main() {
	use_time_zone("UTC");
	return check::run();
}

TEST("a collage's layer is raised past the one above it, a group with all it holds, in its own layer, and lowered past the one below, as on the web") {
	using one::screens::value;
	auto row = [](std::string id, std::string layer, double depth, std::string group) {
		return value(std::map<std::string, value>{{"id", value(std::move(id))}, {"layer", value(std::move(layer))}, {"depth", value(depth)}, {"group", value(std::move(group))}});
	};
	// In layer L, lowest first: a, then group g of b and c, then d; e is in another layer.
	value rows = one::screens::grouped(value(std::vector<value>{row("a", "L", 0, ""), row("g", "L", 9, ""), row("b", "L", 1, "g"), row("c", "L", 2, "g"), row("d", "L", 3, ""), row("e", "M", 0, "")}));
	using moved = std::vector<std::pair<std::string, double>>;
	check::verify(one::screens::restacked(rows, "a", true, "layer") == moved{{"b", 0}, {"c", 1}, {"a", 2}}, "a raised past the group");
	check::verify(one::screens::restacked(rows, "g", true, "layer") == moved{{"d", 1}, {"b", 2}, {"c", 3}}, "the group raised with all it holds");
	check::verify(one::screens::restacked(rows, "d", false, "layer") == moved{{"d", 1}, {"b", 2}, {"c", 3}}, "d lowered past the group");
	check::verify(one::screens::restacked(rows, "c", false, "layer") == moved{{"c", 1}, {"b", 2}}, "inside the group");
	check::verify(one::screens::restacked(rows, "d", true, "layer").empty(), "nothing above the top");
}
