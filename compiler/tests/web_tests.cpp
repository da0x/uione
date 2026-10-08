// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The web generator is held to site/target/web: files written by hand, reviewed, and
// kept as the specification of what it must produce.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/markdown.hpp"
#include "generators/web.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    std::vector<generators::output_file> generate_at(const std::string& dir) {
        language::diagnostics out;
        std::vector<language::file> files;
        for (const auto& path : platform::find_one_files({dir})) {
            files.push_back(language::parse(path, *platform::read_file(path), out));
        }
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return generators::generate_web(files, dir, dir + "/build/web");
    }

    std::vector<generators::output_file> generate(const std::string& project) { return generate_at(root + project); }

    const generators::output_file* find(const std::vector<generators::output_file>& files, const std::string& path) {
        auto it = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.path == path; });
        return it == files.end() ? nullptr : &*it;
    }

    // A target without the license header it carries as a file in this repository.
    // Generated code carries none, because it belongs to whoever generated it.
    std::string without_license(const std::string& text) {
        if (!text.starts_with("// Copyright")) return text;
        auto end = text.find("\n\n");
        return end == std::string::npos ? text : text.substr(end + 2);
    }

    std::size_t lines(const std::string& text) { return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')); }

} // namespace

TEST_CASE("the site is generated exactly as its targets say") {
    auto files = generate("/site");
    for (const char* path : {"package.json", "tsconfig.json", "vite.config.ts", "src/main.tsx", "src/app.tsx",
                             "src/screens/home.tsx", "src/screens/language.tsx",
                             "src/screens/releases.tsx", "src/screens/mission.tsx", "src/screens/install.tsx"}) {
        CAPTURE(path);
        auto target = platform::read_file(root + "/site/target/web/" + path);
        REQUIRE(target);
        const auto* generated = find(files, path);
        REQUIRE(generated != nullptr);
        CHECK(generated->content == without_license(*target));
    }
}

TEST_CASE("a generated screen is about as long as the .one it came from") {
    for (const char* project : {"/site", "/examples/library", "/examples/tasks", "/examples/tracker"}) {
        for (const auto& f : generate(project)) {
            if (!f.path.starts_with("src/screens/")) continue;
            CAPTURE(project);
            CAPTURE(f.path);
            std::string stem = f.path.substr(std::string("src/screens/").size());
            stem = stem.substr(0, stem.rfind('.'));
            auto source = platform::read_file(root + project + "/" + stem + ".one");
            REQUIRE(source);
            CHECK(lines(f.content) <= lines(*source) + 10);  // its imports
        }
    }
}

TEST_CASE("table columns that name a command on the row become actions") {
    auto files = generate("/examples/library");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Table view={shelf} link="/library/books/:book" columns={{ shelfmark: "Shelfmark", title: "Title", author: "Author", status: "Status", lent_to: "Lent to" }} actions={["library::book::withdraw"]} choices={{ status: Object.fromEntries([["on_shelf", "On shelf"], ["lent", "Lent"], ["withdrawn", "Withdrawn"]]) }} />)") != std::string::npos);
    // Several screens in one file are each named after their title.
    CHECK(screens->content.find("export const shelf = screen(") != std::string::npos);
    CHECK(screens->content.find("export const myLoans = screen(") != std::string::npos);
    // A confirm is kept for the command it asks about.
    CHECK(screens->content.find(R"(<Confirm command="library::book::withdraw" question="Withdraw {title}? It will not be lent again." />)") != std::string::npos);
}

TEST_CASE("a row's button with a when is on only the rows it holds for") {
    const auto& content = find(generate("/examples/tracker"), "src/screens/main.tsx")->content;
    CHECK(content.find(R"(actions={[{ name: "tracker::member::delete", label: "Remove", allowed: holds(memberRoles, "project", projectId, ["maintainer"]), when: (row) => ((row["person"] ?? null) !== viewer) }]})") != std::string::npos);
    CHECK(content.find("const viewer = useAuth()?.person?.uid ?? null;") != std::string::npos);
}

TEST_CASE("a button for a command with a form becomes the form's own button") {
    auto files = generate("/examples/library");
    const auto& content = find(files, "src/screens/main.tsx")->content;
    CHECK(content.find(R"(<Form command="library::book::create" fields={["title", "author", "shelfmark", { name: "summary", type: "markdown" }]} button authenticated />)") != std::string::npos);
    CHECK(content.find("<Command name=\"library::book::create\"") == std::string::npos);
}

TEST_CASE("form fields get their input type from the entity") {
    auto files = generate("/examples/library");
    CHECK(find(files, "src/screens/main.tsx")->content.find(R"({ name: "due_at", type: "date" })") != std::string::npos);
}

TEST_CASE("markdown pages are turned into HTML when the site is built, a set for each screen of them") {
    auto files = generate("/site");
    const auto* language = find(files, "src/pages/language.generated.ts");
    REQUIRE(language != nullptr);
    CHECK(language->content.find(R"(slug: "reference",)") != std::string::npos);
    CHECK(language->content.find(R"(title: "Reference",)") != std::string::npos);
    CHECK(language->content.find(R"(<h2 id=\"entity\">entity</h2>)") != std::string::npos);
    const auto* releases = find(files, "src/pages/releases.generated.ts");
    REQUIRE(releases != nullptr);
    CHECK(releases->content.find(R"(slug: "v0-6-18",)") != std::string::npos);
    CHECK(releases->content.find("<h2>entity</h2>") == std::string::npos);
}

TEST_CASE("the app is told which views have one document per person, and only those") {
    auto app = find(generate("/examples/library"), "src/app.tsx");
    REQUIRE(app != nullptr);
    CHECK(app->content.find("personal: [\"library::mine\"] ") != std::string::npos);
}

TEST_CASE("markdown") {
    CHECK(generators::markdown_to_html("# Title\n\nSome *words*.\n") == "<h1 id=\"title\">Title</h1>\n<p>Some <em>words</em>.</p>\n");
    CHECK(generators::markdown_to_html("| a | b |\n|---|---|\n| 1 | 2 |\n").find("<table>") != std::string::npos);
    CHECK(generators::markdown_title("intro\n# Language\n", "fallback") == "Language");
    CHECK(generators::markdown_title("no heading\n", "fallback") == "fallback");
    CHECK(generators::markdown_slug("02-language") == "language");
    CHECK(generators::markdown_slug("language") == "language");
    CHECK(generators::markdown_slug("2026") == "2026");
}

TEST_CASE("names and text for JavaScript") {
    CHECK(generators::web_detail::js_name("sort_title") == "sortTitle");
    CHECK(generators::web_detail::js_name("My loans") == "myLoans");
    CHECK(generators::web_detail::js_name("uione") == "uione");
    CHECK(generators::web_detail::js_string("say \"hi\"\n") == R"("say \"hi\"\n")");
    CHECK(generators::web_detail::jsx_text("a {b} <c>") == R"(a {"{"}b{"}"} {"<"}c{">"})");
}

TEST_CASE("the project's icon is copied into the app and linked from its page") {
    auto files = generate("/site");
    const auto* icon = find(files, "public/icon.svg");
    REQUIRE(icon != nullptr);
    CHECK(icon->content == *platform::read_file(root + "/site/assets/icon.svg"));
    const auto* index = find(files, "index.html");
    REQUIRE(index != nullptr);
    CHECK(index->content.find("<link rel=\"icon\" type=\"image/svg+xml\" href=\"/icon.svg\" />") != std::string::npos);
    CHECK(find(generate("/examples/tasks"), "public/icon.svg") == nullptr);
}

TEST_CASE("a screen showing a view per entity reads it for the entity its address names") {
    auto files = generate("/examples/library");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find("export const book = screen({ title: \"Book\", route: \"/library/books/:book\" }, () => {\n"
                                "  const bookId = useParam(\"book\");\n"
                                "  const bookPage = useView(\"library::book_page\", bookId);") != std::string::npos);
    // A screen that needs a parameter isn't listed in the navigation.
    CHECK(screens->content.find("route: \"/library/books/:book\", nav:") == std::string::npos);
}


TEST_CASE("a markdown field is written with a preview, and shown rendered") {
    auto files = generate("/examples/library");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"({ name: "summary", type: "markdown" })") != std::string::npos);
    CHECK(screens->content.find(R"(<Markdown view={bookPage} field="summary" />)") != std::string::npos);
}

TEST_CASE("an update form starts from the entity's page and acts on the entity its address names") {
    auto files = generate("/examples/library");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Form command="library::book::update" fields={["title", "author", { name: "summary", type: "markdown" }]} from={bookPage} id={bookId} button authenticated />)") !=
          std::string::npos);
}


TEST_CASE("a table shows the list of a view it names") {
    auto files = generate("/examples/library");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Table view={bookPage} list="loans" columns={{ number: "Loan", "member.picture": "", "member.name": "Member", lent_at: "Lent", returned_at: "Back" }} pictures={["member.picture"]} />)") !=
          std::string::npos);
}

TEST_CASE("an issue can be copied whole, as its view holds it") {
    auto files = generate("/examples/tracker");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Copy view={issuePage} label="Copy issue" fields={[["title", "Title"], ["body", "Body", "markdown"], ["labels", "Labels"], ["status", "Status"], ["visibility", "Visibility"]]} lists={[["comments", "Comments", "thread", ["author.picture", "author.name", "body", "created_at"]], ["mentions", "Mentions", "rows", ["kind", "title", "author", "url"]], ["changes", "Changes", "changes", ["field", "before", "after", "created_by.name", "created_at"]]]} choices={{ status: Object.fromEntries([["open", "Open"], ["closed", "Closed"]]), visibility: Object.fromEntries([["public", "Public"], ["private", "Private"]]) }} />)") !=
          std::string::npos);
}

TEST_CASE("a project's issues are in tabs by their status, with their labels each on its own") {
    auto files = generate("/examples/tracker");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    std::size_t at = screens->content.find(R"(<Table view={projectPage} list="issues")");
    REQUIRE(at != std::string::npos);
    std::string line = screens->content.substr(at, screens->content.find('\n', at) - at);
    CHECK(line.find(R"( labels={["labels"]})") != std::string::npos);
    CHECK(line.find(R"( by="status")") != std::string::npos);
    CHECK(line.find(R"( search={["title", "labels"]} sort="-number" page={25})") != std::string::npos);
    CHECK(line.find(R"(status: Object.fromEntries([["open", "Open"], ["closed", "Closed"]]))") != std::string::npos);
}

TEST_CASE("a copy names values as the screen does, and leaves out a person's id for their name") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-copy";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\nentity project {\n\tlifecycle  enum  full \"Implement, verify\" | simple\n}\nentity issue {\n\tproject  project\n\tbody  markdown\n\timplemented_by  user\n}\n"
                         "entity comment {\n\tbody  text\n}\ncommand issue::update\ncommand comment::create\n"
                         "view issue_page per issue {\n\tbody = issue.body\n\tlifecycle = issue.project.lifecycle\n\timplemented_by = issue.implemented_by\n\timplementer = issue.implemented_by.name\n}\n"
                         "screen \"Issue\" /issues/:issue {\n\tdetails issue_page {\n\t\timplementer \"Implemented by\"\n\t}\n"
                         "\tform issue::update {\n\t\tbody \"Description\"\n\t}\n\tform comment::create {\n\t\tbody \"Comment\"\n\t}\n\tcopy issue_page\n}\n}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(fields={[["body", "Description", "markdown"], ["lifecycle", "Lifecycle"], ["implementer", "Implemented by"]]} lists={[]} )"
                                R"(choices={{ lifecycle: Object.fromEntries([["full", "Implement, verify"], ["simple", "Simple"]]) }} />)") != std::string::npos);
}

TEST_CASE("a screen is laid out in regions, everything in main unless it says") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-layouts";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\nentity issue {\n\ttitle  text\n}\nview issue_page per issue {\n\ttitle = issue.title\n}\n"
                         "screen \"Issue\" /issues/:issue layout two_columns {\n\tmain {\n\t\ttext \"{issue_page.title}\"\n\t}\n\tside {\n\t\ttext \"aside\"\n\t}\n}\n"
                         "screen \"Other\" /other layout two_columns {\n\ttext \"all in main\"\n}\n"
                         "screen \"Plain\" /plain {\n\ttext \"as it was\"\n}\n}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find("<Layout name=\"two_columns\">\n        <Region name=\"main\">\n          <Text><Live view={issuePage} field=\"title\" /></Text>\n        </Region>\n        <Region name=\"side\">") != std::string::npos);
    std::size_t other = tsx.find("export const other");
    REQUIRE(other != std::string::npos);
    CHECK(tsx.find("<Region name=\"main\">", other) < tsx.find("<Text>all in main</Text>", other));
    std::size_t plain = tsx.find("export const plain");
    REQUIRE(plain != std::string::npos);
    CHECK(tsx.find("<Layout", plain) == std::string::npos);  // laid out as it was
    fs::remove_all(dir);
}

TEST_CASE("an issue's details are its values, each beside what it is") {
    auto files = generate("/examples/tracker");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Details view={issuePage} fields={[["status", "Status"], ["labels", "Labels"]]} choices={{ status: Object.fromEntries([["open", "Open"], ["closed", "Closed"]]) }} labels={["labels"]} />)") !=
          std::string::npos);
}

TEST_CASE("a text can be shown only while its when holds") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-text-when";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\nentity issue {\n\ttitle  text\n\towner  user\n}\n"
                         "view issue_page per issue {\n\towner = issue.owner\n\towner_name = issue.owner.name\n}\n"
                         "screen \"Issue\" /issues/:issue {\n\ttext \"Taken by {issue_page.owner_name}\" when issue_page.owner != none\n}\n}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"({issuePage.status === "live" && (((issuePage.data?.["owner"] ?? null) !== null)) && <Text>Taken by <Live view={issuePage} field="owner_name" /></Text>})") !=
          std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a project's timeline names each issue that changed, and links to it") {
    auto files = generate("/examples/tracker");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Timeline view={projectPage} list="timeline" subject={["issue.number", "issue.title"]} link="/tracker/issues/:issue" />)") !=
          std::string::npos);
}

TEST_CASE("an issue's comments are a thread, and its changes a timeline") {
    auto files = generate("/examples/tracker");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Thread view={issuePage} list="comments" />)") != std::string::npos);
    CHECK(screens->content.find(R"(<Timeline view={issuePage} list="changes" />)") != std::string::npos);
}

TEST_CASE("a create form takes what the screen's address names, without asking for it") {
    auto files = generate("/examples/tracker");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Form command="tracker::issue::create" fields={["title", { name: "body", type: "markdown" }, { name: "labels", type: "list" }]} given={{ project: projectId }} button authenticated allowed={holds(memberRoles, "project", projectId, ["maintainer", "reporter"])} />)") !=
          std::string::npos);
    CHECK(screens->content.find(R"(const memberRoles = useView("tracker::member_roles");)") != std::string::npos);
    CHECK(screens->content.find(R"(<Form command="tracker::comment::create" fields={[{ name: "body", type: "markdown" }]} given={{ issue: issueId }} button authenticated />)") !=
          std::string::npos);
    // On the list of every project, no project is named, so none is given.
    CHECK(screens->content.find(R"(<Form command="tracker::project::create" fields={["slug", "name", "repository"]} button authenticated />)") != std::string::npos);
}

TEST_CASE("a hand-written component is drawn, imported, and copied into the app as it is") {
    auto files = generate("/examples/library");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find("import OpeningHours from \"../components/opening_hours\";\n") != std::string::npos);
    CHECK(screens->content.find("      <OpeningHours />\n") != std::string::npos);
    const auto* copied = find(files, "src/components/opening_hours.tsx");
    REQUIRE(copied != nullptr);
    CHECK(copied->content == *platform::read_file(root + "/examples/library/components/opening_hours.tsx"));
}

TEST_CASE("what components share comes along with them") {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(root) / "compiler" / "build" / "shared-component-project";
    fs::remove_all(dir);
    fs::create_directories(dir / "components" / "parts");
    fs::create_directories(dir / "components" / "node_modules" / "left");
    REQUIRE(platform::write_file((dir / "main.one").string(), "screen \"Editor\" /edit {\n\tcomponent workbench\n}\n"));
    REQUIRE(platform::write_file((dir / "components" / "workbench.tsx").string(), "import { shared } from \"./commits\";\nexport default function Workbench() { return null; }\n"));
    REQUIRE(platform::write_file((dir / "components" / "commits.ts").string(), "export const shared = 1;\n"));
    REQUIRE(platform::write_file((dir / "components" / "parts" / "look.css").string(), ".a { color: red; }\n"));
    REQUIRE(platform::write_file((dir / "components" / "notes.txt").string(), "not code\n"));
    REQUIRE(platform::write_file((dir / "components" / "node_modules" / "left" / "index.ts").string(), "export {};\n"));
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* shared = find(files, "src/components/commits.ts");
    REQUIRE(shared != nullptr);
    CHECK(shared->content == "export const shared = 1;\n");
    CHECK(find(files, "src/components/parts/look.css") != nullptr);
    CHECK(find(files, "src/components/notes.txt") == nullptr);
    CHECK(find(files, "src/components/node_modules/left/index.ts") == nullptr);
    CHECK(find(files, "src/components/workbench.tsx") != nullptr);
}

TEST_CASE("what a project's components need is added to the app's dependencies, the compiler kept whole for Vite") {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(root) / "compiler" / "build" / "component-project";
    fs::remove_all(dir);
    fs::create_directories(dir / "components");
    REQUIRE(platform::write_file((dir / "main.one").string(), "screen \"Editor\" /edit {\n\tcomponent workbench\n}\n"));
    REQUIRE(platform::write_file((dir / "components" / "workbench.tsx").string(), "export default function Workbench() { return null; }\n"));
    REQUIRE(platform::write_file((dir / "components" / "package.json").string(),
                                 "{\n  \"private\": true,\n  \"dependencies\": {\n    \"@uione/compiler\": \"0.1.0\",\n    \"@uione/editor\": \"0.1.0\",\n    \"react\": \"^19.0.0\"\n  }\n}\n"));
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* manifest = find(files, "package.json");
    REQUIRE(manifest != nullptr);
    CHECK(manifest->content.find("\"@uione/editor\": \"0.1.0\",\n") != std::string::npos);
    CHECK(manifest->content.find("\"react\": \"^19.3.0\",\n") != std::string::npos);  // the app's own version wins
    const auto* vite = find(files, "vite.config.ts");
    REQUIRE(vite != nullptr);
    CHECK(vite->content.find("optimizeDeps: { exclude: [\"@uione/compiler\"] },") != std::string::npos);
}

TEST_CASE("a link to a namespace goes to its address") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-namespace-link";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace projects {\nnamespace archive {\nscreen \"Old\" / {\n\ttext \"a\"\n}\n}\n}\n"
                         "screen \"Home\" / {\n\tlink namespace projects::archive \"See the old projects\"\n}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Link to="/projects/archive">See the old projects</Link>)") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a page's headings can be linked to") {
    auto html = generators::markdown_to_html("# Reference\n\n## Built-in types\n\ntext\n\n## `entity`\n\n### Built-in types\n");
    CHECK(html.find(R"(<h1 id="reference">Reference</h1>)") != std::string::npos);
    CHECK(html.find(R"(<h2 id="built-in-types">Built-in types</h2>)") != std::string::npos);
    CHECK(html.find(R"(<h2 id="entity"><code>entity</code></h2>)") != std::string::npos);
    CHECK(html.find(R"(<h3 id="built-in-types-1">Built-in types</h3>)") != std::string::npos);
    CHECK(html.find("<hr") == std::string::npos);
}

TEST_CASE("a project can serve a folder's files as they are") {
    auto files = generate("/site");
    const auto* installer = find(files, "public/install.sh");
    REQUIRE(installer != nullptr);
    CHECK(installer->content == *platform::read_file(root + "/site/public/install.sh"));
}

TEST_CASE("a link goes inside its namespace") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-relative-link";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace projects {\nscreen \"Project\" /:project {\n\tlink /:project/reports \"Reports\"\n}\n"
                         "screen \"Reports\" /:project/reports {\n\ttext \"r\"\n}\n}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Link to="/projects/:project/reports">Reports</Link>)") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a menu's links go down the side, with the rest of the screen beside them") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-menu";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace projects {\nscreen \"General\" /:project/settings {\n\ttext \"before\"\n"
                         "\tmenu {\n\t\tlink /:project/settings \"General\"\n\t\tlink /:project/settings/deployments \"Deployments\"\n\t}\n"
                         "\ttext \"beside\"\n}\n"
                         "screen \"Deployments\" /:project/settings/deployments {\n\ttext \"d\"\n}\n}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    auto menu = tsx.find(R"(<Menu links={[{ to: "/projects/:project/settings", label: "General" }, )"
                         R"({ to: "/projects/:project/settings/deployments", label: "Deployments" }]}>)");
    REQUIRE(menu != std::string::npos);
    auto before = tsx.find("before"), beside = tsx.find("beside"), end = tsx.find("</Menu>");
    CHECK(before < menu);
    CHECK(menu < beside);
    CHECK(beside < end);
    fs::remove_all(dir);
}

TEST_CASE("a project's title is the name at the top of its pages") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-title";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "project studio {\n\ttitle \"uione & co\"\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* app = find(files, "src/app.tsx");
    const auto* page = find(files, "index.html");
    const auto* manifest = find(files, "package.json");
    REQUIRE(app != nullptr);
    REQUIRE(page != nullptr);
    REQUIRE(manifest != nullptr);
    CHECK(app->content.find(R"(export const site = { name: "uione & co")") != std::string::npos);
    CHECK(page->content.find("<title>uione &amp; co</title>") != std::string::npos);
    CHECK(manifest->content.find(R"("name": "studio-web")") != std::string::npos);  // the project keeps its own name
}

TEST_CASE("a project's own roles decide its buttons, and its forms pick from them") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-roles";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(namespace crew {
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
	captain "Captain"  hand::create  rank::create
}
command crew::create {
	permission signed_in
	create hand {
		crew = id  person = me  rank = rank::captain
	}
}
command hand::create
command rank::create
view crew_page per crew {
	readers hand
	ranks = each rank where crew == crew.id {
		name  title
	}
}
screen "Crew" /crews/:crew {
	hand::create "Add someone"
	form hand::create "Add" {
		person  rank
	}
	rank::create "New rank"
	form rank::create "Create" {
		name  title  may
	}
}
}
)");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"({ name: "rank", type: "pick", choices: listChoices(crewPage, "ranks", "title") })") != std::string::npos);
    CHECK(tsx.find(R"(allowed={allows(handRoles, "crew", crewId, "hand::create", "rank.may")})") != std::string::npos);
    CHECK(tsx.find(R"({ name: "may", type: "choices", choices: [["crew::create", "Create crew"], ["hand::create", "Create hand"], ["rank::create", "Create rank"]] })") == std::string::npos);
    CHECK(tsx.find(R"({ name: "may", type: "choices", choices: [["hand::create", "Create hand"], ["rank::create", "Create rank"]] })") != std::string::npos);
}

TEST_CASE("a workflow's steps are buttons along them, and a step's form picks its phases and roles") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-workflow";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(namespace work {
entity project {
	slug  slug  required  unique  key
}
entity role {
	project  project  required  key
	name     slug     required  key
	title    text     required
	may      list of permission
}
entity member {
	project  project  required  key
	person   user     required  key
	role     role     required  key
}
roles role per project from member {
	owner "Owner"  step::create  issue::move
}
entity phase {
	project  project  required  key
	name     slug     required  key
	title    text     required
}
entity step {
	project  project  required  key
	from     phase    required  key
	to       phase    required  key
	title    text
	roles    list of role
}
entity issue {
	project  project  required
	phase    phase
}
command step::create
command issue::move {
	changes phase
	require exists(step where from == was issue.phase && to == issue.phase && held(roles))  "not from there to there"
}
view project_page per project {
	readers member
	phases = each phase where project == project.id {
		name  title
	}
	steps = each step where project == project.id {
		from  to  title  from.title  to.title  roles
	}
	roles = each role where project == project.id {
		name  title
	}
}
view issue_page per issue {
	readers member
	phase = issue.phase
}
screen "Issue" /:project/issues/:issue {
	issue::move along project_page.steps
	copy issue_page
}
screen "Steps" /:project/steps {
	step::create "New step"
	form step::create "Create" {
		from  to  title  roles
	}
}
}
)");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Steps command="work::issue::move" id={issueId} field="phase" current={issuePage} steps={projectPage} list="steps" shown="title" from="from.title" to="to.title" held="roles" roles={memberRoles} within={projectId} place="project" role="role" />)") != std::string::npos);
    CHECK(tsx.find(R"({ name: "from", type: "pick", choices: listChoices(projectPage, "phases", "title") })") != std::string::npos);
    CHECK(tsx.find(R"({ name: "roles", type: "choices", choices: listChoices(projectPage, "roles", "title") })") != std::string::npos);
    // Copying an issue leaves out its phase's id, which means nothing pasted.
    CHECK(tsx.find(R"(<Copy view={issuePage} fields={[]})") != std::string::npos);
}

TEST_CASE("a project's theme and corners are on its page from the first paint") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-theme";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "project tracker {\n\ttheme    papercolor\n\tcorners  square\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* page = find(files, "index.html");
    REQUIRE(page != nullptr);
    CHECK(page->content.find(R"(<html lang="en" data-palette="papercolor" data-corners="square">)") != std::string::npos);
}

TEST_CASE("a screen's title can show what the page does, once it's arrived") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-live-title";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity issue {\n\tnumber  number  key\n\ttitle  text\n}\n"
                         "view issue_page per issue {\n\tnumber = issue.number\n\ttitle = issue.title\n}\n"
                         "screen \"#{issue_page.number} {issue_page.title}\" /issues/:issue {\n\ttext \"{issue_page.title}\"\n}\n"
                         "screen \"Home\" / {\n\ttext \"hi\"\n}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(export const issues = screen({ title: "", route: "/tracker/issues/:issue" }, () => {)") != std::string::npos);
    CHECK(tsx.find(R"(useTitle(["#", [issuePage, "number"], " ", [issuePage, "title"]]);)") != std::string::npos);
    CHECK(tsx.find("import { Crumbs, Live, Text, screen, useParam, useTitle, useView }") != std::string::npos);
    // The pages above it, by address: the home of its namespace.
    CHECK(tsx.find(R"(<Crumbs items={[{ to: "/tracker", title: ["Home"] }]} />)") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a button says what it does, and shows only while its when holds") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-button-when";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity issue {\n\ttitle  text\n\tstatus  enum  open | closed = status::open\n}\n"
                         "command issue::create\n"
                         "command issue::close {\n\tstatus = status::closed\n}\n"
                         "view issue_page per issue {\n\tstatus = issue.status\n}\n"
                         "screen \"Issue\" /issues/:issue {\n"
                         "\tissue::close \"Close issue\" when issue_page.status == status::open\n"
                         "}\n"
                         "screen \"Issues\" /issues {\n\tissue::create \"New issue\"\n\tform issue::create {\n\t\ttitle\n\t}\n}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Command name="tracker::issue::close" id={issueId} label="Close issue" when={issuePage.status === "live" && (((issuePage.data?.["status"] ?? null) === "open"))} />)") !=
          std::string::npos);
    CHECK(tsx.find(R"( button opener="New issue")") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a when block shows a button, a text or a row's button only while all its lines hold") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-when-block";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity issue {\n\ttitle  text\n\towner  user\n\tstatus  enum  open | closed = status::open\n}\n"
                         "command issue::close {\n\tstatus = status::closed\n}\n"
                         "command issue::delete\n"
                         "view issue_page per issue {\n\tstatus = issue.status\n\towner = issue.owner\n}\n"
                         "view issues {\n\teach issue {\n\t\ttitle  owner  status\n\t}\n}\n"
                         "screen \"Issue\" /issues/:issue {\n"
                         "\tissue::close \"Close issue\" when {\n"
                         "\t\tissue_page.status == status::open\n"
                         "\t\tissue_page.owner == me\n"
                         "\t}\n"
                         "\ttext \"Yours\" when {\n\t\tissue_page.owner == me\n\t}\n"
                         "}\n"
                         "screen \"Issues\" /issues {\n"
                         "\ttable issues {\n"
                         "\t\ttitle\n"
                         "\t\tdelete \"Remove\" when {\n\t\t\towner == me\n\t\t\tstatus == status::closed\n\t\t}\n"
                         "\t}\n"
                         "}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(when={issuePage.status === "live" && ((((issuePage.data?.["status"] ?? null) === "open") && ((issuePage.data?.["owner"] ?? null) === viewer)))} />)") !=
          std::string::npos);
    CHECK(tsx.find(R"({issuePage.status === "live" && (((issuePage.data?.["owner"] ?? null) === viewer)) && <Text>Yours</Text>})") != std::string::npos);
    CHECK(tsx.find(R"(when: (row) => (((row["owner"] ?? null) === viewer) && ((row["status"] ?? null) === "closed")))") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a row's button opens its command's form, started from the row, and the form isn't drawn on its own") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-row-form";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity phase {\n\ttitle  text  required\n\tposition  number\n}\n"
                         "command phase::update\n"
                         "view phases {\n\teach phase {\n\t\ttitle  position\n\t}\n}\n"
                         "screen \"Phases\" /phases {\n"
                         "\ttable phases {\n\t\ttitle\n\t\tupdate \"Rename\"\n\t}\n"
                         "\tform phase::update \"Save\" {\n\t\ttitle \"Called\"\n\t}\n"
                         "}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(actions={[{ name: "tracker::phase::update", label: "Rename", form: { fields: [{ name: "title", label: "Called" }], submit: "Save" } }]})") != std::string::npos);
    CHECK(tsx.find("<Form ") == std::string::npos);

    // The row holds what the form asks for, or it couldn't start from it.
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace tracker {\n"
                                                "entity phase {\n\ttitle  text  required\n\tposition  number\n}\n"
                                                "command phase::update\n"
                                                "view phases {\n\teach phase {\n\t\ttitle\n\t}\n}\n"
                                                "screen \"Phases\" /phases {\n"
                                                "\ttable phases {\n\t\ttitle\n\t\tupdate \"Rename\"\n\t}\n"
                                                "\tform phase::update {\n\t\ttitle  position\n\t}\n"
                                                "}\n"
                                                "}\n", out));
    language::check(files, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "form phase::update opens from each row of phases, so the list needs position; add it to the list's block");
    fs::remove_all(dir);
}

TEST_CASE("a form asks for a command's input as it would a field of its type") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-form-input";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace board {\n"
                         "entity board {\n\ttitle  text\n}\n"
                         "entity column {\n\tboard  board\n\ttitle  text\n}\n"
                         "entity card {\n\tcolumn  column\n}\n"
                         "command column::delete {\n\tinput into column\n\teach card where column == id {\n\t\tcolumn = into\n\t}\n}\n"
                         "view board_page per board {\n\tcolumns = each column where board == board.id {\n\t\ttitle\n\t}\n}\n"
                         "screen \"Columns\" /boards/:board {\n"
                         "\ttable board_page.columns {\n\t\ttitle\n\t\tdelete \"Remove\"\n\t}\n"
                         "\tform column::delete \"Remove\" {\n\t\tinto \"Move its cards to\"\n\t}\n"
                         "}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(form: { fields: [{ name: "into", label: "Move its cards to", type: "pick", choices: listChoices(boardPage, "columns", "title") }], submit: "Remove" })") !=
          std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a grid shows what goes between a list's things, its cells opening create, update and delete, and rows are dragged into order") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-grid";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace board {\n"
                         "entity board {\n\ttitle  text\n}\n"
                         "entity column {\n\tboard  board  required  key\n\tname  text  required  key = slug(title)\n\ttitle  text\n\tposition  number\n}\n"
                         "entity arrow {\n\tboard  board  required  key\n\tfrom  column  required  key\n\tto  column  required  key\n\tsays  text\n}\n"
                         "command column::update\n"
                         "command arrow::create\ncommand arrow::update\ncommand arrow::delete\n"
                         "view board_page per board {\n"
                         "\tcolumns = each column where board == board.id {\n\t\torder by position\n\t\ttitle  position\n\t}\n"
                         "\tarrows = each arrow where board == board.id {\n\t\tfrom  to  says\n\t}\n"
                         "}\n"
                         "screen \"Board\" /boards/:board {\n"
                         "\ttable board_page.columns {\n\t\treorder position\n\t\ttitle\n\t}\n"
                         "\tgrid board_page.arrows by from and to over board_page.columns {\n\t\tsays\n\t}\n"
                         "\tform arrow::create \"Allow\" {\n\t\tfrom  to  says\n\t}\n"
                         "\tform arrow::update \"Save\" {\n\t\tsays\n\t}\n"
                         "}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(reorder={{ command: "board::column::update", field: "position" }})") != std::string::npos);
    // The cell gives the row and column, so create asks only the rest, and sends the board from the address.
    CHECK(tsx.find(R"(<Grid view={boardPage} list="arrows" from="from" to="to" cell="says" over={boardPage} overList="columns" shown="title" create={{ name: "board::arrow::create", fields: ["says"], submit: "Allow", given: { board: boardId } }} update={{ name: "board::arrow::update", fields: ["says"], submit: "Save" }} remove={{ name: "board::arrow::delete" }} />)") != std::string::npos);
    // Their forms open from the cells, not on their own.
    CHECK(tsx.find("<Form ") == std::string::npos);
    fs::remove_all(dir);

    // A diagram is the same, drawn.
    {
        fs::create_directories(dir);
        platform::write_file((dir / "main.one").string(),
                             "namespace board {\n"
                             "entity board {\n\ttitle  text\n}\n"
                             "entity column {\n\tboard  board  required  key\n\tname  text  required  key\n\ttitle  text\n}\n"
                             "entity arrow {\n\tboard  board  required  key\n\tfrom  column  required  key\n\tto  column  required  key\n}\n"
                             "command arrow::delete\n"
                             "view board_page per board {\n"
                             "\tcolumns = each column where board == board.id {\n\t\ttitle\n\t}\n"
                             "\tarrows = each arrow where board == board.id {\n\t\tfrom  to\n\t}\n"
                             "}\n"
                             "screen \"Board\" /boards/:board {\n"
                             "\tdiagram board_page.arrows by from and to over board_page.columns\n"
                             "}\n"
                             "}\n");
        const auto* drawn = find(generate_at(dir.string()), "src/screens/main.tsx");
        REQUIRE(drawn != nullptr);
        CHECK(drawn->content.find(R"(<Diagram view={boardPage} list="arrows" from="from" to="to" over={boardPage} overList="columns" shown="title" remove={{ name: "board::arrow::delete" }} />)") !=
              std::string::npos);
        fs::remove_all(dir);
    }

    // What the grid's entries point at is what its rows and columns are.
    language::diagnostics out;
    std::vector<language::file> wrong;
    wrong.push_back(language::parse("main.one", "namespace board {\n"
                                                "entity board {\n\ttitle  text\n}\n"
                                                "entity column {\n\tboard  board\n\ttitle  text\n}\n"
                                                "entity arrow {\n\tboard  board\n\tfrom  column\n\tto  board\n}\n"
                                                "view board_page per board {\n"
                                                "\tcolumns = each column where board == board.id {\n\t\ttitle\n\t}\n"
                                                "\tarrows = each arrow where board == board.id {\n\t\tfrom  to\n\t}\n"
                                                "}\n"
                                                "screen \"Board\" /boards/:board {\n"
                                                "\tgrid board_page.arrows by from and to over board_page.columns\n"
                                                "}\n"
                                                "}\n", out));
    language::check(wrong, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "arrow.to is a row or column of the grid, so it points at a column, what board_page.columns lists");
}

TEST_CASE("a board shows a list's rows as cards in columns, moved along steps, and a table before it of the same list switches with it") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-board";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work {\n"
                         "entity project {\n\ttitle  text\n}\n"
                         "entity phase {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n\tposition  number\n}\n"
                         "entity step {\n\tproject  project  required  key\n\tfrom  phase  required  key\n\tto  phase  required  key\n}\n"
                         "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\ttitle  text\n\tphase  phase\n}\n"
                         "command issue::move {\n\tchanges phase\n}\n"
                         "view project_page per project {\n"
                         "\tphases = each phase where project == project.id {\n\t\torder by position\n\t\ttitle\n\t}\n"
                         "\tsteps = each step where project == project.id {\n\t\tfrom  to\n\t}\n"
                         "\tissues = each issue where project == project.id {\n\t\tnumber  title  phase\n\t}\n"
                         "}\n"
                         "screen \"Project\" /projects/:project {\n"
                         "\ttable project_page.issues {\n\t\tnumber  title\n\t}\n"
                         "\tboard project_page.issues by phase over project_page.phases {\n\t\tmove issue::move along project_page.steps\n\t\ttitle\n\t\tnumber \"#\"\n\t}\n"
                         "}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Switched id="work::project_page.issues" label="Show issues as" options={["Table", "Board"]} icons={["table", "board"]}>)") != std::string::npos);
    CHECK(tsx.find(R"(<Board view={projectPage} list="issues" by="phase" over={projectPage} overList="phases" shown="title" columns={{ title: "Title", number: "#" }} move={{ command: "work::issue::move", steps: projectPage, list: "steps" }} />)") !=
          std::string::npos);
    CHECK(tsx.find("</Switched>") != std::string::npos);
    fs::remove_all(dir);

    // A card's column is one of the over list's things.
    language::diagnostics out;
    std::vector<language::file> wrong;
    wrong.push_back(language::parse("main.one", "namespace work {\n"
                                                "entity project {\n\ttitle  text\n}\n"
                                                "entity phase {\n\tproject  project\n\ttitle  text\n}\n"
                                                "entity issue {\n\tproject  project\n\ttitle  text\n\tphase  phase\n}\n"
                                                "view project_page per project {\n"
                                                "\tphases = each phase where project == project.id {\n\t\ttitle\n\t}\n"
                                                "\tissues = each issue where project == project.id {\n\t\ttitle  project\n\t}\n"
                                                "}\n"
                                                "screen \"Project\" /projects/:project {\n"
                                                "\tboard project_page.issues by project over project_page.phases {\n\t\ttitle\n\t}\n"
                                                "}\n"
                                                "}\n", out));
    language::check(wrong, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "issue.project is a card's column, so it points at a phase, what project_page.phases lists");
}

TEST_CASE("a table's tabs can be a list's records, a command in its block is in its toolbar, and a heading sits beside the title") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-toolbar";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work {\n"
                         "entity project {\n\ttitle  text\n}\n"
                         "entity phase {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n}\n"
                         "entity issue {\n\tproject  project  required\n\ttitle  text\n\tphase  phase\n}\n"
                         "command issue::create\ncommand project::update\n"
                         "view project_page per project {\n"
                         "\ttitle = project.title\n"
                         "\tphases = each phase where project == project.id {\n\t\ttitle\n\t}\n"
                         "\tissues = each issue where project == project.id {\n\t\ttitle  phase\n\t}\n"
                         "}\n"
                         "screen \"Project\" /projects/:project {\n"
                         "\theading {\n\t\tproject::update \"Edit project\"\n\t\tform project::update \"Save\" {\n\t\t\ttitle\n\t\t}\n\t}\n"
                         "\ttable project_page.issues by phase over project_page.phases {\n\t\tsearch title\n\t\ttitle\n\t\tissue::create \"New issue\"\n\t}\n"
                         "\tboard project_page.issues by phase over project_page.phases {\n\t\ttitle\n\t}\n"
                         "\tform issue::create \"Open issue\" {\n\t\ttitle\n\t}\n"
                         "}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(choices={{ phase: Object.fromEntries(listChoices(projectPage, "phases", "title")) }})") != std::string::npos);
    CHECK(tsx.find(R"( by="phase")") != std::string::npos);
    CHECK(tsx.find(R"(tools={<><Form command="work::issue::create" fields={["title"]} given={{ project: projectId }} submit="Open issue" button opener="New issue" authenticated /></>})") != std::string::npos);
    // The board beside it takes its search and its toolbar.
    CHECK(tsx.find(R"(search={["title"]} tools={<><Form command="work::issue::create")") != std::string::npos);
    CHECK(tsx.find("<Heading>") < tsx.find("<Switched"));
    CHECK(tsx.find(R"(<Form command="work::project::update" fields={["title"]} from={projectPage} id={projectId} submit="Save" button opener="Edit project" authenticated />)") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a screen's trail is the pages its address goes on from, or the page it's under, called as it says") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-under";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work at / {\n"
                         "entity project {\n\tslug  text  required  unique  key\n\tname  text\n}\n"
                         "entity board {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n}\n"
                         "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\tboard  board\n\ttitle  text\n}\n"
                         "view project_page per project {\n\tname = project.name\n}\n"
                         "view board_page per board {\n\ttitle = board.title\n}\n"
                         "view issue_page per issue {\n\ttitle = issue.title\n\tboard = issue.board\n\tboard_title = issue.board.title\n}\n"
                         "screen \"{project_page.name}\" /:project {\n\ttext \"hi\"\n}\n"
                         "screen \"{board_page.title}\" /:project/boards/:board {\n\ttext \"hi\"\n}\n"
                         "screen \"{issue_page.title}\" /:project/:issue under /:project/boards/:board \"{issue_page.board_title}\" {\n\ttext \"hi\"\n}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Crumbs items={[{ to: "/:project", title: [[projectPage, "name"]] }]} />)") != std::string::npos);
    CHECK(tsx.find(R"(<Crumbs items={[{ to: "/:project", title: [[projectPage, "name"]] }, { to: "/:project/boards/:board", title: [[issuePage, "board_title"]], fill: { board: [issuePage, "board", 2] } }]} />)") !=
          std::string::npos);
    fs::remove_all(dir);

    // What fills the page above's parameters is held by a view of this one.
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace work {\n"
                                                "entity board {\n\ttitle  text\n}\n"
                                                "entity issue {\n\ttitle  text\n}\n"
                                                "view issue_page per issue {\n\ttitle = issue.title\n}\n"
                                                "screen \"Board\" /boards/:board {\n\ttext \"hi\"\n}\n"
                                                "screen \"Issue\" /issues/:issue under /boards/:board \"Board\" {\n\ttext \"hi\"\n}\n"
                                                "}\n", out));
    language::check(files, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "this screen's address has no :board, so a view per what it shows holds it, like board = issue.board");
}

TEST_CASE("cards show a list's rows large, with a tally and filters, under a subtitle and an icon to edit") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-cards";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work at / {\n"
                         "entity project {\n\tslug  text  required  unique  key\n\tname  text\n\tsummary  markdown\n}\n"
                         "entity board {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n}\n"
                         "entity phase {\n\tboard  board  required  key\n\tname  text  required  key\n\ttitle  text\n\tposition  number\n}\n"
                         "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\tboard  board\n\tphase  phase\n\tauthor  user = me\n\tpriority  enum { low  high }\n}\n"
                         "command project::update\n"
                         "view project_page per project {\n\tname = project.name\n\tsummary = project.summary\n"
                         "\tboards = each board where project == project.id {\n\t\ttitle\n\t}\n"
                         "\tissues = each issue where project == project.id {\n\t\tboard  phase  phase.title  phase.position\n\t}\n}\n"
                         "screen \"{project_page.name}\" /:project {\n"
                         "\theading {\n\t\tproject::update \"Edit project\" icon edit\n\t\tform project::update \"Save\" {\n\t\t\tname\n\t\t}\n\t}\n"
                         "\tsubtitle \"{project_page.summary}\"\n"
                         "\tcards project_page.boards link /:project/boards/:board {\n\t\ttitle\n\t\ttally project_page.issues by board and phase\n"
                         "\t\tfilter \"Opened by me\" author == me\n\t\tfilter \"High priority\" priority == priority::high\n\t}\n"
                         "}\n"
                         "screen \"Board\" /:project/boards/:board {\n\ttext \"hi\"\n}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Subtitle><Markdown view={projectPage} field="summary" plain /></Subtitle>)") != std::string::npos);
    CHECK(tsx.find(R"( button opener="Edit project" icon="edit" )") != std::string::npos);
    CHECK(tsx.find(R"(<Cards view={projectPage} list="boards" columns={{ title: "Title" }} link="/:project/boards/:board" keyed={["project"]} named={{ project: 1 }})") == std::string::npos);
    CHECK(tsx.find(R"(tally={{ view: projectPage, list: "issues", by: "board", and: "phase", shown: "phase.title", order: "phase.position", noun: "issues" }})") != std::string::npos);
    CHECK(tsx.find(R"(filters={[{ label: "Opened by me", query: { author: "me" } }, { label: "High priority", query: { priority: "high" } }]})") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a button's when can ask whether a list has whoever is reading") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-when-me";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity issue {\n\ttitle  text\n\tassignees  list of user\n}\n"
                         "command issue::take {\n\tadd me to assignees\n}\n"
                         "command issue::drop {\n\tremove me from assignees\n}\n"
                         "view issue_page per issue {\n\tassignees = issue.assignees\n}\n"
                         "screen \"Issue\" /issues/:issue {\n"
                         "\tissue::take \"Assign to me\" when !(issue_page.assignees has me)\n"
                         "\tissue::drop \"Unassign me\" when issue_page.assignees has me\n"
                         "}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find("const viewer = useAuth()?.person?.uid ?? null;") != std::string::npos);
    CHECK(tsx.find(R"(when={issuePage.status === "live" && (listHas((issuePage.data?.["assignees"] ?? null), viewer))})") != std::string::npos);
    CHECK(tsx.find("import { Actions, Command, listHas, screen, useAuth, useParam, useView }") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a form can call a field something other than its name") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-field-label";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\nentity comment {\n\tbody  markdown  required\n\tnote  text\n}\ncommand comment::create\n"
                         "screen \"Comments\" /comments {\n\tform comment::create \"Comment\" {\n\t\tbody \"Comment\"  note\n\t}\n}\n}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(fields={[{ name: "body", label: "Comment", type: "markdown" }, "note"]})") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("buttons one after another sit in a row") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-actions";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity issue {\n\ttitle  text\n\tstatus  enum  open | closed = status::open\n}\n"
                         "command issue::update\n"
                         "command issue::close {\n\tstatus = status::closed\n}\n"
                         "command issue::reopen {\n\tstatus = status::open\n}\n"
                         "view issue_page per issue {\n\ttitle = issue.title\n\tstatus = issue.status\n}\n"
                         "screen \"Issue\" /issues/:issue {\n"
                         "\tissue::update \"Edit\"\n\tform issue::update {\n\t\ttitle\n\t}\n\tissue::close\n\tissue::reopen\n"
                         "\ttext \"{issue_page.title}\"\n\tissue::close\n"
                         "}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    std::size_t row = tsx.find("<Actions>");
    REQUIRE(row != std::string::npos);
    std::size_t end = tsx.find("</Actions>", row);
    REQUIRE(end != std::string::npos);
    std::string inside = tsx.substr(row, end - row);
    CHECK(inside.find("<Form command=\"tracker::issue::update\"") != std::string::npos);
    CHECK(inside.find("<Command name=\"tracker::issue::reopen\"") != std::string::npos);
    CHECK(tsx.find("<Actions>", end) == std::string::npos);  // a button alone isn't in a row
    fs::remove_all(dir);
}

TEST_CASE("a command's button on its entity's page acts on that entity") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-command-button";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity project {\n\tslug  text  required  key\n}\n"
                         "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n"
                         "\tstatus  enum  open | closed = status::open\n}\n"
                         "command issue::close {\n\tstatus = status::closed\n}\n"
                         "command project::create\n"
                         "screen \"Issue\" /projects/:project/issues/:issue {\n\tissue::close\n}\n"
                         "screen \"Projects\" /projects {\n\tproject::create\n}\n"
                         "}\n");
    const auto* screens = find(generate_at(dir.string()), "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(const issueId = keyOf([useParam("project"), useParam("issue")]);)") != std::string::npos);
    CHECK(screens->content.find(R"(<Command name="tracker::issue::close" id={issueId} />)") != std::string::npos);
    CHECK(screens->content.find(R"(<Command name="tracker::project::create" />)") != std::string::npos);  // nothing to act on yet
    fs::remove_all(dir);
}

TEST_CASE("an app offers the ways of signing in its project names, in its order, and only those") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-authentication";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "project p {\n\tsignin github\n\tsignin microsoft\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto app = find(generate_at(dir.string()), "src/app.tsx");
    REQUIRE(app != nullptr);
    CHECK(app->content.find(R"(import { firebaseSource, github as signInWithGitHub, microsoft as signInWithMicrosoft } from "@uione/react/firebase";)") !=
          std::string::npos);
    CHECK(app->content.find("authentication: [signInWithGitHub, signInWithMicrosoft] });") != std::string::npos);
    CHECK(app->content.find("google") == std::string::npos);
    CHECK(app->content.find("authentication: false") == std::string::npos);
    fs::remove_all(dir);

    // A project that names no way offers none.
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "project p {\n\tui radix\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto none = find(generate_at(dir.string()), "src/app.tsx");
    REQUIRE(none != nullptr);
    CHECK(none->content.find(", authentication: false") != std::string::npos);
    CHECK(none->content.find("import { firebaseSource } from") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a namespace can put its screens at the root, and an address can name an entity by its key's parts") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-keyed-addresses";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace studio at / {\n"
                         "entity project {\n\towner  text  key  = me.username\n\tslug  text  required  key\n\tname  text\n}\n"
                         "command project::create {\n\tpermission signed_in\n}\n"
                         "view all {\n\teach project {\n\t\tname\n\t}\n}\n"
                         "view page per project {\n\tname = project.name\n}\n"
                         "screen \"Projects\" / {\n\ttable all link /:owner/:project {\n\t\tname\n\t}\n}\n"
                         "screen \"Project\" /:owner/:project {\n\ttext \"{page.name}\"\n\tlink /:owner/:project/more \"More\"\n}\n"
                         "screen \"More\" /:owner/:project/more {\n\ttext \"{page.name}\"\n}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(route: "/", nav: "Projects")") != std::string::npos);
    CHECK(tsx.find(R"(route: "/:owner/:project")") != std::string::npos);
    CHECK(tsx.find(R"(const projectId = keyOf([useParam("owner"), useParam("project")]);)") != std::string::npos);
    CHECK(tsx.find(R"(link="/:owner/:project" keyed={["owner"]})") != std::string::npos);
    CHECK(tsx.find(R"(<Link to="/:owner/:project/more">)") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a choice is picked from a list in a form, and shown by its label") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-choice-labels";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace studio {\nentity project {\n\tname  text\n\tlicense  enum mit \"MIT\" | apache_2_0 \"Apache-2.0\" | none = license::none\n}\n"
                         "command project::create\nview all {\n\teach project {\n\t\tname  license\n\t}\n}\n"
                         "screen \"Projects\" / {\n\ttable all {\n\t\tname\n\t\tlicense\n\t}\n\tform project::create \"Start a project\" {\n\t\tname  license\n\t}\n}\n}\n");
    auto files = generate_at(dir.string());
    const auto& tsx = find(files, "src/screens/main.tsx")->content;
    CHECK(tsx.find(R"({ name: "license", type: "choice", choices: [["mit", "MIT"], ["apache_2_0", "Apache-2.0"], ["none", "None"]], start: "none" })") != std::string::npos);
    CHECK(tsx.find(R"(choices={{ license: Object.fromEntries([["mit", "MIT"], ["apache_2_0", "Apache-2.0"], ["none", "None"]]) }})") != std::string::npos);
    // A form can say what its button does.
    CHECK(tsx.find(R"( submit="Start a project")") != std::string::npos);
    fs::remove_all(dir);
}
