// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The backend and rules generators are held to site/target, like the web generator.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/rules.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    std::vector<language::file> read(const std::string& project) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({root + project})) {
            files.push_back(language::parse(path, *platform::read_file(path), out));
        }
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return files;
    }

    generators::generated_api api(const std::string& project) {
        return generators::generate_api(read(project), root + project, root + project + "/build/api");
    }

    const generators::output_file* find(const std::vector<generators::output_file>& files, const std::string& path) {
        auto it = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.path == path; });
        return it == files.end() ? nullptr : &*it;
    }

    std::string without_license(const std::string& text) {
        if (!text.starts_with("// Copyright")) return text;
        auto end = text.find("\n\n");
        return end == std::string::npos ? text : text.substr(end + 2);
    }

} // namespace

TEST_CASE("the site's backend is generated exactly as its targets say") {
    auto generated = api("/site");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    for (const char* path : {"go.mod", "main.go", "waitlist/waitlist.go", "studio/studio.go"}) {
        CAPTURE(path);
        auto target = platform::read_file(root + "/site/target/api/" + path);
        REQUIRE(target);
        const auto* file = find(generated.files, path);
        REQUIRE(file != nullptr);
        CHECK(file->content == without_license(*target));
    }
}

TEST_CASE("the site's rules are generated exactly as their target says") {
    auto target = platform::read_file(root + "/site/target/firestore.rules");
    REQUIRE(target);
    CHECK(generators::generate_rules(root + "/site") == without_license(*target));
}

TEST_CASE("a command's body becomes Go that reads like it") {
    auto generated = api("/examples/tasks");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* tasks = find(generated.files, "tasks/tasks.go");
    REQUIRE(tasks != nullptr);
    CHECK(tasks->content.find(R"(var Complete = one.Command[Task]("task::complete").Allow(one.Owner).
	Do(func(c *one.Ctx, t *Task) error {
		if t.Done {
			return c.Fail("that task is already done")
		}
		t.Done = true
		return nil
	})
)") != std::string::npos);
    CHECK(tasks->content.find(R"(var List = one.View("list").PerUser().
	Each(one.Where[Task]("owner", one.Viewer)).
	Order("done", "-created_at").
	Fields("title", "done")
)") != std::string::npos);
}

TEST_CASE("the library example's backend is generated exactly as its target says") {
    auto generated = api("/examples/library");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto target = platform::read_file(root + "/site/target/api/library/library.go");
    REQUIRE(target);
    const auto* file = find(generated.files, "library/library.go");
    REQUIRE(file != nullptr);
    CHECK(file->content == without_license(*target));
}

TEST_CASE("an update changes what its forms ask for and what it says it changes, and nothing else") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace tracker {
entity issue {
	title   text
	status  enum  open | done
	owner   user
}
command issue::update {
	changes owner
}
command issue::create
view issue_page per issue {
	title = issue.title
}
screen "Issue" /issues/:issue {
	form issue::update {
		title
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "tracker/tracker.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(var Update = one.Command[Issue]("issue::update").Fields("title", "owner"))") != std::string::npos);
    CHECK(found->content.find(R"(var Create = one.Command[Issue]("issue::create"))" "\n") != std::string::npos);
}

TEST_CASE("a command is sent inputs besides its fields, and changes or deletes other entities in the same step") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace board {
entity board {
	title  text
	start  column
}
entity column {
	board  board  required
	title  text
}
entity card {
	column  column
}
entity arrow {
	from  column
	to    column
}
command column::delete {
	input into column
	input note text
	require into != id  "pick another column"
	each card where column == id {
		column = into
	}
	delete each arrow where from == id || to == id
	if board.start == id {
		board.start = into
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "board/board.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CHECK(go.find(R"(one.Command[Column]("column::delete").Inputs("into", "note").)") != std::string::npos);
    CHECK(go.find(R"(into, _ := c.Input("into").(string))") != std::string::npos);
    // An input the body doesn't name isn't read, so the Go has nothing unused.
    CHECK(go.find(R"(c.Input("note"))") == std::string::npos);
    CHECK(go.find(R"(if err := one.EachIn(c, "column", x.ID, func(card *Card) error {)") != std::string::npos);
    CHECK(go.find("card.Column = into") != std::string::npos);
    CHECK(go.find(R"(one.DeleteWhere[Arrow](c, "from", x.ID))") != std::string::npos);
    CHECK(go.find(R"(one.DeleteWhere[Arrow](c, "to", x.ID))") != std::string::npos);
    CHECK(go.find("board.Start = into") != std::string::npos);
}

TEST_CASE("a key made from another field tells the library which") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace board {\nentity lane {\n\tname   text  required  key = slug(title)\n\ttitle  text  required\n}\n}\n", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "board/board.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(one:"required,key,from=title")") != std::string::npos);
}

TEST_CASE("a once changes what's stored, each step a body done to the entities it names") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug   slug  required  unique  key
	start  stage
}
entity rank {
	crew   crew  required  key
	name   slug  required  key
	title  text  required
	may    list of permission
}
entity stage {
	crew  crew  required  key
	name  slug  required  key
}
entity task {
	crew   crew  required  key
	title  text
	stage  stage
}
command crew::update
command task::update
once "2026-10-07 stages" {
	each rank where name == "mate" {
		title = "First mate"
		may = [
			crew::update,
			task::update,
		]
	}
	each crew {
		create stage { crew = id  name = "todo" }
		start = stage::todo
	}
	each task {
		stage = stage::todo
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "crew/crew.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CHECK(go.find(R"(var Once20261007Stages = one.Once("2026-10-07 stages",)") != std::string::npos);
    CHECK(go.find(R"(one.Each[Rank]("name", "mate", func(c *one.Ctx, r *Rank) error {)") != std::string::npos);
    CHECK(go.find(R"(r.May = []string{"crew::update", "task::update"})") != std::string::npos);
    CHECK(go.find(R"(one.Each[Crew]("", nil, func(c *one.Ctx, x *Crew) error {)") != std::string::npos);
    CHECK(go.find(R"(x.Start = one.Key(x.ID, "todo"))") != std::string::npos);
    // A task names its crew's stage through the crew it's in.
    CHECK(go.find(R"(t.Stage = one.Key(t.Crew, "todo"))") != std::string::npos);
    CHECK(go.find(", Once20261007Stages") != std::string::npos);
}

TEST_CASE("a project can start from a preset: what it makes when, named by name, with lists") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug      slug  required  unique  key
	workflow  enum { simple  steady } = workflow::simple
	start     stage
}
entity rank {
	crew   crew  required  key
	name   slug  required  key
	title  text  required
	may    list of permission
}
entity hand {
	crew    crew  required  key
	person  user  required  key
	rank    rank  required  key
}
roles rank per crew from hand {
	captain "Captain"  crew::create
}
entity stage {
	crew  crew  required  key
	name  slug  required  key
}
entity leg {
	crew   crew   required  key
	from   stage  required  key
	to     stage  required  key
	ranks  list of rank
}
command crew::create {
	permission authenticated
	if workflow == workflow::steady {
		create rank { crew = id  name = "bosun"  title = "Bosun"  may = [crew::create] }
		create stage { crew = id  name = "todo" }
		create stage { crew = id  name = "done" }
		create leg { crew = id  from = stage::todo  to = stage::done  ranks = [rank::captain, rank::bosun] }
		start = stage::todo
	}
}
view legs per crew {
	legs = each leg where crew == crew.id {
		order by from.name  to.name descending
		from.name  to.name
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "crew/crew.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find("if x.Workflow == WorkflowSteady {") != std::string::npos);
    CHECK(found->content.find(R"(x.Start = one.Key(x.ID, "todo"))") != std::string::npos);
    CHECK(found->content.find(R"(Order("from.name", "-to.name").)") != std::string::npos);
    CHECK(found->content.find(R"(&Rank{Crew: x.ID, Name: "bosun", Title: "Bosun", May: []string{"crew::create"}})") != std::string::npos);
    CHECK(found->content.find(R"(&Leg{Crew: x.ID, From: one.Key(x.ID, "todo"), To: one.Key(x.ID, "done"), Ranks: []string{one.Key(x.ID, "captain"), one.Key(x.ID, "bosun")}})") != std::string::npos);

    // A name the command doesn't make, and every crew doesn't start with, is a mistake.
    language::diagnostics wrong;
    std::vector<language::file> typo;
    std::string text = R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
entity stage {
	crew  crew  required  key
	name  slug  required  key
}
entity leg {
	crew  crew   required  key
	from  stage  required  key
	to    stage  required  key
}
command crew::create {
	permission authenticated
	create stage { crew = id  name = "todo" }
	create leg { crew = id  from = stage::todo  to = stage::dnoe }
}
}
)";
    typo.push_back(language::parse("main.one", text, wrong));
    language::check(typo, wrong);
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0].message == "stage::dnoe isn't a stage this command makes or every project starts with; those are stage::todo");
}

TEST_CASE("a move takes what it changes, and asks what exists and what roles are held") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
entity rank {
	crew   crew  required  key
	name   slug  required  key
	title  text  required
	may    list of permission
}
entity hand {
	crew    crew  required  key
	person  user  required  key
	rank    rank  required  key
}
roles rank per crew from hand {
	captain "Captain"  job::move
}
entity stage {
	crew  crew  required  key
	name  slug  required  key
}
entity leg {
	crew   crew   required  key
	from   stage  required  key
	to     stage  required  key
	ranks  list of rank
}
entity job {
	crew   crew  required
	stage  stage
}
command job::move {
	changes stage
	require exists(leg where from == was job.stage && to == job.stage && held(ranks))  "not from there to there"
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "crew/crew.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(var Move = one.Command[Job]("job::move").Fields("stage").)") != std::string::npos);
    CHECK(found->content.find(R"(found, err := one.Exists(c, one.Where[Leg]("from", c.Was("stage")).And("to", j.Stage), func(l *Leg) (bool, error) { return c.Held(l.Ranks) }))") != std::string::npos);
    CHECK(found->content.find("if !found {") != std::string::npos);
}

TEST_CASE("exists and was are a command's, and held takes a project's roles") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace crew {\nentity job {\n\ttitle  text\n\ttags  list of text\n}\n"
                                                "view jobs {\n\tbusy = exists(job where title == none)\n}\n"
                                                "command job::close {\n\trequire exists(job where held(tags))  \"no\"\n}\n}\n", out));
    language::check(files, out);
    REQUIRE(out.size() >= 2);
    CHECK(std::any_of(out.begin(), out.end(), [](const auto& d) { return d.message == "exists goes in a command, like require exists(step where ...)  \"...\""; }));
    CHECK(std::any_of(out.begin(), out.end(), [](const auto& d) { return d.message == "held takes a list of a project's roles, like held(roles)"; }));
}

TEST_CASE("a project's own roles become one.Roles, and a role it starts with its id") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
entity rank {
	crew   crew  required  key
	name   slug  required  key
	title  text  required
	may    list of permission
}
entity hand {
	crew    crew  required  key
	person  user  required  key
	rank    rank  required  key
}
roles rank per crew from hand {
	captain "Captain" {
		hand::create
	}
	deckhand "Deckhand"  hand::create
}
command crew::create {
	permission authenticated
	create hand {
		crew = id  person = me  rank = rank::captain
	}
}
command hand::create
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "crew/crew.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find("Rank: one.Key(x.ID, \"captain\")") != std::string::npos);
    CHECK(found->content.find("var Roles = one.Roles(one.Entity[Rank](), one.Entity[Crew](), one.Entity[Hand](), \"may\").\n"
                              "\tDefault(\"captain\", \"Captain\", \"hand::create\").\n"
                              "\tDefault(\"deckhand\", \"Deckhand\", \"hand::create\")\n") != std::string::npos);
    CHECK(found->content.find(", Roles)") != std::string::npos);
}

TEST_CASE("changes goes only in a command that changes what's there, naming the entity's fields") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace tracker {\nentity issue {\n\ttitle  text\n}\n"
                                                "command issue::create {\n\tchanges title\n}\ncommand issue::update {\n\tchanges colour\n}\n}\n", out));
    language::check(files, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].message == "changes goes in a command that changes what's there, like an update or a move, naming the fields it takes");
    CHECK(out[1].message == "'changes colour' names a field entity issue doesn't have");
}

TEST_CASE("what the library can't do yet stops the build, and says where") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace shop {
entity order {
	total  number
}
view orders {
	newest = first(order).total
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    CHECK(generated.files.empty());
    REQUIRE(generated.errors.size() == 1);
    CHECK(generated.errors[0].message == "not supported yet: a view value other than count(...), or a field of the entity a view per entity is for, or of what it points at");
    CHECK(generated.errors[0].where.line == 6);
}

TEST_CASE("what can't be done yet is said in the file it's written in") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("orders.one", "namespace shop {\nentity order {\n\ttotal  number\n}\n}\n", out));
    files.push_back(language::parse("closing.one", "namespace shop {\n\ncommand order::close {\n"
                                                   "\trequire first(order).total == 0  \"not yet\"\n}\n}\n", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.size() == 1);
    CHECK(generated.errors[0].path == "closing.one");
    CHECK(generated.errors[0].where.line == 4);
}

TEST_CASE("what a view can't do yet is said in the file the view is in") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("orders.one", "namespace shop {\nentity order {\n\ttotal  number\n}\n}\n", out));
    files.push_back(language::parse("pages.one", "namespace shop {\nview order_page per order {\n\ttotal = order.total + 1\n}\n}\n", out));
    files.push_back(language::parse("more.one", "namespace shop {\nentity line {\n\torder  order\n}\n}\n", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.size() == 1);
    CHECK(generated.errors[0].path == "pages.one");
    CHECK(generated.errors[0].where.line == 3);
}

TEST_CASE("a command names its entity's fields plainly, or with the entity's name, alike") {
    auto backend = [](const std::string& condition) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", "namespace tracker {\n"
                                                    "entity project {\n\tname  text  required\n\ttakes_reports  boolean = false\n}\n"
                                                    "entity report {\n\tproject  project  required\n\ttitle  text  required\n}\n"
                                                    "command report::create {\n\tpermission authenticated\n"
                                                    "\trequire " + condition + "  \"this project doesn't take reports\"\n}\n}\n", out));
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
        for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
        std::string all;
        for (const auto& f : generated.files) all += f.content;
        return all;
    };
    auto plain = backend("project.takes_reports");
    CHECK(plain.find("TakesReports") != std::string::npos);
    CHECK(backend("report.project.takes_reports") == plain);
}

TEST_CASE("generated Go is about as long as the .one it came from") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/library", "/examples/tracker"}) {
        std::size_t source = 0;
        for (const auto& path : platform::find_one_files({root + project})) {
            auto text = platform::read_file(path);
            source = std::max<std::size_t>(source, std::count(text->begin(), text->end(), '\n'));
        }
        for (const auto& f : api(project).files) {
            CAPTURE(project);
            CAPTURE(f.path);
            CHECK(static_cast<std::size_t>(std::count(f.content.begin(), f.content.end(), '\n')) <= std::max<std::size_t>(source, 60));
        }
    }
}

TEST_CASE("Go names") {
    CHECK(generators::api_detail::go_name("created_at") == "CreatedAt");
    CHECK(generators::api_detail::go_name("owner_id") == "OwnerID");
    CHECK(generators::api_detail::go_name("email") == "Email");
    CHECK(generators::api_detail::go_name("on_shelf") == "OnShelf");
}

TEST_CASE("an entity that keeps its history has changes a view can list, by itself or by what it points at") {
    auto generated = api("/examples/tracker");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find("\tone.Record\n\tone.History\n") != std::string::npos);
    CHECK(file->content.find(R"(List("changes", one.Where[one.ChangeOf[Issue]]("issue", one.Subject)).)") != std::string::npos);
    CHECK(file->content.find(R"(List("timeline", one.Where[one.ChangeOf[Issue]]("project", one.Subject)).)") != std::string::npos);
    CHECK(file->content.find("\tLimit(50).\n") != std::string::npos);
}

TEST_CASE("a github webhook makes what each event's handler creates, and is part of its module") {
    auto generated = api("/examples/tracker");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find(R"(var WebhookGithub = one.GitHub("/hooks/github").For(one.Entity[Project](), "repository").Mentions(one.Entity[Issue]()).)") !=
          std::string::npos);
    CHECK(file->content.find(R"(one.Create(c, &Mention{Issue: m.Issue, URL: m.URL, Kind: KindCommit, Title: m.Message, Author: m.Author}))") !=
          std::string::npos);
    CHECK(file->content.find(", WebhookGithub,") != std::string::npos);
}

TEST_CASE("a view shows a project's webhook secret through the library") {
    auto generated = api("/examples/tracker");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find("\tGitHubSecret(\"webhook_secret\")") != std::string::npos);
}

TEST_CASE("two entities whose fields share a name and a choice get constants of their own") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-shared-choices";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace desk {\n"
                         "entity issue {\n\ttitle  text\n\tstatus  enum open | closed = status::open\n\tsize  enum small | large\n}\n"
                         "entity report {\n\ttitle  text\n\tstatus  enum open | resolved = status::open\n}\n"
                         "command issue::close {\n\trequire status == status::open  \"already closed\"\n\tstatus = status::closed\n}\n"
                         "command report::resolve {\n\tstatus = status::resolved\n}\n"
                         "}\n");
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse((dir / "main.one").string(), *platform::read_file((dir / "main.one").string()), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    const auto* desk = find(generated.files, "desk/desk.go");
    REQUIRE(desk != nullptr);
    const auto& go = desk->content;
    CHECK(go.find("IssueStatusOpen") != std::string::npos);
    CHECK(go.find("ReportStatusOpen") != std::string::npos);
    CHECK(go.find("ReportStatusResolved") != std::string::npos);
    CHECK(go.find("if i.Status != IssueStatusOpen {") != std::string::npos);
    CHECK(go.find("i.Status = IssueStatusClosed") != std::string::npos);
    CHECK(go.find("r.Status = ReportStatusResolved") != std::string::npos);
    // A field no other entity shares keeps its short names.
    CHECK(go.find("SizeSmall") != std::string::npos);
    CHECK(go.find("IssueSizeSmall") == std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("Go written by hand beside a .one file is built into its namespace's package") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-backend";
    fs::remove_all(dir);
    fs::create_directories(dir / "backend");
    const std::string go = "// Written by hand.\n\npackage desk\n\nfunc init() {}\n";
    platform::write_file((dir / "backend" / "deploy.go").string(), go);
    platform::write_file((dir / "main.one").string(),
                         "namespace desk {\nentity ticket {\n\ttitle  text\n}\ncommand ticket::create\nbackend deploy\n}\n");
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse((dir / "main.one").string(), *platform::read_file((dir / "main.one").string()), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    const auto* copied = find(generated.files, "desk/deploy.go");
    REQUIRE(copied != nullptr);
    CHECK(copied->content == go);
    fs::remove_all(dir);
}

TEST_CASE("a field starting as the person's username tells the library so") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-username";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace studio {\nentity project {\n\towner  text  key  = me.username\n\tname  slug  key\n}\ncommand project::create\n}\n");
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse((dir / "main.one").string(), *platform::read_file((dir / "main.one").string()), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    const auto* go = find(generated.files, "studio/studio.go");
    REQUIRE(go != nullptr);
    CHECK(go->content.find(R"(one:"key,default=me.username")") != std::string::npos);
    CHECK(go->content.find(R"(one:"key,slug")") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a view per entity can show a field of what it points at") {
    auto generated = api("/examples/tracker");
    const auto* tracker = find(generated.files, "tracker/tracker.go");
    REQUIRE(tracker != nullptr);
    CHECK(tracker->content.find(R"(Copy("visibility", "project.visibility"))") != std::string::npos);
}
