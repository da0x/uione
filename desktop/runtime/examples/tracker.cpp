// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A small tracker on one::desktop::runtime, its data kept in memory: written as the
// desktop apps `one build` writes are, a screen to a function, so what they'll be
// like can be tried. Its commands change what's kept, and every screen reading it
// shows the change.

#include <print>

#include "one/desktop/app.hpp"
#include "one/desktop/runtime/components.hpp"

namespace {

	using namespace one::desktop::runtime;
	using fields = std::map<std::string, value>;
	using list = std::vector<value>;

	// The views, as a backend would keep them from its records.
	struct records {
		struct issue {
			std::string id, title, status, priority, body;
			int number;
		};
		std::vector<issue> issues{{"atlas-12", "Copy an issue whole", "open", "high", "Copy its title, body and labels.", 12},
		                          {"atlas-9", "Search by label", "closed", "normal", "", 9}};
		int next = 13;

		void publish(memory_source& data) {
			list rows;
			for (const auto& i : issues) rows.push_back(value(fields{{"id", i.id}, {"number", i.number}, {"title", i.title}, {"status", i.status}, {"priority", i.priority}}));
			data.set("tracker::project_page", "atlas", value(fields{{"name", "Atlas"}, {"repository", "github.com/example/atlas"}, {"visibility", "public"}, {"issues", rows}}));
			data.set("tracker::projects", "", value(fields{{"rows", list{value(fields{{"id", "atlas"}, {"name", "Atlas"}, {"slug", "atlas"}, {"open", static_cast<int>(std::ranges::count(issues, std::string("open"), &issue::status))}})}}}));
			for (const auto& i : issues)
				data.set("tracker::issue_page", i.id, value(fields{{"number", i.number}, {"title", i.title}, {"status", i.status}, {"priority", i.priority}, {"body", i.body}}));
		}
		issue* find(const std::string& id) {
			auto it = std::ranges::find(issues, id, &issue::id);
			return it == issues.end() ? nullptr : &*it;
		}
	};

	const std::vector<one::screens::option> priorities{{"high", "High"}, {"normal", "Normal"}, {"low", "Low"}};

	site tracker() {
		site s{.name = "tracker", .id = "org.uione.runtime-tracker"};
		s.screens.push_back({"Projects", "/", "Projects", [](page& p) {
			                     return items(table(p, {.view = p.view("tracker::projects"), .columns = {{"name", "Project"}, {"open", "Open issues"}}, .link = "/projects/:project"}));
		                     }});
		s.screens.push_back({"", "/projects/:project", "", [](page& p) {
			                     auto project = p.view("tracker::project_page", p.param("project"));
			                     p.title({live(project, "name")});
			                     crumbs(p, {{"/", {words("Projects")}}});
			                     subtitle(p, {words("Kept at "), live(project, "repository")});
			                     p.beside_title(form(p, {.command = "tracker::issue::create",
			                                             .fields = {{"title"}, {.name = "priority", .type = "choice", .choices = priorities, .start = "normal", .required = true}},
			                                             .button = true,
			                                             .given = {{"project", p.param("project")}},
			                                             .opener = "New issue"}));
			                     return items(details(p, project, {{"visibility", "Visibility"}}),
			                                  section({"Issues"}, table(p, {.view = project,
			                                                                .list = "issues",
			                                                                .columns = {{"number", "#"}, {"title", "Title"}, {"priority", "Priority"}, {"status", "Status"}},
			                                                                .link = "/issues/:issue",
			                                                                .actions = {{.command = "tracker::issue::close", .label = "Close",
			                                                                             .when = [](const value& row) { return row["status"].as_string() == "open"; }}}})));
		                     }});
		s.screens.push_back({"", "/issues/:issue", "", [](page& p) {
			                     auto issue = p.view("tracker::issue_page", p.param("issue"));
			                     p.title({words("#"), live(issue, "number"), words(" "), live(issue, "title")});
			                     crumbs(p, {{"/", {words("Projects")}}, {"/projects/atlas", {words("Atlas")}}});
			                     return items(details(p, issue, {{"status", "Status"}, {"priority", "Priority"}}),
			                                  form(p, {.command = "tracker::issue::update",
			                                           .fields = {{"title"}, {.name = "body", .label = "Description", .type = "markdown"},
			                                                      {.name = "priority", .type = "choice", .choices = priorities, .required = true}},
			                                           .from = issue,
			                                           .id = p.param("issue"),
			                                           .submit = "Save"}));
		                     }});
		return s;
	}

}  // namespace

int main() {
	auto app = one::desktop::app::connect();
	if (!app) {
		std::println(stderr, "tracker: {}", app.error());
		return 1;
	}
	memory_source data;
	records kept;
	data.sign_in(person{"maya", "Maya Garcia"});
	data.on("tracker::issue::create", [&](const value& f, memory_source& self) -> answer {
		if (f["title"].as_string().empty()) return std::unexpected(std::string("an issue needs a title"));
		std::string id = "atlas-" + std::to_string(kept.next);
		kept.issues.push_back({id, f["title"].as_string(), "open", f["priority"].as_string(), "", kept.next++});
		kept.publish(self);
		return id;
	});
	data.on("tracker::issue::close", [&](const value& f, memory_source& self) -> answer {
		auto* i = kept.find(f["id"].as_string());
		if (!i || i->status == "closed") return std::unexpected(std::string("that issue is already closed"));
		i->status = "closed";
		kept.publish(self);
		return i->id;
	});
	data.on("tracker::issue::update", [&](const value& f, memory_source& self) -> answer {
		auto* i = kept.find(f["id"].as_string());
		if (!i) return std::unexpected(std::string("there's no such issue"));
		i->title = f["title"].as_string();
		i->body = f["body"].as_string();
		i->priority = f["priority"].as_string();
		kept.publish(self);
		return i->id;
	});
	kept.publish(data);
	auto s = tracker();
	return run(s, *app, data, "/");
}
