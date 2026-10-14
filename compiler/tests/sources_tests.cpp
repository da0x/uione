// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Every generated line says which .one line it came from, or that it's the same
// for every project, so `one show` can show what any line of uione turns into.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/project.hpp"
#include "generators/native.hpp"
#include "generators/web.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    generators::generated_project generate(const std::string& project) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({root + project})) {
            files.push_back(language::parse(path, *platform::read_file(path), out));
        }
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return generators::generate_project(files, root + project, root + project + "/build");
    }

    // The lines of a file that have text but no source.
    std::vector<std::string> gaps(const generators::output_file& f) {
        std::vector<std::string> found;
        std::size_t at = 0, n = 0;
        while (at < f.content.size()) {
            std::size_t end = f.content.find('\n', at);
            std::string text = f.content.substr(at, end - at);
            if (!text.empty() && (n >= f.sources.size() || !f.sources[n])) {
                found.push_back(f.path + ":" + std::to_string(n + 1) + ": " + text);
            }
            // A last line without a newline, as an image's is, ends it.
            if (end == std::string::npos) break;
            at = end + 1;
            ++n;
        }
        return found;
    }

} // namespace

TEST_CASE("every generated line knows where it came from") {
    for (const char* project : {"/site", "/examples/tasks", "/examples/library", "/examples/tracker", "/examples/print_shop"}) {
        auto generated = generate(project);
        REQUIRE(generated.errors.empty());
        for (const auto& f : generated.files) {
            CAPTURE(project);
            for (const auto& gap : gaps(f)) FAIL_CHECK(gap);
        }
    }
}

TEST_CASE("each agent is a program of its own, whose rules lead back to the lines saying them") {
    auto generated = generate("/examples/print_shop");
    REQUIRE(generated.errors.empty());
    auto found = std::ranges::find_if(generated.files, [](const auto& f) { return f.path == "agents/press/main.cpp"; });
    REQUIRE(found != generated.files.end());
    const auto& main = found->content;
    CAPTURE(main);
    CHECK(main.find(R"(press.rules.push_back({.queue = "shop::plate_work", .entity = "shop::plate", .along = "stage", .stage = "etching", )"
                    R"(.program = "tools/etch {plate.name}", .then = "etched", .otherwise = "failed", .lease_minutes = 1});)") != std::string::npos);
    CHECK(main.find(R"(.then = "printed", .otherwise = "failed", .lease_minutes = 10});)") != std::string::npos);
    CHECK(main.find("return one::client::work(app, loop, press);") != std::string::npos);
    // The line of the order's rule is the .one line saying it.
    auto source = *platform::read_file(root + "/examples/print_shop/main.one");
    int said = 0, at = 0;
    for (std::size_t i = 0, line = 1; i < source.size(); ++i) {
        if (source.compare(i, 22, "on order in printing  ") == 0) said = static_cast<int>(line);
        if (source[i] == '\n') ++line;
    }
    for (std::size_t i = 0, line = 0; i < main.size(); ++i) {
        if (main.compare(i, 40, R"(press.rules.push_back({.queue = "shop::o)") == 0) at = static_cast<int>(line);
        if (main[i] == '\n') ++line;
    }
    REQUIRE(said > 0);
    REQUIRE(found->sources[static_cast<std::size_t>(at)]);
    CHECK(found->sources[static_cast<std::size_t>(at)]->line == said);
    CHECK(std::ranges::any_of(generated.files, [](const auto& f) { return f.path == "agents/press/CMakeLists.txt"; }));
}

TEST_CASE("a board over stages written in the code has them as its columns, its workflow's steps a person takes, and chimes where it says") {
    language::diagnostics out;
    std::vector<language::file> files;
    for (const auto& path : platform::find_one_files({root + "/examples/print_shop"})) files.push_back(language::parse(path, *platform::read_file(path), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_project(files, root + "/examples/print_shop", root + "/examples/print_shop/build", language::targets_of(files));
    REQUIRE(generated.errors.empty());
    auto content = [&](const std::string& path) {
        auto found = std::ranges::find_if(generated.files, [&](const auto& f) { return f.path == path; });
        REQUIRE(found != generated.files.end());
        return found->content;
    };
    auto web = content("web/src/screens/main.tsx");
    CAPTURE(web);
    CHECK(web.find(R"(over={{ status: "live", data: { stages: [{ id: "to_do", title: "To do" }, { id: "etching", title: "Etching" }, { id: "etched", title: "Etched" }, )"
                   R"({ id: "failed", title: "Failed" }] } }} overList="stages" shown="title" statuses={{ etched: "success", failed: "failed" }} notify={["failed"]})") != std::string::npos);
    // Etching is the press's to move on, so a person's steps are only into it and back from Failed.
    CHECK(web.find(R"(steps: { status: "live", data: { steps: [{ from: "to_do", to: "etching" }, { from: "failed", to: "to_do" }] } }, list: "steps")") != std::string::npos);
    for (const char* target : {"desktop", "terminal"}) {
        auto native = content(std::string(target) + "/src/screens/main.cpp");
        CAPTURE(native);
        CHECK(native.find(R"(.over = state<view_state>(view_state{status::ready, ember::parse_json("{\"stages\":[{\"id\":\"to_do\",\"title\":\"To do\"},)") != std::string::npos);
        CHECK(native.find(R"(.notify = {"printed"}, .statuses = {{"printed", "success"}, {"failed", "failed"}})") != std::string::npos);
    }
}

TEST_CASE("a pipeline lanes cards by a field of theirs and lays each kind's stages along its row") {
    const std::string source = R"(namespace yard {
entity stage {
	title   text  required
	kinds   list of text
	status  text
}
entity job {
	title   text  required
	stage   stage
	labels  list of text
}
view yard_page {
	jobs = each job {
		title  stage  labels
	}
	stages = each stage {
		title  kinds  status
	}
}
screen "Yard" /yard {
	pipeline yard_page.jobs by stage over yard_page.stages {
		lanes by labels
		path by kinds
		status by status
		title
	}
}
}
)";
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", source, out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return f.path.find("screens/") != std::string::npos && f.content.find("<Pipeline") != std::string::npos; });
    REQUIRE(screen != web.end());
    CAPTURE(screen->content);
    CHECK(screen->content.find(R"(lanes="labels" path="kinds" statusBy="status")") != std::string::npos);
    auto native = generators::generate_native(files, root + "/examples/tasks", "terminal");
    auto drawn = std::ranges::find_if(native, [](const auto& f) { return f.content.find("pipeline_board(p, {") != std::string::npos; });
    REQUIRE(drawn != native.end());
    CAPTURE(drawn->content);
    CHECK(drawn->content.find(R"(.lanes = "labels", .path = "kinds", .status_by = "status")") != std::string::npos);
    // A path by a field that isn't a list says so.
    std::string single = source;
    single.replace(single.find("\tkinds   list of text\n"), std::string("\tkinds   list of text\n").size(), "\tkinds   text\n");
    language::diagnostics wrong;
    std::vector<language::file> again;
    again.push_back(language::parse("main.one", single, wrong));
    language::check(again, wrong);
    std::string said;
    for (const auto& d : wrong) said += language::format(d) + "\n";
    CHECK(said.find("stage.kinds lists the kinds of card that go through a stage, so it's a list") != std::string::npos);
}

TEST_CASE("cards tag each with its choices, and one that says hide when empty isn't there while it has none") {
    const std::string source = R"(namespace yard {
enum kind {
	public
	private
}
entity shed {
	title  text  required
	kind   kind  = kind::public
}
view yard_page {
	sheds = each shed {
		title  kind
	}
}
screen "Yard" /yard {
	cards yard_page.sheds {
		hide when empty
		title
		kind
	}
}
}
)";
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", source, out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return f.path.find("screens/") != std::string::npos && f.content.find("<Cards") != std::string::npos; });
    REQUIRE(screen != web.end());
    CAPTURE(screen->content);
    CHECK(screen->content.find(" hideEmpty") != std::string::npos);
    CHECK(screen->content.find("choices={{ kind: ") != std::string::npos);
    auto native = generators::generate_native(files, root + "/examples/tasks", "desktop");
    auto drawn = std::ranges::find_if(native, [](const auto& f) { return f.content.find("cards(p, {") != std::string::npos; });
    REQUIRE(drawn != native.end());
    CAPTURE(drawn->content);
    CHECK(drawn->content.find(R"(.choices = {{"kind", std::vector<one::screens::option>{{"public", "Public"}, {"private", "Private"}}}}, .hide_empty = true)") != std::string::npos);
}

TEST_CASE("a switch between a table, a board and a whiteboard draws one track above it, which each keeps to") {
    const std::string source = R"(namespace ideas {
entity wall {
	title  text  required
}
entity stage {
	wall   wall  required
	title  text  required
}
entity release {
	wall   wall  required
	title  text  required
}
entity idea {
	wall     wall  required
	title    text  required
	stage    stage
	release  release
	x  number
	y  number
	w  number
	h  number
}
command idea::place {
	changes x  y  w  h
}
view wall_page per wall {
	title
	ideas = each idea in wall {
		title  stage  release  x  y  w  h
	}
	stages = each stage in wall {
		title
	}
	releases = each release in wall {
		title
	}
}
screen "{wall_page.title}" /walls/:wall {
	table wall_page.ideas {
		track release over wall_page.releases
		title
	}
	board wall_page.ideas by stage over wall_page.stages {
		title
	}
	canvas wall_page.ideas by stage over wall_page.stages {
		place idea::place
		title
	}
}
}
)";
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", source, out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto native = generators::generate_native(files, root + "/examples/tasks", "desktop");
    auto drawn = std::ranges::find_if(native, [](const auto& f) { return f.content.find("switched(p, ") != std::string::npos; });
    REQUIRE(drawn != native.end());
    CAPTURE(drawn->content);
    std::string shared = R"([&] { auto t = track_spec{"release", wall_page, "releases", "title", {}}; t.key = "switched:ideas::wall_page.ideas"; t.above = true;)";
    // Drawn once above the switch, its dots from the board's stages.
    CHECK(drawn->content.find("tracked(p, " + shared + R"( t.by = "stage"; t.over = wall_page; t.over_list = "stages"; return t; }(), wall_page, "ideas", switched(p, )") != std::string::npos);
    // Each way it's shown keeps to its pick, the whiteboard too.
    CHECK(drawn->content.find(".track = " + shared + " return t; }()") != std::string::npos);
    CHECK(drawn->content.find(R"(.over_list = "stages", .track = )" + shared) != std::string::npos);
    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return f.path.find("screens/") != std::string::npos && f.content.find("<Switched") != std::string::npos; });
    REQUIRE(screen != web.end());
    CAPTURE(screen->content);
    CHECK(screen->content.find(R"(columns: {  }, above: true, by: "stage", over: wallPage, overList: "stages" }} view={wallPage} list="ideas">)") != std::string::npos);
    CHECK(screen->content.find(R"(<Canvas )") != std::string::npos);
    CHECK(screen->content.find(R"(overList="stages" track={{ field: "release", view: wallPage, list: "releases", shown: "title", columns: {  }, above: true }})") != std::string::npos);
}

TEST_CASE("a canvas places a list's cards by their fields, placed by a command and snapped to a grid, and a board shows instead while a field says so") {
    const std::string source = R"(namespace ideas {
enum style {
	kanban
	canvas
}
enum kind {
	note
	text
}
entity wall {
	title  text   required
	style  style  = style::kanban
}
entity stage {
	wall   wall   required
	title  text   required
	width  number
}
entity idea {
	wall   wall   required
	title  text   required
	stage  stage
	kind   kind   = kind::note
	x  number
	y  number
	w  number
	h  number
}
command idea::place {
	changes x  y  w  h
}
command idea::move {
	changes stage
}
command stage::update
view wall_page per wall {
	title  style
	ideas = each idea in wall {
		title  stage  kind  x  y  w  h
	}
	stages = each stage in wall {
		title  width
	}
}
screen "{wall_page.title}" /walls/:wall {
	table wall_page.ideas by stage over wall_page.stages when wall_page.style == style::kanban {
		title
	}
	board wall_page.ideas by stage over wall_page.stages when wall_page.style == style::kanban {
		title
	}
	canvas wall_page.ideas by stage over wall_page.stages when wall_page.style == style::canvas {
		place idea::place
		move idea::move
		regions by width
		resize stage::update
		kind kind
		grid 160 from 40 80
		title
	}
}
}
)";
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", source, out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return f.path.find("screens/") != std::string::npos && f.content.find("<Canvas") != std::string::npos; });
    REQUIRE(screen != web.end());
    CAPTURE(screen->content);
    CHECK(screen->content.find(R"(place="ideas::idea::place" grid={{ step: 160, x: 40, y: 80 }} by="stage")") != std::string::npos);
    CHECK(screen->content.find(R"(kind="kind")") != std::string::npos);
    // The table, the board and the canvas of the same list are one switch, opening in
    // the one whose when holds.
    CHECK(screen->content.find(R"(options={["Table", "Board", "Whiteboard"]} icons={["table", "board", "whiteboard"]} start={)") != std::string::npos);
    CHECK(screen->content.find(R"(=== "canvas"))) ? 2 : -1})") != std::string::npos);
    auto native = generators::generate_native(files, root + "/examples/tasks", "desktop");
    auto drawn = std::ranges::find_if(native, [](const auto& f) { return f.content.find("canvas_board(p, {") != std::string::npos; });
    REQUIRE(drawn != native.end());
    CAPTURE(drawn->content);
    CHECK(drawn->content.find(R"(.place = "ideas::idea::place", .grid = 160, .origin_x = 40, .origin_y = 80, .by = "stage")") != std::string::npos);
    CHECK(drawn->content.find(R"(.regions = "width", .move = "ideas::idea::move", .resize = "ideas::stage::update")") != std::string::npos);
    CHECK(drawn->content.find(R"(switched(p, "ideas::wall_page.ideas", "Show ideas as", {"Table", "Board", "Whiteboard"}, )") != std::string::npos);
    CHECK(drawn->content.find("? 2 : -1, table(p, {") != std::string::npos);
    // Without the fields it places by, or with one that isn't a number, it says so.
    std::string missing = source;
    missing.replace(missing.find("\t\ttitle  stage  kind  x  y  w  h\n"), std::string("\t\ttitle  stage  kind  x  y  w  h\n").size(), "\t\ttitle  stage  kind  x  y  w\n");
    missing.replace(missing.find("\tx  number\n"), std::string("\tx  number\n").size(), "\tx  text\n");
    language::diagnostics wrong;
    std::vector<language::file> again;
    again.push_back(language::parse("main.one", missing, wrong));
    language::check(again, wrong);
    std::string said;
    for (const auto& d : wrong) said += language::format(d) + "\n";
    CHECK(said.find("idea.x keeps where a card is, so it's a number") != std::string::npos);
    CHECK(said.find("view wall_page has no h in its ideas; add it to the list's block") != std::string::npos);
}

TEST_CASE("a board over stage records chimes and shows a status where each stage's own fields say") {
    const std::string source = R"(namespace yard {
entity stage {
	title   text  required
	notify  list of text
	status  text
}
entity job {
	title  text  required
	stage  stage
}
view yard_page {
	jobs = each job {
		title  stage
	}
	stages = each stage {
		title  notify  status
	}
}
screen "Yard" /yard {
	board yard_page.jobs by stage over yard_page.stages {
		chime when notify has chime
		status by status
		title
	}
}
}
)";
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", source, out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return f.path.find("screens/") != std::string::npos && f.content.find("<Board") != std::string::npos; });
    REQUIRE(screen != web.end());
    CAPTURE(screen->content);
    CHECK(screen->content.find(R"(notifyBy={{ field: "notify", has: "chime" }} statusBy="status")") != std::string::npos);
    // A field its columns don't have is said to be missing.
    std::string missing = source;
    missing.replace(missing.find("\t\ttitle  notify  status\n"), std::string("\t\ttitle  notify  status\n").size(), "\t\ttitle\n");
    language::diagnostics wrong;
    std::vector<language::file> again;
    again.push_back(language::parse("main.one", missing, wrong));
    language::check(again, wrong);
    std::vector<std::string> said;
    for (const auto& d : wrong) said.push_back(d.message);
    CAPTURE(said);
    CHECK(std::ranges::contains(said, std::string("view yard_page has no notify in its stages; add it to the list's block")));
    CHECK(std::ranges::contains(said, std::string("view yard_page has no status in its stages; add it to the list's block")));
}

TEST_CASE("a command's lines lead to the Go they became") {
    auto generated = generate("/examples/library");
    const generators::output_file* go = nullptr;
    for (const auto& f : generated.files) {
        if (f.path == "api/library/library.go") go = &f;
    }
    REQUIRE(go != nullptr);
    // loan::create in main.one: lending takes the book off the shelf.
    // The line of main.one that says it, found rather than counted, so editing the
    // example elsewhere doesn't move it.
    auto source = *platform::read_file(root + "/examples/library/main.one");
    int lending = 1 + static_cast<int>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(source.find("dispatch book::update { id = book  status = status::lent }")), '\n'));
    std::vector<std::string> from;
    std::size_t at = 0, n = 0;
    while (at < go->content.size()) {
        std::size_t end = go->content.find('\n', at);
        const auto& s = go->sources[n];
        if (s && !s->fixed() && s->path.ends_with("main.one") && s->line == lending) from.push_back(go->content.substr(at, end - at));
        at = end + 1;
        ++n;
    }
    CHECK(from == std::vector<std::string>{"\t\tif err := one.DispatchUpdate(c, l.Book, func(dispatched *Book) {", "\t\t\tdispatched.Status = StatusLent",
                                           "\t\t}, nil, nil); err != nil {", "\t\t\treturn err", "\t\t}"});
}

TEST_CASE("a project whose roles are held within something gets a view per person of where they hold them") {
    auto built = generate("/examples/tracker");
    const generators::output_file* app = nullptr;
    const generators::output_file* api = nullptr;
    for (const auto& f : built.files) {
        if (f.path == "web/src/app.tsx") app = &f;
        if (f.path == "api/tracker/tracker.go") api = &f;
    }
    REQUIRE(app != nullptr);
    REQUIRE(api != nullptr);
    CHECK(app->content.find(R"(personal: ["tracker::mine", "tracker::member_roles"])") != std::string::npos);
    CHECK(api->content.find(R"(one.View("member_roles").PerUser().)") != std::string::npos);
}
