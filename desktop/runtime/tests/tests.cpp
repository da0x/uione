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
#include "one/desktop/runtime/markdown.hpp"
#include "ui_internal.hpp"

#include <algorithm>

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

TEST("an address opens the screen most particular to it, a part named outright before a parameter") {
	memory_source data;
	site s{.name = "neotrac"};
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
	matches_golden("project-page", {900, 560}, [&] { return nav.show(); });
}

namespace {

	// The window's view built and placed, as it is each time something it read changes.
	std::unique_ptr<one::desktop::element> built(navigator& nav) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		auto root = nav.show().take();
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

TEST("a form's fields: text, several lines, a choice, a list's pick, a box, several boxes and a hint, with what turned it down") {
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
		                                                      {.name = "assignee", .type = "pick", .choices = list_choices(project, "members", "person.name", "person")},
		                                                      {.name = "urgent", .type = "boolean"},
		                                                      {.name = "platforms", .type = "choices", .choices = {{"linux", "Linux"}, {"windows", "Windows"}}}}}));
	                     }});
	navigator nav(s, data);
	nav.open("/projects/atlas/new");
	nav.current().kept<std::string>("form:tracker::issue::create::title").set("Draw forms natively");
	nav.current().kept<bool>("form:tracker::issue::create::platforms:linux").set(true);
	nav.current().run("tracker::issue::create", value::map(), {}, true);
	matches_golden("form", {900, 860}, [&] { return nav.show(); });
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
	matches_golden("issue-page", {900, 760}, [&] { return nav.show(); });
	CHECK(data.runs().size() == 1);  // said it's been seen, once
	CHECK(data.runs().front().command == "tracker::seen::touch");
}

namespace {

	void fill_board(memory_source& data) {
		auto issue = [](std::string id, int n, std::string title, std::string stage, std::string priority, list labels = {}, list people = {}) {
			return value(fields{{"id", id}, {"number", n}, {"title", title}, {"stage", stage}, {"priority", priority}, {"labels", labels}, {"assignees.name", people}});
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

	site with_board() {
		site s{.name = "neotrac", .id = "org.uione.runtime-tests"};
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

TEST("a board: its columns, cards tinted by priority with their labels and people, and a move offered") {
	memory_source data;
	fill_board(data);
	auto s = with_board();
	navigator nav(s, data);
	nav.open("/atlas/boards/main");
	nav.current().kept<std::string>("board:issues:moving").set("atlas-12");
	matches_golden("board", {1000, 600}, [&] { return nav.show(); });
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
	site s{.name = "neotrac", .id = "org.uione.runtime-tests"};
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
	matches_golden("cards", {900, 520}, [&] { return nav.show(); });
}

namespace {

	site with_issue() {
		site s{.name = "neotrac", .id = "org.uione.runtime-tests"};
		s.look = tested_theme();
		s.screens.push_back({"", "/:project/:issue", "", [](page& p) {
			                     auto issue = p.view("projects::issue_page", p.param("issue"));
			                     auto roles = p.view("projects::member_roles");
			                     p.title({words("#"), live(issue, "number"), words(" "), live(issue, "title")});
			                     confirm(p, "projects::issue::close", "Close this issue?");
			                     heading(p, actions(command(p, {.command = "projects::issue::follow", .label = "Follow", .id = p.param("issue"), .when = !issue->data["followed"].as_bool()}),
			                                        command(p, {.command = "projects::issue::close", .label = "Close", .id = p.param("issue")})));
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

TEST("an issue in two columns, its buttons beside its title, the steps it can take, and a question asked") {
	memory_source data;
	fill_issue(data, "atlas-maintainer");
	auto s = with_issue();
	navigator nav(s, data);
	nav.open("/atlas/atlas-12");
	matches_golden("two-columns", {1000, 520}, [&] { return nav.show(); });
	nav.open("/atlas/settings");
	nav.current().ask_before("projects::project::delete", "Delete this project and everything in it?");
	nav.current().run("projects::project::delete", value::map());
	matches_golden("menu", {1000, 420}, [&] { return nav.show(); });
}

namespace {

	site with_table() {
		site s{.name = "neotrac", .id = "org.uione.runtime-tests"};
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
	matches_golden("table", {1000, 420}, [&] { return nav.show(); });
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
		site s{.name = "neotrac", .id = "org.uione.runtime-tests"};
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
	matches_golden("workflow", {1000, 640}, [&] { return nav.show(); });
}

int main() {
	use_time_zone("UTC");  // so what's drawn is the same wherever it's run
	return check::run();
}
