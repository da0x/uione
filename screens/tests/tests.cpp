// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::screens' tests: values read as people read them, addresses, keys and routes
// as the web makes them, changes said in words, and what a page does: its views read
// live, its commands run, asked about first and turned down, and what it keeps.
// What's drawn is held by each toolkit's own tests.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "check.hpp"
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
	canvas_spec spec{.view = board, .list = "issues", .columns = {{"title", "Title"}}, .place = "tracker::issue::place", .grid = 160, .origin_x = 40, .origin_y = 80,
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

int main() {
	use_time_zone("UTC");
	return check::run();
}
