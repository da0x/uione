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

TEST("a value worked out on a screen leaves what's over as a clock counts, and none over none") {
	CHECK(same(worked('%', value(5), value(4)), value(1)));
	CHECK(same(worked('%', value(-1), value(4)), value(3)));
	CHECK(same(worked('%', value(2.5), value(1)), value(0.5)));
	CHECK(same(worked('%', value(3), value(0)), value()));
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
		                      value(fields{{"id", "a-2"}, {"title", "neotrac"}, {"stage", "failed"}, {"labels", value(list{"websites"})}, {"data", value(fields{{"at", "Checking"}, {"note", "not pushed"}})}}),
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
		c::backdrop b{k["sits"].as_string(), size.is_null() ? 0 : n(size, 0), size.is_null() ? 0 : n(size, 1), k["zoom"].is_null() ? 1 : k["zoom"].as_double(),
		              k["slide"].is_null() ? 0.5 : n(k["slide"], 0), k["slide"].is_null() ? 0.5 : n(k["slide"], 1)};
		if (what == "sitting") {
			auto got = c::sitting(b.sits, n(k["picture"], 0), n(k["picture"], 1), b.width, b.height);
			check::verify(same(got, box_of(k["box"])), "sitting", note + ": " + said(got));
		} else if (what == "background_at") {
			auto got = c::background_at(b, n(k["picture"], 0), n(k["picture"], 1));
			check::verify(same(got, box_of(k["box"])), "background_at", note + ": " + said(got));
		} else if (what == "slides") {
			check::verify(c::slides(b, box_of(k["at"])) == k["slides"].as_bool(), "slides", note);
		} else if (what == "whole_of") {
			auto got = c::whole_of(box_of(k["shown"]), box_of(k["crop"]));
			check::verify(same(got, box_of(k["box"])), "whole_of", note + ": " + said(got));
		} else if (what == "snap_lines") {
			auto got = c::snap_lines(b.width, b.height, boxes_of(k["others"]));
			std::vector<double> across, down;
			for (const auto& v : k["across"].as_array()) across.push_back(v.as_double());
			for (const auto& v : k["down"].as_array()) down.push_back(v.as_double());
			check::verify(got.across == across && got.down == down, "snap_lines", note);
		} else if (what == "moved" || what == "resized") {
			auto lines = c::snap_lines(b.width, b.height, boxes_of(k["others"]));
			auto from = box_of(k["from"]);
			double dx = n(k["by"], 0), dy = n(k["by"], 1), close = k["close"].as_double();
			auto got = what == "moved" ? c::moved(from, dx, dy, lines, b.width, b.height, close, k["snapping"].as_bool())
			                           : c::resized(from, static_cast<int>(k["corner"].as_integer()), dx, dy, k["keep_shape"].as_bool(), lines, close, k["snapping"].as_bool());
			check::verify(same(got.at, box_of(k["box"])) && guided_as(got, k), "a gesture", note + ": " + said(got.at));
		} else if (what == "slid") {
			auto got = c::slid(box_of(k["from"]), n(k["by"], 0), n(k["by"], 1), {n(k["across"], 0), n(k["across"], 1)}, {n(k["down"], 0), n(k["down"], 1)}, b.width,
			                   b.height, k["close"].as_double(), k["snapping"].as_bool());
			check::verify(same(got.at, box_of(k["box"])) && guided_as(got, k), "slid", note + ": " + said(got.at));
		} else if (what == "cropped") {
			auto got = c::cropped(box_of(k["kept"]), static_cast<int>(k["edges"].as_integer()), n(k["by"], 0), n(k["by"], 1), box_of(k["whole"]));
			check::verify(same(got, box_of(k["box"])), "cropped", note + ": " + said(got));
		} else if (what == "corner_at") {
			auto got = c::corner_at(box_of(k["box"]), {n(k["at"], 0), n(k["at"], 1)}, k["handle"].as_double());
			check::verify(got == k["corner"].as_integer(), "corner_at", note);
		} else if (what == "crop_edges_at") {
			auto got = c::crop_edges_at(box_of(k["box"]), {n(k["at"], 0), n(k["at"], 1)}, k["handle"].as_double());
			check::verify(got == k["edges"].as_integer(), "crop_edges_at", note);
		} else {
			check::verify(false, "a case of something known", note);
		}
	}
	CHECK(count >= 40);
}

int main() {
	use_time_zone("UTC");
	return check::run();
}
