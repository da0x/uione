// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The backend and rules generators are held to site/target, like the web generator.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/web.hpp"
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

    std::vector<language::file> read_at(const std::string& dir) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({dir})) files.push_back(language::parse(path, *platform::read_file(path), out));
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

TEST_CASE("a field can be worked out from what another points at, and a list can start with values") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace shop {
entity project {
	slug  text  required  key
}
entity board {
	project  project  required  key
	name     text     required  key
}
entity step {
	board    board    required  key
	name     text     required  key
	project  project  = board.project
	roles    list of role  = [role::clerk, role::printer]
}
define role clerk "Clerk" in project {
	step::create
}
define role printer "Printer" in project {
}
command step::create
command board::create {
	by anyone signed in
	dispatch step::create { board = id  name = "proof" }
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "shop/shop.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(one:"refers=shop::project,through=board.project")") != std::string::npos);
    CHECK(found->content.find(R"(one:"refers=shop::role,default=clerk|printer,within=project")") != std::string::npos);
}

TEST_CASE("a workflow over stage records depends on others until each is at a stage done when says, and a link names its own record in an each") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace yard {
enum status {
	open
	success
}
entity project {
	slug  text  required  key
}
entity stage {
	project    project  required  key
	name       text     required  key
	title      text     required
	status     status   = status::open
	worked_by  list of role
}
entity step {
	project  project  required  key
	from     stage    required  key
	to       stage    required  key
	roles    list of role
}
entity job {
	project   project  required  key
	number    serial   per project  key
	title     text     required
	stage     stage
	waits_on  list of job
}
entity tie {
	project  project  required
	from     job      required
	to       job      required
}
define role hand "Hand" in project {
	job::update
	job::claim
	job::renew
	job::release
	job::move
	tie::create
}
define workflow job by stage {
	worked by worked_by
	steps step
	depends on waits_on
	done when stage.status == status::success
}
command tie::create {
	each job where id == from {
		dispatch job::update { add tie.to to waits_on }
	}
}
command job::update
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "yard/yard.go"; });
    REQUIRE(found != generated.files.end());
    CAPTURE(found->content);
    // Neither taken nor moved while what it waits on isn't at a success stage.
    CHECK(found->content.find(R"(xThrough, throughErr := one.Read[Stage](c, x.Stage))") != std::string::npos);
    CHECK(found->content.find(R"(if throughErr != nil || xThrough.Status != "success" {)") != std::string::npos);
    CHECK(found->content.find(R"(return c.Fail("waiting on " + one.Listed(waiting)))") != std::string::npos);
    // tie.to, the tie's own, read from inside the each over jobs.
    CHECK(found->content.find(R"(dispatched.WaitsOn = one.Add(dispatched.WaitsOn, t.To))") != std::string::npos);
}

// A closed job keeps its stage, to reopen where it was, and while closed it's in no
// one's queue and can't be claimed.
TEST_CASE("a workflow's while keeps what it works to what holds, in its queue and its claim") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace yard {
entity project {
	slug  text  required  key
}
entity stage {
	project    project  required  key
	name       text     required  key
	worked_by  list of role
}
entity step {
	project  project  required  key
	from     stage    required  key
	to       stage    required  key
	roles    list of role
}
entity job {
	project  project  required  key
	number   serial   per project  key
	stage    stage
	closed   enum  done | dropped
}
define role hand "Hand" in project {
	job::claim
	job::renew
	job::release
	job::move
}
define workflow job by stage {
	worked by worked_by
	steps step
	while closed == none
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "yard/yard.go"; });
    REQUIRE(found != generated.files.end());
    CAPTURE(found->content);
    CHECK(found->content.find(R"(return c.Fail("it isn't worked now: only while closed == none"))") != std::string::npos);
    CHECK(found->content.find(R"(Has("stage.worked_by", one.Subject).And("closed", nil))") != std::string::npos);
}

TEST_CASE("who may read is said once on an entity, and the views of what's within it read the same") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace shop {
entity project {
	readers member
	public when visibility == visibility::public
	slug        text  required  key
	visibility  enum  public | private = visibility::private
}
entity order {
	project  project  required
	title    text
	author   user  = me
}
define role clerk "Clerk" in project {
}
view project_page per project {
	slug
}
view order_page per order {
	title
}
view order_notes per order {
	readers order.author
	title
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "shop/shop.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    auto view = [&](const std::string& name) {
        std::size_t at = go.find("one.View(\"" + name + "\")");
        return at == std::string::npos ? std::string() : go.substr(at, go.find('\n', at) - at);
    };
    CHECK(view("project_page").find(R"(.Readers(one.Entity[Member]()).PublicWhen("visibility", "public"))") != std::string::npos);
    CHECK(view("order_page").find(R"(.Readers(one.Entity[Member]()).PublicWhen("project.visibility", "public"))") != std::string::npos);
    // A view that says its own readers says all of who may read it.
    CHECK(view("order_notes").find("PublicWhen") == std::string::npos);
    CHECK(view("order_notes").find("Readers(one.Entity[Member]())") == std::string::npos);
}

TEST_CASE("a view within two that say who reads it is read as both say, public only while both are") {
    auto generate = [](const std::string& board_reads) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", "namespace shop {\nentity member {\n\tproject  project  required  key\n\tperson  user  required  key\n}\n"
                                                    "entity project {\n\treaders member\n\tpublic when visibility == visibility::public\n\tslug  slug  required  unique  key\n"
                                                    "\tvisibility  enum  public | private = visibility::public\n}\n"
                                                    "entity board {\n" + board_reads + "\tproject  project  required\n\tvisibility  enum  public | private = visibility::public\n}\n"
                                                    "entity card {\n\tproject  project  required\n\tboard  board  required\n\ttitle  text\n}\n"
                                                    "view card_page per card {\n\ttitle = card.title\n}\n"
                                                    "view board_page per board {\n\treaders member\n\tpublic when {\n\t\tboard.visibility == visibility::public\n"
                                                    "\t\tboard.project.visibility == visibility::public\n\t}\n}\n}\n", out));
        language::check(files, out);
        std::vector<std::string> said;
        for (const auto& d : out) said.push_back(d.message);
        auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
        auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "shop/shop.go"; });
        std::string go = found == generated.files.end() ? "" : found->content;
        return std::pair{said, go};
    };
    auto [said, go] = generate("\treaders member\n\tpublic when visibility == visibility::public\n");
    CAPTURE(said);
    CAPTURE(go);
    auto view = [&](const std::string& name) {
        std::size_t at = go.find("one.View(\"" + name + "\")");
        return at == std::string::npos ? std::string() : go.substr(at, go.find('\n', at) - at);
    };
    // A card is within its project and its board: both conditions hold.
    bool both = view("card_page").find(R"(.Readers(one.Entity[Member]()).PublicWhen("project.visibility", "public").PublicWhen("board.visibility", "public"))") !=
                    std::string::npos ||
                view("card_page").find(R"(.Readers(one.Entity[Member]()).PublicWhen("board.visibility", "public").PublicWhen("project.visibility", "public"))") !=
                    std::string::npos;
    CHECK(both);
    // A view says several conditions, through more than one pointer.
    CHECK(view("board_page").find(R"(.PublicWhen("visibility", "public").PublicWhen("project.visibility", "public"))") != std::string::npos);
}

TEST_CASE("a view per something takes its fields bare, and what they point at by their own names") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace shop {
entity stage {
	title  text
}
entity order {
	number  serial  key
	title   text
	stage   stage
}
view order_page per order {
	number  title
	stage  stage.title
}
screen "{order_page.title}" /orders/:order {
	text "In {order_page.stage.title}"
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "shop/shop.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(Copy("number", "number"))") != std::string::npos);
    CHECK(found->content.find(R"(Copy("title", "title"))") != std::string::npos);
    CHECK(found->content.find(R"(Copy("stage", "stage"))") != std::string::npos);
    CHECK(found->content.find(R"(Copy("stage.title", "stage.title"))") != std::string::npos);
    // A view that isn't per anything has nothing to take them from.
    language::diagnostics wrong;
    std::vector<language::file> others;
    others.push_back(language::parse("main.one", "namespace shop {\nentity order {\n\ttitle  text\n}\nview orders {\n\ttitle\n}\n}\n", wrong));
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0].message == "a view per something takes its fields bare, like view order_page per order { title }; this one says what each value is, like total = count(order)");
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
	title
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
	start    stage
}
entity stage {
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
		dispatch stage::create { board = id  name = "open"  project = project }
		start = stage::open
	}
}
command stage::create
command stage::update
once "2026-10-08 boards" {
	each stage {
		dispatch stage::update { board = board::main }
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
    CHECK(go.find(R"(made := &Stage{Board: b.ID, Name: "open", Project: b.Project})") != std::string::npos);
    CHECK(go.find("if err := one.DispatchCreate(c, made, nil, nil); err != nil {") != std::string::npos);
    // A once gives what was made before boards their board, though it's a key.
    CHECK(go.find(R"(dispatched.Board = one.Key(s.Project, "main"))") != std::string::npos);

    // What's run is a create that's declared.
    language::diagnostics wrong;
    std::vector<language::file> bad;
    bad.push_back(language::parse("main.one", "namespace work {\nentity board {\n\ttitle  text\n}\nentity project {\n\tname  text\n}\n"
                                              "command project::create {\n\tboard::update { title = name }\n}\n}\n", wrong));
    language::check(bad, wrong);
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0].message.starts_with("expected"));
}

TEST_CASE("an update may set the field a board's cards are dragged into order by") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace work {
entity stage {
	title     text
	position  number
}
entity card {
	title     text
	stage     stage
	position  number
}
command card::update
view board_page {
	stages = each stage {
		order by position
		title
	}
	cards = each card {
		order by position
		title  stage  position
	}
}
screen "Cards" /cards {
	board board_page.cards by stage over board_page.stages {
		reorder position
		title
	}
}
view card_page per card {
	title
}
screen "Card" /cards/:card {
	form card::update {
		title
	}
}
}
)", out));
    language::check(files, out);
    REQUIRE(out.empty());
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "work/work.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find(R"(one.Command[Card]("card::update").Fields("title", "position"))") != std::string::npos);
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
	title
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

TEST_CASE("a claim asks whether a role of the stage's is held, and whether nobody has taken it") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
define role hand "Hand" in crew {
	job::claim
}
entity stage {
	crew       crew  required  key
	name       slug  required  key
	worked_by  list of role
}
entity job {
	crew      crew  required
	stage     stage
	takers    list of user
}
command job::claim {
	require held(stage.worked_by)  "this stage isn't worked by a role you hold"
	require takers == none || takers has me  "someone else has taken it"
	add me to takers
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
    // The stage is read, then whether one of its roles is held.
    CHECK(go.find("stage, err := one.Read[Stage](c, j.Stage)") != std::string::npos);
    CHECK(go.find("held, err := c.Held(stage.WorkedBy)") != std::string::npos);
    CHECK(go.find("if !held {") != std::string::npos);
    // A list compared with none is whether it's empty.
    CHECK(go.find("len(j.Takers) == 0") != std::string::npos);
    CHECK(go.find("[]string{}") == std::string::npos);
}

TEST_CASE("a lease is held until a time counted from now, and runs out by the clock") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
define role hand "Hand" in crew {
	job::claim
	job::release
}
entity job {
	crew        crew  required
	held_by     user
	held_until  date
}
command job::claim {
	require held_by == none || held_by == me || held_until < now  "someone else holds it"
	held_by = me
	held_until = 10 minutes from now
}
command job::release {
	require held_by == me  "you don't hold it"
	held_by = none
	held_until = none
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
    CAPTURE(go);
    CHECK(go.find("j.HeldUntil.Before(c.Now())") != std::string::npos);
    CHECK(go.find("j.HeldUntil = c.Now().Add(10 * time.Minute)") != std::string::npos);
    CHECK(go.find("j.HeldBy = \"\"") != std::string::npos);
    CHECK(go.find("j.HeldUntil = time.Time{}") != std::string::npos);
}

TEST_CASE("a workflow says in full who claims a job, for how long, and where it moves") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace shop {
entity shop {
	slug  slug  required  unique  key
}
define role proofreader "Proofreader" in shop {
	order::claim
	order::renew
	order::release
	order::move
}
entity stage {
	shop       shop  required  key
	name       slug  required  key
	title      text
	worked_by  list of role
}
entity step {
	shop   shop   required  key
	from   stage  required  key
	to     stage  required  key
	roles  list of role
}
entity order {
	shop   shop   required
	title  text
	stage  stage
}
define workflow order by stage {
	worked by worked_by
	steps step
	lease 30 minutes
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "shop/shop.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CAPTURE(go);
    // Who holds it, and until when.
    CHECK(go.find("HeldBy    string    `firestore:\"held_by\"") != std::string::npos);
    CHECK(go.find("HeldUntil time.Time `firestore:\"held_until\"") != std::string::npos);
    // A claim: a role of the stage's, or one that may take a step out of it, nobody
    // else holding it, and a lease.
    CHECK(go.find(R"(var Claim = one.Command[Order]("order::claim").)") != std::string::npos);
    CHECK(go.find("held, err := c.Held(stage.WorkedBy)") != std::string::npos);
    CHECK(go.find(R"(found2, err := one.Exists(c, one.Where[Step]("from", o.Stage), func(s *Step) (bool, error) { return c.Held(s.Roles) }))") != std::string::npos);
    CHECK(go.find("if !(held || found2) {") != std::string::npos);
    CHECK(go.find("if !(o.HeldBy == \"\" || o.HeldBy == c.Me() || o.HeldUntil.Before(c.Now())) {") != std::string::npos);
    CHECK(go.find("o.HeldUntil = c.Now().Add(30 * time.Minute)") != std::string::npos);
    CHECK(go.find(R"(var Renew = one.Command[Order]("order::renew").)") != std::string::npos);
    CHECK(go.find(R"(var Release = one.Command[Order]("order::release").)") != std::string::npos);
    // A move: along a step a role holds, by whoever holds it, which ends the lease.
    CHECK(go.find(R"(var Move = one.Command[Order]("order::move").Fields("stage").)") != std::string::npos);
    CHECK(go.find(R"(one.Where[Step]("from", c.Was("stage")).And("to", o.Stage))") != std::string::npos);
    // What each role works, for the shop's people.
    CHECK(go.find(R"(one.View("order_work").Per(one.Entity[Role]()).Readers(one.Entity[Member]()))") != std::string::npos);
    CHECK(go.find(R"(Has("stage.worked_by", one.Subject))") != std::string::npos);
}

TEST_CASE("a workflow is for an entity moving along stages that roles work, by steps") {
    auto errors = [](const std::string& workflow, const std::string& extra = "") {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", "namespace shop {\nentity shop {\n\tslug  slug  required  unique  key\n}\n"
                                                    "define role proofreader \"Proofreader\" in shop {\n\tshop::create\n}\n"
                                                    "command shop::create  by anyone signed in\n"
                                                    "entity stage {\n\tshop  shop  required  key\n\tname  slug  required  key\n\tworked_by  list of role\n\tlabel  text\n}\n"
                                                    "entity step {\n\tshop  shop  required  key\n\tfrom  stage  required  key\n\tto  stage  required  key\n\troles  list of role\n}\n"
                                                    "entity order {\n\tshop  shop  required\n\tstage  stage\n\ttitle  text\n}\n" + extra + workflow + "}\n", out));
        language::check(files, out);
        std::vector<std::string> messages;
        for (const auto& d : out) messages.push_back(d.message);
        return messages;
    };
    auto says = [](const std::vector<std::string>& messages, const std::string& message) {
        CAPTURE(messages);
        return std::find(messages.begin(), messages.end(), message) != messages.end();
    };
    CHECK(says(errors("define workflow bill by stage {\n\tworked by worked_by\n\tsteps step\n}\n"),
               "a workflow is for an entity, and there's no entity bill in namespace shop"));
    CHECK(says(errors("define workflow order by title {\n\tworked by worked_by\n\tsteps step\n}\n"),
               "order moves along a field pointing at its stages, and title isn't one of its fields that does"));
    CHECK(says(errors("define workflow order by stage {\n\tworked by label\n\tsteps step\n}\n"),
               "stage's label isn't a list of roles, like worked_by  list of role"));
    CHECK(says(errors("define workflow order by stage {\n\tworked by worked_by\n}\n"),
               "say which entity's records are the steps between stages, like steps step"));
    CHECK(says(errors("define workflow order by stage {\n\tworked by worked_by\n\tsteps shop\n}\n"),
               "a step goes from one stage to another: shop needs from  stage"));
    CHECK(says(errors("define workflow order by stage {\n\tworked by worked_by\n\tsteps step\n}\n", "command order::claim\n"),
               "the workflow makes order::claim; take out the one declared"));
}

TEST_CASE("a workflow's stages written in it are checked from the code: who claims, who moves where, and each role's queue") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace shop {
entity shop {
	slug  slug  required  unique  key
}
define role proofreader "Proofreader" in shop {
	order::claim
	order::renew
	order::release
	order::move
}
define role press_operator "Press operator" in shop {
	order::claim
	order::move
}
define role foreman "Foreman" in shop {
	order::move
}
command shop::create {
	by anyone signed in
}
enum stage {
	proofing  "Proofing"
	press     "Press"
	shipped   "Shipped"
}
entity order {
	shop   shop   required
	title  text
	stage  stage = stage::proofing
}
define workflow order by stage {
	proofing  worked by proofreader
	press     worked by press_operator
	proofing -> press
	press -> proofing  by foreman
	press -> shipped
}
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "shop/shop.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CAPTURE(go);
    // A role is asked about once, however many stages and steps name it.
    CHECK(go.find(R"(held, err := c.Held([]string{one.Key(o.Shop, "proofreader")}))") != std::string::npos);
    // Whoever may take a step out of a stage works it too: foreman, who sends press back to proofing.
    CHECK(go.find(R"(held2, err := c.Held([]string{one.Key(o.Shop, "press_operator"), one.Key(o.Shop, "foreman")}))") != std::string::npos);
    CHECK(go.find("if !(o.Stage == StageProofing && held || o.Stage == StagePress && held2) {") != std::string::npos);
    // A step is taken by who works where it starts, or by whom it names.
    CHECK(go.find(R"(held2, err := c.Held([]string{one.Key(o.Shop, "foreman")}))") != std::string::npos);
    CHECK(go.find("c.Was(\"stage\") == string(StageProofing) && o.Stage == StagePress && held || c.Was(\"stage\") == string(StagePress) && "
                  "o.Stage == StageProofing && held2 || c.Was(\"stage\") == string(StagePress) && o.Stage == StageShipped && held3") != std::string::npos);
    CHECK(go.find("o.HeldUntil = c.Now().Add(10 * time.Minute)") != std::string::npos);
    // Each role's queue: the shop's orders in the stages it works, foreman's in press too, and none for a role that works none.
    CHECK(go.find(R"(List("items", one.Where[Order]("shop", one.SubjectField("shop")).And("stage", StageProofing).When(one.SubjectField("name"), "proofreader").)"
                  R"(Or(one.Where[Order]("shop", one.SubjectField("shop")).And("stage", StagePress).When(one.SubjectField("name"), "press_operator")).)"
                  R"(Or(one.Where[Order]("shop", one.SubjectField("shop")).And("stage", StagePress).When(one.SubjectField("name"), "foreman"))))") != std::string::npos);
    // A role's name is a word of the code's, like press_operator, so it's text, made from its title when it isn't given.
    CHECK(go.find("Name  string   `firestore:\"name\" one:\"required,key,from=title\"`") != std::string::npos);
}

TEST_CASE("a workflow's stages written in it are each worked or reached, by roles there are, along steps that leave only worked stages") {
    auto errors = [](const std::string& workflow, const std::string& stages = "\tproofing  \"Proofing\"\n\tpress  \"Press\"\n\tshipped  \"Shipped\"\n") {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", "namespace shop {\nentity shop {\n\tslug  slug  required  unique  key\n}\n"
                                                    "define role proofreader \"Proofreader\" in shop {\n\torder::claim\n\torder::renew\n\torder::release\n\torder::move\n}\n"
                                                    "define role shipper \"Shipper\" in shop {\n\torder::claim\n}\n"
                                                    "command shop::create {\n\tby anyone signed in\n}\n"
                                                    "enum stage {\n" + stages + "}\n"
                                                    "entity order {\n\tshop  shop  required\n\tstage  stage\n}\n" + workflow + "}\n", out));
        language::check(files, out);
        std::vector<std::string> messages;
        for (const auto& d : out) messages.push_back(d.message);
        return messages;
    };
    auto says = [](const std::vector<std::string>& messages, const std::string& message) {
        CAPTURE(messages);
        return std::find(messages.begin(), messages.end(), message) != messages.end();
    };
    const std::string fine = "\tproofing worked by proofreader\n\tpress worked by shipper\n\tproofing -> press\n\tpress -> shipped\n";
    auto workflow = [](const std::string& lines) { return "define workflow order by stage {\n" + lines + "}\n"; };
    auto none = errors(workflow(fine));
    CAPTURE(none);
    CHECK(none.empty());
    CHECK(says(errors(workflow("\tproofing worked by proofreader\n\tproofing -> press\n")),
               "stage shipped is worked by nobody and no step reaches it; say who works it, like shipped worked by a role, or a step to it"));
    CHECK(says(errors(workflow(fine + "\tproofng worked by shipper\n")), "proofng isn't a stage of an order: its stages are proofing, press or shipped"));
    CHECK(says(errors(workflow(fine + "\tpress -> shiped\n")), "shiped isn't a stage of an order: its stages are proofing, press or shipped"));
    CHECK(says(errors(workflow(fine + "\tproofing worked by shipper\n")),
               "proofing is said twice; put the roles that work it on one line, like proofing worked by proofreader, shipper"));
    CHECK(says(errors(workflow("\tproofing worked by proofreeder\n\tpress worked by shipper\n\tproofing -> press\n\tpress -> shipped\n")),
               "there's no role proofreeder: the roles are proofreader and shipper"));
    CHECK(says(errors(workflow(fine + "\tpress -> proofing by foreman\n")), "there's no role foreman: the roles are proofreader and shipper"));
    CHECK(says(errors(workflow(fine + "\tshipped -> press\n")),
               "shipped is worked by nobody, so it's an end and no step leaves it; say who works it, like shipped worked by a role"));
    CHECK(says(errors(workflow(fine + "\tpress -> press\n")), "a step goes from one stage to another, and this one stays at press"));
    CHECK(says(errors(workflow(fine + "\tproofing -> press\n")), "the step proofing -> press is said twice"));
    CHECK(says(errors(workflow("\tproofing worked by proofreader\n\tpress worked by shipper\n\tshipped worked by shipper\n")),
               "say the steps between its stages, one a line, like proofing -> shipped"));
    CHECK(says(errors(workflow(fine + "\tworked by worked_by\n")),
               "stage is a choice, so its stages are said here: who works each, like proofing worked by a role; worked by and steps are for stages kept as records"));
    CHECK(says(errors(workflow(fine + "\tsteps step\n")),
               "stage is a choice, so the steps between its stages are said here, like proofing -> shipped; worked by and steps are for stages kept as records"));
}

TEST_CASE("one flow depends on another: an order waits on its plates, by name, until each is done, and a stage can chime") {
    const std::string flows = R"(namespace shop {
entity shop {
	slug  slug  required  unique  key
}
define role press_operator "Press operator" in shop {
	plate::claim
	plate::renew
	plate::release
	plate::move
	order::claim
	order::renew
	order::release
	order::move
}
command shop::create {
	by anyone signed in
}
enum plate_stage {
	to_do    "To do"
	etching  "Etching"
	etched   "Etched"
}
enum order_stage {
	to_do     "To do"
	printing  "Printing"
	printed   "Printed"
}
entity plate {
	shop   shop  required
	name   text  required
	stage  plate_stage = plate_stage::to_do
}
entity order {
	shop    shop  required
	title   text
	plates  list of plate
	stage   order_stage = order_stage::to_do
}
define workflow plate by stage {
	to_do    worked by press_operator
	etching  worked by press_operator
	to_do -> etching
	etching -> etched
	done at etched
}
)";
    auto compiled = [&](const std::string& order_flow) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", flows + order_flow + "}\n", out));
        language::check(files, out);
        return std::pair{std::move(files), std::move(out)};
    };
    auto [files, out] = compiled("define workflow order by stage {\n\tto_do  worked by press_operator\n\tprinting  worked by press_operator\n"
                                 "\tto_do -> printing\n\tprinting -> printed\n\tdone at printed\n\tdepends on plates\n}\n");
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "shop/shop.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CAPTURE(go);
    // Each plate is read in the move's transaction, and those not etched yet are named.
    CHECK(go.find("for _, id := range o.Plates {\n\t\t\tp, err := one.Read[Plate](c, id)") != std::string::npos);
    CHECK(go.find("if p.Stage != PlateStageEtched {\n\t\t\t\twaiting = append(waiting, p.Name)") != std::string::npos);
    // Only leaving its first stage waits; a move further on, or back, doesn't.
    CHECK(go.find("if !(len(waiting) == 0 || c.Was(\"stage\") != string(OrderStageToDo)) {\n\t\t\treturn c.Fail(\"waiting on \" + one.Listed(waiting))") !=
          std::string::npos);

    auto messages = [&](const std::string& order_flow) {
        std::vector<std::string> said;
        for (const auto& d : compiled(order_flow).second) said.push_back(d.message);
        return said;
    };
    auto says = [](const std::vector<std::string>& said, const std::string& message) {
        CAPTURE(said);
        return std::find(said.begin(), said.end(), message) != said.end();
    };
    const std::string steps = "\tto_do  worked by press_operator\n\tprinting  worked by press_operator\n\tto_do -> printing\n\tprinting -> printed\n";
    CHECK(says(messages("define workflow order by stage {\n" + steps + "\tdepends on title\n}\n"),
               "order depends on a list of what it waits on, and title isn't one of its fields that is, like title  list of order"));
    CHECK(says(messages("define workflow order by stage {\n" + steps + "\tdone at shipped\n}\n"),
               "shipped isn't a stage of an order: its stages are to_do, printing or printed"));
    // What it waits on says where its own work is done.
    language::diagnostics plain;
    std::vector<language::file> unfinished;
    std::string without = flows;
    without.replace(without.find("\tdone at etched\n"), std::string("\tdone at etched\n").size(), "");
    unfinished.push_back(language::parse("main.one", without + "define workflow order by stage {\n" + steps + "\tdepends on plates\n}\n}\n", plain));
    language::check(unfinished, plain);
    std::vector<std::string> unfinished_said;
    for (const auto& d : plain) unfinished_said.push_back(d.message);
    CHECK(says(unfinished_said, "order waits on a plate until it's done, so plate's workflow says which stage that is, like done at etched"));
    // A stage notifies with a chime; email and text are for later.
    CHECK(messages("define workflow order by stage {\n" + steps + "\tprinted  notify chime\n}\n").empty());
    CHECK(says(messages("define workflow order by stage {\n" + steps + "\tprinted  notify email\n}\n"),
               "notify email isn't sent yet; a stage notifies with a chime, like failed notify chime"));
    CHECK(says(messages("define workflow order by stage {\n" + steps + "\tprinted  notify bell\n}\n"),
               "bell isn't a way to notify; a stage notifies with a chime, like failed notify chime"));
    CHECK(says(messages("define workflow order by stage {\n" + steps + "\tprnted  notify chime\n}\n"),
               "prnted isn't a stage of an order: its stages are to_do, printing or printed"));
    // A message names a field, or waiting(...) of a list, in braces, and nothing else.
    CHECK(says(messages("command order::hold {\n\trequire title != none  \"waiting on {waiting(title)}\"\n}\n"),
               "a message says in braces a field of order, or waiting(...) of a list of them, like \"waiting on {waiting(plates)}\""));
}

TEST_CASE("an agent works the stages its role for services works, moving each along a step the workflow has") {
    auto messages = [](const std::string& agent, const std::string& services = " for services") {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", "namespace shop {\nentity shop {\n\tslug  slug  required  unique  key\n}\n"
                                                    "define role manager \"Manager\" in shop {\n\torder::move\n\tservice::create\n\tservice::key\n\tservice::revoke\n}\n"
                                                    "define role press_operator \"Press operator\" in shop" + services +
                                                    " {\n\torder::claim\n\torder::renew\n\torder::release\n\torder::move\n}\n"
                                                    "command shop::create {\n\tby anyone signed in\n}\n"
                                                    "command service::create\ncommand service::key\ncommand service::revoke\n"
                                                    "enum stage {\n\tto_do  \"To do\"\n\tprinting  \"Printing\"\n\tprinted  \"Printed\"\n\tfailed  \"Failed\"\n}\n"
                                                    "entity order {\n\tshop  shop  required\n\ttitle  text  required\n\tstage  stage = stage::to_do\n}\n"
                                                    "define workflow order by stage {\n\tto_do  worked by manager\n\tprinting  worked by press_operator\n"
                                                    "\tto_do -> printing\n\tprinting -> printed\n\tprinting -> failed\n\tfailed  worked by manager\n\tfailed -> to_do\n}\n" +
                                                    agent + "}\n", out));
        language::check(files, out);
        std::vector<std::string> said;
        for (const auto& d : out) said.push_back(d.message);
        return said;
    };
    auto says = [](const std::vector<std::string>& said, const std::string& message) {
        CAPTURE(said);
        return std::find(said.begin(), said.end(), message) != said.end();
    };
    auto agent = [](const std::string& rules) { return "define agent press for press_operator {\n" + rules + "}\n"; };
    auto fine = messages(agent("\ton order in printing  run \"tools/print {order.title} {id}\"  then printed  else failed\n"));
    CAPTURE(fine);
    CHECK(fine.empty());
    CHECK(says(messages(agent("\ton order in printing  run \"print\"  then printed\n"), ""),
               "press_operator is a role for people; an agent works as a role for services, like define role press_operator \"…\" in project for services { ... }"));
    CHECK(says(messages("define agent press for pressman {\n\ton order in printing  run \"print\"  then printed\n}\n"),
               "there's no role pressman: an agent works as a role for services, like define role pressman \"…\" in project for services { ... }"));
    CHECK(says(messages(agent("\ton order in to_do  run \"print\"  then printing\n")),
               "press_operator doesn't work to_do; say so in the workflow, like to_do  worked by press_operator"));
    CHECK(says(messages(agent("\ton order in printing  run \"print\"  then to_do\n")),
               "there's no step printing -> to_do for an order to take when the program succeeds; add it to the workflow, or say a stage a step reaches"));
    CHECK(says(messages(agent("\ton order in printing  run \"print\"  then printed  else to_do\n")),
               "there's no step printing -> to_do for an order to take when it fails; add it to the workflow, or say a stage a step reaches"));
    CHECK(says(messages(agent("\ton order in pressing  run \"print\"  then printed\n")), "pressing isn't a stage of an order's workflow"));
    CHECK(says(messages(agent("\ton plate in printing  run \"print\"  then printed\n")),
               "an agent works what moves by a workflow, and there's no workflow for plate: define workflow plate by stage { … }"));
    CHECK(says(messages(agent("\ton order in printing  run \"print {order.titel}\"  then printed\n")),
               "the program says in braces a field of an order, like {order.id}, and titel isn't one"));
    CHECK(says(messages(agent("\ton order in printing  run \"a\"  then printed\n\ton order in printing  run \"b\"  then printed\n")),
               "what the agent does with an order in printing is said twice"));
}

TEST_CASE("held takes a project's roles by name, the ones every project starts with") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace shop {\nentity shop {\n\tslug  slug  required  unique  key\n}\n"
                                                "define role proofreader \"Proofreader\" in shop {\n\torder::close\n}\n"
                                                "command shop::create {\n\tby anyone signed in\n}\n"
                                                "entity order {\n\tshop  shop  required\n}\n"
                                                "command order::close {\n\trequire held([role::proofreader, role::shipper, crate::x])  \"no\"\n}\n}\n", out));
    language::check(files, out);
    std::vector<std::string> messages;
    for (const auto& d : out) messages.push_back(d.message);
    CAPTURE(messages);
    CHECK(messages.size() == 2);
    CHECK(std::find(messages.begin(), messages.end(), "role::shipper isn't a role every project starts with; those are role::proofreader") != messages.end());
    CHECK(std::find(messages.begin(), messages.end(),
                    "held takes a project's roles by name, like held([role::shipper]), or a list of them, like held(roles)") != messages.end());
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

TEST_CASE("a role for services is said last, with the service accounts that hold it") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace crew {
entity crew {
	slug  slug  required  unique  key
}
define role captain "Captain" in crew {
	member::create
	service::create
	service::key
}
define role welder "Welder" in crew for services {
	crew::update
}
command crew::create {
	by anyone signed in
	dispatch member::create {
		crew = id  person = me  role = role::captain
	}
}
command crew::update
command member::create
command service::create
command service::key
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "crew/crew.go"; });
    REQUIRE(found != generated.files.end());
    const auto& go = found->content;
    CHECK(go.find("type Service struct {") != std::string::npos);
    CHECK(go.find("\tDefault(\"welder\", \"Welder\", \"crew::update\").\n\tServices(one.Entity[Service](), \"welder\")\n") != std::string::npos);
    CHECK(go.find(R"(one.Command[Service]("service::key"))") != std::string::npos);
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

TEST_CASE("a command reads a field two records away, each read in its transaction in turn") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace tracker {\n"
                                                "entity board {\n\tvisibility  enum  public | private = visibility::public\n}\n"
                                                "entity issue {\n\tboard  board  required\n}\n"
                                                "entity link {\n\tfrom  issue  required\n\tto  issue  required\n}\n"
                                                "command link::create {\n\tby anyone signed in\n"
                                                "\trequire from.board.visibility == to.board.visibility  \"link issues on boards alike\"\n}\n}\n", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    for (const auto& d : generated.errors) FAIL_CHECK(language::format(d));
    std::string all;
    for (const auto& f : generated.files) all += f.content;
    CAPTURE(all);
    CHECK(all.find("from, err := one.Read[Issue](c, l.From)") != std::string::npos);
    CHECK(all.find("fromBoard, err := one.Read[Board](c, from.Board)") != std::string::npos);
    CHECK(all.find("if fromBoard.Visibility != toBoard.Visibility {") != std::string::npos);
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

TEST_CASE("a file field is a path the library attaches, one or a list") {
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", R"(namespace press {
entity order {
	title     text  required
	proof     file
	pictures  list of file
}
command order::create
}
)", out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto found = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "press/press.go"; });
    REQUIRE(found != generated.files.end());
    CHECK(found->content.find("Proof    string   `firestore:\"proof\" one:\"file\"`") != std::string::npos);
    CHECK(found->content.find("Pictures []string `firestore:\"pictures\" one:\"file\"`") != std::string::npos);
}

// A project outside uione, built with UIONE_LIBRARIES naming a checkout of it, uses
// that checkout's one module and @uione packages, to be tried before they're released.
TEST_CASE("UIONE_LIBRARIES builds a project against a checkout's libraries rather than the released ones") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-libraries";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "import one\nproject trial {\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    ::setenv("UIONE_LIBRARIES", root.c_str(), 1);
    auto files = read_at(dir.string());
    auto api_files = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    auto web_files = generators::generate_web(files, dir.string(), (dir / "build/web").string());
    ::unsetenv("UIONE_LIBRARIES");
    auto without = generators::generate_api(files, dir.string(), (dir / "build/api").string());
    fs::remove_all(dir);
    const auto* mod = find(api_files.files, "go.mod");
    const auto* manifest = find(web_files, "package.json");
    REQUIRE(mod != nullptr);
    REQUIRE(manifest != nullptr);
    CHECK(mod->content.find("replace github.com/da0x/uione/one => " + platform::relative_import((dir / "build/api").string(), root + "/one")) != std::string::npos);
    CHECK(manifest->content.find("\"@uione/react\": \"file:" + fs::weakly_canonical(root).string() + "/packages/react\"") != std::string::npos);
    const auto* released = find(without.files, "go.mod");
    REQUIRE(released != nullptr);
    CHECK(released->content.find("replace") == std::string::npos);
}
