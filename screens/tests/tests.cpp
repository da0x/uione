// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::screens' tests: values read as people read them, addresses, keys and routes
// as the web makes them, changes said in words, and what a page does: its views read
// live, its commands run, asked about first and turned down, and what it keeps.
// What's drawn is held by each toolkit's own tests.

#include <map>

#include "check.hpp"
#include "one/screens/models.hpp"

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

int main() {
	use_time_zone("UTC");
	return check::run();
}
