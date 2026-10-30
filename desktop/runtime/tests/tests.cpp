// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::desktop::runtime's tests: addresses and keys made as the web makes them,
// pages opened by address and left, views read live from a source, commands run and
// turned down, and a whole window of a small tracker drawn and compared with its
// golden image, all on the in-memory source.

#include <cstdlib>
#include <filesystem>
#include <format>
#include <set>
#include <fstream>
#include <thread>

#include "check.hpp"
#include "app_internal.hpp"
#include "offscreen.hpp"
#include "photos.hpp"
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
	    {"stroke_width", "stroke_width"}, {"opacity", "opacity"}, {"hidden", "hidden"}, {"rotation", "rotation"}};

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

TEST("a card's title is set in the theme's text face, strong, not in its headings' face, which the page's title keeps") {
	memory_source data;
	data.set("projects::project_page", "atlas", value(fields{{"boards", list{value(fields{{"id", "atlas-main"}, {"title", "Main"}}), value(fields{{"id", "atlas-ops"}, {"title", "Operations"}})}}}));
	auto drawn_in = [&](const std::string& heading_family, bool titled) {
		one::desktop::theme look = tested_theme();
		look.heading_family = heading_family;
		site s{.name = "tracker", .id = "org.uione.runtime-tests"};
		s.look = look;
		s.screens.push_back({titled ? "Atlas" : "tracker", "/:project", "", [](page& p) {
			                     return items(cards(p, {.view = p.view("projects::project_page", p.param("project")), .list = "boards", .columns = {{"title", "Title"}}}));
		                     }});
		navigator nav(s, data);
		nav.open("/atlas");
		std::unique_ptr<one::desktop::element> root;
		return one::desktop::offscreen::draw({900, 400}, 1, look.page, [&](one::desktop::canvas& c) {
			one::desktop::detail::theme_in_use() = &look;
			root = drawn(nav).take();
			root->place({0, 0, 900, 400});
			root->paint(c);
		});
	};
	// The cards alone are drawn the same whatever face headings are in.
	CHECK(one::desktop::offscreen::differences(drawn_in("", false), drawn_in("Noto Serif", false), 3) == 0);
	// The page's title over them is a heading, and changes with it.
	CHECK(one::desktop::offscreen::differences(drawn_in("", true), drawn_in("Noto Serif", true), 3) > 0);
	one::desktop::detail::theme_in_use() = &tested_theme();
}

TEST("cards as a mosaic: every eight two blocks of a tall tile, a wide one and two small, the second mirrored, each picture filling its tile around its middle, its title over its foot") {
	auto folder = std::filesystem::temp_directory_path() / "uione-mosaic";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	// Each picture its color on the left and a lighter one on the right, some wide and
	// some tall, so where it's cropped shows.
	auto picture = [&](const std::string& name, int w, int h, std::uint32_t rgb) {
		std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				std::uint8_t* at = &pixels[(static_cast<std::size_t>(y) * w + x) * 4];
				int lift = x < w / 2 ? 0 : 70;
				at[0] = static_cast<std::uint8_t>(std::min(255, static_cast<int>((rgb >> 16) & 0xff) + lift));
				at[1] = static_cast<std::uint8_t>(std::min(255, static_cast<int>((rgb >> 8) & 0xff) + lift));
				at[2] = static_cast<std::uint8_t>(std::min(255, static_cast<int>(rgb & 0xff) + lift));
				at[3] = 255;
			}
		}
		CHECK(one::desktop::picture::of(w, h, std::move(pixels)).write_png(folder / name).has_value());
	};
	const std::uint32_t colors[] = {0x2457d6, 0x1a7f37, 0xc2312d, 0x9a6700, 0x6e40c9, 0x0f766e, 0xb4357a, 0x3f4a5a, 0x2d6a9f, 0x8a5a2b, 0x4f7c2a};
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	list clippings;
	for (int i = 0; i < 11; ++i) {
		std::string name = std::format("clipping-{}.png", i + 1);
		picture(name, i % 2 == 0 ? 160 : 90, i % 2 == 0 ? 90 : 160, colors[i]);
		clippings.push_back(value(fields{{"id", std::format("c{}", i + 1)}, {"name", std::format("Clipping {}", i + 1)}, {"image", name}}));
	}
	data.set("studio::clippings_page", "", value(fields{{"clippings", clippings}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Clippings", "/", "", [](page& p) {
		                     return items(cards(p, {.view = p.view("studio::clippings_page"), .list = "clippings", .columns = {{"name", "Name"}}, .link = "/clippings/:clipping", .picture = "image", .mosaic = true}));
	                     }});
	s.screens.push_back({"Clipping", "/clippings/:clipping", "", [](page&) { return items(); }});
	navigator nav(s, data);
	nav.open("/");
	matches_golden("cards-mosaic", {900, 1820}, [&] { return drawn(nav); });
	// Wider, it's the same tiles, larger, as tall as they are wide.
	auto root = drawn(nav).take();
	root->place({0, 0, 900, 1500});
	// The mosaic: what takes the keyboard and grows as it widens, with nothing made inside
	// it until it's drawn.
	std::function<one::desktop::element*(one::desktop::element&)> find = [&](one::desktop::element& e) -> one::desktop::element* {
		if (e.focusable() && e.children.empty() && e.measure(1200).height > e.measure(600).height) return &e;
		for (auto& child : e.children) {
			if (auto* in = find(*child)) return in;
		}
		return nullptr;
	};
	one::desktop::element* mosaic = find(*root);
	CHECK(mosaic != nullptr);
	if (mosaic) {
		CHECK(mosaic->measure(1200).height > mosaic->measure(600).height);
		mosaic->place({0, 0, 1200, mosaic->measure(1200).height});
		float side = (1200 - 32) / 3.f;
		auto opened = [&](float x, float y) {
			nav.open("/");
			auto again = drawn(nav).take();
			again->place({0, 0, 1200, 1500});
			auto* m = find(*again);
			m->place({0, 0, 1200, m->measure(1200).height});
			CHECK(m->click({x, y}));
			return nav.current().address();
		};
		// The first card's tile is tall at the left, the second's wide at the top right,
		// and the eleventh's the third of the second eight.
		CHECK(opened(10, side * 1.5f) == "/clippings/c1");
		CHECK(opened(side + 26, 10) == "/clippings/c2");
		auto eleventh = mosaic_tile(10);
		CHECK(opened(eleventh.column * (side + 16) + 10, eleventh.row * (side + 16) + 10) == "/clippings/c11");
	}
	std::filesystem::remove_all(folder);
}

TEST("cards take pictures chosen or dropped, each a record made by their add, its file in their picture and its own size, name, bytes and date in the fields that keep them") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-pictures";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	auto file = folder / "harbor.png";
	one::desktop::offscreen::write_png(file, {6, 4, std::vector<std::uint8_t>(6 * 4 * 4, 200)});
	using namespace std::chrono;
	std::filesystem::last_write_time(file, clock_cast<file_clock>(sys_days{2026y / 10 / 12} + 8h + 30min + 15s));
	memory_source data;
	data.set("studio::room_page", "r1", value(fields{{"pieces", list{}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Pieces", "/:room", "", [](page& p) {
		                     return items(cards(p, {.view = p.view("studio::room_page", p.param("room")),
		                                            .list = "pieces",
		                                            .columns = {{"name", "Name"}},
		                                            .picture = "image",
		                                            .add = "studio::piece::create",
		                                            .fields = {{"picture", "image"}, {"picture_width", "picture_width"}, {"picture_height", "picture_height"}, {"name", "name"}, {"picture_bytes", "picture_bytes"}, {"picture_date", "picture_date"}},
		                                            .given = {{"room", p.param("room")}},
		                                            .key = "/:room cards pieces",
		                                            .paste = true}));
	                     }});
	navigator nav(s, data);
	nav.open("/r1");
	built(nav);
	// Two chosen at once by an add pictures button, as the desktop's file chooser hands them over.
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({file.string(), (folder / "missing.png").string()});
	built(nav);
	CHECK(data.runs().size() == 1);
	const value& made = data.runs().front().fields;
	CHECK(data.runs().front().command == "studio::piece::create");
	CHECK(made["room"].as_string() == "r1");
	CHECK(made["image"].as_string() == "uploads/harbor.png");
	CHECK(made["picture_width"].as_integer() == 6 && made["picture_height"].as_integer() == 4);
	CHECK(made["name"].as_string() == "harbor");
	CHECK(made["picture_bytes"].as_integer() == static_cast<long long>(std::filesystem::file_size(file)));
	CHECK(made["picture_date"].as_string() == "2026-10-12T08:30:15Z");
	CHECK(nav.current().told().peek() == "missing.png isn't a picture this can read.");
	// The later of several is joined to the first, one step to undo.
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({file.string(), file.string()});
	built(nav);
	CHECK(data.runs().size() == 3);
	CHECK(data.runs()[1].fields["one:history"].is_null() && data.runs()[2].fields["one:history"].as_string() == "joined");
	std::filesystem::remove_all(folder);
}

namespace {

	// Cards that take pictures, kept as a folder keeps them, by what's in them.
	struct kept_by_content : memory_source {
		std::optional<std::expected<std::string, std::string>> keep(const std::filesystem::path& file) override {
			std::ifstream in(file, std::ios::binary);
			std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			return "files/" + std::to_string(std::hash<std::string>{}(bytes));
		}
	};
	site taking_cards() {
		site s{.name = "studio", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Pieces", "/:room", "", [](page& p) {
			                     return items(cards(p, {.view = p.view("studio::room_page", p.param("room")),
			                                            .list = "pieces",
			                                            .columns = {{"name", "Name"}},
			                                            .picture = "image",
			                                            .add = "studio::piece::create",
			                                            .fields = {{"picture", "image"}, {"name", "name"}},
			                                            .given = {{"room", p.param("room")}},
			                                            .key = "/:room cards pieces",
			                                            .paste = true}));
		                     }});
		return s;
	}

}  // namespace

TEST("cards keep each picture once: one whose file is a card's already, or one before it in the same batch, isn't made again, and they say how many were") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-once";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	auto picture = [&](const std::string& name, std::uint8_t shade) {
		one::desktop::offscreen::write_png(folder / name, {6, 4, std::vector<std::uint8_t>(6 * 4 * 4, shade)});
		return (folder / name).string();
	};
	kept_by_content data;
	auto harbor = picture("harbor.png", 200), dusk = picture("dusk.png", 90), pier = picture("pier.png", 30);
	std::filesystem::copy_file(dusk, folder / "dusk copy.png");
	data.set("studio::room_page", "r1", value(fields{{"pieces", list{value(fields{{"id", "n0"}, {"name", "harbor"}, {"image", **data.keep(harbor)}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Pieces", "/:room", "", [](page& p) {
		                     return items(cards(p, {.view = p.view("studio::room_page", p.param("room")),
		                                            .list = "pieces",
		                                            .columns = {{"name", "Name"}},
		                                            .picture = "image",
		                                            .add = "studio::piece::create",
		                                            .fields = {{"picture", "image"}, {"name", "name"}},
		                                            .given = {{"room", p.param("room")}},
		                                            .key = "/:room cards pieces",
		                                            .paste = true}));
	                     }});
	navigator nav(s, data);
	nav.open("/r1");
	built(nav);
	// Harbor's in already, and the second dusk is the first again, by what's in it, whatever it's called.
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({harbor, dusk, (folder / "dusk copy.png").string(), pier});
	built(nav);
	std::vector<std::string> made;
	for (const auto& run : data.runs()) made.push_back(run.fields["name"].as_string());
	CHECK((made == std::vector<std::string>{"dusk", "pier"}));
	CHECK(nav.current().told().peek() == "Added 2 pictures, 2 were already in.");
	// One alone that's in already.
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({harbor});
	built(nav);
	CHECK(data.runs().size() == 2);
	CHECK(nav.current().told().peek() == "That picture was already in.");
	CHECK(pictures_already_said(0, 9) == "All 9 pictures were already in.");
	CHECK(pictures_already_said(1, 1) == "Added 1 picture, 1 was already in.");
	std::filesystem::remove_all(folder);
}

TEST("a folder handed to cards brings in every picture in it and in its folders, by what's in it, past hidden ones, what isn't a picture, and links out of it") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-folder";
	std::filesystem::remove_all(folder);
	auto summer = folder / "Summer";
	std::filesystem::create_directories(summer / "beach" / "evening");
	std::filesystem::create_directories(summer / ".thumbnails");
	std::filesystem::create_directories(folder / "elsewhere");
	std::uint8_t shade = 10;
	auto picture = [&](const std::filesystem::path& at) { one::desktop::offscreen::write_png(at, {6, 4, std::vector<std::uint8_t>(6 * 4 * 4, shade += 20)}); };
	picture(summer / "harbor.png");
	picture(summer / "beach" / "dunes.png");
	picture(summer / "beach" / "evening" / "dusk");  // a picture, whatever it's called
	picture(summer / ".thumbnails" / "harbor.png");
	picture(summer / ".hidden.png");
	picture(folder / "elsewhere" / "far.png");
	std::ofstream(summer / "notes.png") << "not a picture";
	std::ofstream(summer / "list.txt") << "harbor, dunes";
	std::filesystem::create_symlink(folder / "elsewhere" / "far.png", summer / "far.png");
	std::filesystem::create_directory_symlink(folder / "elsewhere", summer / "elsewhere");
	std::filesystem::create_symlink(summer / "harbor.png", summer / "beach" / "harbor again.png");
	kept_by_content data;
	data.set("studio::room_page", "r1", value(fields{{"pieces", list{}}}));
	auto s = taking_cards();
	navigator nav(s, data);
	nav.open("/r1");
	built(nav);
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({summer.string()});
	built(nav);
	std::multiset<std::string> made;
	for (const auto& run : data.runs()) made.insert(run.fields["name"].as_string());
	// The link to harbor inside it is harbor again, so it's in once.
	CHECK((made == std::multiset<std::string>{"harbor", "dunes", "dusk"}));
	CHECK(nav.current().told().peek() == "Added 3 pictures, 1 was already in.");
	// None at all.
	std::filesystem::create_directories(folder / "Empty");
	std::ofstream(folder / "Empty" / "list.txt") << "nothing";
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({(folder / "Empty").string()});
	built(nav);
	CHECK(nav.current().told().peek() == "There are no pictures in Empty.");
	std::filesystem::remove_all(folder);
}

TEST("a folder of thousands of pictures is gone through away from the window, which keeps being built, a picture at a time, saying how far it's got") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-many";
	std::filesystem::remove_all(folder);
	constexpr int count = 3000;
	for (int i = 0; i < count; ++i) {
		auto at = folder / std::format("roll-{}", i / 500);
		if (i % 500 == 0) std::filesystem::create_directories(at);
		std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
		pixels[0] = static_cast<std::uint8_t>(i), pixels[1] = static_cast<std::uint8_t>(i >> 8);
		one::desktop::offscreen::write_png(at / std::format("p{}.png", i), {4, 4, std::move(pixels)});
	}
	kept_by_content data;
	data.set("studio::room_page", "r1", value(fields{{"pieces", list{}}}));
	auto s = taking_cards();
	navigator nav(s, data);
	nav.open("/r1");
	built(nav);
	one::desktop::runtime::hold_posted(true);
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({folder.string()});
	built(nav);
	int frames = 0;
	std::size_t most_waiting = 0;
	std::string said_while;
	for (; data.runs().size() < count && frames < 200000; ++frames) {
		if (one::desktop::runtime::run_posted(1) == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
		if (std::string now = nav.current().told().peek(); now.starts_with("Adding")) said_while = now;
		most_waiting = std::max(most_waiting, nav.current().kept<std::vector<coming_picture>>("pictures come /:room cards pieces").peek().size());
		built(nav);
	}
	for (int i = 0; i < 2000 && one::desktop::runtime::run_posted() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	built(nav);
	one::desktop::runtime::hold_posted(false);
	CHECK(data.runs().size() == count);
	CHECK(frames > count);
	// What's waiting for the window is a picture or two, never the folder.
	CHECK_SAYING(most_waiting < 50, std::to_string(most_waiting));
	CHECK(said_while.starts_with("Adding pictures from uione-cards-many, "));
	CHECK(nav.current().told().peek() == std::format("Added {} pictures.", count));
	std::filesystem::remove_all(folder);
}

TEST("cards take a photo's date and camera as it says them, and its size the way up it was held, and the small copy they draw is that way up too") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-photos";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	// 40 × 20, red on its left, held turned a quarter clockwise, so it's 20 × 40, red at its top.
	std::vector<std::uint8_t> pixels;
	for (int y = 0; y < 20; ++y) {
		for (int x = 0; x < 40; ++x) pixels.insert(pixels.end(), {static_cast<std::uint8_t>(x < 20 ? 255 : 0), 0, static_cast<std::uint8_t>(x < 20 ? 0 : 255), 255});
	}
	auto bytes = photos::jpeg(40, 20, pixels, photos::exif({.orientation = 6, .make = "FUJIFILM", .model = "X-T5", .taken = "2026:10:12 08:30:15", .offset = "+02:00"}));
	auto file = folder / "harbor.jpg";
	std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	memory_source data;
	data.set("studio::room_page", "r1", value(fields{{"pieces", list{}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Pieces", "/:room", "", [](page& p) {
		                     return items(cards(p, {.view = p.view("studio::room_page", p.param("room")),
		                                            .list = "pieces",
		                                            .columns = {{"name", "Name"}},
		                                            .picture = "image",
		                                            .add = "studio::piece::create",
		                                            .fields = {{"picture", "image"}, {"picture_width", "picture_width"}, {"picture_height", "picture_height"}, {"picture_date", "picture_date"}, {"picture_camera", "picture_camera"}},
		                                            .given = {{"room", p.param("room")}},
		                                            .key = "/:room cards pieces",
		                                            .paste = true}));
	                     }});
	navigator nav(s, data);
	nav.open("/r1");
	built(nav);
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set({file.string()});
	built(nav);
	CHECK(data.runs().size() == 1);
	if (data.runs().empty()) return;
	const value& made = data.runs().front().fields;
	CHECK(made["picture_width"].as_integer() == 20 && made["picture_height"].as_integer() == 40);
	CHECK(made["picture_date"].as_string() == "2026-10-12T06:30:15Z");
	CHECK(made["picture_camera"].as_string() == "FUJIFILM X-T5");
	// Without a field for it, it isn't kept.
	CHECK(!made.fields.contains("picture_bytes"));
	// A card's small copy of a large one, named by its hash, is made upright, and kept by
	// a name of its own, so one made before pictures were turned isn't drawn on its side.
	std::vector<std::uint8_t> large;
	for (int y = 0; y < 700; ++y) {
		for (int x = 0; x < 1400; ++x) large.insert(large.end(), {static_cast<std::uint8_t>(x < 700 ? 255 : 0), 0, static_cast<std::uint8_t>(x < 700 ? 0 : 255), 255});
	}
	auto taken = photos::jpeg(1400, 700, large, photos::exif({.orientation = 6}));
	std::string hash(64, 'b');
	std::filesystem::create_directories(folder / "files");
	std::ofstream(folder / "files" / hash, std::ios::binary).write(reinterpret_cast<const char*>(taken.data()), static_cast<std::streamsize>(taken.size()));
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} there;
	there.in = folder;
	there.set("studio::room_page", "r1", value(fields{{"pieces", list{value(fields{{"id", "c1"}, {"name", "Harbor"}, {"image", "files/" + hash}})}}}));
	site shown{.name = "studio", .id = "org.uione.runtime-tests"};
	shown.look = tested_theme();
	shown.screens.push_back({"Pieces", "/:room", "", [](page& p) {
		                         return items(cards(p, {.view = p.view("studio::room_page", p.param("room")), .list = "pieces", .columns = {{"name", "Name"}}, .picture = "image"}));
	                         }});
	navigator cards_nav(shown, there);
	cards_nav.open("/r1");
	(void)drawn(cards_nav);
	auto small = one::desktop::picture::read(folder / "files" / "small" / (hash + "-640-upright.png"));
	CHECK(small.has_value());
	if (small) {
		CHECK(small->width() == 320 && small->height() == 640);
		CHECK(small->rgba()[0] > 180 && small->rgba()[2] < 80);  // red, its stored left, at its top
	}
	CHECK(!std::filesystem::exists(folder / "files" / "small" / (hash + "-640.png")));
	std::filesystem::remove_all(folder);
}

TEST("pictures coming into cards are measured and kept away from the window, which is built again and again while they come in, saying how far they've got") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-coming";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	// As large as a camera's.
	auto file = folder / "harbor.png";
	one::desktop::offscreen::write_png(file, {4032, 3024, std::vector<std::uint8_t>(std::size_t{4032} * 3024 * 4, 200)});
	memory_source data;
	data.set("studio::room_page", "r1", value(fields{{"pieces", list{}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Pieces", "/:room", "", [](page& p) {
		                     return items(cards(p, {.view = p.view("studio::room_page", p.param("room")),
		                                            .list = "pieces",
		                                            .columns = {{"name", "Name"}},
		                                            .picture = "image",
		                                            .add = "studio::piece::create",
		                                            .fields = {{"picture", "image"}, {"picture_width", "picture_width"}, {"picture_height", "picture_height"}},
		                                            .key = "/:room cards pieces",
		                                            .paste = true}));
	                     }});
	navigator nav(s, data);
	nav.open("/r1");
	built(nav);
	one::desktop::runtime::hold_posted(true);
	nav.current().kept<std::vector<std::string>>("pictures chosen /:room cards pieces").set(std::vector<std::string>(8, file.string()));
	built(nav);
	// Nothing measured or made while the window was built: it's all away.
	CHECK(data.runs().empty());
	int frames = 0;
	std::vector<std::string> said;
	for (; data.runs().size() < 8 && frames < 5000; ++frames) {
		if (one::desktop::runtime::run_posted(1) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (std::string now = nav.current().told().peek(); !now.empty() && (said.empty() || said.back() != now)) said.push_back(now);
		built(nav);
	}
	for (int i = 0; i < 200 && one::desktop::runtime::run_posted() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	built(nav);
	CHECK(frames > 8);
	CHECK(data.runs().size() == 8);
	if (data.runs().size() == 8) CHECK(data.runs().back().fields["picture_width"].as_integer() == 4032 && data.runs().back().fields["picture_height"].as_integer() == 3024);
	CHECK(!said.empty() && said.front() == "Adding 1 of 8 pictures…" && said.back() == "Adding 8 of 8 pictures…");
	// And once they're in, it's no longer said.
	CHECK(nav.current().told().peek().empty());
	one::desktop::runtime::hold_posted(false);
	std::filesystem::remove_all(folder);
}

TEST("a card's picture is drawn from a smaller copy, made once and kept beside the file it's of when that's named by its hash") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-small";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder / "files");
	std::string hash(64, 'a');
	one::desktop::offscreen::write_png(folder / "files" / hash, {2000, 1000, std::vector<std::uint8_t>(std::size_t{2000} * 1000 * 4, 120)});
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	data.set("studio::room_page", "r1", value(fields{{"pieces", list{value(fields{{"id", "c1"}, {"name", "Harbor"}, {"image", "files/" + hash}})}}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Pieces", "/:room", "", [](page& p) {
		                     return items(cards(p, {.view = p.view("studio::room_page", p.param("room")), .list = "pieces", .columns = {{"name", "Name"}}, .picture = "image"}));
	                     }});
	navigator nav(s, data);
	nav.open("/r1");
	(void)drawn(nav);
	auto small = one::desktop::picture::read(folder / "files" / "small" / (hash + "-640.png"));
	CHECK(small.has_value());
	if (small) CHECK(small->width() == 640 && small->height() == 320);
	std::filesystem::remove_all(folder);
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

TEST("a toolbar is a thin strip while every group it shows is a single row of small tools, and full with any other") {
	memory_source data;
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	static bool large = false;
	s.screens.push_back({"Studio", "/", "", [](page& p) {
		                     std::vector<one::desktop::view> groups;
		                     groups.push_back(group(p, "View", true, true, [&] {
			                     return tools(toggle(p, "/ a", "A", "eye", std::vector<std::string>{}), toggle(p, "/ b", "B", "fit", std::vector<std::string>{}));
		                     }, 2));
		                     if (large) groups.push_back(group(p, "Poster", false, false, [&] { return tools(command(p, {.command = "studio::poster::create", .label = "New", .icon = "add"})); }));
		                     return workspace(p, region{"toolbar", along(std::move(groups))}, region{"main", items(text({words("Work")}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	// The height of the View group as it's drawn, its name under its tools or not.
	auto view_height = [&] {
		auto root = built(nav);
		float found = 0;
		std::function<void(one::desktop::element&)> look = [&](one::desktop::element& e) {
			if (e.key == "group one row") found = e.frame.height;
			for (auto& c : e.children) look(*c);
		};
		look(*root);
		return found;
	};
	float thin = view_height();
	large = true;
	float full = view_height();
	large = false;
	CHECK(thin > 0);
	CHECK(thin + 8 < full);
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

TEST("a paper's insert button puts its block where the caret is: between a block's halves from its middle, as one step") {
	memory_source data;
	list passages{value(fields{{"id", "b1"}, {"kind", "text"}, {"words", "One two"}, {"position", 1}, {"align", "left"}})};
	data.set("studio::notes_page", "p1", value(fields{{"passages", passages}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Notes", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(paper(p, {.view = p.view("studio::notes_page", "p1"), .list = "passages", .fields = {{"words", "words"}, {"kind", "kind"}, {"position", "position"}},
		                                                                        .write = "studio::passage::write", .add = "studio::passage::create", .key = "/ paper", .fills = true}))});
	                     }});
	navigator nav(s, data);
	nav.open("/");
	auto root = built(nav);
	(void)one::desktop::offscreen::draw({900, 600}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
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
	// Opened, the caret's at the end; three to the left, it's before "two".
	for (int n = 0; n < 3; ++n) CHECK(page->handle_key(one::desktop::key_event{.symbol = one::desktop::keys::left, .text = ""}));
	nav.current().kept<std::optional<value>>("paper insert / paper").set(value(fields{{"kind", "math"}}));
	(void)built(nav);
	CHECK(data.runs().size() == 3);
	if (data.runs().size() < 3) return;
	CHECK(data.runs()[0].command == "studio::passage::write");
	CHECK(data.runs()[0].fields["words"].as_string() == "One ");
	CHECK(data.runs()[1].fields["kind"].as_string() == "math");
	CHECK(data.runs()[1].fields["position"].as_double() == 2);
	CHECK(data.runs()[1].fields["one:history"].as_string() == "joined");
	CHECK(data.runs()[2].fields["kind"].as_string() == "text");
	CHECK(data.runs()[2].fields["words"].as_string() == "two");
	CHECK(data.runs()[2].fields["position"].as_double() == 3);
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

TEST("a collage's layer is turned by a drag just past a corner, drawn turned as it goes, and once turned is picked, drawn and resized in its own frame") {
	memory_source data;
	auto page_turned = [&](double rotation) {
		data.set("studio::poster_page", "p1",
		         value(fields{{"width", 1600}, {"height", 900},
		                      {"layers", list{value(fields{{"id", "l1"}, {"shape", "rectangle"}, {"fill", "#ff0000"}, {"x", 400}, {"y", 300}, {"w", 400}, {"h", 200}, {"rotation", rotation}})}}}));
	};
	page_turned(0);
	data.on("studio::layer::rotate", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("studio::layer::place", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/", "", [](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = named_like_parts, .place = "studio::layer::place", .rotate = "studio::layer::rotate",
		                                                                         .pick = "/ layer", .fills = true}))});
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
	std::unique_ptr<one::desktop::element> root;
	one::desktop::element* board = nullptr;
	one::desktop::offscreen::image shown;
	auto again = [&] {
		root = built(nav);
		board = drawing(*root);
		shown = one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	};
	again();
	CHECK(board != nullptr);
	if (!board) return;
	auto area = board->frame;
	float scale = std::min((area.width - 64) / 1600.f, (area.height - 64) / 900.f);
	one::desktop::point origin{area.x + (area.width - 1600 * scale) / 2, area.y + (area.height - 900 * scale) / 2};
	auto on = [&](double x, double y) { return one::desktop::point{origin.x + static_cast<float>(x) * scale, origin.y + static_cast<float>(y) * scale}; };
	auto red = [&](double x, double y) {
		auto at = on(x, y);
		const auto* px = &shown.rgba[static_cast<std::size_t>((static_cast<int>(at.y) * shown.width + static_cast<int>(at.x)) * 4)];
		return px[0] > 200 && px[1] < 60 && px[2] < 60;
	};
	auto press = [&](one::desktop::point at) {
		again();
		bool took = board->press(at, 1);
		board->release(at, board);
		return took;
	};
	CHECK(red(450, 350));
	CHECK(!red(600, 250));
	// Picked, then dragged from just past its lower right corner a quarter of the way
	// round its middle: drawn standing up as it goes, and kept a quarter turned, where
	// it was.
	press(on(600, 400));
	CHECK(nav.current().picked_id("/ layer") == "l1");
	again();
	one::desktop::point middle = on(600, 400), from = on(800, 500);
	from.x += 16, from.y += 16;
	one::desktop::point to{middle.x - (from.y - middle.y), middle.y + (from.x - middle.x)};
	CHECK(board->press(from, 1));
	board->drag(to);
	shown = one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	CHECK(red(600, 250));
	CHECK(!red(450, 350));
	board->release(to, board);
	CHECK(data.runs().size() == 1);
	if (data.runs().size() != 1) return;
	CHECK(data.runs().back().command == "studio::layer::rotate");
	const auto& sent = data.runs().back().fields;
	CHECK(std::abs(sent["rotation"].as_double() - 90) < 0.5);
	CHECK(std::abs(sent["x"].as_double() - 400) < 0.01);
	CHECK(std::abs(sent["y"].as_double() - 300) < 0.01);
	// Turned, it's drawn standing up, and picked where it is, not where its box was.
	page_turned(90);
	press(on(1400, 800));
	CHECK(nav.current().picked_id("/ layer").empty());
	CHECK(red(600, 250));
	CHECK(red(600, 560));
	CHECK(!red(450, 400));
	press(on(450, 400));
	CHECK(nav.current().picked_id("/ layer").empty());
	press(on(600, 250));
	CHECK(nav.current().picked_id("/ layer") == "l1");
	// Its own lower right corner, turned to 500 600, dragged 40 left and down: 40 longer
	// and wider in its own frame, and its own top left corner where it was shown, at 700
	// 200.
	again();
	CHECK(board->press(on(500, 600), 1));
	board->drag(on(470, 640));
	board->release(on(460, 640), board);
	CHECK(data.runs().size() == 2);
	if (data.runs().size() != 2) return;
	CHECK(data.runs().back().command == "studio::layer::place");
	const auto& placed = data.runs().back().fields;
	double x = placed["x"].as_double(), y = placed["y"].as_double(), w = placed["w"].as_double(), h = placed["h"].as_double();
	CHECK(std::abs(w - 440) < 0.01);
	CHECK(std::abs(h - 240) < 0.01);
	// Turned a quarter about its middle, its top left is its middle less half its height
	// across and plus half its width up.
	CHECK(std::abs(x + w / 2 + h / 2 - 700) < 0.01);
	CHECK(std::abs(y + h / 2 - w / 2 - 200) < 0.01);
}

TEST("a collage's group turned by a drag past its corner turns all it holds about its middle, a path by its points, in one step") {
	memory_source data;
	data.set("studio::poster_page", "p1",
	         value(fields{{"width", 1600}, {"height", 900},
	                      {"layers", list{value(fields{{"id", "g"}, {"x", 0}, {"y", 0}, {"w", 0}, {"h", 0}}),
	                                      value(fields{{"id", "a"}, {"shape", "rectangle"}, {"fill", "#2e86de"}, {"x", 100}, {"y", 100}, {"w", 100}, {"h", 100}, {"group", "g"}}),
	                                      value(fields{{"id", "b"}, {"shape", "path"}, {"points", "M 0 0 L 1 1"}, {"stroke", "#1d1d1f"}, {"stroke_width", 4}, {"x", 300}, {"y", 100},
	                                                   {"w", 100}, {"h", 100}, {"group", "g"}})}}}));
	data.on("studio::layer::rotate", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	data.on("studio::layer::reshape", [](const value& f, memory_source&) -> answer { return f["id"].as_string(); });
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	auto parts = named_like_parts;
	parts["group"] = "group";
	s.screens.push_back({"Poster", "/", "", [parts](page& p) {
		                     return workspace(p, region{"main", items(collage(p, {.view = p.view("studio::poster_page", "p1"), .list = "layers", .width = "width", .height = "height",
		                                                                         .fields = parts, .place = "studio::layer::place", .reshape = "studio::layer::reshape",
		                                                                         .rotate = "studio::layer::rotate", .group = "studio::layer::group", .pick = "/ layer",
		                                                                         .fills = true}))});
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
	CHECK(board->press(on(150, 150), 1));
	board->release(on(150, 150), board);
	CHECK(nav.current().picked_id("/ layer") == "g");
	// Around all it holds, 100 100 to 400 200: a quarter turn about 250 150.
	root = built(nav);
	board = drawing(*root);
	(void)one::desktop::offscreen::draw({900, 900}, 1, tested_theme().page, [&](one::desktop::canvas& c) { root->paint(c); });
	one::desktop::point middle = on(250, 150), from = on(400, 200);
	from.x += 16, from.y += 16;
	one::desktop::point to{middle.x - (from.y - middle.y), middle.y + (from.x - middle.x)};
	CHECK(board->press(from, 1));
	board->drag(to);
	board->release(to, board);
	CHECK(data.runs().size() == 2);
	if (data.runs().size() != 2) return;
	std::map<std::string, const memory_source::ran*> by;
	for (const auto& r : data.runs()) by[r.fields["id"].as_string()] = &r;
	CHECK(by["a"]->command == "studio::layer::rotate");
	CHECK(std::abs(by["a"]->fields["rotation"].as_double() - 90) < 0.5);
	CHECK(std::abs(by["a"]->fields["x"].as_double() - 200) < 0.5);
	CHECK(std::abs(by["a"]->fields["y"].as_double() - 0) < 0.5);
	// The path's ends, 300 100 and 400 200, are at 300 200 and 200 300.
	CHECK(by["b"]->command == "studio::layer::reshape");
	CHECK(std::abs(by["b"]->fields["x"].as_double() - 200) < 0.5);
	CHECK(std::abs(by["b"]->fields["y"].as_double() - 200) < 0.5);
	CHECK(by["b"]->fields["points"].as_string() == "M 1 0 L 0 1");
	CHECK(data.runs()[1].fields["one:history"].as_string() == "joined");
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

TEST("on open, a toggle is turned on or off as its press would, where its line applies") {
	memory_source data;
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Drawing", "/drawn", "", [](page& p) {
		                     return items(on_open(p, true, {opening{.turn = "/drawn layers", .on = false, .when = false}, opening{.turn = "/drawn layers", .on = true}}));
	                     }});
	navigator nav(s, data);
	nav.open("/drawn");
	(void)drawn(nav);
	CHECK(nav.current().toggled("/drawn layers"));
	(void)drawn(nav);
	CHECK(nav.current().toggled("/drawn layers"));
}

TEST("work handed back from another thread is dropped when no app is running to take it") {
	bool ran = false;
	one::desktop::runtime::post([&ran] { ran = true; });
	CHECK(!ran);
}

TEST("an address isn't handed over when no copy of the app is running to take it") {
	CHECK(!one::desktop::runtime::hand_over("org.uione.tests.nobody-running", "/"));
}

namespace {

	// An archive of posters, each with a screen of its own that steps to the one before or
	// after it, and back to them all.
	site archive_of(int count, memory_source& data, cards_spec shown) {
		list posters;
		for (int i = 0; i < count; ++i) posters.push_back(value(fields{{"id", std::format("p{}", i)}, {"title", std::format("Poster {}", i)}, {"image", ""}}));
		data.set("studio::archive", "", value(fields{{"posters", posters}}));
		site s{.name = "studio", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Posters", "/posters", "", [shown](page& p) {
			                     cards_spec spec = shown;
			                     spec.view = p.view("studio::archive");
			                     return items(cards(p, std::move(spec)));
		                     }});
		s.screens.push_back({"Poster", "/posters/:poster", "", [](page& p) {
			                     auto archive = p.view("studio::archive");
			                     return items(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster"}, "Previous", "previous", {"left"}),
			                                  step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", {"right"}),
			                                  link(p, "Posters", "/posters", "", {"escape"}));
		                     }});
		return s;
	}

	const one::desktop::key_event right_key{.symbol = one::desktop::keys::right, .text = ""};
	const one::desktop::key_event left_key{.symbol = one::desktop::keys::left, .text = ""};
	const one::desktop::key_event escape_key{.symbol = one::desktop::keys::escape, .text = ""};

	// A window's view as it's kept up to date, with nowhere to draw it.
	struct kept_window {
		one::desktop::app::state app;
		one::desktop::window::state w;
		explicit kept_window(navigator& nav) {
			w.owner = &app;
			w.size = {900, 600};
			w.look = tested_theme();
			w.build = [&nav] { return drawn(nav); };
		}
		void update() {
			one::desktop::detail::theme_in_use() = &tested_theme();
			w.needs_building = true;
			one::desktop::bring_up_to_date(w);
		}
		one::desktop::element* focused() { return one::desktop::detail::focused_in(*w.root); }
		// What's at a place in it, clicked, as a press let go there is.
		void click(one::desktop::point at) {
			for (auto* e = w.root->hit(at); e; e = e->parent) {
				if (e->click(at)) return;
			}
		}
	};

}  // namespace

TEST("a record's screen steps to the one before or after it in its list, by its keys, in its place, and not past either end") {
	memory_source data;
	auto s = archive_of(4, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
	navigator nav(s, data);
	nav.open("/posters");
	nav.open("/posters/p0");
	// At the first, there's none before it: its button can't be pressed, nor its key.
	std::size_t at_first = focusable_in(*built(nav)).size();
	CHECK(!key(nav, left_key));
	CHECK(nav.current().address() == "/posters/p0");
	CHECK(key(nav, right_key));
	CHECK(nav.current().address() == "/posters/p1");
	CHECK(focusable_in(*built(nav)).size() == at_first + 1);
	CHECK(nav.depth() == 2);
	for (int i = 0; i < 2; ++i) {
		CHECK(key(nav, right_key));
		built(nav);
	}
	CHECK(nav.current().address() == "/posters/p3");
	// Nor after the last: it doesn't go around.
	CHECK(!key(nav, right_key));
	CHECK(nav.current().address() == "/posters/p3");
	CHECK(focusable_in(*built(nav)).size() == at_first);
	CHECK(key(nav, left_key));
	CHECK(nav.current().address() == "/posters/p2");
	// And going back goes to the list, where it was opened from.
	CHECK(nav.back());
	CHECK(nav.current().address() == "/posters");
	CHECK(nav.current().where_left().opened == "/posters/p2");
}

TEST("going back to cards, or opening them again, finds them scrolled where they were left, the card opened or stepped to last chosen, in view and with the keyboard") {
	const one::desktop::key_event enter{.symbol = one::desktop::keys::enter, .text = ""};
	for (std::string shape : {"list", "pictures", "mosaic"}) {
		memory_source data;
		cards_spec shown{.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"};
		if (shape != "list") shown.picture = "image";
		shown.mosaic = shape == "mosaic";
		auto s = archive_of(40, data, shown);
		navigator nav(s, data);
		nav.open("/posters");
		kept_window window(nav);
		window.update();
		one::desktop::deliver_wheel(window.w, {450, 350}, 0, 900);
		float left = nav.current().where_left().scroll.peek();
		CHECK(left > 0);
		window.update();
		// A card in the middle of what's shown opened, then stepped on from and back to.
		window.click({450, 350});
		std::string last = nav.current().address();
		CHECK(last.starts_with("/posters/p"));
		int number = std::stoi(last.substr(std::string("/posters/p").size()));
		window.update();
		for (const auto& k : {right_key, right_key, left_key, left_key}) {
			CHECK(key(nav, k));
			window.update();
		}
		CHECK(nav.current().address() == last);
		// By the window's own back.
		CHECK(nav.back());
		window.update();
		// Where it was, or as little further as brings the card wholly into view.
		CHECK_SAYING(std::abs(nav.current().where_left().scroll.peek() - left) < 20, shape);
		auto* chosen = window.focused();
		CHECK(chosen);
		if (!chosen) continue;
		CHECK(chosen->shows_focus());
		if (shape == "pictures") CHECK((chosen->frame.y >= 0 && chosen->frame.bottom() <= 600));
		chosen->handle_key(enter);
		CHECK(nav.current().address() == last);
		// And by a link to the list, as Escape is on the record's screen.
		window.update();
		CHECK(key(nav, escape_key));
		CHECK(nav.depth() == 3);
		window.update();
		CHECK_SAYING(std::abs(nav.current().where_left().scroll.peek() - left) < 20, shape);
		chosen = window.focused();
		CHECK(chosen);
		if (!chosen) continue;
		chosen->handle_key(enter);
		CHECK(nav.current().address() == last);
		// Stepped on past what was in view, it's brought into view.
		window.update();
		for (int i = 0; i < 12; ++i) {
			CHECK(key(nav, right_key));
			window.update();
		}
		CHECK(nav.back());
		window.update();
		CHECK(nav.current().where_left().opened == std::format("/posters/p{}", number + 12));
		chosen = window.focused();
		CHECK(chosen);
		if (chosen && shape == "pictures") CHECK((chosen->frame.y >= 0 && chosen->frame.bottom() <= 600));
		if (chosen) chosen->handle_key(enter);
		CHECK(nav.current().address() == std::format("/posters/p{}", number + 12));
	}
}

TEST("holding the arrow through records with large pictures keeps the window's loop free, and reads the picture shown, not each one passed") {
	auto folder = std::filesystem::temp_directory_path() / "uione-cards-stepping";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder / "files");
	// Large enough to take far longer to read than a step, each its own file.
	one::desktop::offscreen::write_png(folder / "files" / "p0.png", {1600, 1200, std::vector<std::uint8_t>(std::size_t{1600} * 1200 * 4, 90)});
	for (int i = 1; i < 6; ++i) std::filesystem::copy_file(folder / "files" / "p0.png", folder / "files" / std::format("p{}.png", i));
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	list posters;
	for (int i = 0; i < 6; ++i) posters.push_back(value(fields{{"id", std::format("p{}", i)}, {"image", std::format("files/p{}.png", i)}}));
	data.set("studio::archive", "", value(fields{{"posters", posters}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Poster", "/posters/:poster", "", [](page& p) {
		                     auto archive = p.view("studio::archive");
		                     return items(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", {"right"}),
		                                  picture_of(p, std::format("files/{}.png", p.param("poster")), false, 400));
	                     }});
	// How long one takes to read, on this machine.
	auto started = std::chrono::steady_clock::now();
	CHECK(one::desktop::picture::read(folder / "files" / "p0.png").has_value());
	auto one_read = std::chrono::steady_clock::now() - started;
	navigator nav(s, data);
	one::desktop::runtime::hold_posted(true);
	nav.open("/posters/p0");
	built(nav);
	auto longest = std::chrono::steady_clock::duration::zero();
	for (int i = 0; i < 5; ++i) {
		auto from = std::chrono::steady_clock::now();
		CHECK(key(nav, right_key));
		built(nav);
		longest = std::max(longest, std::chrono::steady_clock::now() - from);
	}
	CHECK(nav.current().address() == "/posters/p5");
	// Each step and the window built after it took far less than reading a picture.
	CHECK(longest * 4 < one_read);
	auto shown = [&](int i) { return picture_soon(nav.current(), std::format("files/p{}.png", i)); };
	for (auto until = std::chrono::steady_clock::now() + 20 * one_read + std::chrono::seconds(5); !shown(5) && std::chrono::steady_clock::now() < until;) {
		if (one::desktop::runtime::run_posted() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		built(nav);
	}
	CHECK(shown(5).has_value());
	if (shown(5)) CHECK(shown(5)->width() == 1600);
	// Those passed while another was read were let go, not read.
	CHECK(!shown(2).has_value());
	CHECK(!shown(3).has_value());
	one::desktop::runtime::hold_posted(false);
	std::filesystem::remove_all(folder);
}

namespace {

	// A picture 1800 × 1000, red on its left, blue on its right, a green band across its
	// top: fitted in a window 900 × 600, it's 900 × 500, black above and under it.
	std::filesystem::path banded_picture(const std::filesystem::path& folder) {
		std::vector<std::uint8_t> pixels(std::size_t{1800} * 1000 * 4, 255);
		for (int y = 0; y < 1000; ++y) {
			for (int x = 0; x < 1800; ++x) {
				std::uint8_t* at = &pixels[(std::size_t(y) * 1800 + std::size_t(x)) * 4];
				bool green = y < 100;
				at[0] = !green && x < 900 ? 255 : 0;
				at[1] = green ? 255 : 0;
				at[2] = !green && x >= 900 ? 255 : 0;
			}
		}
		std::filesystem::create_directories(folder / "files");
		one::desktop::offscreen::write_png(folder / "files" / "banded.png", {1800, 1000, std::move(pixels)});
		return folder;
	}

	// The window as it's drawn now, and what color it is at a place in it.
	struct seen {
		one::desktop::offscreen::image drawn;
		std::array<int, 3> at(int x, int y) const {
			const std::uint8_t* p = &drawn.rgba[(std::size_t(y) * std::size_t(drawn.width) + std::size_t(x)) * 4];
			return {p[0], p[1], p[2]};
		}
	};
	seen looked(one::desktop::window::state& w) {
		return {one::desktop::offscreen::draw(w.size, 1, tested_theme().page, [&](one::desktop::canvas& c) {
			one::desktop::detail::theme_in_use() = &tested_theme();
			w.root->paint(c);
		})};
	}
	const std::array<int, 3> black{0, 0, 0}, red{255, 0, 0}, blue{0, 0, 255}, green{0, 255, 0};

}  // namespace

TEST("a picture alone is all of its screen, fitted on black, its bars over it until it's been still, then hidden with the pointer, and a step leaves them so") {
	auto folder = banded_picture(std::filesystem::temp_directory_path() / "uione-alone");
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	auto s = archive_of(3, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
	s.screens.back().build = [](page& p) {
		auto archive = p.view("studio::archive");
		return items(workspace(p, region{"toolbar", along(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster"}, "Previous", "previous", {"left"}),
		                                                  step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", {"right"}))},
		                          region{"main", items(picture_alone(p, "files/banded.png", "/posters/:poster collage"))}, region{"status", along(text({words("Banded")}))}));
	};
	navigator nav(s, data);
	nav.open("/posters");
	nav.open("/posters/p0");
	kept_window window(nav);
	window.update();
	window.update();
	// Just opened, its bars are over it, the picture where it is without them.
	CHECK(awake());
	auto shown = looked(window.w);
	CHECK(shown.at(450, 20) != black);
	CHECK(shown.at(450, 590) != black);
	CHECK(shown.at(200, 300) == red);
	CHECK(shown.at(700, 300) == blue);
	CHECK(one::desktop::cursor_at(window.w, {450, 300}) == one::desktop::cursor::normal);
	std::size_t with_bars = focusable_in(*window.w.root).size();
	// Still for long enough, they're gone, and so is the pointer; the picture hasn't moved.
	rest();
	window.update();
	shown = looked(window.w);
	CHECK(shown.at(450, 20) == black);
	CHECK(shown.at(450, 590) == black);
	CHECK(shown.at(450, 48) == black);
	CHECK(shown.at(200, 52) == green);
	CHECK(shown.at(200, 300) == red);
	CHECK(focusable_in(*window.w.root).size() < with_bars);
	CHECK(one::desktop::cursor_at(window.w, {450, 300}) == one::desktop::cursor::hidden);
	// A step by its key leaves them hidden, the next picture fitted.
	key_down(right_key);
	CHECK(key(nav, right_key));
	window.update();
	CHECK(nav.current().address() == "/posters/p1");
	CHECK(!awake());
	CHECK(looked(window.w).at(450, 20) == black);
	// Ctrl alone, as for the wheel, doesn't bring them back; another key does, and so
	// does the pointer moving.
	key_down({.symbol = 0xffe3, .text = "", .control = true});
	CHECK(!awake());
	key_down({.symbol = 'a', .text = "a"});
	CHECK(awake());
	window.update();
	CHECK(looked(window.w).at(450, 20) != black);
	rest();
	wake();
	CHECK(awake());
	// Gone back to them all, and opened again, they're shown.
	rest();
	CHECK(key(nav, escape_key));
	window.update();
	nav.open("/posters/p2");
	window.update();
	CHECK(awake());
	std::filesystem::remove_all(folder);
}

TEST("a side a toggle opens is a drawer over a picture alone, there while it's open whether the bars are resting or not, and closed until it's opened") {
	auto folder = banded_picture(std::filesystem::temp_directory_path() / "uione-drawer");
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	auto s = archive_of(2, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
	s.screens.back().build = [](page& p) {
		auto archive = p.view("studio::archive");
		one::desktop::view drawer;
		if (p.toggled("/posters/:poster about")) drawer = items(details(p, archive, {{"posters", "Posters"}}));
		return items(workspace(p, region{"toolbar", along(toggle(p, "/posters/:poster about", "About", "info", std::vector<std::string>{"i"}))},
		                          region{"main", items(picture_alone(p, "files/banded.png", "/posters/:poster collage"))}, region{"side", std::move(drawer), true}));
	};
	navigator nav(s, data);
	nav.open("/posters");
	nav.open("/posters/p0");
	kept_window window(nav);
	window.update();
	window.update();
	// Closed at first, the picture's right half is blue under the bars.
	CHECK(!nav.current().toggled("/posters/:poster about"));
	CHECK(looked(window.w).at(850, 300) == blue);
	// Opened by its key, it's over the picture, and stays while the bars rest.
	const one::desktop::key_event i_key{.symbol = 'i', .text = "i"};
	key_down(i_key);
	CHECK(key(nav, i_key));
	window.update();
	CHECK(looked(window.w).at(850, 300) != blue);
	rest();
	window.update();
	auto shown = looked(window.w);
	CHECK(shown.at(450, 20) == black);
	CHECK(shown.at(850, 300) != blue);
	CHECK(shown.at(200, 300) == red);
	// Closed again, it's gone with them.
	CHECK(key(nav, i_key));
	rest();
	window.update();
	CHECK(looked(window.w).at(850, 300) == blue);
	std::filesystem::remove_all(folder);
}

TEST("a step steps by itself every so long while its slideshow's toggle is on, the next picture read ahead, and stops at the end, at a key or a press, and opened afresh") {
	memory_source data;
	auto s = archive_of(3, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster", .picture = "image"});
	list posters;
	for (int i = 0; i < 3; ++i) posters.push_back(value(fields{{"id", std::format("p{}", i)}, {"title", std::format("Poster {}", i)}, {"image", std::format("files/p{}.png", i)}}));
	data.set("studio::archive", "", value(fields{{"posters", posters}}));
	s.screens.back().build = [](page& p) {
		auto archive = p.view("studio::archive");
		return items(toggle(p, "/posters/:poster show", "Slideshow", "play", std::vector<std::string>{"s"}),
		             step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true, .opened = true, .every = 0.02, .while_on = "/posters/:poster show"}, "Next", "next",
		                  std::vector<std::string>{"right"}));
	};
	navigator nav(s, data);
	one::desktop::runtime::hold_posted(true);
	auto opened = [&](const std::string& address) {
		nav.open("/posters");
		built(nav);
		nav.open(address);
		built(nav);
	};
	// Run for a while, or until it's at an address.
	auto waited = [&](const std::string& at, std::chrono::milliseconds most) {
		for (auto until = std::chrono::steady_clock::now() + most; nav.current().address() != at && std::chrono::steady_clock::now() < until;) {
			if (one::desktop::runtime::run_posted() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
			built(nav);
		}
	};
	const one::desktop::key_event s_key{.symbol = 's', .text = "s"}, a_key{.symbol = 'a', .text = "a"};
	auto pressed = [&](const one::desktop::key_event& e) {
		key_down(e);
		(void)key(nav, e);
		built(nav);
	};
	auto playing = [&] { return nav.current().toggled("/posters/:poster show"); };
	opened("/posters/p0");
	// The picture the cards show for the next one, to read ahead.
	CHECK(picture_shown_for(nav.current(), "/posters/p1") == "files/p1.png");
	CHECK(picture_shown_for(nav.current(), "/posters/p9").empty());
	// On, it steps on by itself to the end of the list, and goes off there.
	pressed(s_key);
	CHECK(playing());
	waited("/posters/p2", std::chrono::seconds(5));
	CHECK(nav.current().address() == "/posters/p2");
	waited("", std::chrono::milliseconds(150));
	CHECK(nav.current().address() == "/posters/p2");
	CHECK(!playing());
	// Any key stops it.
	opened("/posters/p0");
	pressed(s_key);
	pressed(a_key);
	CHECK(!playing());
	waited("", std::chrono::milliseconds(100));
	CHECK(nav.current().address() == "/posters/p0");
	// So does its own toggle's key, which doesn't start it again; and a press.
	pressed(s_key);
	CHECK(playing());
	pressed(s_key);
	CHECK(!playing());
	pressed(s_key);
	input_heard();
	CHECK(!playing());
	waited("", std::chrono::milliseconds(100));
	CHECK(nav.current().address() == "/posters/p0");
	// Opened afresh, though it was left on, it starts off.
	nav.current().toggle("/posters/:poster show");
	CHECK(playing());
	opened("/posters/p1");
	CHECK(!playing());
	one::desktop::runtime::hold_posted(false);
}

TEST("a record's screen reads the whole pictures of the records before and after it ahead, so a step draws the whole one in its first frame, and lets go of those stepped away from") {
	auto folder = std::filesystem::temp_directory_path() / "uione-ahead";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder / "files");
	// Each a picture of stripes a pixel wide, black and white, which only the whole of it
	// draws as they are at a pixel of it to a pixel of the window: a smaller copy grows gray.
	for (int i = 0; i < 3; ++i) {
		std::vector<std::uint8_t> pixels(std::size_t{1600} * 1000 * 4, 255);
		for (std::size_t at = 0; at < std::size_t{1600} * 1000; ++at) {
			if (at % 2 == 0) pixels[at * 4] = pixels[at * 4 + 1] = pixels[at * 4 + 2] = 0;
		}
		one::desktop::offscreen::write_png(folder / "files" / std::format("p{}.png", i), {1600, 1000, std::move(pixels)});
	}
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	auto s = archive_of(3, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
	list posters;
	for (int i = 0; i < 3; ++i) posters.push_back(value(fields{{"id", std::format("p{}", i)}, {"title", std::format("Poster {}", i)}, {"image", std::format("files/p{}.png", i)}}));
	data.set("studio::archive", "", value(fields{{"posters", posters}}));
	s.screens.back().build = [](page& p) {
		auto archive = p.view("studio::archive");
		return items(workspace(p, region{"toolbar", along(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .picture = "image"}, "Previous", "previous", {"left"}),
		                                                  step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true, .picture = "image"}, "Next", "next", {"right"}))},
		                          region{"main", items(picture_alone(p, std::format("files/{}.png", p.param("poster")), "/posters/:poster collage"))}));
	};
	navigator nav(s, data);
	one::desktop::runtime::hold_posted(true);
	nav.open("/posters/p0");
	kept_window window(nav);
	window.w.size = {1600, 1000};
	window.update();
	auto read = [&](int i) { return read_whole(nav.current(), std::format("files/p{}.png", i)); };
	// A smaller copy of the next one, as cards would have, which would be drawn first
	// were the whole one not read.
	for (auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20); !(read(0) && read(1) && picture_soon(nav.current(), "files/p1.png", 640)) && std::chrono::steady_clock::now() < until;) {
		if (one::desktop::runtime::run_posted() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		window.update();
	}
	CHECK(read(0));
	CHECK(read(1));
	CHECK(picture_soon(nav.current(), "files/p1.png", 640).has_value());
	rest();
	key_down(right_key);
	CHECK(key(nav, right_key));
	window.update();
	CHECK(nav.current().address() == "/posters/p1");
	// Its first frame, nothing run since the step: the whole picture, stripe by stripe.
	auto shown = looked(window.w);
	auto a = shown.at(800, 500), b = shown.at(801, 500);
	CHECK(((a == black && b == std::array<int, 3>{255, 255, 255}) || (b == black && a == std::array<int, 3>{255, 255, 255})));
	// Beside it now, the one before is kept and the one after is read.
	for (auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20); !read(2) && std::chrono::steady_clock::now() < until;) {
		if (one::desktop::runtime::run_posted() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		window.update();
	}
	CHECK(read(0));
	CHECK(read(2));
	// Stepped on again, the first is let go, as it's no longer beside the one shown.
	key_down(right_key);
	CHECK(key(nav, right_key));
	window.update();
	CHECK(nav.current().address() == "/posters/p2");
	CHECK(!read(0));
	CHECK(read(1));
	CHECK(read(2));
	one::desktop::runtime::hold_posted(false);
	std::filesystem::remove_all(folder);
}

TEST("a picture alone fills its window, edge to edge, fitted on black, wide or tall, at any size and at any scale") {
	auto folder = std::filesystem::temp_directory_path() / "uione-alone-fills";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder / "files");
	// A wide picture and a tall one, each white all over.
	for (auto [name, w, h] : {std::tuple{"wide.png", 1800, 1000}, std::tuple{"tall.png", 1000, 1800}}) {
		one::desktop::offscreen::write_png(folder / "files" / name, {w, h, std::vector<std::uint8_t>(std::size_t(w) * std::size_t(h) * 4, 255)});
	}
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	for (const char* name : {"wide.png", "tall.png"}) {
		auto s = archive_of(1, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
		s.screens.back().build = [name](page& p) {
			auto archive = p.view("studio::archive");
			return items(workspace(p, region{"toolbar", along(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", {"right"}))},
			                          region{"main", items(picture_alone(p, std::string("files/") + name, "/posters/:poster collage"))}, region{"status", along(text({words("Status")}))}));
		};
		navigator nav(s, data);
		nav.open("/posters");
		nav.open("/posters/p0");
		kept_window window(nav);
		for (auto [width, height, scale] : {std::tuple{900, 600, 1.0}, std::tuple{1600, 1000, 1.0}, std::tuple{1280, 800, 2.0}, std::tuple{700, 1100, 1.5}}) {
			window.w.size = {width, height};
			for (int built = 0; built < 3; ++built) {
				window.update();
				one::desktop::runtime::run_posted();
			}
			rest();
			window.update();
			auto drawn = one::desktop::offscreen::draw(window.w.size, scale, tested_theme().page, [&](one::desktop::canvas& c) {
				one::desktop::detail::theme_in_use() = &tested_theme();
				window.w.root->paint(c);
			});
			// Where it's white, in the window's own pixels, and so where the picture is.
			int left = drawn.width, top = drawn.height, right = -1, bottom = -1;
			for (int y = 0; y < drawn.height; ++y) {
				for (int x = 0; x < drawn.width; ++x) {
					const std::uint8_t* at = &drawn.rgba[(std::size_t(y) * std::size_t(drawn.width) + std::size_t(x)) * 4];
					if (at[0] < 200 || at[1] < 200 || at[2] < 200) continue;
					left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
				}
			}
			bool wide = std::string(name) == "wide.png";
			double fit = std::min(drawn.width / (wide ? 1800.0 : 1000.0), drawn.height / (wide ? 1000.0 : 1800.0));
			double w = (wide ? 1800 : 1000) * fit, h = (wide ? 1000 : 1800) * fit;
			CHECK_SAYING(std::abs((right - left + 1) - w) <= 2 && std::abs((bottom - top + 1) - h) <= 2,
			             std::format("{} in {}x{} at {}: drawn {}x{} at {},{}, fitted is {:.0f}x{:.0f}", name, width, height, scale, right - left + 1, bottom - top + 1, left, top, w, h));
			// Centered, so it touches two opposite edges of the window.
			CHECK(std::abs(left - (drawn.width - 1 - right)) <= 2);
			CHECK(std::abs(top - (drawn.height - 1 - bottom)) <= 2);
		}
	}
	std::filesystem::remove_all(folder);
}

TEST("on a picture alone, left and right step through the list whatever has the keyboard, its bar's link, a step's button or a zoom button") {
	auto folder = banded_picture(std::filesystem::temp_directory_path() / "uione-alone-keys");
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	auto s = archive_of(12, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
	s.screens.back().build = [](page& p) {
		auto archive = p.view("studio::archive");
		return items(workspace(p, region{"bar", along(link(p, "Posters", "/posters", "gallery", {"escape"}))},
		                          region{"toolbar", along(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster"}, "Previous", "previous", {"left"}),
		                                                  step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", {"right"}),
		                                                  collage_button(p, {.key = "/posters/:poster collage", .does = "zoom in", .label = "Larger", .icon = "zoom_in"}),
		                                                  collage_button(p, {.key = "/posters/:poster collage", .does = "zoom out", .label = "Smaller", .icon = "zoom_out"}))},
		                          region{"main", items(picture_alone(p, "files/banded.png", "/posters/:poster collage"))}));
	};
	navigator nav(s, data);
	nav.open("/posters");
	nav.open("/posters/p0");
	one::desktop::app::state app;
	one::desktop::window outer(std::make_unique<one::desktop::window::state>());
	auto& w = outer.internals();
	w.outside = &outer;
	w.owner = &app;
	w.size = {900, 600};
	w.look = tested_theme();
	w.build = [&nav] { return drawn(nav); };
	outer.on_key = [&nav](const one::desktop::key_event& e) { return key(nav, e); };
	auto update = [&] {
		one::desktop::detail::theme_in_use() = &tested_theme();
		w.needs_building = true;
		one::desktop::bring_up_to_date(w);
	};
	update();
	wake();
	update();
	std::size_t count = focusable_in(*w.root).size();
	CHECK_SAYING(count >= 5, std::format("{} take the keyboard", count));
	// Each of them given the keyboard in turn, right steps on, and left back and on again.
	for (std::size_t i = 0; i < count; ++i) {
		wake();
		update();
		auto all = focusable_in(*w.root);
		for (auto* e : all) e->focused = false;
		all[i]->focused = true;
		std::string before = nav.current().address();
		one::desktop::deliver_key(w, right_key);
		update();
		CHECK_SAYING(nav.current().address() == std::format("/posters/p{}", i + 1), std::format("right with {} of {} focused went to {}, from {}", i, count, nav.current().address(), before));
		auto again = focusable_in(*w.root);
		for (auto* e : again) e->focused = false;
		again[std::min(i, again.size() - 1)]->focused = true;
		one::desktop::deliver_key(w, left_key);
		update();
		CHECK(nav.current().address() == std::format("/posters/p{}", i));
		one::desktop::deliver_key(w, right_key);
		update();
	}
	std::filesystem::remove_all(folder);
}

TEST("a picture alone steps by a swipe across it at fit, a finger's, a drag's or three fingers' on a touchpad, following it, and not for one too short, nor while it's zoomed") {
	auto folder = banded_picture(std::filesystem::temp_directory_path() / "uione-alone-swiped");
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	auto s = archive_of(5, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
	s.screens.back().build = [](page& p) {
		auto archive = p.view("studio::archive");
		return items(workspace(p, region{"toolbar", along(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster"}, "Previous", "previous", {"left"}),
		                                                  step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", {"right"}))},
		                          region{"main", items(picture_alone(p, "files/banded.png", "/posters/:poster collage"))}));
	};
	navigator nav(s, data);
	nav.open("/posters");
	nav.open("/posters/p2");
	kept_window window(nav);
	window.update();
	rest();
	window.update();
	auto left_edge = [&] {
		auto shown = looked(window.w);
		for (int x = 0; x < 900; ++x) {
			if (shown.at(x, 300) != black) return x;
		}
		return -1;
	};
	CHECK(left_edge() == 0);
	// Too short: it follows the finger, then goes back.
	one::desktop::deliver_press(window.w, {500, 300}, 1);
	one::desktop::deliver_pointer(window.w, {470, 300});
	CHECK(left_edge() == 0);  // its left edge is under the window's, moved left
	CHECK(looked(window.w).at(875, 300) == black);
	one::desktop::deliver_release(window.w, {470, 300});
	window.update();
	CHECK(nav.current().address() == "/posters/p2");
	CHECK(left_edge() == 0);
	CHECK(looked(window.w).at(875, 300) != black);
	// To the left, far enough: the next.
	one::desktop::deliver_press(window.w, {600, 300}, 1);
	one::desktop::deliver_pointer(window.w, {400, 300});
	one::desktop::deliver_release(window.w, {400, 300});
	window.update();
	CHECK(nav.current().address() == "/posters/p3");
	// Three fingers to the right: the one before, but not when they're called off.
	one::desktop::deliver_swipe(window.w, {450, 300}, 150, 0, false);
	one::desktop::deliver_swipe(window.w, {450, 300}, 0, 0, true);
	window.update();
	CHECK(nav.current().address() == "/posters/p3");
	one::desktop::deliver_swipe(window.w, {450, 300}, 150, 0, false);
	CHECK(left_edge() == 150);
	one::desktop::deliver_swipe(window.w, {450, 300}, 200, 0, true);
	window.update();
	CHECK(nav.current().address() == "/posters/p2");
	// Zoomed, a drag moves it about instead.
	(void)looked(window.w);
	one::desktop::deliver_pinch(window.w, {450, 300}, 2.f);
	one::desktop::deliver_press(window.w, {600, 300}, 1);
	one::desktop::deliver_pointer(window.w, {300, 300});
	one::desktop::deliver_release(window.w, {300, 300});
	window.update();
	CHECK(nav.current().address() == "/posters/p2");
	std::filesystem::remove_all(folder);
}

TEST("a picture alone is zoomed by Ctrl and its keys, its zoom buttons and a pinch, to 100% and back by a double click, and dragged about once it's larger, and stepping fits the next") {
	auto folder = banded_picture(std::filesystem::temp_directory_path() / "uione-alone-zoomed");
	struct kept_there : memory_source {
		std::filesystem::path in;
		std::optional<std::filesystem::path> file_here(const std::string& kept) const override { return in / kept; }
	} data;
	data.in = folder;
	auto s = archive_of(3, data, {.view = {}, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"});
	s.screens.back().build = [](page& p) {
		auto archive = p.view("studio::archive");
		return items(workspace(p, region{"toolbar", along(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", {"right"}))},
		                          region{"main", items(picture_alone(p, "files/banded.png", "/posters/:poster collage"))}));
	};
	navigator nav(s, data);
	nav.open("/posters");
	nav.open("/posters/p0");
	kept_window window(nav);
	window.update();
	window.update();
	rest();
	window.update();
	const one::desktop::key_event larger{.symbol = '=', .text = "=", .control = true}, smaller{.symbol = '-', .text = "-", .control = true},
	    fitted{.symbol = '0', .text = "0", .control = true};
	// Fitted, a drag down doesn't move it: there's nowhere to move it to.
	CHECK(looked(window.w).at(450, 40) == black);
	one::desktop::deliver_press(window.w, {450, 300}, 1);
	one::desktop::deliver_pointer(window.w, {450, 500});
	CHECK(looked(window.w).at(450, 40) == black);
	one::desktop::deliver_release(window.w, {450, 500});
	// Larger by a quarter about its middle: 1125 × 625, past the window's top and foot.
	CHECK(key(nav, larger));
	CHECK(looked(window.w).at(450, 40) == green);
	CHECK(looked(window.w).at(450, 590) != black);
	CHECK(key(nav, smaller));
	CHECK(looked(window.w).at(450, 40) == black);
	// Smaller than fitted it doesn't go.
	CHECK(key(nav, smaller));
	CHECK(looked(window.w).at(450, 40) == black);
	CHECK(looked(window.w).at(200, 300) == red);
	// A zoom button asks it, as a collage is asked.
	nav.current().kept<std::string>("collage do /posters/:poster collage").set("zoom in");
	window.update();
	CHECK(looked(window.w).at(450, 40) == green);
	CHECK(key(nav, fitted));
	CHECK(looked(window.w).at(450, 40) == black);
	// Twice pressed, it's 100%, a pixel of it a pixel of the screen, about where it was
	// pressed: what's under the pointer stays, and the green band's above the window.
	one::desktop::deliver_press(window.w, {200, 300}, 1);
	one::desktop::deliver_release(window.w, {200, 300});
	one::desktop::deliver_press(window.w, {200, 300}, 2);
	one::desktop::deliver_release(window.w, {200, 300});
	auto shown = looked(window.w);
	CHECK(shown.at(200, 300) == red);
	CHECK(shown.at(450, 20) == red);
	CHECK(shown.at(850, 300) == blue);
	CHECK(shown.at(450, 590) != black);
	// Larger than fits, it's dragged about: down, so its top comes into view, and no
	// further than its edge however far it's dragged.
	CHECK(one::desktop::cursor_at(window.w, {450, 300}) == one::desktop::cursor::hidden);
	wake();
	window.update();
	CHECK(one::desktop::cursor_at(window.w, {450, 300}) == one::desktop::cursor::grab);
	one::desktop::deliver_press(window.w, {450, 300}, 1);
	CHECK(window.w.dragging != nullptr);
	CHECK(one::desktop::cursor_at(window.w, {450, 300}) == one::desktop::cursor::grabbing);
	one::desktop::deliver_pointer(window.w, {450, 500});
	one::desktop::deliver_pointer(window.w, {450, 900});
	one::desktop::deliver_release(window.w, {450, 900});
	rest();
	window.update();
	shown = looked(window.w);
	CHECK(shown.at(450, 50) == green);
	CHECK(shown.at(450, 110) == red);
	// Twice pressed again, it's fitted.
	one::desktop::deliver_press(window.w, {450, 300}, 2);
	one::desktop::deliver_release(window.w, {450, 300});
	CHECK(looked(window.w).at(450, 40) == black);
	// Pinched apart, larger, about where the fingers are.
	one::desktop::deliver_pinch(window.w, {200, 300}, 1.5f);
	shown = looked(window.w);
	CHECK(shown.at(450, 40) != black);
	CHECK(shown.at(200, 300) == red);
	// Stepped on to the next picture, it's fitted, as one's first shown.
	CHECK(key(nav, right_key));
	window.update();
	CHECK(nav.current().address() == "/posters/p1");
	CHECK(looked(window.w).at(450, 40) == black);
	std::filesystem::remove_all(folder);
}

namespace {

	// Posters taken a few hours apart, newest first, from the end of October 2026 back,
	// under a heading for each month as a mosaic, and the years and months beside them.
	site dated_archive(int count, memory_source& data) {
		using namespace std::chrono;
		list posters;
		auto newest = sys_days(year(2026) / October / day(30)) + hours(12);
		for (int i = 0; i < count; ++i) {
			value taken;
			taken.type = value::kind::time;
			taken.time.seconds = duration_cast<seconds>((newest - hours(30) * i).time_since_epoch()).count();
			posters.push_back(value(fields{{"id", std::format("p{}", i)}, {"title", std::format("Poster {}", i)}, {"image", ""}, {"taken", taken}}));
		}
		data.set("studio::archive", "", value(fields{{"posters", posters}}));
		site s{.name = "studio", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"Posters", "/posters", "", [](page& p) {
			                     auto archive = p.view("studio::archive");
			                     return workspace(p, region{"main", items(cards(p, {.view = archive, .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster", .picture = "image", .mosaic = true, .by = "taken", .unit = time_unit::month, .key = "/posters cards"}))},
			                                      region{"side", items(dated_timeline(p, {.view = archive, .list = "posters", .by = "taken", .unit = time_unit::month, .cards = "/posters cards"}))});
		                     }});
		s.screens.push_back({"Poster", "/posters/:poster", "", [](page&) { return items(); }});
		return s;
	}

	// What's drawn in a window: each mosaic, taking the keyboard with nothing made inside
	// it until it's drawn, and the timeline, as wide as it says.
	void each_dated(one::desktop::element& e, std::vector<one::desktop::element*>& mosaics, one::desktop::element*& timeline) {
		if (e.focusable() && e.fixed_width) timeline = &e;
		else if (e.focusable() && e.children.empty()) mosaics.push_back(&e);
		for (auto& child : e.children) each_dated(*child, mosaics, timeline);
	}

	// How many marks are drawn down a column of a picture: runs of what isn't the
	// background, between two places down it.
	int marks_down(const seen& picture, int x, int from, int to, std::array<int, 3> background) {
		int runs = 0;
		bool in = false;
		for (int y = from; y < to; ++y) {
			bool marked = picture.at(x, y) != background;
			if (marked && !in) ++runs;
			in = marked;
		}
		return runs;
	}

}  // namespace

TEST("cards by a date are under a heading for each month, a mosaic's tiles starting again under each, and their timeline is drawn along them, a mark for each day, scrolling them as it's pressed and dragged and saying the day there") {
	use_time_zone("UTC");
	memory_source data;
	// 30 hours apart from October 30: 24 in October, 24 in September, 25 in August, the rest in July; 80 days.
	auto s = dated_archive(80, data);
	navigator nav(s, data);
	nav.open("/posters");
	kept_window window(nav);
	window.w.size = {1200, 700};
	window.update();
	(void)looked(window.w);
	window.update();
	std::vector<one::desktop::element*> mosaics;
	one::desktop::element* timeline = nullptr;
	each_dated(*window.w.root, mosaics, timeline);
	CHECK(mosaics.size() == 4);
	CHECK(timeline != nullptr);
	if (mosaics.size() != 4 || !timeline) return;
	// Each month's mosaic starts with its own tall tile, so its height is its count's.
	float side = (mosaics[1]->frame.width - 32) / 3;
	CHECK(std::abs(mosaics[0]->frame.height - (mosaic_rows(24) * side + (mosaic_rows(24) - 1) * 16)) < 2);
	CHECK(std::abs(mosaics[2]->frame.height - (mosaic_rows(25) * side + (mosaic_rows(25) - 1) * 16)) < 2);
	// A strip at the window's right edge, as tall as what scrolls the cards.
	auto scrolled = mosaics[0]->scrolled();
	CHECK(scrolled.has_value());
	if (!scrolled) return;
	one::desktop::rect strip = timeline->frame;
	CHECK(strip.width == 72);
	CHECK(std::abs(strip.right() - 1200) < 1);
	CHECK(std::abs(strip.height - scrolled->visible) < 1);
	// A mark for each day, from its right edge, the year marked at its top.
	auto seen = looked(window.w);
	auto background = seen.at(static_cast<int>(strip.x) + 20, static_cast<int>(strip.y + strip.height / 2));
	CHECK(marks_down(seen, static_cast<int>(strip.right()) - 3, static_cast<int>(strip.y) + 4, static_cast<int>(strip.bottom()), background) >= 75);
	CHECK(marks_down(seen, static_cast<int>(strip.x) + 8, static_cast<int>(strip.y) + 2, static_cast<int>(strip.y) + 20, background) > 0);
	float top = mosaics[0]->frame.y;
	// Pressed halfway down it, the cards are scrolled halfway, and dragged, along with it.
	auto offset = [&] {
		each_dated(*window.w.root, mosaics = {}, timeline);
		return mosaics[0]->scrolled()->offset;
	};
	float length = scrolled->length;
	one::desktop::deliver_press(window.w, {strip.x + 30, strip.y + strip.height / 2}, 1);
	window.update();
	CHECK_SAYING(std::abs(offset() - length / 2) < 2, std::format("{} {}", offset(), length));
	one::desktop::deliver_pointer(window.w, {strip.x + 30, strip.y + strip.height * 0.75f});
	window.update();
	CHECK(std::abs(offset() - length * 0.75f) < 2);
	// Where they're scrolled to is a line across it, and the day there is said beside it.
	// What's drawn over the rest, as a window draws it last, too.
	auto all_of = [&] {
		return decltype(looked(window.w)){one::desktop::offscreen::draw(window.w.size, 1, tested_theme().page, [&](one::desktop::canvas& c) {
			one::desktop::detail::theme_in_use() = &tested_theme();
			window.w.root->paint(c);
			c.finish();
		})};
	};
	seen = all_of();
	float marker = strip.y + offset() / length * strip.height;
	CHECK(seen.at(static_cast<int>(strip.x) + 10, static_cast<int>(marker)) != background);
	one::desktop::point said{strip.x - 30, strip.y + strip.height * 0.75f - 10};
	auto saying = seen.at(static_cast<int>(said.x), static_cast<int>(said.y));
	one::desktop::deliver_release(window.w, {strip.x + 30, strip.y + strip.height * 0.75f});
	one::desktop::deliver_leave(window.w);
	window.update();
	CHECK(all_of().at(static_cast<int>(said.x), static_cast<int>(said.y)) != saying);
	// With the keyboard: Home to the top, then down a heading, to September's.
	each_dated(*window.w.root, mosaics = {}, timeline);
	timeline->handle_key({.symbol = one::desktop::keys::home, .text = ""});
	timeline->handle_key({.symbol = one::desktop::keys::down, .text = ""});
	window.update();
	(void)looked(window.w);
	each_dated(*window.w.root, mosaics = {}, timeline);
	CHECK_SAYING(std::abs(mosaics[1]->frame.y - top) < 50, std::format("{} {}", mosaics[1]->frame.y, top));
	timeline->handle_key({.symbol = one::desktop::keys::up, .text = ""});
	window.update();
	(void)looked(window.w);
	CHECK(offset() < 50);
	timeline->handle_key({.symbol = one::desktop::keys::end, .text = ""});
	window.update();
	CHECK(std::abs(offset() - (length - scrolled->visible)) < 1);
}

TEST("twenty thousand cards by a date have their days worked out, and the oldest scrolled to as their timeline is dragged past its foot, in a moment, drawing only what's in view") {
	use_time_zone("UTC");
	memory_source data;
	auto s = dated_archive(20000, data);
	navigator nav(s, data);
	nav.open("/posters");
	using clock = std::chrono::steady_clock;
	auto rows = rows_of(nav.current().view("studio::archive").peek(), "posters");
	auto started = clock::now();
	auto ruler = ruler_of(rows, "taken", time_unit::month);
	auto days = clock::now() - started;
	CHECK(ruler.days.size() == 20000);
	CHECK(ruler.years.size() == 69);
	kept_window window(nav);
	window.w.size = {1200, 700};
	window.update();
	(void)looked(window.w);
	std::vector<one::desktop::element*> mosaics;
	one::desktop::element* timeline = nullptr;
	each_dated(*window.w.root, mosaics, timeline);
	CHECK(timeline != nullptr);
	if (!timeline) return;
	one::desktop::rect strip = timeline->frame;
	// Days closer than two pixels are one mark, so there are fewer marks than half its height.
	auto seen = looked(window.w);
	auto background = seen.at(static_cast<int>(strip.x) + 20, static_cast<int>(strip.y + strip.height / 2) + 1);
	int marks = marks_down(seen, static_cast<int>(strip.right()) - 3, static_cast<int>(strip.y), static_cast<int>(strip.bottom()), background);
	CHECK(marks > 0);
	CHECK(marks <= strip.height / 2);
	started = clock::now();
	// Scrolled, not built again, as it's pressed and dragged past its foot.
	one::desktop::deliver_press(window.w, {strip.x + 30, strip.bottom() - 1}, 1);
	one::desktop::deliver_pointer(window.w, {strip.x + 30, strip.bottom() + 20});
	one::desktop::deliver_release(window.w, {strip.x + 30, strip.bottom() + 20});
	one::desktop::bring_up_to_date(window.w);
	(void)looked(window.w);
	auto jump = clock::now() - started;
	each_dated(*window.w.root, mosaics = {}, timeline);
	CHECK_SAYING(mosaics.back()->frame.y < 700, std::format("{}", mosaics.back()->frame.y));
	auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
	std::fprintf(stderr, "    20000 cards in %zu months: days worked out in %.1f ms, the oldest scrolled to and drawn in %.1f ms\n", mosaics.size(), ms(days), ms(jump));
	// Unoptimized and checked as these tests are built, a good deal slower than an app.
	CHECK(ms(days) < 200);
	CHECK(ms(jump) < 500);
}

TEST("a record's screen steps through the list it was opened from, cards or a table in its own order, and else through the first list that opens it, not past either end") {
	memory_source data;
	// By title in the archive, and newest first by year.
	list by_title, by_year;
	const std::pair<const char*, int> posters[] = {{"Arches", 1990}, {"Bridges", 2010}, {"Canyons", 2000}, {"Dunes", 2020}};
	for (int i = 0; i < 4; ++i) by_title.push_back(value(fields{{"id", std::format("p{}", i)}, {"title", posters[i].first}, {"year", posters[i].second}}));
	for (int i : {3, 1, 2, 0}) by_year.push_back(by_title[static_cast<std::size_t>(i)]);
	data.set("studio::archive", "", value(fields{{"posters", by_title}}));
	data.set("studio::by_year", "", value(fields{{"posters", by_year}}));
	site s{.name = "studio", .id = "org.uione.runtime-tests"};
	s.look = tested_theme();
	s.screens.push_back({"Posters", "/posters", "", [](page& p) {
		                     return items(cards(p, {.view = p.view("studio::archive"), .list = "posters", .columns = {{"title", "Title"}}, .link = "/posters/:poster"}));
	                     }});
	s.screens.push_back({"Years", "/years", "", [](page& p) {
		                     return items(table(p, {.view = p.view("studio::by_year"), .list = "posters", .columns = {{"title", "Title"}, {"year", "Year"}}, .link = "/posters/:poster"}));
	                     }});
	s.screens.push_back({"Poster", "/posters/:poster", "", [](page& p) {
		                     auto archive = p.view("studio::archive");
		                     return items(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .opened = true}, "Previous", "previous", {"left"}),
		                                  step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true, .opened = true}, "Next", "next", {"right"}));
	                     }});
	navigator nav(s, data);
	auto steps = [&](const one::desktop::key_event& k, int times) {
		std::vector<std::string> went;
		for (int i = 0; i < times; ++i) {
			built(nav);
			if (!key(nav, k)) break;
			went.push_back(nav.current().address());
		}
		return went;
	};
	// Opened from the years, newest first: Dunes, Bridges, Canyons, Arches; and it stops at the end.
	nav.open("/years");
	built(nav);
	nav.open("/posters/p1");
	CHECK((steps(right_key, 4) == std::vector<std::string>{"/posters/p2", "/posters/p0"}));
	CHECK((steps(left_key, 4) == std::vector<std::string>{"/posters/p2", "/posters/p1", "/posters/p3"}));
	// Back at the years, what was stepped to last is what was opened from it.
	CHECK(nav.back());
	CHECK(nav.current().where_left().opened == "/posters/p3");
	// Opened from the cards, by title.
	nav.open("/posters");
	built(nav);
	nav.open("/posters/p1");
	CHECK((steps(right_key, 4) == std::vector<std::string>{"/posters/p2", "/posters/p3"}));
	// Opened by its address alone, it steps through the archive the screen names.
	navigator alone(s, data);
	alone.open("/posters/p0");
	built(alone);
	CHECK(key(alone, right_key));
	CHECK(alone.current().address() == "/posters/p1");
}
