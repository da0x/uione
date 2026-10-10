// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The backend and rules generators are held to site/target, like the web generator.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/indexes.hpp"
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
command card::update
command arrow::delete
command board::update
command column::delete {
	input into column
	input note text
	require into != id  "pick another column"
	each card where column == id {
		dispatch card::update { column = into }
	}
	each arrow where from == id || to == id {
		dispatch arrow::delete
	}
	if board.start == id {
		dispatch board::update { id = board  start = into }
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
    // Each it picks is changed or deleted by its own command, dispatched.
    CHECK(go.find("eachCard := func(card *Card) error {") != std::string::npos);
    CHECK(go.find(R"(if err := one.EachIn(c, "column", x.ID, eachCard); err != nil {)") != std::string::npos);
    CHECK(go.find("if err := one.DispatchUpdate(c, card.ID, func(dispatched *Card) {\n\t\t\t\t\tdispatched.Column = into\n\t\t\t\t}, nil, nil); err != nil {") !=
          std::string::npos);
    CHECK(go.find(R"(one.EachIn(c, "from", x.ID, eachArrow))") != std::string::npos);
    CHECK(go.find(R"(one.EachIn(c, "to", x.ID, eachArrow))") != std::string::npos);
    CHECK(go.find("one.DispatchDelete[Arrow](c, arrow.ID, nil, nil)") != std::string::npos);
    CHECK(go.find("if err := one.DispatchUpdate(c, x.Board, func(dispatched *Board) {\n\t\t\t\tdispatched.Start = into") != std::string::npos);
}

TEST_CASE("each other link picks rows of the command's own kind by their fields") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace board {
entity issue {
	title  text
}
entity link {
	from  issue
	to    issue
	pair  link
}
command link::delete {
	each other link where other.pair == id || other.id == pair {
		dispatch link::delete
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
    CAPTURE(go);
    CHECK(go.find(R"(one.EachIn(c, "pair", l.ID, eachLink))") != std::string::npos);
    CHECK(go.find(R"(one.EachIn(c, "id", l.Pair, eachLink))") != std::string::npos);
}

TEST_CASE("a match is one.Match, with each choice's value") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace board {
entity issue {
	title  text
}
entity link {
	from      issue
	to        issue
	pair      link
	relation  enum  blocks | blocked_by | relates
	weight    number
}
command link::create {
	if pair == none {
		dispatch link::create {
			pair = id  from = to  to = from
			relation = match relation {
				blocks      blocked_by
				blocked_by  blocks
				relates     relates
			}
			weight = match relation {
				blocks  2
				else    1
			}
		}
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "board/board.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CHECK(go.find(R"(one.Match(l.Relation, map[string]string{RelationBlocks: RelationBlockedBy, RelationBlockedBy: RelationBlocks, RelationRelates: RelationRelates}, ""))") !=
          std::string::npos);
    CHECK(go.find(R"(one.Match(l.Relation, map[string]float64{RelationBlocks: 2}, 1))") != std::string::npos);
}

TEST_CASE("an update may set the field a table's rows are dragged into order by") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace board {
entity column {
	title     text
	position  number
}
command column::update
view columns {
	each column {
		order by position
		title  position
	}
}
screen "Columns" /columns {
	table columns {
		reorder position
		title
	}
	form column::update {
		title
	}
}
}
)", out));
    language::check(files, out);
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "board/board.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(one.Command[Column]("column::update").Fields("title", "position"))") != std::string::npos);
}

TEST_CASE("a command can run another's create, making the entity and doing what that command does") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace work {
enum preset {
	simple  "Simple"
	none    "None"
}
entity project {
	slug    slug  required  unique  key
	preset  preset = preset::simple
}
entity board {
	project  project  required  key
	name     text     required  key = slug(title)
	title    text     required
	preset   preset = preset::simple
	start    phase
}
entity phase {
	board    board    required  key
	name     text     required  key
	project  project  required
}
command project::create {
	by anyone signed in
	dispatch board::create { project = id  name = "main"  title = slug  preset = preset }
}
command board::create {
	if preset == preset::simple {
		dispatch phase::create { board = id  name = "open"  project = project }
		start = phase::open
	}
}
command phase::create
command phase::update
once "2026-10-08 boards" {
	each phase {
		dispatch phase::update { board = board::main }
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "work/work.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CHECK(go.find("func createBoard(c *one.Ctx, b *Board) error {") != std::string::npos);
    CHECK(go.find(R"(var BoardCreate = one.Command[Board]("board::create").Do(createBoard))") != std::string::npos);
    CHECK(go.find(R"(made := &Board{Project: p.ID, Name: "main", Title: p.Slug, Preset: p.Preset})") != std::string::npos);
    CHECK(go.find("if err := one.DispatchCreate(c, made, nil, createBoard); err != nil {") != std::string::npos);
    CHECK(go.find(R"(made := &Phase{Board: b.ID, Name: "open", Project: b.Project})") != std::string::npos);
    CHECK(go.find("if err := one.DispatchCreate(c, made, nil, nil); err != nil {") != std::string::npos);
    // A once gives what was made before boards their board, though it's a key.
    CHECK(go.find(R"(dispatched.Board = one.Key(p.Project, "main"))") != std::string::npos);

    // What's run is a create that's declared.
    language::diagnostics wrong;
    std::vector<language::file> bad;
    bad.push_back(language::parse("main.one", "namespace work {\nentity board {\n\ttitle  text\n}\nentity project {\n\tname  text\n}\n"
                                              "command project::create {\n\tboard::update { title = name }\n}\n}\n", wrong));
    language::check(bad, wrong);
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0].message.starts_with("expected"));
}

TEST_CASE("a view per entity lists what a field of that entity picks, like the steps of an issue's board") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace work {
entity board {
	title  text
}
entity step {
	board  board
	title  text
}
entity issue {
	board  board
	title  text
}
view issue_page per issue {
	title = issue.title
	steps = each step where board == issue.board {
		title
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "work/work.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(List("steps", one.Where[Step]("board", one.SubjectField("board"))))") != std::string::npos);
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
command rank::update
command stage::create
once "2026-10-07 stages" {
	each rank where name == "mate" {
		dispatch rank::update {
			title = "First mate"
			may = [
				crew::update,
				task::update,
			]
		}
	}
	each crew {
		dispatch stage::create { crew = id  name = "todo" }
		dispatch crew::update { start = stage::todo }
	}
	each task {
		dispatch task::update { stage = stage::todo }
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
    CHECK(go.find(R"(if err := one.DispatchUpdate(c, r.ID, func(dispatched *Rank) {)") != std::string::npos);
    CHECK(go.find(R"(dispatched.May = []string{"crew::update", "task::update"})") != std::string::npos);
    CHECK(go.find(R"(one.Each[Crew]("", nil, func(c *one.Ctx, x *Crew) error {)") != std::string::npos);
    CHECK(go.find(R"(made := &Stage{Crew: x.ID, Name: "todo"})") != std::string::npos);
    CHECK(go.find(R"(dispatched.Start = one.Key(x.ID, "todo"))") != std::string::npos);
    // A task names its crew's stage through the crew it's in.
    CHECK(go.find(R"(dispatched.Stage = one.Key(t.Crew, "todo"))") != std::string::npos);
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
define role captain "Captain" in crew {
	role::create
	stage::create
	leg::create
}
entity stage {
	crew  crew  required  key
	name  slug  required  key
}
entity leg {
	crew   crew   required  key
	from   stage  required  key
	to     stage  required  key
	roles  list of role
}
command crew::create {
	by anyone signed in
	if workflow == workflow::steady {
		dispatch role::create { crew = id  name = "bosun"  title = "Bosun"  may = [crew::create] }
		dispatch stage::create { crew = id  name = "todo" }
		dispatch stage::create { crew = id  name = "done" }
		dispatch leg::create { crew = id  from = stage::todo  to = stage::done  roles = [role::captain, role::bosun] }
		start = stage::todo
	}
}
command role::create
command stage::create
command leg::create
view legs per crew {
	legs = each leg in crew {
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
    CHECK(found->content.find(R"(&Role{Crew: x.ID, Name: "bosun", Title: "Bosun", May: []string{"crew::create"}})") != std::string::npos);
    CHECK(found->content.find(R"(&Leg{Crew: x.ID, From: one.Key(x.ID, "todo"), To: one.Key(x.ID, "done"), Roles: []string{one.Key(x.ID, "captain"), one.Key(x.ID, "bosun")}})") != std::string::npos);

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
	by anyone signed in
	dispatch stage::create { crew = id  name = "todo" }
	dispatch leg::create { crew = id  from = stage::todo  to = stage::dnoe }
}
command stage::create
command leg::create
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
define role captain "Captain" in crew {
	job::move
}
entity stage {
	crew  crew  required  key
	name  slug  required  key
}
entity leg {
	crew   crew   required  key
	from   stage  required  key
	to     stage  required  key
	roles  list of role
}
entity job {
	crew   crew  required
	stage  stage
}
command job::move {
	changes stage
	require exists(leg where from == was job.stage && to == job.stage && held(roles))  "not from there to there"
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
    CHECK(found->content.find(R"(found, err := one.Exists(c, one.Where[Leg]("from", c.Was("stage")).And("to", j.Stage), func(l *Leg) (bool, error) { return c.Held(l.Roles) }))") != std::string::npos);
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
define role captain "Captain" in crew {
	member::create
}
define role deckhand "Deckhand" in crew {
	member::create
}
command crew::create {
	by anyone signed in
	dispatch member::create {
		crew = id  person = me  role = role::captain
	}
}
command member::create
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "crew/crew.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find("Role: one.Key(x.ID, \"captain\")") != std::string::npos);
    CHECK(found->content.find("var Roles = one.Roles(one.Entity[Role](), one.Entity[Crew](), one.Entity[Member](), \"may\").\n"
                              "\tDefault(\"captain\", \"Captain\", \"member::create\").\n"
                              "\tDefault(\"deckhand\", \"Deckhand\", \"member::create\")\n") != std::string::npos);
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
    CHECK(generated.errors[0].message == "not supported yet: a view value other than count(...), first(...).field, or a field of the entity a view per entity is for, or of what it points at");
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
                                                    "command report::create {\n\tby anyone signed in\n"
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
    CHECK(file->content.find(R"(one.DispatchCreate(c, &Mention{Issue: m.Issue, URL: m.URL, Kind: KindCommit, Title: m.Message, Author: m.Author}, nil, nil))") !=
          std::string::npos);
    CHECK(file->content.find(R"(var ServiceGithub = one.Service("github", "GitHub", "mention::create"))") != std::string::npos);
    CHECK(file->content.find(", ServiceGithub, WebhookGithub)") != std::string::npos);
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

TEST_CASE("a person's news is the changes on boards they follow or of what's assigned them, not their own") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace tracker {
entity board {
	title      text
	followers  list of user
}
entity issue history {
	board      board  required
	title      text
	assignees  list of user
}
view news per user {
	changes = each change in issue where (board.followers has me || assignees has me) && created_by != me {
		issue  field  after
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find(R"(List("changes", one.All[one.ChangeOf[Issue]]().Except("created_by", one.Viewer).Has("board.followers", one.Viewer).)"
                             R"(Or(one.All[one.ChangeOf[Issue]]().Except("created_by", one.Viewer).Has("assignees", one.Viewer))).)") != std::string::npos);
}

TEST_CASE("a list kept to its newest rows has the index each way it's picked by needs") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace tracker {
entity project {
	slug  text  key
}
entity board {
	project    project
	followers  list of user
}
entity issue history {
	project    project
	board      board
	assignees  list of user
}
view news per user {
	changes = each change in issue where (board.followers has me || assignees has me) && created_by != me {
		order by created_at descending
		limit 30
		field
	}
}
view project_page per project {
	timeline = each change in issue in project {
		order by created_at descending
		limit 50
		field
	}
	everything = each change in issue in project {
		field
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto file = generators::indexes_file(files);
    CHECK(file.path == "firestore.indexes.json");
    CHECK(file.content == R"({
  "indexes": [
    { "collection": "tracker_issue_history", "fields": [{ "field": "assignees", "array": "CONTAINS" }, { "field": "created_at", "order": "DESCENDING" }] },
    { "collection": "tracker_issue_history", "fields": [{ "field": "board", "order": "ASCENDING" }, { "field": "created_at", "order": "DESCENDING" }] },
    { "collection": "tracker_issue_history", "fields": [{ "field": "project", "order": "ASCENDING" }, { "field": "created_at", "order": "DESCENDING" }] }
  ]
}
)");
}

TEST_CASE("a comment kept in its issue's history, mentioning people, is tagged so") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace tracker {
entity issue history {
	title  text
}
entity comment history of issue {
	issue      issue     required
	body       markdown
	mentioned  list of user = mentions(body)
}
view news per user {
	changes = each change in issue where mentioned has me {
		field
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "tracker/tracker.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find(R"(one:"required,refers=tracker::issue,history")") != std::string::npos);
    CHECK(file->content.find(R"(one:"refers=user,mentions=body")") != std::string::npos);
    CHECK(file->content.find(R"(one.All[one.ChangeOf[Issue]]().Has("mentioned", one.Viewer))") != std::string::npos);
}

TEST_CASE("an invitation, and steps on signing in, become what the library does then") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace work {
entity team {
	slug  text  required  key
}
entity seat {
	team    team  required  key
	person  user  required  key
}
entity invitation invites seat {
	team   team   required  key
	email  email  required  key
}
on signin {
	delete each invitation where email == me.email
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    const auto* file = find(generated.files, "work/work.go");
    REQUIRE(file != nullptr);
    CHECK(file->content.find(R"(var InvitationInvites = one.Invites[Invitation, Seat]("email", "person"))") != std::string::npos);
    CHECK(file->content.find(R"(one.DeleteEach[Invitation]("email", one.MyEmail),)") != std::string::npos);
    CHECK(file->content.find("var OnSignIn = one.OnSignIn(") != std::string::npos);
}

TEST_CASE("a view's count of its own list's rows becomes CountRows, each test a RowWhere") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace work {
entity issue history {
	title  text
}
entity reader {
	person  user  key  = me
	seen_at  date
}
view news per user {
	changes = each change in issue where created_by != me {
		field  created_at
	}
	seen = first(reader where person == me).seen_at
	unread = count(changes where created_at > seen && field != seen)
	listed = count(changes)
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "work/work.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CHECK(go.find(R"(CountRows("unread", "changes", one.RowWhere("created_at", ">", "seen"), one.RowWhere("field", "!=", "seen")))") != std::string::npos);
    CHECK(go.find(R"(CountRows("listed", "changes"))") != std::string::npos);
}

TEST_CASE("a command asks whether a list holds one of the project's own, and whether any of an entity exists") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
entity leg {
	crew   crew  required  key
	name   slug  required  key
	roles  list of role
}
define role captain "Captain" in crew {
	leg::update
	role::delete
	member::delete
}
command leg::update
command member::delete
command role::delete {
	require !exists(member where role == id)  "give its people another role first"
}
once "2026-10-09 captains" {
	each leg {
		if roles has role::captain {
			dispatch leg::update { remove role::captain from roles }
		}
	}
	each member {
		if role == role::captain {
			dispatch member::delete
		}
	}
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "crew/crew.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CHECK(go.find(R"(if one.Has(l.Roles, one.Key(l.Crew, "captain")) {)") != std::string::npos);
    CHECK(go.find(R"(dispatched.Roles = one.Remove(dispatched.Roles, one.Key(l.Crew, "captain")))") != std::string::npos);
    CHECK(go.find(R"(if m.Role == one.Key(m.Crew, "captain") {)") != std::string::npos);
    CHECK(go.find(R"(found, err := one.Exists[Member](c, one.Where[Member]("role", r.ID), nil))") != std::string::npos);
}
