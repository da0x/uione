// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::desktop::runtime's tests: addresses and keys made as the web makes them,
// pages opened by address and left, views read live from a source, commands run and
// turned down, and a whole window of a small tracker drawn and compared with its
// golden image, all on the in-memory source.

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

#include "check.hpp"
#include "offscreen.hpp"
#include "one/desktop/runtime/components.hpp"
#include "one/desktop/runtime/markdown.hpp"
#include "ui_internal.hpp"

#include <algorithm>

namespace {

	using namespace one::desktop::runtime;
	using fields = std::map<std::string, value>;
	using list = std::vector<value>;

	// Each of a collage's parts in the field named like it, as an app's are by default.
	const std::map<std::string, std::string> named_like_parts = {{"x", "x"}, {"y", "y"}, {"w", "w"}, {"h", "h"}, {"depth", "depth"}, {"picture", "picture"}, {"picture_width", "picture_width"}, {"picture_height", "picture_height"},
	    {"crop_x", "crop_x"}, {"crop_y", "crop_y"}, {"crop_w", "crop_w"}, {"crop_h", "crop_h"}, {"turns", "turns"}, {"mirrored", "mirrored"}, {"around", "around"}, {"corners", "corners"},
	    {"words", "words"}, {"size", "size"}, {"color", "color"}, {"weight", "weight"}, {"shape", "shape"}, {"points", "points"}, {"fill", "fill"}, {"stroke", "stroke"},
	    {"stroke_width", "stroke_width"}, {"opacity", "opacity"}, {"hidden", "hidden"}};

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

	// Drawn and held to its golden image; `placed` is told of what's drawn once it's laid
	// out, to give one part the keyboard, say.
	void matches_golden(const std::string& name, one::desktop::size size, const std::function<one::desktop::view()>& build,
	                    const std::function<void(one::desktop::element&)>& placed = {}) {
		// Kept until it's drawn, as a window keeps its own: what's drawn last, like an open
		// popover, is drawn after the rest.
		std::unique_ptr<one::desktop::element> root;
		auto drawn = one::desktop::offscreen::draw(size, 1, tested_theme().page, [&](one::desktop::canvas& c) {
			one::desktop::detail::theme_in_use() = &tested_theme();
			root = build().take();
			root->place({0, 0, static_cast<float>(size.width), static_cast<float>(size.height)});
			if (placed) placed(*root);
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
	CHECK(nav.shown(nav.current()).route == "/projects");
	CHECK(!nav.back());  // the first page stays
}

TEST("an address opens the screen most particular to it, a part named outright before a parameter") {
	memory_source data;
	site s{.name = "tracker"};
	s.screens.push_back({"Issue", "/:project/:issue"});
	s.screens.push_back({"Settings", "/:project/settings"});
	s.screens.push_back({"Docs", "/docs/:page?"});
	navigator nav(s, data);
	std::map<std::string, std::string> params;
	CHECK(nav.match("/atlas/settings", params)->title == "Settings");
	CHECK(nav.match("/atlas/12", params)->title == "Issue");
	CHECK(params.at("issue") == "12");
	CHECK(nav.match("/docs", params)->title == "Docs");
	CHECK(nav.match("/docs/start", params)->title == "Docs");
	CHECK(nav.match("/docs/start/more", params) == nullptr);
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
	matches_golden("project-page", {900, 560}, [&] { return drawn(nav); });
}

namespace {

	// The window's view built and placed, as it is each time something it read changes.
	std::unique_ptr<one::desktop::element> built(navigator& nav) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		auto root = drawn(nav).take();
		root->place({0, 0, 900, 900});
		return root;
	}

	std::vector<one::desktop::element*> focusable_in(one::desktop::element& root) {
		std::vector<one::desktop::element*> all;
		one::desktop::detail::focusables(root, all);
		return all;
	}

	one::desktop::key_event typed(const std::string& text) { return {.symbol = static_cast<std::uint32_t>(text[0]), .text = text}; }
	const one::desktop::key_event enter{.symbol = one::desktop::keys::enter, .text = ""};

	site with_forms() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Issues", "/projects/:project/issues", "", [](page& p) {
			                     return items(form(p, {.command = "tracker::issue::create",
			                                           .fields = {{"title"}, {.name = "labels", .type = "list"}},
			                                           .given = {{"project", p.param("project")}},
			                                           .submit = "Open issue"}));
		                     }});
		s.screens.push_back({"Project", "/projects/:project", "", [](page& p) {
			                     auto project = p.view("tracker::project_page", p.param("project"));
			                     return items(form(p, {.command = "tracker::project::update", .fields = {{"name"}}, .from = project, .id = p.param("project")}));
		                     }});
		return s;
	}

}  // namespace

TEST("a form sends what's typed with what it's given, then empties, and says why it was turned down") {
	memory_source data;
	int made = 0;
	data.on("tracker::issue::create", [&](const value& f, memory_source&) -> answer {
		if (f["title"].as_string().empty()) return std::unexpected(std::string("an issue needs a title"));
		return "atlas-" + std::to_string(++made);
	});
	auto s = with_forms();
	navigator nav(s, data);
	nav.open("/projects/atlas/issues");
	auto root = built(nav);
	auto parts = focusable_in(*root);
	// The bar's name link, the title, the labels, and the button.
	auto* title = parts[parts.size() - 3];
	auto* labels = parts[parts.size() - 2];
	auto* send = parts.back();
	send->handle_key(enter);
	CHECK(data.runs().size() == 1);
	CHECK(nav.current().error("tracker::issue::create") == "an issue needs a title");
	CHECK(nav.current().problem().peek().empty());  // said in the form, not across the page
	for (char c : std::string("Ship it")) title->handle_key(typed(std::string(1, c)));
	for (char c : std::string("ui, desktop")) labels->handle_key(typed(std::string(1, c)));
	send->handle_key(enter);
	CHECK(data.runs().size() == 2);
	const value& sent = data.runs().back().fields;
	CHECK(sent["title"].as_string() == "Ship it");
	CHECK(sent["labels"].as_array().size() == 2);
	CHECK(sent["labels"].as_array()[1].as_string() == "desktop");
	CHECK(sent["project"].as_string() == "atlas");
	CHECK(nav.current().error("tracker::issue::create").empty());
	CHECK(nav.current().kept<std::string>("form:tracker::issue::create::title").peek().empty());  // emptied for the next
}

TEST("an update form follows what's stored until it's typed in, and sends the id it changes") {
	memory_source data;
	data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas"}}));
	auto s = with_forms();
	navigator nav(s, data);
	nav.open("/projects/atlas");
	auto& p = nav.current();
	auto name = p.kept<std::string>("form:tracker::project::update:atlas:name");
	auto root = built(nav);
	CHECK(name.peek() == "Atlas");
	data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas, renamed elsewhere"}}));
	root = built(nav);
	CHECK(name.peek() == "Atlas, renamed elsewhere");
	auto parts = focusable_in(*root);
	auto* field = parts[parts.size() - 2];
	field->handle_key(typed("!"));
	data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas again"}}));
	root = built(nav);
	CHECK(name.peek() == "Atlas, renamed elsewhere!");  // what's typed stays
	focusable_in(*root).back()->handle_key(enter);
	CHECK(data.runs().back().fields["id"].as_string() == "atlas");
	CHECK(data.runs().back().fields["name"].as_string() == "Atlas, renamed elsewhere!");
	data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas, renamed elsewhere!"}}));
	built(nav);
	data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas at last"}}));
	built(nav);
	CHECK(name.peek() == "Atlas at last");  // following again, once sent
}

TEST("a form's fields: text, several lines, a few choices as radio buttons, a slider, a list's pick, a box, several boxes and a hint, with what turned it down") {
	memory_source data;
	data.on("tracker::issue::create", [](const value&, memory_source&) -> answer { return std::unexpected(std::string("the board is closed")); });
	data.set("tracker::project_page", "atlas", value(fields{{"members", list{value(fields{{"person", "maya"}, {"person.name", "Maya Garcia"}})}}}));
	site s{.name = "tracker", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"New issue", "/projects/:project/new", "", [](page& p) {
		                     auto project = p.view("tracker::project_page", p.param("project"));
		                     return items(form(p, {.command = "tracker::issue::create",
		                                           .fields = {{.name = "title", .hint = "What it is, in a few words"},
		                                                      {.name = "body", .label = "Description", .type = "markdown"},
		                                                      {.name = "priority", .type = "choice", .choices = {{"high", "High"}, {"normal", "Normal"}}, .start = "normal", .required = true},
		                                                      {.name = "effort", .type = "slider", .start = "40", .range = {0, 100, 5}},
		                                                      {.name = "assignee", .type = "pick", .choices = list_choices(project, "members", "person.name", "person")},
		                                                      {.name = "urgent", .type = "boolean"},
		                                                      {.name = "platforms", .type = "choices", .choices = {{"linux", "Linux"}, {"windows", "Windows"}}}}}));
	                     }});
	navigator nav(s, data);
	nav.open("/projects/atlas/new");
	nav.current().kept<std::string>("form:tracker::issue::create::title").set("Draw forms natively");
	nav.current().kept<bool>("form:tracker::issue::create::platforms:linux").set(true);
	nav.current().run("tracker::issue::create", value::map(), {}, true);
	matches_golden("form", {900, 860}, [&] { return drawn(nav); });
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

TEST("a change is said in words, a command's action as done, and a command's several changes once") {
	CHECK(done("close") == "closed");
	CHECK(done("take") == "took");
	CHECK(done("take_over") == "took over");
	CHECK(done("stop") == "stopped");
	CHECK(done("copy") == "copied");
	CHECK(done("submit") == "submitted");
	CHECK(done("visit") == "visited");
	CHECK(done("reopen") == "reopened");
	CHECK(done("follow") == "followed");
	CHECK(changed("status", "open", "closed", "tracker::issue::close") == "closed this");
	CHECK(changed("stage", "Reported", "Triaged", "tracker::issue::move") == "moved this from Reported to Triaged");
	CHECK(changed("title", "Old", "New", "tracker::issue::update") == "changed title from Old to New");
	CHECK(changed("", value(), value(), "tracker::issue::create") == "created this");
	CHECK(changed("comment", value(), value(), "tracker::comment::create") == "added a comment to this");
	auto [head, tail] = phrase("due_at", "", "Oct 1", "tracker::milestone::update");
	CHECK(head == "set due at of");
	CHECK(tail == "to Oct 1");
}

TEST("an issue's page: its body in Markdown, its comments, and its timeline with what's new marked") {
	memory_source data;
	auto at = [](int minutes) {
		value v;
		v.type = value::kind::time;
		v.time.seconds = 1'790'000'000 + minutes * 60;
		return v;
	};
	data.set("tracker::issue_page", "atlas-12",
	         value(fields{{"number", 12},
	                      {"title", "Copy an issue whole"},
	                      {"body", "Copy its **title**, body and `labels`, so a new one\nstarts where this one is.\n\n- one\n- two"},
	                      {"comments", list{value(fields{{"id", "c1"}, {"author.name", "Maya Garcia"}, {"body", "Seen."}, {"created_at", at(1)}}),
	                                        value(fields{{"id", "c2"}, {"author.name", "Ade Bello"}, {"body", "Taking it."}, {"created_at", at(5)}})}},
	                      {"changes", list{value(fields{{"id", "h1"}, {"field", ""}, {"action", "tracker::issue::create"}, {"created_by.name", "Maya Garcia"}, {"created_at", at(0)}}),
	                                       value(fields{{"id", "h2"}, {"field", "status"}, {"before", "open"}, {"after", "closed"}, {"action", "tracker::issue::close"},
	                                                    {"created_by.name", "Ade Bello"}, {"created_at", at(9)}}),
	                                       value(fields{{"id", "h3"}, {"field", "closed_at"}, {"before", value()}, {"after", at(9)}, {"action", "tracker::issue::close"},
	                                                    {"created_by.name", "Ade Bello"}, {"created_at", at(9)}})}}}));
	data.set("tracker::seen", "", value(fields{{"at", at(3)}}));
	site s{.name = "tracker", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"", "/issues/:issue", "", [](page& p) {
		                     auto issue = p.view("tracker::issue_page", p.param("issue"));
		                     auto seen = p.view("tracker::seen");
		                     p.title({words("#"), live(issue, "number"), words(" "), live(issue, "title")});
		                     return items(markdown(p, issue, "body"), section({"Comments"}, thread(p, {.view = issue, .list = "comments", .body = markdown_view})),
		                                  timeline(p, {.view = issue,
		                                               .list = "changes",
		                                               .title = "History",
		                                               .fresh_views = {seen},
		                                               .fresh = [](const value& row, const std::vector<value>& read) {
			                                               return row["created_at"].time.seconds > read[0]["at"].time.seconds;
		                                               },
		                                               .on_open = "tracker::seen::touch"}));
	                     }});
	navigator nav(s, data);
	nav.open("/issues/atlas-12");
	matches_golden("issue-page", {900, 760}, [&] { return drawn(nav); });
	CHECK(data.runs().size() == 1);  // said it's been seen, once
	CHECK(data.runs().front().command == "tracker::seen::touch");
}

namespace {

	// first: more of the first card's fields, like who holds it.
	void fill_board(memory_source& data, const fields& first = {}) {
		auto issue = [&first](std::string id, int n, std::string title, std::string stage, std::string priority, list labels = {}, list people = {}) {
			fields f{{"id", id}, {"number", n}, {"title", title}, {"stage", stage}, {"priority", priority}, {"labels", labels}, {"assignees.name", people}};
			if (n == 12) f.insert(first.begin(), first.end());
			return value(f);
		};
		data.set("projects::board_page", "atlas-main",
		         value(fields{{"stages", list{value(fields{{"id", "atlas-main-to_do"}, {"title", "To do"}}), value(fields{{"id", "atlas-main-doing"}, {"title", "Doing"}}),
		                                      value(fields{{"id", "atlas-main-done"}, {"title", "Done"}})}},
		                      // To do leads to doing for maintainers and contributors; doing to done for maintainers alone.
		                      {"steps", list{value(fields{{"from", "atlas-main-to_do"}, {"to", "atlas-main-doing"}, {"roles", list{"atlas-maintainer", "atlas-contributor"}}}),
		                                     value(fields{{"from", "atlas-main-doing"}, {"to", "atlas-main-done"}, {"roles", list{"atlas-maintainer"}}})}},
		                      {"issues", list{issue("atlas-12", 12, "Copy an issue whole", "atlas-main-to_do", "high", list{"ui"}, list{"Maya Garcia"}),
		                                      issue("atlas-14", 14, "Draw boards natively", "atlas-main-to_do", "normal"),
		                                      issue("atlas-9", 9, "Search by label", "atlas-main-doing", "critical", list{"search", "ui"}, list{"Ade Bello", "Maya Garcia"}),
		                                      issue("atlas-3", 3, "Sign in by key", "atlas-main-done", "low")}}}));
		data.set("projects::member_roles", "", value(fields{{"rows", list{value(fields{{"project", "atlas"}, {"role", "atlas-contributor"}})}}}));
	}

	site with_pipeline() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Releases", "/:project/boards/:board", "", [](page& p) {
			                     auto board_page = p.view("projects::board_page", p.param("project") + "-" + p.param("board"));
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

	site with_canvas() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
			                     auto board_page = p.view("projects::board_page", p.param("project") + "-" + p.param("board"));
			                     return items(canvas_board(p, {.view = board_page,
			                                                   .list = "issues",
			                                                   .columns = {{"title", "Title"}, {"number", "#"}},
			                                                   .link = "/:project/:issue",
			                                                   .keyed = {"project"},
			                                                   .x = "x", .y = "y", .w = "w", .h = "h",
			                                                   .place = "projects::issue::place",
			                                                   .grid = 160,
			                                                   .origin_x = 20,
			                                                   .origin_y = 20,
			                                                   .by = "stage",
			                                                   .over = board_page,
			                                                   .over_list = "stages",
			                                                   .tint = tint_spec{"priority", {"critical", "high", "normal", "low", "trivial"}}}));
		                     }});
		s.screens.push_back({"Issue", "/:project/:issue", "", [](page& p) { return items(text({words(p.param("issue"))})); }});
		return s;
	}

	site with_board() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
			                     auto board_page = p.view("projects::board_page", p.param("project") + "-" + p.param("board"));
			                     auto roles = p.view("projects::member_roles");
			                     return items(board(p, {.view = board_page,
			                                            .list = "issues",
			                                            .by = "stage",
			                                            .over = board_page,
			                                            .over_list = "stages",
			                                            .columns = {{"title", "Title"}, {"number", "#"}, {"labels", "Labels"}, {"assignees.name", "Assigned to"}},
			                                            .link = "/:project/:issue",
			                                            .keyed = {"project"},
			                                            .people = {{"assignees.name", "assignees.picture"}},
			                                            .labels = {"labels"},
			                                            .move = board_move{.command = "projects::issue::move", .steps = board_page, .list = "steps", .held = "roles", .roles = roles, .within = p.param("project")},
			                                            .search = {"title", "labels"},
			                                            .tint = tint_spec{"priority", {"critical", "high", "normal", "low", "trivial"}}}));
		                     }});
		s.screens.push_back({"Issue", "/:project/:issue", "", [](page& p) { return items(text({words(p.param("issue"))})); }});
		return s;
	}

	// A key as a window gives it: to what has focus, then to what it does with keys it doesn't take.
	bool pressed(one::desktop::element* e, const one::desktop::key_event& k) { return e && (e->handle_key(k) || (e->on_key && e->on_key(k))); }

	one::desktop::element* list_holding(one::desktop::element& e, const std::string& row) {
		if (e.prefers_focus()) {
			for (auto& child : e.children) {
				if (child->key == row) return &e;
			}
		}
		for (auto& child : e.children) {
			if (auto* found = list_holding(*child, row)) return found;
		}
		return nullptr;
	}

}  // namespace

TEST("a board's card is dragged to a column a step leads to, and dropping it anywhere else moves nothing") {
	memory_source data;
	fill_board(data);
	data.on("projects::issue::move", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	auto root = built(nav);
	auto* to_do = list_holding(*root, "atlas-12");
	auto* doing = list_holding(*root, "atlas-9");
	CHECK(to_do != nullptr && doing != nullptr);
	if (!to_do || !doing) return;
	// What's dragged, inside its row: the one that shows a hand.
	std::function<one::desktop::element*(one::desktop::element&)> grabbed = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.cursor() == one::desktop::cursor::grab) return &e;
		for (auto& child : e.children) {
			if (auto* found = grabbed(*child)) return found;
		}
		return nullptr;
	};
	one::desktop::element* card = nullptr;
	for (auto& row : to_do->children) {
		if (row->key == "atlas-12") card = grabbed(*row);
	}
	CHECK(card != nullptr);
	if (!card) return;
	// Pressed and hardly moved, it's still a click; moved further, it's carried.
	CHECK(card->press({10, 10}, 1));
	card->drag({12, 11});
	CHECK(!one::desktop::carrying());
	card->drag({60, 40});
	CHECK(one::desktop::carrying() && one::desktop::carrying()->payload == "atlas-12");
	CHECK(card->cursor() == one::desktop::cursor::grabbing);
	// Let go over its own column, which no step leads to: nothing moves.
	auto runs = data.runs().size();
	card->release({60, 40}, to_do);
	CHECK(!one::desktop::carrying());
	CHECK(data.runs().size() == runs);
	// Carried to Doing, a step from To do for a contributor: it moves there.
	card->click({60, 40});
	card->press({10, 10}, 1);
	card->drag({300, 40});
	card->release({300, 40}, doing);
	CHECK(data.runs().size() == runs + 1);
	CHECK(data.runs().back().fields["stage"].as_string() == "atlas-main-doing");
	// What was dragged isn't also a click on the card.
	CHECK(card->click({300, 40}));
}

TEST("a board's card moves along a step the person's roles may take, at once, and back when it's turned down") {
	memory_source data;
	fill_board(data);
	bool refuse = false;
	data.on("projects::issue::move", [&](const value& f, memory_source&) -> answer {
		if (refuse) return std::unexpected(std::string("that stage is full"));
		return f["id"].as_string();
	});
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	auto root = built(nav);
	auto* to_do = list_holding(*root, "atlas-12");
	CHECK(to_do != nullptr);
	if (!to_do) return;
	pressed(to_do, typed("m"));
	CHECK(nav.current().kept<std::string>("board:issues:moving").peek() == "atlas-12");
	root = built(nav);
	auto* offered = list_holding(*root, "atlas-main-doing");
	CHECK(offered != nullptr);  // to do leads to doing, for a contributor
	CHECK(!list_holding(*root, "atlas-main-done"));  // not done, which only maintainers reach
	if (!offered) return;
	offered->handle_key(enter);
	CHECK(data.runs().back().fields["stage"].as_string() == "atlas-main-doing");
	auto moved = nav.current().kept<std::map<std::string, std::string>>("board:issues:moved");
	CHECK(moved.peek().at("atlas-12") == "atlas-main-doing");
	root = built(nav);
	CHECK(list_holding(*root, "atlas-12") == list_holding(*root, "atlas-9"));  // shown in doing at once
	// From doing, a contributor has nowhere to go; and a move turned down goes back.
	refuse = true;
	auto* again = list_holding(*root, "atlas-14");
	pressed(again, typed("m"));
	root = built(nav);
	list_holding(*root, "atlas-main-doing")->handle_key(enter);
	CHECK(nav.current().error("projects::issue::move") == "that stage is full");
	CHECK(!moved.peek().contains("atlas-14"));
	CHECK(nav.current().problem().peek().empty());  // said on the board
}

TEST("a pipeline: a lane for each kind, each card's stages along its row, done, now, failed and to come") {
	memory_source data;
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
	                                      card("a-3", "tracker", "failed", "websites", {{"data", value(fields{{"at", "Deploying"}, {"note", "the deploy failed"}})}}),
	                                      card("a-4", "tracker desktop app", "to_do", "local apps", {{"data", value(fields{{"status", "waiting"}, {"note", "waiting on tracker"}})}})}}}));
	auto s = with_pipeline();
	navigator nav(s, data);
	nav.open("/atlas/boards/releases");
	matches_golden("pipeline", {1200, 560}, [&] { return drawn(nav); });
}

TEST("a whiteboard: its stages as columns, a card let go in the next moving there, and a column's edge dragged wider") {
	memory_source data;
	fill_board(data);
	data.on("projects::issue::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("projects::issue::move", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("projects::stage::update", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "tracker", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
		                     auto board_page = p.view("projects::board_page", p.param("project") + "-" + p.param("board"));
		                     return items(canvas_board(p, {.view = board_page,
		                                                   .list = "issues",
		                                                   .columns = {{"title", "Title"}, {"number", "#"}},
		                                                   .x = "x", .y = "y", .w = "w", .h = "h",
		                                                   .place = "projects::issue::place",
		                                                   .grid = 160,
		                                                   .origin_x = 20,
		                                                   .origin_y = 40,
		                                                   .by = "stage",
		                                                   .over = board_page,
		                                                   .over_list = "stages",
		                                                   .regions = "width",
		                                                   .move = "projects::issue::move",
		                                                   .resize = "projects::stage::update"}));
	                     }});
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	matches_golden("whiteboard", {1000, 600}, [&] { return drawn(nav); });
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> surface = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.focusable() && e.cursor() == one::desktop::cursor::grab && !e.children.empty()) return &e;
		for (auto& child : e.children) {
			if (auto* found = surface(*child)) return found;
		}
		return nullptr;
	};
	auto* board = surface(*root);
	CHECK(board != nullptr);
	if (!board) return;
	// #12, in To do, dragged a column over, into Doing.
	auto at = board->children[0]->frame;
	one::desktop::point start{at.x + 20, at.y + 20};
	CHECK(board->press(start, 1));
	board->drag({start.x + 320, start.y});
	board->release({start.x + 320, start.y}, board);
	bool moved = false;
	for (const auto& run : data.runs()) moved = moved || (run.command == "projects::issue::move" && run.fields["stage"].as_string() == "atlas-main-doing");
	CHECK(moved);
	// To do's edge, at 20 + 320 across the surface, dragged 80 wider.
	auto inner = board->frame;
	one::desktop::point edge{inner.x + 20 + 320, inner.y + 200};
	CHECK(board->press(edge, 1));
	CHECK(board->cursor() == one::desktop::cursor::resize_across);
	board->drag({edge.x + 80, edge.y});
	board->release({edge.x + 80, edge.y}, board);
	CHECK(data.runs().back().command == "projects::stage::update");
	CHECK(data.runs().back().fields["width"].as_double() == 400);
}

TEST("a canvas: its cards laid out by stage, one dragged is placed where it's let go, snapped to the grid") {
	memory_source data;
	fill_board(data);
	data.on("projects::issue::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	auto s = with_canvas();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	matches_golden("canvas", {1000, 600}, [&] { return drawn(nav); });
	auto root = built(nav);
	// The surface: what takes the keyboard and shows a hand.
	std::function<one::desktop::element*(one::desktop::element&)> surface = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.focusable() && e.cursor() == one::desktop::cursor::grab && !e.children.empty()) return &e;
		for (auto& child : e.children) {
			if (auto* found = surface(*child)) return found;
		}
		return nullptr;
	};
	auto* placed = surface(*root);
	CHECK(placed != nullptr);
	if (!placed || placed->children.empty()) return;
	// The first card, #12, in To do: pressed inside it and dragged 150 across and 40 down.
	auto at = placed->children[0]->frame;
	one::desktop::point start{at.x + 20, at.y + 20};
	CHECK(placed->press(start, 1));
	placed->drag({start.x + 150, start.y + 40});
	placed->release({start.x + 150, start.y + 40}, placed);
	CHECK(!data.runs().empty());
	if (data.runs().empty()) return;
	CHECK(data.runs().back().command == "projects::issue::place");
	CHECK(data.runs().back().fields["x"].as_double() == 180);  // 20 + 150, the grid's nearest: 180
	CHECK(data.runs().back().fields["y"].as_double() == 20);   // 20 + 40, back to 20
	CHECK(data.runs().back().fields["w"].as_double() == 150);
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
		value stats(fields{{"cpu", numbers({5, 20, 45, 80, 60, 72, 90, 64, 70, 55, 58, 62, 75, 81, 77, 69, 74, 79, 85, 88})},
		                   {"memory", numbers({1e8, 2e8, 4e8, 4.5e8, 5e8, 5.2e8, 6e8, 6.4e8, 6.1e8, 6.3e8, 6.6e8, 6.8e8, 7e8, 6.9e8, 7.1e8, 7.3e8, 7.2e8, 7.4e8, 7.5e8, 7.6e8})}});
		data.set("projects::agents", "atlas",
		         value(fields{{"agents", list{value(fields{{"id", "atlas-npm"}, {"title", "npm: Publishing"}, {"status", "working"}, {"since", at(std::chrono::seconds(95))},
		                                                  {"doing", "#12 @uione/react 0.16.0: Uploading"}, {"step", 3}, {"steps", 5}, {"seen_at", at(std::chrono::seconds(2))}, {"stats", stats}}),
		                                    value(fields{{"id", "atlas-idle"}, {"title", "installs: To do"}, {"status", "idle"}, {"seen_at", at(std::chrono::seconds(1))}, {"doing", "waiting for issues"}}),
		                                    value(fields{{"id", "atlas-sites"}, {"title", "sites: Deploying"}, {"status", "idle"}, {"seen_at", at(std::chrono::seconds(600))}})}}}));
		data.set("projects::agent_page", "atlas-npm", value(fields{{"title", "npm: Publishing"}, {"stats", stats}}));
	}

	site with_agents() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
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

// A monitor: a tile for each agent, its dot, state and how long, what it's doing and
// how far, and its charts; one unheard from for minutes is offline. Its page draws
// the same numbers large, side by side.
TEST("a monitor of agents, working, idle and offline, and an agent's charts") {
	memory_source data;
	fill_agents(data);
	auto s = with_agents();
	navigator nav(s, data);
	nav.open("/atlas/agents");
	matches_golden("monitor", {1100, 420}, [&] { return drawn(nav); });
	nav.open("/atlas/agents/npm");
	matches_golden("charts", {1100, 420}, [&] { return drawn(nav); });
}

TEST("a board: its columns, cards tinted by priority with their labels and people, and a move offered") {
	memory_source data;
	fill_board(data);
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	nav.current().kept<std::string>("board:issues:moving").set("atlas-12");
	matches_golden("board", {1000, 600}, [&] { return drawn(nav); });
}

TEST("a calendar: a week of days, each row on its day, a day alone first, the one picked marked, and one moved keeping its time of day") {
	using namespace std::chrono;
	memory_source data;
	auto at = [](int d, int h = -1, int m = 0) {
		value t;
		t.type = value::kind::time;
		auto day = sys_days(year(2026) / September / d);
		t.time.seconds = (h < 0 ? sys_seconds(day) : sys_seconds(day + hours(h) + minutes(m))).time_since_epoch().count();
		return t;
	};
	auto task = [&](std::string id, std::string title, value due, std::string priority) {
		return value(fields{{"id", std::move(id)}, {"title", std::move(title)}, {"due", std::move(due)}, {"priority", std::move(priority)}});
	};
	data.set("orbit::planner", "",
	         value(fields{{"tasks", list{task("t1", "Call the bank", at(15, 9, 30), "normal"), task("t2", "Water plants", at(15), "low"), task("t3", "Dentist", at(17, 14), "high"),
	                                     task("t4", "Pay rent", at(18), "high")}}}));
	site s{.name = "orbit", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Calendar", "/", "", [](page& p) {
		                     return calendar(p, {.view = p.view("orbit::planner", ""), .list = "tasks", .by = "due", .columns = {{"title", "Title"}}, .move = "orbit::task::plan",
		                                         .tint = tint_spec{"priority", {"high", "normal", "low"}}, .pick = "/ task", .key = "/ tasks"});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	// The week of Monday, September 14, 2026, read in UTC.
	nav.current().kept<std::string>("calendar / tasks").set("2026-09-16");
	nav.current().pick("/ task", "t3");
	matches_golden("calendar-week", {1000, 420}, [&] { return drawn(nav); });
	auto& p = nav.current();
	auto model = calendar_of(p, {.view = p.view("orbit::planner", ""), .list = "tasks", .by = "due", .columns = {{"title", "Title"}}, .key = "/ tasks"});
	CHECK(model.days.size() == 7);
	if (model.days.size() != 7) return;
	CHECK(model.days[0].date == "2026-09-14");
	CHECK(model.days[0].weekday == "Mon");
	CHECK(model.days[1].cards.size() == 2);
	if (model.days[1].cards.size() != 2) return;
	CHECK(model.days[1].cards[0].title == "Water plants");  // a day alone first
	CHECK(model.days[1].cards[1].time == "09:30");
	// Moved to Friday, at the same time of day; a day alone stays one.
	calendar_move(p, {.view = p.view("orbit::planner", ""), .list = "tasks", .by = "due", .move = "orbit::task::plan", .key = "/ tasks"}, "t1", "2026-09-18");
	calendar_move(p, {.view = p.view("orbit::planner", ""), .list = "tasks", .by = "due", .move = "orbit::task::plan", .key = "/ tasks"}, "t2", "2026-09-16");
	CHECK(data.runs().size() == 2);
	if (data.runs().size() != 2) return;
	CHECK(data.runs()[0].command == "orbit::task::plan");
	CHECK(data.runs()[0].fields["due"].time.seconds == at(18, 9, 30).time.seconds);
	CHECK(data.runs()[1].fields["due"].time.seconds == at(16).time.seconds);
	// A month's grid: from the Monday before the first, to the Sunday after the last.
	calendar_go(p, {.view = p.view("orbit::planner", ""), .list = "tasks", .by = "due", .month = true, .key = "/ tasks"}, 0);
	p.kept<std::string>("calendar / tasks").set("2026-09-16");
	auto month = calendar_of(p, {.view = p.view("orbit::planner", ""), .list = "tasks", .by = "due", .month = true, .columns = {{"title", "Title"}}, .key = "/ tasks"});
	CHECK(month.title == "September 2026");
	CHECK(month.days.size() == 35);
	CHECK(month.days.front().date == "2026-08-31");
	CHECK(!month.days.front().in_month);
	CHECK(month.days.back().date == "2026-10-04");
}

// Started by publish, whose agents stop when it quits: closing the window asks first
// while one is working on a card, naming it, Esc puts the question away, and closing
// again closes it; with nothing worked on, or agents that don't stop with it, it closes.
TEST("closing the window asks first while agents that stop with it are working on cards") {
	memory_source data;
	fill_board(data);
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	(void)drawn(nav);
	CHECK(may_close(nav, true));  // nothing's held
	value until;
	until.type = value::kind::time;
	until.time.seconds = std::chrono::duration_cast<std::chrono::seconds>((std::chrono::system_clock::now() + std::chrono::minutes(10)).time_since_epoch()).count();
	fill_board(data, {{"held_by", "npm-agent"}, {"held_by.name", "npm"}, {"held_until", until}});
	CHECK(may_close(nav, false));  // its agents don't stop with it
	CHECK(!may_close(nav, true));
	matches_golden("board-closing", {1000, 600}, [&] { return drawn(nav); });
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::escape, .text = ""}));
	CHECK(!may_close(nav, true));
	CHECK(may_close(nav, true));  // asked again, it closes
}

TEST("a card's link opens its page filtered, said in words, and Show all opens it whole in its place") {
	memory_source data;
	fill_board(data);
	data.sign_in(person{"maya", "Maya Garcia"});
	data.set("projects::board_page", "atlas-main",
	         value(fields{{"stages", list{value(fields{{"id", "s1"}, {"title", "To do"}})}},
	                      {"issues", list{value(fields{{"id", "atlas-12"}, {"title", "Mine"}, {"stage", "s1"}, {"assignees", list{"maya"}}}),
	                                      value(fields{{"id", "atlas-14"}, {"title", "Ade's"}, {"stage", "s1"}, {"assignees", list{"ade"}}})}}}));
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	nav.open("/atlas/boards/main?is=Assigned+to+me&assignees=me");
	auto& p = nav.current();
	CHECK(p.param("board") == "main");
	CHECK(p.filtered() == "Assigned to me");
	CHECK(p.keeps(value(fields{{"assignees", list{"maya"}}})));
	CHECK(!p.keeps(value(fields{{"assignees", list{"ade"}}})));
	auto root = built(nav);
	CHECK(list_holding(*root, "atlas-12") != nullptr);
	CHECK(list_holding(*root, "atlas-14") == nullptr);
	nav.current().clear_filter();
	CHECK(nav.depth() == 2);  // in its place, not after it
	CHECK(nav.current().filtered().empty());
	root = built(nav);
	CHECK(list_holding(*root, "atlas-14") != nullptr);
}

TEST("cards: each with what it holds counted, and links opening it filtered") {
	memory_source data;
	data.set("projects::project_page", "atlas",
	         value(fields{{"boards", list{value(fields{{"id", "atlas-main"}, {"title", "Main"}}), value(fields{{"id", "atlas-ops"}, {"title", "Operations"}})}},
	                      {"issues", list{value(fields{{"board", "atlas-main"}, {"stage", "s1"}, {"stage.title", "To do"}, {"stage.position", 1}}),
	                                      value(fields{{"board", "atlas-main"}, {"stage", "s2"}, {"stage.title", "Doing"}, {"stage.position", 2}}),
	                                      value(fields{{"board", "atlas-main"}, {"stage", "s1"}, {"stage.title", "To do"}, {"stage.position", 1}})}}}));
	site s{.name = "tracker", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Atlas", "/:project", "", [](page& p) {
		                     auto project = p.view("projects::project_page", p.param("project"));
		                     return items(section({"Boards"}, cards(p, {.view = project,
		                                                                .list = "boards",
		                                                                .columns = {{"title", "Title"}},
		                                                                .link = "/:project/boards/:board",
		                                                                .keyed = {"project"},
		                                                                .tally = tally_spec{project, "issues", "board", "stage", "stage.title", "stage.position", "issues"},
		                                                                .filters = {{"Assigned to me", {{"assignees", "me"}}, "blue"}, {"High priority", {{"priority", "high"}}, "red"}}})));
	                     }});
	navigator nav(s, data);
	nav.open("/atlas");
	matches_golden("cards", {900, 520}, [&] { return drawn(nav); });
}

namespace {

	site with_issue() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"", "/:project/:issue", "", [](page& p) {
			                     auto issue = p.view("projects::issue_page", p.param("issue"));
			                     auto roles = p.view("projects::member_roles");
			                     p.title({words("#"), live(issue, "number"), words(" "), live(issue, "title")});
			                     confirm(p, "projects::issue::close", "Close this issue?");
			                     heading(p, actions(command(p, {.command = "projects::issue::follow", .label = "Follow", .id = p.param("issue"), .when = !issue->data["followed"].as_bool()}),
			                                        command(p, {.command = "projects::issue::close", .label = "Close", .id = p.param("issue")}),
			                                        form(p, {.command = "projects::issue::update", .fields = {{.name = "title"}}, .button = true, .id = p.param("issue"), .submit = "Save", .opener = "Edit issue", .icon = "edit"}),
			                                        link(p, "Workflow", "/:project/workflow", "workflow")));
			                     std::vector<region> regions;
			                     regions.push_back({"main", items(text({live(issue, "body")}))});
			                     regions.push_back({"side", items(details(p, issue, {{"stage.title", "Stage"}, {"priority", "Priority"}}),
			                                                      steps(p, {.command = "projects::issue::move",
			                                                                .id = p.param("issue"),
			                                                                .field = "stage",
			                                                                .current = issue,
			                                                                .steps = issue,
			                                                                .list = "steps",
			                                                                .to = "to.title",
			                                                                .here = "stage.title",
			                                                                .held = "roles",
			                                                                .roles = roles,
			                                                                .within = p.param("project")}))});
			                     return items(layout("two_columns", std::move(regions)));
		                     }});
		s.screens.push_back({"Settings", "/:project/settings", "", [](page& p) {
			                     return items(menu(p, {{"/:project/settings", "General"}, {"/:project/settings/people", "People"}, {"/:project/settings/roles", "Roles"}},
			                                       items(text({words("Who may do what in this project.")}))));
		                     }});
		return s;
	}

	void fill_issue(memory_source& data, const std::string& role) {
		data.set("projects::issue_page", "atlas-12",
		         value(fields{{"number", 12},
		                      {"title", "Copy an issue whole"},
		                      {"body", "Copy its title, body and labels."},
		                      {"stage", "s1"},
		                      {"stage.title", "To do"},
		                      {"priority", "high"},
		                      {"steps", list{value(fields{{"from", "s1"}, {"to", "s2"}, {"to.title", "Doing"}, {"roles", list{"atlas-maintainer", "atlas-contributor"}}}),
		                                     value(fields{{"from", "s1"}, {"to", "s3"}, {"to.title", "Done"}, {"roles", list{"atlas-maintainer"}}})}}}));
		data.set("projects::member_roles", "", value(fields{{"rows", list{value(fields{{"project", "atlas"}, {"role", role}})}}}));
	}

}  // namespace

TEST("a command is asked about first when the screen says so, runs on yes and not on cancel, and steps are those the reader's roles may take") {
	memory_source data;
	fill_issue(data, "atlas-contributor");
	auto s = with_issue();
	navigator nav(s, data);
	nav.open("/atlas/atlas-12");
	auto& p = nav.current();
	auto root = built(nav);
	p.run("projects::issue::close", value(fields{{"id", "atlas-12"}}));
	CHECK(data.runs().empty());
	CHECK(p.asked().peek().has_value());
	p.answer_asked(false);
	CHECK(data.runs().empty());
	p.run("projects::issue::close", value(fields{{"id", "atlas-12"}}));
	p.answer_asked(true);
	CHECK(data.runs().size() == 1);
	CHECK(data.runs().back().fields["id"].as_string() == "atlas-12");
	// A contributor may move it to doing, not done; a reader holding no role there sees no steps.
	std::vector<std::string> buttons;
	std::function<void(one::desktop::element&)> look = [&](one::desktop::element& e) {
		if (e.focusable() && e.children.empty() && e.cursor() == one::desktop::cursor::pointer && !e.prefers_focus()) buttons.push_back("button");
		for (auto& c : e.children) look(*c);
	};
	look(*root);
	CHECK(buttons.size() >= 4);  // name link, Follow, Close, Move to Doing
	fill_issue(data, "atlas-reporter");
	auto none = built(nav);
	std::size_t before = buttons.size();
	buttons.clear();
	look(*none);
	CHECK(buttons.size() == before - 1);  // no move for a reporter's role
}

TEST("an issue in two columns, its buttons beside its title, a form's and a link's among them, the steps it can take, and a question asked") {
	memory_source data;
	fill_issue(data, "atlas-maintainer");
	auto s = with_issue();
	navigator nav(s, data);
	nav.open("/atlas/atlas-12");
	matches_golden("two-columns", {1000, 520}, [&] { return drawn(nav); });
	nav.open("/atlas/settings");
	nav.current().ask_before("projects::project::delete", "Delete this project and everything in it?");
	nav.current().run("projects::project::delete", value::map());
	matches_golden("menu", {1000, 420}, [&] { return drawn(nav); });
}

namespace {

	site with_table() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Issues", "/:project/issues", "", [](page& p) {
			                     auto project = p.view("projects::project_page", p.param("project"));
			                     return items(table(p, {.view = project,
			                                            .list = "issues",
			                                            .columns = {{"number", "#"}, {"title", "Title"}, {"labels", "Labels"}, {"assignees.name", "Assigned to"}, {"stage", "Stage"}},
			                                            .actions = {{.command = "projects::issue::rename", .label = "Rename", .form = {{"title"}}, .submit = "Rename"}},
			                                            .people = {{"assignees.name", "assignees.picture"}},
			                                            .choices = {{"stage", {{"s1", "To do"}, {"s2", "Done"}}}},
			                                            .labels = {"labels"},
			                                            .by = "stage",
			                                            .search = {"title", "labels"},
			                                            .sort = "-number",
			                                            .page = 2,
			                                            .tools = [&p] { return command(p, {.command = "projects::issue::create", .label = "New issue", .primary = true}); },
			                                            .tint = tint_spec{"priority", {"critical", "high", "normal", "low", "trivial"}}}));
		                     }});
		return s;
	}

	void fill_table(memory_source& data) {
		auto issue = [](int n, std::string title, std::string stage, std::string priority, list labels, list people) {
			return value(fields{{"id", "atlas-" + std::to_string(n)}, {"number", n}, {"title", title}, {"stage", stage}, {"priority", priority}, {"labels", labels}, {"assignees.name", people}});
		};
		data.set("projects::project_page", "atlas",
		         value(fields{{"issues", list{issue(12, "Copy an issue whole", "s1", "high", list{"ui"}, list{"Maya Garcia"}),
		                                      issue(14, "Draw tables natively", "s1", "normal", list{}, list{}),
		                                      issue(15, "Search by label", "s1", "critical", list{"search", "ui"}, list{"Ade Bello", "Maya Garcia"}),
		                                      issue(3, "Sign in by key", "s2", "low", list{}, list{"Ade Bello"})}}}));
	}

}  // namespace

TEST("a table reordered descending shows its rows highest first, and a row moved down goes between the two now under it") {
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"layers", list{value(fields{{"id", "l0"}, {"words", "Lowest"}, {"depth", 0}}), value(fields{{"id", "l1"}, {"words", "Middle"}, {"depth", 1}}),
	                                      value(fields{{"id", "l2"}, {"words", "Top"}, {"depth", 2}})}}}));
	data.on("studio::layer::update", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return items(table(p, {.view = p.view("studio::poster_page", "p1"),
		                                            .list = "layers",
		                                            .columns = {{"words", "Layers"}},
		                                            .reorder = reorder_spec{"studio::layer::update", "depth", true, true}}));
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	auto* rows = list_holding(*root, "l2");
	CHECK(rows);
	if (!rows) return;
	std::vector<std::string> shown;
	for (auto& c : rows->children) shown.push_back(c->key);
	CHECK((shown == std::vector<std::string>{"l2", "l1", "l0"}));
	// The top row, first, moved down: below Middle, above Lowest.
	CHECK(pressed(rows, one::desktop::key_event{.symbol = one::desktop::keys::down, .text = "", .alt = true}));
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	CHECK(data.runs().back().fields["id"].as_string() == "l2");
	CHECK(data.runs().back().fields["depth"].as_double() == 0.5);
}

TEST("a table: tabs by a choice, searched, sorted and in pages, with a row's action asking with its form") {
	memory_source data;
	fill_table(data);
	auto s = with_table();
	navigator nav(s, data);
	nav.open("/atlas/issues");
	auto& p = nav.current();
	auto root = built(nav);
	auto* rows = list_holding(*root, "atlas-15");
	CHECK(rows != nullptr);  // the largest number first, on the first page of two
	CHECK(list_holding(*root, "atlas-12") == nullptr);  // on the second page
	CHECK(list_holding(*root, "atlas-3") == nullptr);   // in the Done tab
	p.kept<int>("table:issues:page").set(1);
	root = built(nav);
	CHECK(list_holding(*root, "atlas-12") != nullptr);
	p.kept<int>("table:issues:page").set(0);
	p.kept<std::string>("table:issues:search").set("search");
	root = built(nav);
	CHECK(list_holding(*root, "atlas-15") != nullptr);
	CHECK(list_holding(*root, "atlas-14") == nullptr);
	p.kept<std::string>("table:issues:search").set("");
	p.kept<std::string>("table:issues:acting").set("atlas-15|projects::issue::rename");
	root = built(nav);
	CHECK(p.kept<std::string>("form:projects::issue::rename:atlas-15:title").peek() == "Search by label");  // started from the row
	p.kept<std::string>("form:projects::issue::rename:atlas-15:title").set("Search by labels");
	root = built(nav);
	// The form's own button, Rename, is the one after its field, the only one typed into.
	auto parts = focusable_in(*root);
	std::size_t caret = 0, anchor = 0;
	auto typed_in = std::ranges::find_if(parts, [&](one::desktop::element* e) { return e->entry() && e->entry()->surrounding(caret, anchor) == "Search by labels"; });
	CHECK(typed_in != parts.end());
	if (typed_in == parts.end()) return;
	(*std::next(typed_in))->handle_key(enter);
	CHECK(!data.runs().empty());
	if (data.runs().empty()) return;
	CHECK(data.runs().back().command == "projects::issue::rename");
	CHECK(data.runs().back().fields["id"].as_string() == "atlas-15");
	CHECK(data.runs().back().fields["title"].as_string() == "Search by labels");
	p.kept<std::string>("table:issues:acting").set("");
	matches_golden("table", {1000, 420}, [&] { return drawn(nav); });
}

TEST("what a view holds is copied as Markdown: values, written fields under their names, and lists") {
	value data(fields{{"priority", "high"},
	                  {"stage.title", "To do"},
	                  {"body", "  Copy it whole.  "},
	                  {"comments", list{value(fields{{"author.name", "Maya Garcia"}, {"body", "Seen."}, {"created_at", "Monday"}})}},
	                  {"links", list{value(fields{{"relation", "blocks"}, {"to.number", 9}})}}});
	std::string md = markdown_of("#12 Copy an issue whole", data, {{"priority", "Priority"}, {"stage.title", "Stage"}, {"body", "Body", true}},
	                             {{"comments", "Comments", "thread"}, {"links", "Links", "rows", {"relation", "to.number"}}}, {{"priority", {{"high", "High"}}}});
	CHECK(md == "# #12 Copy an issue whole\n\n**Priority:** High  \n**Stage:** To do  \n\n## Body\n\nCopy it whole.\n\n## Comments\n\n**Maya Garcia** · Monday\n\nSeen.\n\n## Links\n\n- blocks · 9\n");
}

namespace {

	site with_workflow() {
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Workflow", "/:project/boards/:board/workflow", "", [](page& p) {
			                     auto board = p.view("projects::board_page", "atlas-main");
			                     between_spec moves{.view = board,
			                                        .list = "steps",
			                                        .from = "from",
			                                        .to = "to",
			                                        .cell = "roles.title",
			                                        .over = board,
			                                        .over_list = "stages",
			                                        .create = between_command{.command = "projects::step::create", .fields = {{.name = "title", .label = "Says"}}, .submit = "Allow"},
			                                        .update = between_command{.command = "projects::step::update", .fields = {{.name = "title", .label = "Says"}}, .submit = "Save"},
			                                        .remove = between_command{.command = "projects::step::delete", .submit = "Remove"}};
			                     return items(find(p, "Search this board", {{.view = board, .list = "issues", .label = "Issues", .link = "/:project/:issue", .keyed = {"project"}, .by = {"number", "title"}}}),
			                                  diagram(p, moves), grid(p, moves));
		                     }});
		return s;
	}

	void fill_workflow(memory_source& data) {
		data.set("projects::board_page", "atlas-main",
		         value(fields{{"stages", list{value(fields{{"id", "s1"}, {"title", "To do"}}), value(fields{{"id", "s2"}, {"title", "Doing"}}), value(fields{{"id", "s3"}, {"title", "Done"}})}},
		                      {"steps", list{value(fields{{"id", "m1"}, {"from", "s1"}, {"to", "s2"}, {"title", "Start"}, {"roles.title", list{"Maintainer", "Contributor"}}}),
		                                     value(fields{{"id", "m2"}, {"from", "s2"}, {"to", "s3"}, {"title", "Finish"}, {"roles.title", list{"Maintainer"}}})}},
		                      {"issues", list{value(fields{{"id", "atlas-12"}, {"number", 12}, {"title", "Copy an issue whole"}}),
		                                      value(fields{{"id", "atlas-9"}, {"number", 9}, {"title", "Search by label"}})}}}));
	}

}  // namespace

TEST("a grid adds what goes between two from its empty cell, and changes what's there from its full one; find finds by number or words") {
	memory_source data;
	fill_workflow(data);
	auto s = with_workflow();
	navigator nav(s, data);
	nav.open("/atlas/boards/main/workflow");
	auto& p = nav.current();
	p.kept<std::string>("grid:steps").set("s3|s1");  // Done to To do, empty
	auto root = built(nav);
	p.kept<std::string>("form:projects::step::create:s3|s1:title").set("Reopen");
	root = built(nav);
	auto parts = focusable_in(*root);
	std::size_t caret = 0, anchor = 0;
	auto field = std::ranges::find_if(parts, [&](one::desktop::element* e) { return e->entry() && e->entry()->surrounding(caret, anchor) == "Reopen"; });
	CHECK(field != parts.end());
	if (field == parts.end()) return;
	(*std::next(field))->handle_key(enter);
	CHECK(data.runs().back().command == "projects::step::create");
	CHECK(data.runs().back().fields["from"].as_string() == "s3");
	CHECK(data.runs().back().fields["to"].as_string() == "s1");
	CHECK(data.runs().back().fields["title"].as_string() == "Reopen");
	CHECK(data.runs().back().fields.find("id") == nullptr);  // a new one has none yet
	CHECK(p.kept<std::string>("grid:steps").peek().empty());  // closed once it's done
	p.kept<std::string>("grid:steps").set("s1|s2");  // To do to Doing, there
	built(nav);
	CHECK(p.kept<std::string>("form:projects::step::update:m1:title").peek() == "Start");
	p.kept<std::string>("grid:steps").set("");
	p.kept<std::string>("find").set("#9");
	matches_golden("workflow", {1000, 640}, [&] { return drawn(nav); });
}

TEST("a front page: a hero, links within the app and out of it, a badge beside the name, and the footer; and a set of written pages") {
	memory_source data;
	data.sign_in(person{"maya", "Maya Garcia"});
	data.set("projects::news", "maya", value(fields{{"unread", 3}}));
	site s{.name = "tracker", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.badges = {{"projects::news", "unread", "new"}};
	s.footer = [](page& p) { return items(text({words("tracker is open source.")}), link(p, "Source", "https://github.com/someone/tracker")); };
	s.screens.push_back({"tracker", "/", "", [](page& p) {
		                     return items(hero("Boards where people and agents move the same issues", text({words("Each project sets its own stages and who may move what.")}),
		                                       actions(link(p, "Browse public projects", "/projects"), link(p, "Why tracker", "/docs/why"))));
	                     }});
	s.screens.push_back({"Docs", "/docs/:page?", "", [](page& p) {
		                     return items(pages(p, "/docs", {{"start", "Getting started", "Make a project, then a board.\n\n- Name it\n- Add stages"},
		                                                     {"why", "Why tracker", "Every change is **kept**, so anyone can read how work got done."}}));
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("front-page", {1000, 520}, [&] { return drawn(nav); });
	nav.open("/docs/why");
	matches_golden("pages", {1000, 420}, [&] { return drawn(nav); });
}

TEST("a board's cards are put in order with Alt and the arrows, and kept to a milestone picked above it") {
	memory_source data;
	auto card = [](std::string id, int n, double position, std::string milestone) {
		return value(fields{{"id", id}, {"number", n}, {"title", "Issue " + std::to_string(n)}, {"stage", "s1"}, {"position", position}, {"milestone", milestone}});
	};
	data.set("projects::board_page", "atlas-main",
	         value(fields{{"stages", list{value(fields{{"id", "s1"}, {"title", "To do"}})}},
	                      {"milestones", list{value(fields{{"id", "atlas-beta"}, {"title", "Beta"}, {"due", "Oct 30"}}), value(fields{{"id", "atlas-one"}, {"title", "1.0"}})}},
	                      {"issues", list{card("atlas-1", 1, 1, "atlas-beta"), card("atlas-2", 2, 2, "atlas-one"), card("atlas-3", 3, 3, "atlas-beta")}}}));
	bool refuse = false;
	data.on("projects::issue::update", [&](const value& f, memory_source&) -> answer {
		if (refuse) return std::unexpected(std::string("no"));
		return f["id"].as_string();
	});
	site s{.name = "tracker", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Main", "/:project/boards/:board", "", [](page& p) {
		                     auto board_page = p.view("projects::board_page", "atlas-main");
		                     return items(board(p, {.view = board_page,
		                                            .list = "issues",
		                                            .by = "stage",
		                                            .over = board_page,
		                                            .over_list = "stages",
		                                            .columns = {{"title", "Title"}, {"number", "#"}},
		                                            .track = track_spec{"milestone", board_page, "milestones", "title", {{"due", "Due"}}},
		                                            .reorder = reorder_spec{"projects::issue::update", "position"}}));
	                     }});
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	auto& p = nav.current();
	auto root = built(nav);
	one::desktop::key_event down{.symbol = one::desktop::keys::down, .text = "", .alt = true};
	auto* column = list_holding(*root, "atlas-1");
	CHECK(pressed(column, down));  // #1, the first, goes below #2
	CHECK(data.runs().size() == 1);
	CHECK(data.runs().back().fields["id"].as_string() == "atlas-1");
	CHECK(data.runs().back().fields["position"].as_double() == 2.5);  // between #2's 2 and #3's 3
	auto order = [&] {
		std::vector<std::string> ids;
		auto r = built(nav);
		auto* l = list_holding(*r, "atlas-2");
		if (l)
			for (auto& c : l->children) ids.push_back(c->key);
		return ids;
	};
	CHECK((order() == std::vector<std::string>{"atlas-2", "atlas-1", "atlas-3"}));  // at once, before the view says so
	refuse = true;
	root = built(nav);
	pressed(list_holding(*root, "atlas-3"), one::desktop::key_event{.symbol = one::desktop::keys::up, .text = "", .alt = true});
	CHECK((order() == std::vector<std::string>{"atlas-2", "atlas-1", "atlas-3"}));  // turned down, so back where it was
	// A milestone picked keeps its cards alone.
	p.kept<std::string>("board:issues:track").set("atlas-beta");
	root = built(nav);
	CHECK(list_holding(*root, "atlas-2") == nullptr);
	CHECK(list_holding(*root, "atlas-3") != nullptr);
	matches_golden("board-track", {1000, 520}, [&] { return drawn(nav); });
}

TEST("code without the license header its file starts with, icons on buttons and links, and a tinted detail") {
	memory_source data;
	data.set("projects::issue_page", "atlas-12", value(fields{{"priority", "critical"}, {"stage.title", "To do"}}));
	site s{.name = "tracker", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Example", "/example", "", [](page& p) {
		                     auto issue = p.view("projects::issue_page", "atlas-12");
		                     heading(p, actions(command(p, {.command = "projects::board::follow", .label = "Follow", .icon = "follow"}),
		                                        link(p, "Settings", "/example/settings", "settings")));
		                     return items(details(p, issue, {{"priority", "Priority"}, {"stage.title", "Stage"}}, {{"priority", {{"critical", "Critical"}}}},
		                                          tint_spec{"priority", {"critical", "high", "normal", "low", "trivial"}}),
		                                  code("cpp", "// Copyright 2026 Daher Alfawares\n// SPDX-License-Identifier: AGPL-3.0-only\n\n#include <print>\n\nint main() { std::println(\"Hello\"); }\n"));
	                     }});
	navigator nav(s, data);
	nav.open("/example");
	matches_golden("code-icons", {900, 400}, [&] { return drawn(nav); });
}

int main() {
	use_time_zone("UTC");  // so what's drawn is the same wherever it's run
	// Noto's fonts alone, so what Noto Sans hasn't, like ✓, comes from the same font
	// wherever it's run, not from whichever other font a machine has installed.
	auto fonts = std::filesystem::temp_directory_path() / "one-desktop-runtime-tests-fonts.conf";
	std::ofstream(fonts) << "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
	                        "<fontconfig>\n\t<dir>/usr/share/fonts/noto</dir>\n\t<dir>/usr/share/fonts/truetype/noto</dir>\n"
	                        "\t<cachedir prefix=\"xdg\">fontconfig</cachedir>\n</fontconfig>\n";
	setenv("FONTCONFIG_FILE", fonts.c_str(), 1);
	return check::run();
}

TEST("a workspace: its bar, the app's name in its middle and light and dark at its end, a toolbar of groups whose tools have keys, a side shown while a toggle's on, and a status line saying what was undone") {
	memory_source data;
	data.on("studio::poster::create", [](const value&, memory_source&) -> answer { return "p1"; });
	data.on("one::undo", [](const value&, memory_source&) -> answer { return "studio::poster::place"; });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.dark = tested_theme();
	s.screens.push_back({"Studio", "/", "", [](page& p) {
		                     return workspace(p, region{"bar", along(history(p, false, "Undo", "undo", {"ctrl+z"}), history(p, true, "Redo", "redo", {"ctrl+y"}), text({words("Posters")}))},
		                                      region{"toolbar", along(group(p, "Poster", false, false, [&] { return tools(command(p, {.command = "studio::poster::create", .label = "New", .icon = "add", .keys = {"ctrl+n"}})); }),
		                                                              group(p, "View", true, true, [&] { return tools(toggle(p, "/ list", "List", "layers", {"l"}), full_screen(p, "Full screen", "fullscreen", {"f11"})); }))},
		                                      region{"main", items(text({words("The posters")}))},
		                                      region{"side", p.toggled("/ list") ? items(text({words("One a row")})) : one::desktop::view()},
		                                      region{"status", along(messages(p))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	(void)drawn(nav);
	CHECK(nav.current().keys().size() == 5);
	CHECK(key(nav, one::desktop::key_event{.symbol = 'n', .text = "", .control = true}));
	CHECK(data.runs().size() == 1);
	CHECK(!key(nav, one::desktop::key_event{.symbol = 'n', .text = "n"}));
	CHECK(!nav.current().toggled("/ list"));
	CHECK(key(nav, one::desktop::key_event{.symbol = 'l', .text = "l"}));
	CHECK(nav.current().toggled("/ list"));
	(void)drawn(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = 'z', .text = "", .control = true}));
	CHECK(nav.current().told().peek() == "Undid place.");
	matches_golden("workspace", {900, 420}, [&] { return drawn(nav); });
}

TEST("a toolbar's choices of one enum are pressed one at a time, the first until another's chosen by a press or its key") {
	memory_source data;
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Studio", "/", "", [](page& p) {
		                     return workspace(p, region{"toolbar", along(group(p, "Tools", false, false, [&] {
			                                      return tools(choose(p, "/ tool", "select", "select", "Select", "check", {"v"}), choose(p, "/ tool", "pen", "select", "Pen", "edit", {"p"}));
		                                      }))},
		                                      region{"main", items(text({words("Drawing with " + p.chosen("/ tool", "select"))}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	(void)drawn(nav);
	CHECK(nav.current().chosen("/ tool", "select") == "select");
	CHECK(key(nav, one::desktop::key_event{.symbol = 'p', .text = "p"}));
	CHECK(nav.current().chosen("/ tool", "select") == "pen");
	(void)drawn(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = 'v', .text = "v"}));
	CHECK(nav.current().chosen("/ tool", "select") == "select");
	CHECK(data.runs().empty());
}

TEST("a tool's options open over the work, each a choice with what it's for, a page's shape drawn as a page") {
	memory_source data;
	data.on("studio::poster::resize", [](const value&, memory_source&) -> answer { return "p1"; });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Studio", "/", "", [](page& p) {
		                     return workspace(p, region{"toolbar", along(group(p, "Page", false, false, [&] {
			                                      return tools(options(p, "Page", "fit", [&] {
				                                      return tools(command(p, {.command = "studio::poster::resize", .label = "Landscape", .swatch = "page 1920 1080", .pressed = true, .about = "16:9, for screens"}),
				                                                   command(p, {.command = "studio::poster::resize", .label = "Portrait", .swatch = "page 1080 1920", .about = "9:16, for stories"}));
			                                      }));
		                                      }))},
		                                      region{"main", items(text({words("The posters")}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	nav.current().kept<bool>("options Page").set(true);
	matches_golden("toolbar-options", {420, 300}, [&] { return drawn(nav); });
}

TEST("a toolbar's form button is a tool like the others, and the form it opens is shown over the work") {
	memory_source data;
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	auto rename = [] { return form_spec{.command = "studio::poster::update", .fields = {{.name = "title", .label = "Title"}}, .button = true, .opener = "Rename", .key = "rename", .icon = "edit"}; };
	s.screens.push_back({"Studio", "/", "", [rename](page& p) {
		                     return workspace(p, region{"toolbar", along(group(p, "Poster", false, false, [&] { return tools(form(p, rename())); }))},
		                                      region{"main", items(text({words("The posters")}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("toolbar-form", {420, 220}, [&] { return drawn(nav); });
	form_model(nav.current(), rename()).set_open(true);
	matches_golden("toolbar-form-open", {420, 220}, [&] { return drawn(nav); });
}

TEST("in a toolbar, a group that doesn't apply now is there, off, its keys doing nothing, and of tools with the same words the one that holds is drawn") {
	memory_source data;
	data.on("studio::poster::create", [](const value&, memory_source&) -> answer { return "p1"; });
	data.on("studio::poster::update", [](const value&, memory_source&) -> answer { return "p1"; });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Studio", "/", "", [](page& p) {
		                     bool applies = p.toggled("/ on");
		                     return workspace(p, region{"toolbar", along(p.off_unless(applies, [&] {
			                                                return group(p, "Poster", false, false, [&] {
				                                                return tools(command(p, {.command = "studio::poster::create", .label = "New", .icon = "add", .keys = {"ctrl+n"}}),
				                                                             either(p, {command_button{.command = "studio::poster::update", .label = "Bold", .when = false, .icon = "bold", .keys = {"ctrl+b"}},
				                                                                        command_button{.command = "studio::poster::update", .label = "Bold", .when = true, .icon = "bold", .keys = {"ctrl+b"}}}));
			                                                });
		                                                }))},
		                                      region{"main", items(text({words("The posters")}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	(void)drawn(nav);
	// Off: drawn, but no key of it works.
	CHECK(!key(nav, one::desktop::key_event{.symbol = 'n', .text = "", .control = true}));
	CHECK(!key(nav, one::desktop::key_event{.symbol = 'b', .text = "", .control = true}));
	CHECK(data.runs().empty());
	matches_golden("toolbar-off", {420, 120}, [&] { return drawn(nav); });
	// Applying, both work, Bold once, as the one whose when holds.
	nav.current().toggle("/ on");
	(void)drawn(nav);
	CHECK(nav.current().keys().size() == 2);
	CHECK(key(nav, one::desktop::key_event{.symbol = 'n', .text = "", .control = true}));
	CHECK(key(nav, one::desktop::key_event{.symbol = 'b', .text = "", .control = true}));
	CHECK(data.runs().size() == 2);
}

TEST("an equation is drawn as one::math sets it, in the math font the app brings, and one that can't be read is shown as it's written") {
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Equations", "/", "", [](page&) {
		                     return items(equation("x = (-b +- sqrt(b^2 - 4a c)) / 2a", true), equation("sum(k = 1 to n) k^2 = n(n + 1)(2n + 1) / 6", true),
		                                  equation("A = [1, 2; 3, 4]", true), equation("f(x) = cases(x if x >= 0; -x otherwise)", true), equation("(a + b", true));
	                     }});
	memory_source data;
	navigator nav(s, data);
	nav.open("/");
	matches_golden("equations", {520, 420}, [&] { return drawn(nav); });
}

TEST("Markdown sets $…$ as math on the words' baseline, an equation alone on its line centered, and a math block displayed") {
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Notes", "/", "", [](page&) {
		                     return items(markdown_view("The roots of $a x^2 + b x + c$ are where it crosses $y = 0$, and $5 stays money.\n\n"
		                                                "$x = (-b +- sqrt(b^2 - 4a c)) / 2a$\n\n```math\nsum(k = 1 to n) k = n(n + 1) / 2\n```\n"));
	                     }});
	memory_source data;
	navigator nav(s, data);
	nav.open("/");
	matches_golden("markdown-math", {520, 320}, [&] { return drawn(nav); });
}

TEST("a paper: its blocks set as their kinds say and aligned as they say, written in place, Enter splitting one and Backspace joining it to the one before") {
	memory_source data;
	auto block = [](std::string id, std::string kind, std::string words, double position, std::string align = "left") {
		return value(fields{{"id", std::move(id)}, {"kind", std::move(kind)}, {"words", std::move(words)}, {"position", position}, {"align", std::move(align)}});
	};
	list passages{block("b1", "title", "A harbor at dusk", 1, "center"), block("b2", "subtitle", "Notes for the poster", 2, "center"), block("b3", "heading", "The light", 3),
	              block("b4", "text", "The water holds the last of the sun a little longer than the sky does, so the poster's bottom edge is warmer than its top.", 4),
	              block("b5", "quote", "Paint what you see, not what you know.", 5), block("b6", "math", "x = (-b +- sqrt(b^2 - 4a c)) / 2a", 6),
	              block("b7", "text", "Signed, the harbor master", 7, "right")};
	data.set("studio::notes_page", "p1", value(fields{{"passages", passages}}));
	for (const char* command : {"studio::passage::write", "studio::passage::create", "studio::passage::delete"}) {
		data.on(command, [](const value&, memory_source&) -> answer { return std::string("b8"); });
	}
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Notes", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(paper(p, {.view = p.view("studio::notes_page", "p1"), .list = "passages", .fields = {{"words", "words"}, {"kind", "kind"}, {"align", "align"}, {"position", "position"}},
		                                                                        .write = "studio::passage::write", .writes_kind = true, .add = "studio::passage::create", .remove = "studio::passage::delete",
		                                                                        .parent = "poster", .subject = "p1", .pick = "/ passage", .key = "/ paper", .empty = "Title", .fills = true,
		                                                                        .outline = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("paper", {900, 900}, [&] { return drawn(nav); });
	auto root = built(nav);
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	std::function<one::desktop::element*(one::desktop::element&)> sheet = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.entry() && e.frame.width > 400) return &e;
		for (auto& child : e.children) {
			if (auto* found = sheet(*child)) return found;
		}
		return nullptr;
	};
	auto* page = sheet(*root);
	CHECK(page != nullptr);
	if (!page) return;
	// Opened, the caret is at the end of the last block, which is the screen's pick.
	CHECK(nav.current().picked_id("/ passage") == "b7");
	auto key = [](std::uint32_t symbol, std::string text = "") { return one::desktop::key_event{.symbol = symbol, .text = std::move(text)}; };
	// Typed, and nothing sent until typing pauses or the caret leaves the block.
	CHECK(page->handle_key(key('.', ".")));
	CHECK(data.runs().empty());
	// Enter at the end: the words written, and a text block made after it, as one step.
	CHECK(page->handle_key(key(one::desktop::keys::enter)));
	CHECK(data.runs().size() == 2);
	if (data.runs().size() < 2) return;
	CHECK(data.runs()[0].command == "studio::passage::write");
	CHECK(data.runs()[0].fields["words"].as_string() == "Signed, the harbor master.");
	CHECK(data.runs()[1].command == "studio::passage::create");
	CHECK(data.runs()[1].fields["position"].as_double() == 8);
	CHECK(data.runs()[1].fields["kind"].as_string() == "text");
	CHECK(data.runs()[1].fields["poster"].as_string() == "p1");
	CHECK(data.runs()[1].fields["one:history"].as_string() == "joined");
	// Once the view has the block made, the caret is in it, and it's the pick.
	passages.push_back(block("b8", "text", "", 8));
	data.set("studio::notes_page", "p1", value(fields{{"passages", passages}}));
	auto again = built(nav);
	CHECK(nav.current().picked_id("/ passage") == "b8");
	// Typed there, it's that block's words.
	auto* paper_again = sheet(*again);
	CHECK(paper_again != nullptr);
	if (!paper_again) return;
	CHECK(paper_again->handle_key(key(one::desktop::keys::escape)) == false);
	CHECK(paper_again->handle_key(key('A', "A")));
	CHECK(paper_again->handle_key(key(one::desktop::keys::enter)));
	CHECK(data.runs().size() == 4);
	if (data.runs().size() < 4) return;
	CHECK(data.runs()[2].fields["id"].as_string() == "b8");
	CHECK(data.runs()[2].fields["words"].as_string() == "A");
	// An insert button: an empty block after the caret's, given what it gives.
	nav.current().kept<std::optional<value>>("paper insert / paper").set(value(fields{{"kind", "math"}}));
	(void)built(nav);
	CHECK(data.runs().size() == 5);
	if (data.runs().size() < 5) return;
	CHECK(data.runs()[4].command == "studio::passage::create");
	CHECK(data.runs()[4].fields["kind"].as_string() == "math");
	CHECK(data.runs()[4].fields["words"].as_string() == "");
	CHECK(data.runs()[4].fields["position"].as_double() == 9);
}

TEST("a paper's math block is its words while the caret's in it, in a well, and keeps the room its equation takes") {
	memory_source data;
	auto block = [](std::string id, std::string kind, std::string words, double position, std::string align = "left") {
		return value(fields{{"id", std::move(id)}, {"kind", std::move(kind)}, {"words", std::move(words)}, {"position", position}, {"align", std::move(align)}});
	};
	// Opened, the caret is at the end of the last block, the equation.
	data.set("studio::notes_page", "p1",
	         value(fields{{"passages", list{block("b1", "heading", "The roots", 1), block("b2", "math", "x = (-b +- sqrt(b^2 - 4a c)) / 2a", 2, "center"),
	                                        block("b3", "text", "Below it.", 3)}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Notes", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(paper(p, {.view = p.view("studio::notes_page", "p1"), .list = "passages",
		                                                                        .fields = {{"words", "words"}, {"kind", "kind"}, {"align", "align"}, {"position", "position"}},
		                                                                        .key = "/ paper", .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> sheet = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.entry() && e.frame.width > 400) return &e;
		for (auto& child : e.children) {
			if (auto* found = sheet(*child)) return found;
		}
		return nullptr;
	};
	auto* page = sheet(*root);
	CHECK(page != nullptr);
	if (!page) return;
	// Up from the last block goes into the equation, which shows its words.
	CHECK(page->handle_key(one::desktop::key_event{.symbol = one::desktop::keys::up, .text = ""}));
	matches_golden("paper-math-written", {900, 420}, [&] { return drawn(nav); });
}

TEST("a paper is pages, each numbered at its foot, a block that doesn't fit going to the next, and a heading with what it heads, warm when it says") {
	memory_source data;
	auto block = [](std::string id, std::string kind, std::string words, double position) {
		return value(fields{{"id", std::move(id)}, {"kind", std::move(kind)}, {"words", std::move(words)}, {"position", position}, {"align", "left"}});
	};
	std::string said = "The water holds the last of the sun a little longer than the sky does, so the bottom edge is warmer than its top.";
	list passages{block("b1", "title", "A harbor at dusk", 1)};
	for (int i = 0; i < 9; ++i) passages.push_back(block("t" + std::to_string(i), "text", said + " " + said + " " + said, 2 + i));
	passages.push_back(block("h1", "heading", "The water", 10));
	passages.push_back(block("t9", "text", said + " " + said, 11));
	data.set("studio::notes_page", "p1", value(fields{{"passages", passages}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Notes", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(paper(p, {.view = p.view("studio::notes_page", "p1"), .list = "passages",
		                                                                        .fields = {{"words", "words"}, {"kind", "kind"}, {"align", "align"}, {"position", "position"}},
		                                                                        .key = "/ paper", .warm = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("paper-pages", {500, 1320}, [&] { return drawn(nav); });
}

TEST("a paper's picture is as wide as it says, its own shape, its corners as round as it says, its caption under it, and Enter there goes on in text") {
	struct kept_here : memory_source {
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return golden_folder / kept; }
	} data;
	auto block = [](std::string id, std::string kind, std::string words, double position) {
		return value(fields{{"id", std::move(id)}, {"kind", std::move(kind)}, {"words", std::move(words)}, {"position", position}, {"align", "center"}});
	};
	value photo = block("p1", "picture", "The harbor, as the poster has it", 2);
	photo.fields["picture"] = value("collage.png");
	photo.fields["picture_width"] = value(900), photo.fields["picture_height"] = value(560);
	photo.fields["width"] = value(0.5), photo.fields["corners"] = value(0.06);
	data.set("studio::notes_page", "p1", value(fields{{"passages", list{block("b1", "title", "A harbor at dusk", 1), photo, block("b3", "text", "Below the picture.", 3)}}}));
	data.on("studio::passage::create", [](const value&, memory_source&) -> answer { return std::string("b9"); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Notes", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(paper(p, {.view = p.view("studio::notes_page", "p1"), .list = "passages",
		                                                                        .fields = {{"words", "words"}, {"kind", "kind"}, {"align", "align"}, {"position", "position"}, {"picture", "picture"},
		                                                                                   {"picture_width", "picture_width"}, {"picture_height", "picture_height"}, {"width", "width"}, {"corners", "corners"}},
		                                                                        .write = "studio::passage::write", .add = "studio::passage::create", .key = "/ paper", .fills = true, .paste = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("paper-picture", {900, 640}, [&] { return drawn(nav); });
	// In the caption, Enter makes text after it, not another picture.
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> sheet = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.entry() && e.frame.width > 400) return &e;
		for (auto& child : e.children) {
			if (auto* found = sheet(*child)) return found;
		}
		return nullptr;
	};
	auto* page = sheet(*root);
	CHECK(page != nullptr);
	if (!page) return;
	// From the start of the text below, Left goes to the end of the caption.
	CHECK(page->handle_key(one::desktop::key_event{.symbol = one::desktop::keys::home, .text = ""}));
	CHECK(page->handle_key(one::desktop::key_event{.symbol = one::desktop::keys::left, .text = ""}));
	// With the caret in its caption, the picture is ringed in the accent.
	matches_golden("paper-picture-picked", {900, 640}, [&] { return drawn(nav); });
	root = built(nav);
	page = sheet(*root);
	CHECK(page != nullptr);
	if (!page) return;
	CHECK(page->handle_key(one::desktop::key_event{.symbol = 'x', .text = "x"}));
	CHECK(page->handle_key(one::desktop::key_event{.symbol = one::desktop::keys::left, .text = ""}));
	CHECK(page->handle_key(one::desktop::key_event{.symbol = one::desktop::keys::enter, .text = ""}));
	CHECK(!data.runs().empty());
	if (data.runs().empty()) return;
	CHECK(data.runs().back().command == "studio::passage::create");
	CHECK(data.runs().back().fields["kind"].as_string() == "text");
	CHECK(data.runs().back().fields["words"].as_string() == "x");
}

TEST("a paper's selection runs across blocks with Shift, is drawn in each, and is erased as one step, the blocks between deleted") {
	memory_source data;
	auto block = [](std::string id, std::string kind, std::string words, double position) {
		return value(fields{{"id", std::move(id)}, {"kind", std::move(kind)}, {"words", std::move(words)}, {"position", position}, {"align", "left"}});
	};
	data.set("studio::notes_page", "p1",
	         value(fields{{"passages", list{block("b1", "title", "A harbor at dusk", 1), block("b2", "text", "The light was warm and low over the water.", 2),
	                                        block("b3", "text", "The boats were still.", 3)}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Notes", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(paper(p, {.view = p.view("studio::notes_page", "p1"), .list = "passages",
		                                                                        .fields = {{"words", "words"}, {"kind", "kind"}, {"align", "align"}, {"position", "position"}},
		                                                                        .write = "studio::passage::write", .add = "studio::passage::create", .remove = "studio::passage::delete", .key = "/ paper"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	std::function<one::desktop::element*(one::desktop::element&)> sheet = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.entry() && e.frame.width > 400) return &e;
		for (auto& child : e.children) {
			if (auto* found = sheet(*child)) return found;
		}
		return nullptr;
	};
	// Laid out as a window would, so a line's end is where it's drawn.
	auto root = built(nav);
	root->place({0, 0, 900, 520});
	auto* page = sheet(*root);
	CHECK(page != nullptr);
	if (!page) return;
	auto key = [&](std::uint32_t symbol, bool shift = false, bool control = false) { return page->handle_key(one::desktop::key_event{.symbol = symbol, .text = "", .shift = shift, .control = control}); };
	// From after "A " in the title, Shift and Down twice, then Shift and End: into the last block's end.
	CHECK(key(one::desktop::keys::home, false, true));
	CHECK(key(one::desktop::keys::right));
	CHECK(key(one::desktop::keys::right));
	CHECK(key(one::desktop::keys::down, true));
	CHECK(key(one::desktop::keys::down, true));
	CHECK(key(one::desktop::keys::end, true));
	// Drawn with the keyboard, as a selection is shown only then.
	matches_golden("paper-across", {900, 520}, [&] { return drawn(nav); }, [&](one::desktop::element& drawn_root) {
		if (auto* on = sheet(drawn_root)) on->focused = true;
	});
	root = built(nav);
	root->place({0, 0, 900, 520});
	page = sheet(*root);
	CHECK(page != nullptr);
	if (!page) return;
	CHECK(page->handle_key(one::desktop::key_event{.symbol = 'x', .text = "x"}));
	auto runs = data.runs();
	CHECK(runs.size() == 3);
	if (runs.size() != 3) return;
	CHECK(runs[0].command == "studio::passage::write");
	CHECK(runs[0].fields["id"].as_string() == "b1");
	CHECK(runs[0].fields["words"].as_string() == "A ");
	CHECK(runs[1].command == "studio::passage::delete");
	CHECK(runs[1].fields["id"].as_string() == "b2");
	CHECK(runs[1].fields["one:history"].as_string() == "joined");
	CHECK(runs[2].fields["id"].as_string() == "b3");
	CHECK(runs[2].fields["one:history"].as_string() == "joined");
}

TEST("a paper is set in its document's face: a book's serif, the app's own face, a lyric's warmer serif, or a typewriter's") {
	for (std::string face : {"book", "lyric", "typewriter"}) {
		memory_source data;
		auto block = [](std::string id, std::string kind, std::string words, double position, std::string align = "left") {
			return value(fields{{"id", std::move(id)}, {"kind", std::move(kind)}, {"words", std::move(words)}, {"position", position}, {"align", std::move(align)}});
		};
		data.set("studio::notes_page", "p1",
		         value(fields{{"face", face},
		                      {"passages", list{block("b1", "title", "A harbor at dusk", 1, "center"), block("b2", "subtitle", "Notes for the poster", 2, "center"),
		                                        block("b3", "heading", "The light", 3),
		                                        block("b4", "text", "The water holds the last of the sun a little longer than the sky does, so the poster's bottom edge is warmer than its top.", 4),
		                                        block("b5", "quote", "Paint what you see, not what you know.", 5), block("b6", "code", "let warm = sun - sky", 6)}}}));
		site s{.name = "studio", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Notes", "/", "", [](page& p) {
			                     return workspace(p, region{"main", items(paper(p, {.view = p.view("studio::notes_page", "p1"), .list = "passages",
			                                                                        .fields = {{"words", "words"}, {"kind", "kind"}, {"align", "align"}, {"position", "position"}, {"face", "face"}},
			                                                                        .key = "/ paper", .fills = true}))});
		                     }});
		navigator nav(s, data);
		nav.open("/");
		matches_golden("paper-" + face, {900, 640}, [&] { return drawn(nav); });
	}
}

TEST("a status line saying a file was saved offers to show it in its folder, and only while it says so") {
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Studio", "/", "", [](page& p) { return workspace(p, region{"main", items(text({words("The posters")}))}, region{"status", along(messages(p))}); }});
	memory_source data;
	navigator nav(s, data);
	nav.open("/");
	auto& p = nav.current();
	p.kept<std::pair<std::string, std::string>>("said of a file").set({"Saved harbor.png in /home/someone/Pictures/studio.", "/home/someone/Pictures/studio/harbor.png"});
	p.told().set("Saved harbor.png in /home/someone/Pictures/studio.");
	matches_golden("saved", {640, 160}, [&] { return drawn(nav); });
	// Said something else, there's no file to show.
	p.told().set("Undid place.");
	matches_golden("saved-then-not", {640, 160}, [&] { return drawn(nav); });
}

TEST("a collage: its layers drawn as big as it is, scaled to fit; one pressed is picked, and one dragged is placed where it's let go") {
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "w1"}, {"words", "Harbor at dusk"}, {"size", 96}, {"x", 120}, {"y", 600}, {"w", 700}, {"h", 120}})}}}));
	data.on("studio::layer::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height", .fields = named_like_parts, .place = "studio::layer::place",
		                                                                         .snap = true, .empty = "Add a picture to start", .pick = "/ layer", .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	// The drawing: the element with nothing inside it that takes most of the page.
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	// Painted once, as the window would, so it knows where it is.
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	// Scaled to fit with 32 around it, so a point of the collage is where this says.
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	one::desktop::point start{origin.x + 300 * scale, origin.y + 650 * scale};
	CHECK(board->press(start, 1));
	CHECK(nav.current().picked_id("/ layer") == "w1");
	board->drag({start.x + 50, start.y});
	board->release({start.x + 50, start.y}, board);
	CHECK(!data.runs().empty());
	if (data.runs().empty()) return;
	CHECK(data.runs().back().command == "studio::layer::place");
	CHECK(std::abs(data.runs().back().fields["x"].as_double() - (120 + 50 / scale)) < 0.01);
	CHECK(data.runs().back().fields["y"].as_double() == 600);
	matches_golden("collage", {900, 560}, [&] { return drawn(nav); });
}

TEST("a collage draws a layer as big as the drag, given what its draw says, picks it, and sets the choice its when read back to the first; by line, a line, picked from near it") {
	memory_source data;
	data.set("studio::poster_page", "p1", value(fields{{"width", 1600}, {"height", 900}, {"layers", list{}}}));
	data.on("studio::layer::create", [](const value&, memory_source&) -> answer { return "drawn"; });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     bool box = p.chosen("/ tool", "move") == "box";
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height", .fields = named_like_parts,
		                                                                         .pick = "/ layer", .subject = "p1", .fills = true,
		                                                                         .draws = {collage_draw{.command = "studio::layer::create", .fields = value(std::map<std::string, value>{{"fill", value("#2e86de")}}),
		                                                                                                .when = box, .back = {{"/ tool", "move"}}}},
		                                                                         .parent = "poster"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	nav.current().choose("/ tool", "box");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	// Dragged up and to the left, from (500, 400) to (100, 200) on the collage.
	CHECK(board->press({origin.x + 500 * scale, origin.y + 400 * scale}, 1));
	board->drag({origin.x + 300 * scale, origin.y + 300 * scale});
	board->release({origin.x + 100 * scale, origin.y + 200 * scale}, board);
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	const auto& sent = data.runs().back().fields;
	CHECK(data.runs().back().command == "studio::layer::create");
	CHECK(std::abs(sent["x"].as_double() - 100) < 0.5);
	CHECK(std::abs(sent["y"].as_double() - 200) < 0.5);
	CHECK(std::abs(sent["w"].as_double() - 400) < 0.5);
	CHECK(std::abs(sent["h"].as_double() - 200) < 0.5);
	CHECK(sent["fill"].as_string() == "#2e86de");
	CHECK(sent["poster"].as_string() == "p1");
	CHECK(nav.current().picked_id("/ layer") == "drawn");
	CHECK(nav.current().chosen("/ tool", "move") == "move");
	// By line, a path from where the drag began to where it ended, picked within reach.
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"shape", "path"}, {"points", "M 0 0.5 L 1 0.5"}, {"stroke", "#1d1d1f"}, {"stroke_width", 2}, {"x", 100}, {"y", 699.5}, {"w", 800}, {"h", 1}})}}}));
	s.screens.clear();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     bool line = p.chosen("/ tool", "move") == "line";
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height", .fields = named_like_parts,
		                                                                         .pick = "/ layer", .subject = "p1", .fills = true,
		                                                                         .draws = {collage_draw{.command = "studio::layer::create", .when = line, .by = "line", .back = {{"/ tool", "move"}}}},
		                                                                         .parent = "poster"}))});
	                     }});
	navigator lines(s, data);
	lines.open("/");
	lines.current().choose("/ tool", "line");
	root = built(lines);
	board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	CHECK(board->press({origin.x + 100 * scale, origin.y + 400 * scale}, 1));
	board->drag({origin.x + 200 * scale, origin.y + 300 * scale});
	board->release({origin.x + 300 * scale, origin.y + 200 * scale}, board);
	CHECK(data.runs().size() == 2);
	if (data.runs().size() < 2) return;
	const auto& line = data.runs().back().fields;
	CHECK(line["points"].as_string() == "M 0 1 L 1 0");
	CHECK(std::abs(line["x"].as_double() - 100) < 0.5);
	CHECK(std::abs(line["w"].as_double() - 200) < 0.5);
	// A press just above the line, on the line drawn before, picks it.
	root = built(lines);
	board = drawing(*root);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	CHECK(board->press({origin.x + 500 * scale, origin.y + 700 * scale - 4}, 1));
	CHECK(lines.current().picked_id("/ layer") == "l1");
}

TEST("a collage crops its picked picture in place, the background too, kept with Enter, and words picked are written where they are, kept with Enter") {
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "b0"}, {"picture", "files/dusk"}, {"x", 0}, {"y", 0}, {"w", 1600}, {"h", 900}}),
	                                      value(fields{{"id", "s1"}, {"picture", "files/sun"}, {"x", 100}, {"y", 100}, {"w", 300}, {"h", 300}}),
	                                      value(fields{{"id", "w1"}, {"words", "Harbor"}, {"size", 96}, {"x", 120}, {"y", 600}, {"w", 300}, {"h", 120}})}}}));
	data.on("studio::layer::crop", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("studio::layer::write", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height", .fields = named_like_parts, .place = "studio::layer::place",
		                                                                         .pick = "/ layer", .fills = true, .crop = "studio::layer::crop", .write = "studio::layer::write",
		                                                                         .key = "/ layer"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto& p = nav.current();
	// The sun picked and asked to be cropped, then kept with Enter: all of it, where it was.
	p.pick("/ layer", "s1");
	p.kept<std::string>("collage do / layer").set("crop");
	(void)drawn(nav);
	(void)drawn(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::enter, .text = "", .control = false}));
	CHECK(!data.runs().empty());
	if (data.runs().empty()) return;
	CHECK(data.runs().back().command == "studio::layer::crop");
	CHECK(data.runs().back().fields["crop_w"].as_double() == 1);
	CHECK(data.runs().back().fields["x"].as_double() == 100);
	// The background too, cropped where it sits: all of it, kept with Enter.
	std::size_t before = data.runs().size();
	p.pick("/ layer", "b0");
	p.kept<std::string>("collage do / layer").set("crop");
	(void)drawn(nav);
	(void)drawn(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::enter, .text = "", .control = false}));
	CHECK(data.runs().size() == before + 1);
	if (data.runs().size() == before + 1) {
		CHECK(data.runs().back().fields["id"].as_string() == "b0");
		CHECK(data.runs().back().fields["crop_w"].as_double() == 1);
		CHECK(data.runs().back().fields["crop_h"].as_double() == 1);
	}
	// The words picked and written: what's typed goes after them, Esc keeps them, and
	// they're sent as big as they now are.
	p.pick("/ layer", "w1");
	p.kept<std::string>("collage do / layer").set("edit");
	(void)drawn(nav);
	(void)drawn(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = '!', .text = "!", .control = false}));
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::escape, .text = "", .control = false}));
	CHECK(data.runs().back().command == "studio::layer::write");
	CHECK(data.runs().back().fields["words"].as_string() == "Harbor!");
	CHECK(data.runs().back().fields["w"].as_double() > 200);
}

TEST("a picture cropped is drawn by the part it keeps, and around edges draws the rest of the collage out from that part's edges") {
	struct kept_here : memory_source {
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return golden_folder / kept; }
	} data;
	// The left half of a 900 by 560 picture, fitted in the middle, as tall as the collage,
	// and what's either side of it drawn out from its edges.
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "b0"}, {"picture", "collage.png"}, {"crop_x", 0}, {"crop_y", 0}, {"crop_w", 0.5}, {"crop_h", 1}, {"around", "edges"},
	                                                   {"x", 800 - 450 * 900.0 / 560 / 2}, {"y", 0}, {"w", 450 * 900.0 / 560}, {"h", 900}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("collage-cropped-edges", {900, 560}, [&] { return drawn(nav); });
}

TEST("around harmonize fills the rest of the collage from a picture's edges, each color settling between the ones around it, and what's over it is drawn over it, as blend kept before did") {
	struct kept_here : memory_source {
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return golden_folder / kept; }
	} data;
	// A picture small in the middle, blended out to the collage's edges, and one over it,
	// sharp.
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"picture", "collage.png"}, {"around", "blend"}, {"x", 575}, {"y", 310}, {"w", 450}, {"h", 280}}),
	                                      value(fields{{"id", "l2"}, {"picture", "collage.png"}, {"x", 1200}, {"y", 600}, {"w", 300}, {"h", 187}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("collage-blend", {900, 560}, [&] { return drawn(nav); });
}

TEST("a collage's path drawn by hand is what the drag went through, only as much as keeps its shape, as one curve") {
	memory_source data;
	data.set("studio::poster_page", "p1", value(fields{{"width", 1600}, {"height", 900}, {"layers", list{}}}));
	data.on("studio::layer::create", [](const value&, memory_source&) -> answer { return "drawn"; });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .fills = true,
		                                                                         .draws = {collage_draw{.command = "studio::layer::create", .by = "hand"}}}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	CHECK(board->press(on(100, 100), 1));
	board->drag(on(300, 301));
	board->drag(on(500, 500));
	board->release(on(900, 100), board);
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	const auto& sent = data.runs().back().fields;
	CHECK(std::abs(sent["x"].as_double() - 100) < 1);
	CHECK(std::abs(sent["w"].as_double() - 800) < 1);
	std::string points = sent["points"].as_string();
	CHECK(std::ranges::count(points, 'C') == 2);
}

TEST("a collage's words made by typing where it's pressed, of several lines, once they're kept, and nothing when nothing's typed") {
	memory_source data;
	data.set("studio::poster_page", "p1", value(fields{{"width", 1600}, {"height", 900}, {"layers", list{}}}));
	data.on("studio::layer::create", [](const value&, memory_source&) -> answer { return "typed"; });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .fills = true,
		                                                                         .draws = {collage_draw{.command = "studio::layer::create", .fields = value(fields{{"size", 48}, {"color", "#1d1d1f"}}), .by = "typing"}}}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto root = built(nav);
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	auto click = [&](double x, double y) {
		root = built(nav);
		board = drawing(*root);
		(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
		board->press(on(x, y), 1);
		board->release(on(x, y), board);
	};
	auto typed = [&](const std::string& text) {
		(void)built(nav);
		return key(nav, one::desktop::key_event{.symbol = static_cast<std::uint32_t>(text[0]), .text = text});
	};
	// Pressed, then pressed elsewhere with nothing typed: nothing made.
	click(800, 600);
	click(100, 100);
	CHECK(data.runs().empty());
	CHECK(typed("H"));
	CHECK(typed("i"));
	(void)built(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::enter, .text = ""}));
	CHECK(typed(" "));
	CHECK(typed("y"));
	CHECK(typed("o"));
	CHECK(typed("u"));
	CHECK(data.runs().empty());
	(void)built(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::escape, .text = ""}));
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	const value& made = data.runs().back().fields;
	CHECK(made["words"].as_string() == "Hi\n you");
	CHECK(made["size"].as_double() == 48);
	CHECK(made["color"].as_string() == "#1d1d1f");
	CHECK(std::abs(made["x"].as_double() - 100) < 1);
	CHECK(made["y"].as_double() < 100);
	CHECK(made["w"].as_double() > 0);
}

TEST("a collage's path drawn by points, as a pen draws: corners clicked, a smooth node dragged, ended by Enter or closed on its first node") {
	memory_source data;
	data.set("studio::poster_page", "p1", value(fields{{"width", 1600}, {"height", 900}, {"layers", list{}}}));
	data.on("studio::layer::create", [](const value&, memory_source&) -> answer { return "drawn"; });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .fills = true,
		                                                                         .draws = {collage_draw{.command = "studio::layer::create", .by = "points"}}}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto root = built(nav);
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	auto click = [&](double x, double y) {
		root = built(nav);
		board = drawing(*root);
		(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
		board->press(on(x, y), 1);
		board->release(on(x, y), board);
	};
	// Three corners, ended by Enter: two lines.
	click(100, 100);
	click(500, 100);
	click(500, 500);
	CHECK(data.runs().empty());
	(void)built(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::enter, .text = ""}));
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	std::string open = data.runs().back().fields["points"].as_string();
	CHECK(std::ranges::count(open, 'L') == 2);
	CHECK(open.find('Z') == std::string::npos);
	// A corner, a smooth node dragged, a corner, then its first node pressed: closed.
	click(100, 100);
	root = built(nav);
	board = drawing(*root);
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	board->press(on(500, 100), 1);
	board->drag(on(600, 100));
	board->release(on(600, 100), board);
	click(300, 400);
	click(100, 100);
	CHECK(data.runs().size() == 2);
	if (data.runs().size() < 2) return;
	std::string closed = data.runs().back().fields["points"].as_string();
	CHECK(closed.find('C') != std::string::npos);
	CHECK(closed.ends_with("Z"));
}

TEST("a collage's path is picked by its stroke alone, shows its nodes, and is reshaped by one dragged, its box worked out again") {
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"shape", "path"}, {"points", "M 0 0 L 1 1"}, {"stroke", "#1d1d1f"}, {"stroke_width", 4}, {"x", 100}, {"y", 100},
	                                                   {"w", 400}, {"h", 200}})}}}));
	data.on("studio::layer::reshape", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .place = "studio::layer::place", .reshape = "studio::layer::reshape",
		                                                                         .pick = "/ layer", .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	// Inside its box, far from its stroke: nothing; on its stroke: picked.
	board->press(on(450, 120), 1);
	board->release(on(450, 120), board);
	CHECK(nav.current().picked_id("/ layer").empty());
	CHECK(board->press(on(300, 200), 1));
	board->release(on(300, 200), board);
	CHECK(nav.current().picked_id("/ layer") == "l1");
	// Its first node, at 100 100, dragged down 100: from 100 200 to 500 300.
	root = built(nav);
	board = drawing(*root);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	CHECK(board->press(on(100, 100), 1));
	board->drag(on(100, 150));
	board->release(on(100, 200), board);
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	const auto& sent = data.runs().back().fields;
	CHECK(data.runs().back().command == "studio::layer::reshape");
	CHECK(sent["points"].as_string() == "M 0 0 L 1 1");
	CHECK(std::abs(sent["y"].as_double() - 200) < 0.5);
	CHECK(std::abs(sent["h"].as_double() - 100) < 0.5);
}

TEST("a collage's path shows a node's control points once the node is pressed, and is reshaped by one dragged") {
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"shape", "path"}, {"points", "M 0 0 C 0 1 1 1 1 0"}, {"stroke", "#1d1d1f"}, {"stroke_width", 4}, {"x", 100}, {"y", 100},
	                                                   {"w", 400}, {"h", 200}})}}}));
	data.on("studio::layer::reshape", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .place = "studio::layer::place", .reshape = "studio::layer::reshape",
		                                                                         .pick = "/ layer", .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	// Its curve, halfway along, at 300 250: picked.
	CHECK(board->press(on(300, 250), 1));
	board->release(on(300, 250), board);
	CHECK(nav.current().picked_id("/ layer") == "l1");
	root = built(nav);
	board = drawing(*root);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	// Its last node, at 500 100, pressed and let go where it was: nothing sent.
	CHECK(board->press(on(500, 100), 1));
	board->release(on(500, 100), board);
	CHECK(data.runs().empty());
	// Its control point, at 500 300, dragged left 100.
	CHECK(board->press(on(500, 300), 1));
	board->drag(on(450, 300));
	board->release(on(400, 300), board);
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	CHECK(data.runs().back().fields["points"].as_string() == "M 0 0 C 0 1 0.75 1 1 0");
}

TEST("a collage's group is picked by a press on what it holds, moved as one in one step, and ungrouped; pressed twice, what it holds is picked") {
	memory_source data;
	auto shape = [](std::string id, double x, double y, std::string group) {
		return value(fields{{"id", std::move(id)}, {"shape", "rectangle"}, {"fill", "#2e86de"}, {"x", x}, {"y", y}, {"w", 100}, {"h", 100}, {"group", std::move(group)}});
	};
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "g"}, {"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}}), shape("a", 100, 100, "g"), shape("b", 300, 200, "g"), shape("c", 900, 500, "")}}}));
	data.on("studio::layer::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("studio::layer::group", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("studio::layer::delete", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	auto parts = named_like_parts;
	parts["group"] = "group";
	s.screens.push_back({"Poster", "/", "", [parts](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = parts, .place = "studio::layer::place", .group = "studio::layer::group",
		                                                                         .pick = "/ layer", .fills = true, .remove = "studio::layer::delete", .key = "/ layer"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	// A press on one it holds picks the group, and a drag moves all it holds, in one step.
	CHECK(board->press(on(150, 150), 1));
	CHECK(nav.current().picked_id("/ layer") == "g");
	board->drag(on(170, 150));
	board->release(on(200, 150), board);
	CHECK(data.runs().size() == 2);
	if (data.runs().size() != 2) return;
	std::map<std::string, double> xs;
	for (const auto& r : data.runs()) xs[r.fields["id"].as_string()] = r.fields["x"].as_double();
	CHECK(std::abs(xs["a"] - 150) < 1);
	CHECK(std::abs(xs["b"] - 350) < 1);
	CHECK(data.runs()[1].fields["one:history"].as_string() == "joined");
	// Pressed twice, the one it holds.
	root = built(nav);
	board = drawing(*root);
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	board->press(on(350, 250), 2);
	board->release(on(350, 250), board);
	CHECK(nav.current().picked_id("/ layer") == "b");
	// Ungrouped: each it holds in none, and it gone.
	nav.current().pick("/ layer", "g");
	nav.current().kept<std::string>("collage do / layer").set("ungroup");
	(void)drawn(nav);
	const auto& runs = data.runs();
	CHECK(runs.size() == 5);
	if (runs.size() != 5) return;
	CHECK(runs[2].command == "studio::layer::group");
	CHECK(runs[2].fields["group"].is_null());
	CHECK(runs[3].fields["one:history"].as_string() == "joined");
	CHECK(runs[4].command == "studio::layer::delete");
	CHECK(runs[4].fields["id"].as_string() == "g");
}

TEST("a collage zoomed in about its middle has what's on it picked where it's now shown, and the wheel moves what's shown") {
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"shape", "rectangle"}, {"fill", "#000000"}, {"x", 100}, {"y", 100}, {"w", 200}, {"h", 200}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .pick = "/ layer", .fills = true, .key = "/ layer"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto draw = [&] { (void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); }); };
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	draw();
	auto area = board->frame;
	float fit = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point middle{area.x + area.width / 2, area.y + area.height / 2};
	one::desktop::point origin{middle.x - 800 * fit, middle.y - 450 * fit};
	// Zoomed in a quarter about its middle: 200 200 is a quarter further from it.
	nav.current().kept<std::string>("collage do / layer").set("zoom in");
	root = built(nav);
	board = drawing(*root);
	draw();
	auto shown = [&](double x, double y, float zoom) {
		return one::desktop::point{middle.x + (origin.x + static_cast<float>(x) * fit - middle.x) * zoom, middle.y + (origin.y + static_cast<float>(y) * fit - middle.y) * zoom};
	};
	CHECK(board->press(shown(290, 290, 1.25f), 1));
	board->release(shown(290, 290, 1.25f), board);
	CHECK(nav.current().picked_id("/ layer") == "l1");
	// Not where it was before.
	nav.current().pick("/ layer", "");
	board->press(shown(310, 310, 1.f), 1);
	board->release(shown(310, 310, 1.f), board);
	CHECK(nav.current().picked_id("/ layer").empty());
	// The wheel without Ctrl moves what's shown.
	CHECK(board->wheel(middle, 0, 40));
}

TEST("a collage's group gone into by pressing it twice has what it holds picked by a press, and Esc picks the group again; a drag around several chooses them") {
	memory_source data;
	auto shape = [](std::string id, double x, double y, std::string group) {
		return value(fields{{"id", std::move(id)}, {"shape", "rectangle"}, {"fill", "#2e86de"}, {"x", x}, {"y", y}, {"w", 100}, {"h", 100}, {"group", std::move(group)}});
	};
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "g"}, {"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}}), shape("a", 100, 100, "g"), shape("b", 300, 200, "g"), shape("c", 900, 500, ""),
	                                      shape("d", 1100, 500, "")}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	auto parts = named_like_parts;
	parts["group"] = "group";
	s.screens.push_back({"Poster", "/", "", [parts](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = parts, .place = "studio::layer::place", .pick = "/ layer", .fills = true, .key = "/ layer"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	std::function<one::desktop::element*(one::desktop::element&)> drawing = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.children.empty() && e.frame.width > 400 && e.frame.height > 200) return &e;
		for (auto& child : e.children) {
			if (auto* found = drawing(*child)) return found;
		}
		return nullptr;
	};
	auto* board = drawing(*root);
	CHECK(board != nullptr);
	if (!board) return;
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	// Pressed twice, gone into: then a press on b picks b, not the group.
	board->press(on(150, 150), 2);
	board->release(on(150, 150), board);
	CHECK(nav.current().picked_id("/ layer") == "a");
	board->press(on(350, 250), 1);
	board->release(on(350, 250), board);
	CHECK(nav.current().picked_id("/ layer") == "b");
	// Esc: the group again.
	root = built(nav);
	CHECK(key(nav, one::desktop::key_event{.symbol = one::desktop::keys::escape, .text = "", .control = false}));
	CHECK(nav.current().picked_id("/ layer") == "g");
	// A drag around c and d chooses them both, so neither alone is picked.
	root = built(nav);
	board = drawing(*root);
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	CHECK(board->press(on(850, 450), 1));
	board->drag(on(1000, 550));
	board->release(on(1250, 650), board);
	CHECK(nav.current().picked_id("/ layer").empty());
}

TEST("a collage's layers listed beside it, topmost first: each an eye, its preview in the collage's proportions, its name, up, down and delete; a hidden one isn't drawn") {
	memory_source data;
	auto shape = [](std::string id, std::string kind, double x, double y, double w, double h, std::string fill, double depth, double hidden = 0) {
		return value(fields{{"id", std::move(id)}, {"shape", std::move(kind)}, {"x", x}, {"y", y}, {"w", w}, {"h", h}, {"fill", std::move(fill)}, {"depth", depth}, {"hidden", hidden}});
	};
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{shape("s1", "rectangle", 100, 100, 600, 400, "#2e86de", 0), shape("s2", "ellipse", 800, 150, 600, 600, "#ffd23f", 1, 1),
	                                      value(fields{{"id", "w1"}, {"words", "Harbor"}, {"size", 160}, {"x", 900}, {"y", 600}, {"w", 500}, {"h", 200}, {"color", "#e4572e"}, {"depth", 2}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .clear = true, .pick = "/ layer", .fills = true}))},
		                                      region{"side", items(layer_list(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                          .fields = named_like_parts, .clear = true, .pick = "/ layer", .hide = "studio::layer::show",
		                                                                          .order = "studio::layer::update", .remove = "studio::layer::delete"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	nav.current().pick("/ layer", "s1");
	matches_golden("collage-layers", {900, 420}, [&] { return drawn(nav); });
}

TEST("a collage's objects held in layers: drawn in their layers' order, a hidden layer's left out, and listed as their layers, one opened to its objects") {
	memory_source data;
	auto item = [](std::string id, std::string layer, std::string kind, double x, double y, double w, double h, std::string fill, double depth) {
		return value(fields{{"id", std::move(id)}, {"layer", std::move(layer)}, {"shape", std::move(kind)}, {"x", x}, {"y", y}, {"w", w}, {"h", h}, {"fill", std::move(fill)}, {"depth", depth}});
	};
	data.set("studio::drawing_page", "d1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "back"}, {"name", "Backdrop"}, {"depth", 0}}), value(fields{{"id", "logo"}, {"name", "logo"}, {"depth", 1}}),
	                                      value(fields{{"id", "notes"}, {"name", "Notes"}, {"depth", 2}, {"hidden", 1}})}},
	                      {"items", list{item("a", "logo", "ellipse", 600, 200, 500, 500, "#ffd23f", 0), item("b", "back", "rectangle", 100, 100, 1400, 700, "#2e86de", 0),
	                                     item("c", "logo", "rectangle", 500, 400, 300, 300, "#e4572e", 1), item("d", "notes", "rectangle", 0, 0, 1600, 900, "#1d1d1f", 0)}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Drawing", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::drawing_page", "d1"), .list = "items", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .clear = true, .pick = "/ item", .fills = true, .layers = "layers", .layer = "layer",
		                                                                         .layer_pick = "/ layer"}))},
		                                      region{"side", items(layer_list(p, {.view = p.view("studio::drawing_page", "d1"), .list = "layers", .width = "width", .height = "height",
		                                                                          .fields = named_like_parts, .clear = true, .pick = "/ layer", .hide = "studio::layer::show",
		                                                                          .order = "studio::layer::arrange", .remove = "studio::layer::delete", .objects = "items",
		                                                                          .object_layer = "layer", .object_remove = "studio::item::delete", .object_pick = "/ item"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	nav.current().pick("/ layer", "logo");
	nav.current().kept<bool>("layers open logo").set(true);
	matches_golden("collage-held-layers", {900, 420}, [&] { return drawn(nav); });
}

TEST("a collage's shapes: a rectangle rounded and stroked, an ellipse, a path, one half there, drawn smooth from their SVG, and picked as any layer is") {
	memory_source data;
	auto shape = [](std::string id, std::string kind, double x, double y, double w, double h, std::string fill, std::string stroke, double width, double corners = 0,
	                double opacity = 1, std::string points = "") {
		return value(fields{{"id", std::move(id)}, {"shape", std::move(kind)}, {"x", x}, {"y", y}, {"w", w}, {"h", h}, {"fill", std::move(fill)}, {"stroke", std::move(stroke)},
		                    {"stroke_width", width}, {"corners", corners}, {"opacity", opacity}, {"points", std::move(points)}});
	};
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{shape("s1", "rectangle", 100, 100, 600, 400, "#2e86de", "#1d1d1f", 12, 0.1), shape("s2", "ellipse", 800, 150, 600, 300, "#ffd23f", "none", 0),
	                                      shape("s3", "rectangle", 500, 400, 600, 300, "#e4572e", "", 0, 0, 0.5),
	                                      shape("s4", "path", 200, 600, 500, 250, "none", "#2ecc71", 16, 0, 1, "M 0 1 C 0.25 0 0.75 0 1 1")}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .place = "studio::layer::place", .pick = "/ layer", .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	nav.current().pick("/ layer", "s2");
	matches_golden("collage-shapes", {900, 560}, [&] { return drawn(nav); });
}

TEST("harmonized pictures fill what's under them together: past the outermost, each one's color carries on, and between two they cross from one to the other") {
	std::filesystem::path folder = std::filesystem::temp_directory_path() / "uione-harmonize-across-test";
	std::filesystem::create_directories(folder);
	auto solid = [&](const std::string& name, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
		std::vector<std::uint8_t> pixels(100 * 100 * 4);
		for (std::size_t i = 0; i < pixels.size(); i += 4) pixels[i] = r, pixels[i + 1] = g, pixels[i + 2] = b, pixels[i + 3] = 255;
		CHECK(one::desktop::picture::of(100, 100, std::move(pixels)).write_png(folder / name).has_value());
	};
	solid("blue.png", 40, 80, 220);
	solid("green.png", 60, 180, 90);
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	// A blue square on the left and a green one on the right, both harmonized.
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"picture", "blue.png"}, {"around", "harmonize"}, {"x", 250}, {"y", 300}, {"w", 300}, {"h", 300}}),
	                                      value(fields{{"id", "l2"}, {"picture", "green.png"}, {"around", "harmonize"}, {"x", 1050}, {"y", 300}, {"w", 300}, {"h", 300}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto drawn_now = one::desktop::offscreen::draw({900, 560}, 1, tested_theme().page, [&](one::desktop::canvas& c) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		auto root = drawn(nav).take();
		root->place({0, 0, 900, 560});
		root->paint(c);
	});
	auto at = [&](int x, int y) { return &drawn_now.rgba[(static_cast<std::size_t>(y) * 900 + x) * 4]; };
	// Left of the blue, blue; right of the green, green; halfway between, some of each.
	const std::uint8_t* left = at(70, 280);
	const std::uint8_t* right = at(830, 280);
	const std::uint8_t* middle = at(450, 280);
	CHECK(std::abs(left[2] - 220) <= 12);
	CHECK(std::abs(right[1] - 180) <= 12);
	CHECK(middle[2] > 100);
	CHECK(middle[2] < 200);
	CHECK(middle[1] > 90);
	CHECK(middle[1] < 170);
	matches_golden("collage-harmonize-across", {900, 560}, [&] { return drawn(nav); });
	std::filesystem::remove_all(folder);
}

TEST("around harmonize melts a picture into what's under it, its own colors spread past its edges and fading, and one that's clear stays sharp, its corners trimmed round") {
	std::filesystem::path folder = std::filesystem::temp_directory_path() / "uione-harmonize-test";
	std::filesystem::create_directories(folder);
	auto solid = [&](const std::string& name, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
		std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
		for (std::size_t i = 0; i < pixels.size(); i += 4) pixels[i] = r, pixels[i + 1] = g, pixels[i + 2] = b, pixels[i + 3] = 255;
		CHECK(one::desktop::picture::of(w, h, std::move(pixels)).write_png(folder / name).has_value());
	};
	solid("green.png", 160, 90, 60, 180, 90);
	solid("red.png", 100, 100, 220, 40, 40);
	solid("blue.png", 100, 100, 40, 80, 220);
	solid("yellow.png", 100, 100, 240, 210, 60);
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	// Two harmonized pictures overlapping on one that fills the collage, melting into it
	// and each other, and a sharp one beside them with round corners.
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "b0"}, {"picture", "green.png"}, {"x", 0}, {"y", 0}, {"w", 1600}, {"h", 900}}),
	                                      value(fields{{"id", "l1"}, {"picture", "red.png"}, {"x", 250}, {"y", 200}, {"w", 400}, {"h", 400}, {"around", "harmonize"}}),
	                                      value(fields{{"id", "l2"}, {"picture", "blue.png"}, {"x", 550}, {"y", 350}, {"w", 400}, {"h", 400}, {"around", "harmonize"}}),
	                                      value(fields{{"id", "l3"}, {"picture", "yellow.png"}, {"x", 1150}, {"y", 250}, {"w", 300}, {"h", 300}, {"corners", 0.2}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("collage-harmonize", {900, 560}, [&] { return drawn(nav); });
	std::filesystem::remove_all(folder);
}

TEST("a collage matching colors moves each picture's colors most of the way toward all of theirs, so they look like one picture") {
	std::filesystem::path folder = std::filesystem::temp_directory_path() / "uione-match-test";
	std::filesystem::create_directories(folder);
	auto solid = [&](const std::string& name, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
		std::vector<std::uint8_t> pixels(100 * 100 * 4);
		for (std::size_t i = 0; i < pixels.size(); i += 4) pixels[i] = r, pixels[i + 1] = g, pixels[i + 2] = b, pixels[i + 3] = 255;
		CHECK(one::desktop::picture::of(100, 100, std::move(pixels)).write_png(folder / name).has_value());
	};
	solid("red.png", 220, 40, 40);
	solid("blue.png", 40, 80, 220);
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	// As big as each other, so their colors meet halfway: each one's cast all the way,
	// its tones half of 0.85 of the way, its colors following its brightness: red to
	// 130.49 63.82 133.82.
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"picture", "red.png"}, {"x", 0}, {"y", 0}, {"w", 800}, {"h", 900}}),
	                                      value(fields{{"id", "l2"}, {"picture", "blue.png"}, {"x", 800}, {"y", 0}, {"w", 800}, {"h", 900}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .match_colors = true, .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto drawn_now = one::desktop::offscreen::draw({900, 560}, 1, tested_theme().page, [&](one::desktop::canvas& c) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		auto root = drawn(nav).take();
		root->place({0, 0, 900, 560});
		root->paint(c);
	});
	auto at = [&](int x, int y) { return &drawn_now.rgba[(static_cast<std::size_t>(y) * 900 + x) * 4]; };
	const std::uint8_t* red = at(250, 280);
	CHECK(std::abs(red[0] - 130) <= 2);
	CHECK(std::abs(red[1] - 64) <= 2);
	CHECK(std::abs(red[2] - 134) <= 2);
	matches_golden("collage-matched", {900, 560}, [&] { return drawn(nav); });
	std::filesystem::remove_all(folder);
}

TEST("a collage fills in the own size of a picture kept before it was, as it's turned, once, never a step undo takes back, in whichever fields keep it") {
	// Pictures kept beside the goldens, as a folder of the app's keeps them.
	struct kept_here : memory_source {
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return golden_folder / kept; }
	} data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "l1"}, {"image", "collage.png"}, {"image_width", 0}, {"image_height", 0}, {"x", 0}, {"y", 0}, {"w", 1600}, {"h", 900}}),
	                                      value(fields{{"id", "l2"}, {"image", "collage.png"}, {"image_width", 0}, {"image_height", 0}, {"turns", 1}, {"x", 0}, {"y", 0}, {"w", 56}, {"h", 90}}),
	                                      value(fields{{"id", "l3"}, {"image", "collage.png"}, {"image_width", 90}, {"image_height", 56}, {"x", 0}, {"y", 0}, {"w", 90}, {"h", 56}})}}}));
	data.on("studio::layer::update", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	// Its pictures kept in fields named otherwise, read and sent as those.
	static auto renamed = [] {
		auto kept = named_like_parts;
		kept["picture"] = "image", kept["picture_width"] = "image_width", kept["picture_height"] = "image_height";
		return kept;
	}();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height", .fields = renamed, .subject = "p1", .fills = true,
		                                                                         .key = "/ collage", .paste = true, .update = "studio::layer::update"}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	for (int i = 0; i < 200 && data.runs().size() < 2; ++i) {
		(void)drawn(nav);
		if (data.runs().size() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	(void)drawn(nav);
	CHECK(data.runs().size() == 2);
	if (data.runs().size() != 2) return;
	CHECK(data.runs()[0].command == "studio::layer::update");
	CHECK(data.runs()[0].fields["id"].as_string() == "l1");
	CHECK(data.runs()[0].fields["image_width"].as_double() == 900);
	CHECK(data.runs()[0].fields["image_height"].as_double() == 560);
	CHECK(data.runs()[0].fields["one:history"].as_string() == "apart");
	CHECK(data.runs()[1].fields["id"].as_string() == "l2");
	CHECK(data.runs()[1].fields["image_width"].as_double() == 560);
	CHECK(data.runs()[1].fields["image_height"].as_double() == 900);
}

TEST("a picture coming into a collage with nothing on it fills it, and one coming in after is its own size, fitted, in the middle, each picked once it's made") {
	memory_source data;
	data.set("studio::poster_page", "p1", value(fields{{"width", 1600}, {"height", 900}, {"layers", list{}}}));
	data.on("studio::layer::create", [](const value&, memory_source&) -> answer { return std::string("l1"); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height", .fields = named_like_parts, .pick = "/ layer", .subject = "p1",
		                                                                         .fills = true, .add = "studio::layer::create", .parent = "poster", .key = "/ layer",
		                                                                         .paste = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto& p = nav.current();
	// A screenshot handed back, as the thread that took it does: 900 by 560.
	std::string taken = (golden_folder / "collage.png").string();
	p.kept<std::string>("collage screenshot / layer").set(taken);
	(void)drawn(nav);
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	const value& first = data.runs().back().fields;
	CHECK(data.runs().back().command == "studio::layer::create");
	// 900 by 560, as wide as the collage and spilling over its top and bottom alike.
	CHECK(first["x"].as_double() == 0);
	CHECK(first["w"].as_double() == 1600);
	CHECK(std::abs(first["h"].as_double() - 560 * 1600.0 / 900) < 1e-9);
	CHECK(std::abs(first["y"].as_double() - (900 - 560 * 1600.0 / 900) / 2) < 1e-9);
	CHECK(first["poster"].as_string() == "p1");
	CHECK(first["picture_width"].as_double() == 900);
	CHECK(first["picture_height"].as_double() == 560);
	CHECK(data.uploads() == std::vector<std::string>{taken});
	// Picked once it's made, ready to be worked on.
	CHECK(p.picked_id("/ layer") == "l1");
	// Once there's something on it, the next is as big as it is, in the middle, on top.
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900}, {"layers", list{value(fields{{"id", "l1"}, {"depth", 0}, {"x", 0}, {"y", 0}, {"w", 1600}, {"h", 900}})}}}));
	p.kept<std::string>("collage screenshot / layer").set(taken);
	(void)drawn(nav);
	CHECK(data.runs().size() == 2);
	if (data.runs().size() < 2) return;
	const value& second = data.runs().back().fields;
	CHECK(second["x"].as_double() == 350);
	CHECK(second["y"].as_double() == 170);
	CHECK(second["w"].as_double() == 900);
	CHECK(second["depth"].as_double() == 1);
}

TEST("a collage is copied or saved, whole or a layer at a time, drawn away from the window, saved in the app's folder in Pictures, and keeps a preview of itself once changes settle") {
	auto pictures = std::filesystem::temp_directory_path() / ("one-pictures-" + std::to_string(::getpid()));
	std::filesystem::remove_all(pictures);
	::setenv("XDG_PICTURES_DIR", pictures.c_str(), 1);
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"title", "Harbor at dusk"}, {"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "w1"}, {"words", "Harbor"}, {"size", 96}, {"x", 120}, {"y", 600}, {"w", 300}, {"h", 120}})}}}));
	data.on("studio::poster::preview", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height", .fields = named_like_parts, .pick = "/ layer", .subject = "p1",
		                                                                         .fills = true, .key = "/ layer", .preview = "studio::poster::preview", .preview_into = "preview",
		                                                                         .preview_wide = 320}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto& p = nav.current();
	// The words picked and copied: drawn away from the window's thread, and put on the
	// clipboard back on it, which only a running window does.
	p.pick("/ layer", "w1");
	p.kept<std::string>("collage do / layer").set("copy picked");
	(void)drawn(nav);
	CHECK(*p.told() == "Copying…");
	// The words picked and saved: in the app's folder, named for it and the minute, as
	// big as they are, drawn and written away from the window's thread.
	p.kept<std::string>("collage do / layer").set("save picked");
	(void)drawn(nav);
	std::filesystem::path folder = pictures / "studio";
	auto written = [&] {
		std::vector<std::filesystem::path> out;
		if (std::filesystem::exists(folder)) {
			for (const auto& f : std::filesystem::directory_iterator(folder)) out.push_back(f.path());
		}
		return out;
	};
	for (int i = 0; i < 200 && written().size() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(written().size() == 1);
	if (written().size() == 1) {
		CHECK(written()[0].filename().string().starts_with("harbor-at-dusk-layer-"));
		auto read = one::desktop::picture::read(written()[0]);
		CHECK(read.has_value());
		if (read) CHECK(read->width() == 300);
	}
	// All of it saved too, beside it.
	p.kept<std::string>("collage do / layer").set("save");
	(void)drawn(nav);
	for (int i = 0; i < 200 && written().size() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(written().size() == 2);
	// Once what it's drawn from has settled, as the timer it started says, its preview
	// is sent, 320 wide, never a step undo takes back.
	CHECK(data.runs().empty());
	std::string waited = p.kept<std::string>("collage waiting / layer").peek();
	CHECK(!waited.empty());
	p.kept<std::string>("collage settled / layer").set(waited);
	(void)drawn(nav);
	CHECK(data.runs().size() == 1);
	if (!data.runs().empty()) {
		CHECK(data.runs().back().command == "studio::poster::preview");
		CHECK(data.runs().back().fields["one:history"].as_string() == "apart");
		CHECK(data.uploads().size() == 1);
	}
	::unsetenv("XDG_PICTURES_DIR");
	std::filesystem::remove_all(pictures);
}

TEST("on open, a screen opens the first row its condition holds for in its place, or else runs a command, apart from undo, and opens what it made") {
	memory_source data;
	data.set("studio::posters", "", value(fields{{"rows", list{value(fields{{"id", "p1"}, {"width", 1280}}), value(fields{{"id", "p2"}, {"width", 1920}})}}}));
	data.on("studio::poster::create", [](const value&, memory_source&) -> answer { return std::string("p9"); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Home", "/", "", [](page&) { return items(one::desktop::text("Home")); }});
	s.screens.push_back({"New", "/new", "", [](page& p) {
		                     auto posters = p.view("studio::posters");
		                     return items(on_open(p, ready(posters), {opening{.open = "/posters/:poster", .view = posters, .where = [](const value& row) { return row["width"].as_double() == 1920; }},
		                                                              opening{.run = command_button{.command = "studio::poster::create", .label = "Create", .then_open = "/posters/:poster"}}}));
	                     }});
	s.screens.push_back({"Poster", "/posters/:poster", "", [](page& p) { return items(one::desktop::text("Poster " + p.param("poster"))); }});
	navigator nav(s, data);
	nav.open("/");
	nav.open("/new");
	(void)drawn(nav);
	CHECK(nav.current().address() == "/posters/p2");
	CHECK(nav.depth() == 2);  // in the new screen's place, so going back goes home
	// None fits: one's made, and opened.
	data.set("studio::posters", "", value(fields{{"rows", list{}}}));
	nav.open("/new");
	(void)drawn(nav);
	(void)drawn(nav);
	CHECK(data.runs().size() == 1);
	// Never a step undo takes back: the person didn't do it.
	if (!data.runs().empty()) CHECK(data.runs().front().fields["one:history"].as_string() == "apart");
	CHECK(nav.current().address() == "/posters/p9");
}

TEST("work handed back from another thread is dropped when no app is running to take it") {
	bool ran = false;
	one::desktop::runtime::post([&ran] { ran = true; });
	CHECK(!ran);
}

TEST("an address isn't handed over when no copy of the app is running to take it") {
	CHECK(!one::desktop::runtime::hand_over("org.uione.tests.nobody-running", "/"));
}
