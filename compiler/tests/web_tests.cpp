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
    // What's found points into the files, so they're kept, not a temporary's.
    const generators::output_file* find(std::vector<generators::output_file>&&, const std::string&) = delete;

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
                             "src/screens/releases.tsx", "src/screens/mission.tsx", "src/screens/install.tsx", "src/screens/themes.tsx", "src/footer.tsx"}) {
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
    auto content_files = generate("/examples/tracker");
    const auto& content = find(content_files, "src/screens/main.tsx")->content;
    CHECK(content.find(R"(actions={[{ name: "tracker::member::delete", label: "Remove", allowed: allows(memberRoles, "project", projectId, "member::delete", "role.may"), when: (row) => ((row["person"] ?? null) !== viewer) }]})") != std::string::npos);
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
    CHECK(releases->content.find(R"(slug: "v0-6-30",)") != std::string::npos);
    CHECK(releases->content.find("<h2>entity</h2>") == std::string::npos);
}

TEST_CASE("the app is told which views have one document per person, and only those") {
    auto app_files = generate("/examples/library");
    auto app = find(app_files, "src/app.tsx");
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
    auto tasks = generate("/examples/tasks");
    CHECK(find(tasks, "public/icon.svg") == nullptr);
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
    CHECK(screens->content.find(R"(<Table view={bookPage} list="loans" columns={{ number: "Loan", "member.name": "Member", lent_at: "Lent", returned_at: "Back" }} people={{ "member.name": "member.picture" }} />)") !=
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
                         "view issue_page per issue {\n\tbody\n\tlifecycle = issue.project.lifecycle\n\timplemented_by\n\timplementer = issue.implemented_by.name\n}\n"
                         "screen \"Issue\" /issues/:issue {\n\tdetails issue_page {\n\t\timplementer \"Implemented by\"\n\t}\n"
                         "\tform issue::update {\n\t\tbody \"Description\"\n\t}\n\tform comment::create {\n\t\tbody \"Comment\"\n\t}\n\tcopy issue_page\n}\n}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                         "namespace tracker {\nentity issue {\n\ttitle  text\n}\nview issue_page per issue {\n\ttitle\n}\n"
                         "screen \"Issue\" /issues/:issue {\n\tlayout two_columns\n\tmain {\n\t\ttext \"{issue_page.title}\"\n\t}\n\tside {\n\t\ttext \"aside\"\n\t}\n}\n"
                         "screen \"Other\" /other {\n\tlayout two_columns\n\ttext \"all in main\"\n}\n"
                         "screen \"Plain\" /plain {\n\ttext \"as it was\"\n}\n}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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

TEST_CASE("the files a screen names are found beside its .one file") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-beside";
    fs::remove_all(dir);
    fs::create_directories(dir / "site/guide");
    platform::write_file((dir / "site/guide.one").string(),
                         "namespace guide {\nscreen guide /:page {\n\tmarkdown \"guide/*.md\"\n}\nscreen \"Example\" /example {\n\tcode \"example.uione\"\n}\n}\n");
    platform::write_file((dir / "site/guide/01-start.md").string(), "# Starting\n\nHello.\n");
    platform::write_file((dir / "site/example.uione").string(), "entity book {\n}\n");
    auto files = generate_at(dir.string());
    const auto* pages = find(files, "src/pages/guide.generated.ts");
    REQUIRE(pages != nullptr);
    CHECK(pages->content.find(R"(title: "Starting")") != std::string::npos);
    bool imported = false;
    for (const auto& f : files) {
        if (f.path.starts_with("src/screens/") && f.content.find("site/example.uione?raw") != std::string::npos) imported = true;
    }
    CHECK(imported);
    fs::remove_all(dir);
}

TEST_CASE("a text can be shown only while its when holds") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-text-when";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\nentity issue {\n\ttitle  text\n\towner  user\n}\n"
                         "view issue_page per issue {\n\towner\n\towner_name = issue.owner.name\n}\n"
                         "screen \"Issue\" /issues/:issue {\n\ttext \"Taken by {issue_page.owner_name}\" when issue_page.owner != none\n}\n}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"({issuePage.status === "live" && (!isNone((issuePage.data?.["owner"] ?? null))) && <Text>Taken by <Live view={issuePage} field="owner_name" /></Text>})") !=
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
    CHECK(screens->content.find(R"(<Form command="tracker::issue::create" fields={["title", { name: "body", type: "markdown" }, { name: "labels", type: "list" }]} given={{ project: projectId }} button authenticated allowed={allows(memberRoles, "project", projectId, "issue::create", "role.may")} />)") !=
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
    // uione's own packages move with the compiler, whatever the components say.
    CHECK(manifest->content.find("\"@uione/editor\": \"" + std::string(version) + "\",\n") != std::string::npos);
    CHECK(manifest->content.find("\"@uione/compiler\": \"" + std::string(version) + "\",\n") != std::string::npos);
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
    platform::write_file((dir / "main.one").string(), "import one\nproject studio {\n\ttitle \"uione & co\"\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
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
define role captain "Captain" in crew {
	member::create
	role::create
}
command crew::create {
	by anyone signed in
	dispatch member::create {
		crew = id  person = me  role = role::captain
	}
}
command member::create
command role::create
view crew_page per crew {
	readers member
	roles = each role in crew {
		name  title
	}
}
screen "Crew" /crews/:crew {
	member::create "Add someone"
	form member::create "Add" {
		person  role
	}
	role::create "New role"
	form role::create "Create" {
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
    CHECK(tsx.find(R"({ name: "role", type: "pick", choices: listChoices(crewPage, "roles", "title") })") != std::string::npos);
    CHECK(tsx.find(R"(allowed={allows(memberRoles, "crew", crewId, "member::create", "role.may")})") != std::string::npos);
    CHECK(tsx.find(R"({ name: "may", type: "choices", choices: [["crew::create", "Create crew"], ["member::create", "Create member"], ["role::create", "Create role"]] })") == std::string::npos);
    CHECK(tsx.find(R"({ name: "may", type: "choices", choices: [["member::create", "Create member"], ["role::create", "Create role"]] })") != std::string::npos);
}

TEST_CASE("a workflow's steps are buttons along them, and a step's form picks its stages and roles") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-workflow";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(namespace work {
entity project {
	slug  slug  required  unique  key
}
define role owner "Owner" in project {
	step::create
	issue::move
}
entity stage {
	project  project  required  key
	name     slug     required  key
	title    text     required
}
entity step {
	project  project  required  key
	from     stage    required  key
	to       stage    required  key
	title    text
	roles    list of role
}
entity issue {
	project  project  required
	stage    stage
}
command step::create
command issue::move {
	changes stage
	require exists(step where from == was issue.stage && to == issue.stage && held(roles))  "not from there to there"
}
view project_page per project {
	readers member
	stages = each stage in project {
		name  title
	}
	steps = each step in project {
		from  to  title  from.title  to.title  roles
	}
	roles = each role in project {
		name  title
	}
}
view issue_page per issue {
	readers member
	stage
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
    CHECK(tsx.find(R"(<Steps command="work::issue::move" id={issueId} field="stage" current={issuePage} steps={projectPage} list="steps" shown="title" from="from.title" to="to.title" held="roles" roles={memberRoles} within={projectId} place="project" role="role" />)") != std::string::npos);
    CHECK(tsx.find(R"({ name: "from", type: "pick", choices: listChoices(projectPage, "stages", "title") })") != std::string::npos);
    CHECK(tsx.find(R"({ name: "roles", type: "choices", choices: listChoices(projectPage, "roles", "title") })") != std::string::npos);
    // Copying an issue leaves out its stage's id, which means nothing pasted.
    CHECK(tsx.find(R"(<Copy view={issuePage} fields={[]})") != std::string::npos);
}

TEST_CASE("a project's theme is drawn from its roles, and its corners and appearance are on its page from the first paint") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-theme";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "import one\ndefine theme ink \"Ink\" from papercolor {\n\ttext  \"Inter\"\n\tcorners  0\n}\nproject tracker {\n\ttheme  ink\n\tappearance  light\n\taccessibility  menu\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* page = find(files, "index.html");
    REQUIRE(page != nullptr);
    CHECK(page->content.find(R"(<html lang="en" data-palette="papercolor" data-corners="square" data-appearance="light" data-theme="light">)") != std::string::npos);
    CHECK(page->content.find(R"(<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" />)") != std::string::npos);
    const auto* css = find(files, "src/theme.css");
    REQUIRE(css != nullptr);
    // PaperColor's colors, with what it says over harbor's, each as it says them and
    // with more contrast, and what's worked out from whichever is drawn with.
    CHECK(css->content.find("  --std-page: #eeeeee;\n") != std::string::npos);
    CHECK(css->content.find("  --std-ink: #262626;\n  --more-ink: #000000;\n") != std::string::npos);
    CHECK(css->content.find("  --std-line: #d0d0d0;\n  --more-line: #888888;\n") != std::string::npos);
    CHECK(css->content.find("  --std-accent: #005faf;\n  --more-accent: #004d9c;\n") != std::string::npos);
    CHECK(css->content.find("  --std-accent-ink: #ffffff;\n") != std::string::npos);
    CHECK(css->content.find(":root[data-theme=\"dark\"] {\n  --std-page: #1c1c1c;\n") != std::string::npos);
    CHECK(css->content.find(":root {\n  --color-page: var(--std-page);\n") != std::string::npos);
    CHECK(css->content.find("  --color-accent-hover: color-mix(in oklch, var(--color-accent) 85%, var(--color-ink));\n") != std::string::npos);
    // How urgent something is: each in its role's hue made vivid, so a dark warning
    // isn't mixed to tan, over the surface without its own hue, so dark pages stay warm.
    CHECK(css->content.find("  --color-tone-2: color-mix(in oklab, oklch(from var(--color-warning) 0.76 0.16 calc(h - 18)) 18%, oklch(from var(--color-surface) l 0 0));\n") != std::string::npos);
    CHECK(css->content.find("  --color-tone-2-edge: oklch(from var(--color-warning) 0.76 0.16 calc(h - 18));\n") != std::string::npos);
    CHECK(css->content.find("  --color-tone-5: color-mix(in oklab, oklch(from var(--color-accent) 0.68 0.16 h) 14%, oklch(from var(--color-surface) l 0 0));\n") != std::string::npos);
    // More contrast when the system asks, unless the reader says standard, or when they ask.
    CHECK(css->content.find("@media (prefers-contrast: more) {\n  :root:not([data-contrast=\"standard\"]) {\n  --color-page: var(--more-page);\n") != std::string::npos);
    CHECK(css->content.find(":root[data-contrast=\"more\"] {\n  --color-page: var(--more-page);\n") != std::string::npos);
    CHECK(css->content.find("  --radius-box: 0px;\n") != std::string::npos);
    CHECK(css->content.find("  --font-sans: \"Inter\", ui-sans-serif, system-ui, sans-serif;\n") != std::string::npos);
    CHECK(css->content.find("  --shadow-panel: none;\n") != std::string::npos);
    const auto* main = find(files, "src/main.tsx");
    REQUIRE(main != nullptr);
    CHECK(main->content.find("import \"@uione/radix/styles.css\";\nimport \"./theme.css\";\n") != std::string::npos);
    // The display settings, for a site that offers them.
    const auto* app = find(files, "src/app.tsx");
    REQUIRE(app != nullptr);
    CHECK(app->content.find(", display: true };") != std::string::npos);
}

TEST_CASE("a site's footer is drawn with a screen's items, and what it was built from") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-foot";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "import one\nproject shop {\n\tui  radix\n}\n"
                                                      "footer {\n\ttext \"© {year} [Ada Lovelace](https://www.linkedin.com/in/ada)\"\n"
                                                      "\tlink build.release \"uione {build.version}\"\n\tlink build.source \"{build.commit}\"\n}\n"
                                                      "screen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* app = find(files, "src/app.tsx");
    const auto* footer = find(files, "src/footer.tsx");
    REQUIRE(app != nullptr);
    REQUIRE(footer != nullptr);
    CHECK(app->content.find("import { SiteFooter } from \"./footer\";") != std::string::npos);
    CHECK(app->content.find(std::string("build: { version: \"") + std::string(one::version) +
                            "\", commit: import.meta.env.VITE_UIONE_COMMIT, repository: import.meta.env.VITE_UIONE_REPOSITORY }, footer: { layout: \"bar\" as const, content: SiteFooter }") !=
          std::string::npos);
    CHECK(footer->content.find("import { Built, BuiltLink, Link, Text } from \"@uione/react\";") != std::string::npos);
    CHECK(footer->content.find(R"(<Text>© <Built value="year" /> <Link to="https://www.linkedin.com/in/ada">Ada Lovelace</Link></Text>)") != std::string::npos);
    CHECK(footer->content.find(R"(<BuiltLink to="release">uione <Built value="version" /></BuiltLink>)") != std::string::npos);
    CHECK(footer->content.find(R"(<BuiltLink to="source"><Built value="commit" /></BuiltLink>)") != std::string::npos);
}

TEST_CASE("a screen's title can show what the page does, once it's arrived") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-live-title";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace tracker {\n"
                         "entity issue {\n\tnumber  number  key\n\ttitle  text\n}\n"
                         "view issue_page per issue {\n\tnumber\n\ttitle\n}\n"
                         "screen \"#{issue_page.number} {issue_page.title}\" /issues/:issue {\n\ttext \"{issue_page.title}\"\n}\n"
                         "screen \"Home\" / {\n\ttext \"hi\"\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                         "view issue_page per issue {\n\tstatus\n}\n"
                         "screen \"Issue\" /issues/:issue {\n"
                         "\tissue::close \"Close issue\" when issue_page.status == status::open\n"
                         "}\n"
                         "screen \"Issues\" /issues {\n\tissue::create \"New issue\"\n\tform issue::create {\n\t\ttitle\n\t}\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                         "view issue_page per issue {\n\tstatus\n\towner\n}\n"
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
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                         "entity stage {\n\ttitle  text  required\n\tposition  number\n}\n"
                         "command stage::update\n"
                         "view stages {\n\teach stage {\n\t\ttitle  position\n\t}\n}\n"
                         "screen \"Stages\" /stages {\n"
                         "\ttable stages {\n\t\ttitle\n\t\tupdate \"Rename\"\n\t}\n"
                         "\tform stage::update \"Save\" {\n\t\ttitle \"Called\"\n\t}\n"
                         "}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(actions={[{ name: "tracker::stage::update", label: "Rename", form: { fields: [{ name: "title", label: "Called" }], submit: "Save" } }]})") != std::string::npos);
    CHECK(tsx.find("<Form ") == std::string::npos);

    // The row holds what the form asks for, or it couldn't start from it.
    language::diagnostics out;
    std::vector<language::file> files;
    files.push_back(language::parse("main.one", "namespace tracker {\n"
                                                "entity stage {\n\ttitle  text  required\n\tposition  number\n}\n"
                                                "command stage::update\n"
                                                "view stages {\n\teach stage {\n\t\ttitle\n\t}\n}\n"
                                                "screen \"Stages\" /stages {\n"
                                                "\ttable stages {\n\t\ttitle\n\t\tupdate \"Rename\"\n\t}\n"
                                                "\tform stage::update {\n\t\ttitle  position\n\t}\n"
                                                "}\n"
                                                "}\n", out));
    language::check(files, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "form stage::update opens from each row of stages, so the list needs position; add it to the list's block");
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
                         "command card::update\n"
                         "command column::delete {\n\tinput into column\n\teach card where column == id {\n\t\tdispatch card::update { column = into }\n\t}\n}\n"
                         "view board_page per board {\n\tcolumns = each column in board {\n\t\ttitle\n\t}\n}\n"
                         "screen \"Columns\" /boards/:board {\n"
                         "\ttable board_page.columns {\n\t\ttitle\n\t\tdelete \"Remove\"\n\t}\n"
                         "\tform column::delete \"Remove\" {\n\t\tinto \"Move its cards to\"\n\t}\n"
                         "}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                         "\tcolumns = each column in board {\n\t\torder by position\n\t\ttitle  position\n\t}\n"
                         "\tarrows = each arrow in board {\n\t\tfrom  to  says\n\t}\n"
                         "}\n"
                         "screen \"Board\" /boards/:board {\n"
                         "\ttable board_page.columns {\n\t\treorder position\n\t\thide when empty\n\t\ttitle\n\t}\n"
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
    CHECK(tsx.find(R"( hideEmpty)") != std::string::npos);
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
                             "\tcolumns = each column in board {\n\t\ttitle\n\t}\n"
                             "\tarrows = each arrow in board {\n\t\tfrom  to\n\t}\n"
                             "}\n"
                             "screen \"Board\" /boards/:board {\n"
                             "\tdiagram board_page.arrows by from and to over board_page.columns\n"
                             "}\n"
                             "}\n");
        auto drawn_files = generate_at(dir.string());
        const auto* drawn = find(drawn_files, "src/screens/main.tsx");
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
                                                "\tcolumns = each column in board {\n\t\ttitle\n\t}\n"
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
                         "entity stage {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n\tposition  number\n}\n"
                         "entity step {\n\tproject  project  required  key\n\tfrom  stage  required  key\n\tto  stage  required  key\n}\n"
                         "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\ttitle  text\n\tstage  stage\n}\n"
                         "command issue::move {\n\tchanges stage\n}\n"
                         "view project_page per project {\n"
                         "\tstages = each stage in project {\n\t\torder by position\n\t\ttitle\n\t}\n"
                         "\tsteps = each step in project {\n\t\tfrom  to\n\t}\n"
                         "\tissues = each issue in project {\n\t\tnumber  title  stage\n\t}\n"
                         "}\n"
                         "screen \"Project\" /projects/:project {\n"
                         "\ttable project_page.issues {\n\t\tnumber  title\n\t}\n"
                         "\tboard project_page.issues by stage over project_page.stages {\n\t\tmove issue::move along project_page.steps\n\t\ttitle\n\t\tnumber \"#\"\n\t}\n"
                         "}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Switched id="work::project_page.issues" label="Show issues as" options={["Table", "Board"]} icons={["table", "board"]}>)") != std::string::npos);
    CHECK(tsx.find(R"(<Board view={projectPage} list="issues" by="stage" over={projectPage} overList="stages" shown="title" columns={{ title: "Title", number: "#" }} move={{ command: "work::issue::move", steps: projectPage, list: "steps" }} />)") !=
          std::string::npos);
    CHECK(tsx.find("</Switched>") != std::string::npos);
    fs::remove_all(dir);

    // A card's column is one of the over list's things.
    language::diagnostics out;
    std::vector<language::file> wrong;
    wrong.push_back(language::parse("main.one", "namespace work {\n"
                                                "entity project {\n\ttitle  text\n}\n"
                                                "entity stage {\n\tproject  project\n\ttitle  text\n}\n"
                                                "entity issue {\n\tproject  project\n\ttitle  text\n\tstage  stage\n}\n"
                                                "view project_page per project {\n"
                                                "\tstages = each stage in project {\n\t\ttitle\n\t}\n"
                                                "\tissues = each issue in project {\n\t\ttitle  project\n\t}\n"
                                                "}\n"
                                                "screen \"Project\" /projects/:project {\n"
                                                "\tboard project_page.issues by project over project_page.stages {\n\t\ttitle\n\t}\n"
                                                "}\n"
                                                "}\n", out));
    language::check(wrong, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "issue.project is a card's column, so it points at a stage, what project_page.stages lists");
}

TEST_CASE("a board's cards are dragged into order in their column, which an update sets") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-board-order";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto source = [](const std::string& order, const std::string& update) {
        return "namespace work {\n"
               "entity project {\n\ttitle  text\n}\n"
               "entity stage {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n\tposition  number\n}\n"
               "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\ttitle  text\n\tstage  stage\n\tposition  number\n}\n" +
               update +
               "view project_page per project {\n"
               "\tstages = each stage in project {\n\t\torder by position\n\t\ttitle\n\t}\n"
               "\tissues = each issue in project {\n" + order + "\t\tnumber  title  stage  position\n\t}\n"
               "}\n"
               "screen \"Project\" /projects/:project {\n"
               "\tboard project_page.issues by stage over project_page.stages {\n\t\treorder position\n\t\ttitle\n\t}\n"
               "}\n"
               "}\n";
    };
    platform::write_file((dir / "main.one").string(), source("\t\torder by position\n", "command issue::update\n"));
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"( reorder={{ command: "work::issue::update", field: "position" }} />)") != std::string::npos);
    fs::remove_all(dir);

    // The list is in that order, and an update sets it.
    for (const auto& [order, update, message] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"", "command issue::update\n", "the list is put in order by position, so it's ordered by it first, like order by position"},
             {"\t\torder by position\n", "", "cards are put in order by issue::update, which isn't declared in namespace work"},
         }) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source(order, update), out));
        language::check(files, out);
        REQUIRE(out.size() == 1);
        CHECK(out[0].message == message);
    }
}

TEST_CASE("a table's tabs can be a list's records, a command in its block is in its toolbar, and a heading sits beside the title") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-toolbar";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work {\n"
                         "entity project {\n\ttitle  text\n}\n"
                         "entity stage {\n\tproject  project  required  key\n\tname  text  required  key\n\ttitle  text\n}\n"
                         "entity issue {\n\tproject  project  required\n\ttitle  text\n\tstage  stage\n}\n"
                         "command issue::create\ncommand project::update\n"
                         "view project_page per project {\n"
                         "\ttitle\n"
                         "\tstages = each stage in project {\n\t\ttitle\n\t}\n"
                         "\tissues = each issue in project {\n\t\ttitle  stage\n\t}\n"
                         "}\n"
                         "screen \"Project\" /projects/:project {\n"
                         "\theading {\n\t\tproject::update \"Edit project\"\n\t\tform project::update \"Save\" {\n\t\t\ttitle\n\t\t}\n\t}\n"
                         "\ttable project_page.issues by stage over project_page.stages {\n\t\tsearch title\n\t\ttitle\n\t\tissue::create \"New issue\"\n\t}\n"
                         "\tboard project_page.issues by stage over project_page.stages {\n\t\ttitle\n\t}\n"
                         "\tform issue::create \"Open issue\" {\n\t\ttitle\n\t}\n"
                         "}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(choices={{ stage: Object.fromEntries(listChoices(projectPage, "stages", "title")) }})") != std::string::npos);
    CHECK(tsx.find(R"( by="stage")") != std::string::npos);
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
                         "view project_page per project {\n\tname\n}\n"
                         "view board_page per board {\n\ttitle\n}\n"
                         "view issue_page per issue {\n\ttitle\n\tboard\n\tboard_title = issue.board.title\n}\n"
                         "screen \"{project_page.name}\" /:project {\n\ttext \"hi\"\n}\n"
                         "screen \"{board_page.title}\" /:project/boards/:board {\n\ttext \"hi\"\n}\n"
                         "screen \"{issue_page.title}\" /:project/:issue {\n\tunder /:project/boards/:board \"{issue_page.board_title}\"\n\ttext \"hi\"\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                                                "view issue_page per issue {\n\ttitle\n}\n"
                                                "screen \"Board\" /boards/:board {\n\ttext \"hi\"\n}\n"
                                                "screen \"Issue\" /issues/:issue {\n\tunder /boards/:board \"Board\"\n\ttext \"hi\"\n}\n"
                                                "}\n", out));
    language::check(files, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].message == "this screen's address has no :board, so a view per what it shows holds it, like board = issue.board");
}

TEST_CASE("a screen reads a view's value through what it points at, as it's named") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-bare";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work at / {\n"
                         "entity stage {\n\ttitle  text\n}\n"
                         "entity order {\n\ttitle  text\n\tstage  stage\n}\n"
                         "view order_page per order {\n\ttitle  stage.title\n}\n"
                         "screen \"{order_page.title}\" /orders/:order {\n\ttext \"In {order_page.stage.title}\"\n\ttext \"Ready\" when order_page.stage.title == \"Ready\"\n\tdetails order_page {\n\t\tstage.title \"Stage\"\n\t}\n}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Live view={orderPage} field="stage.title" />)") != std::string::npos);
    CHECK(screens->content.find(R"([["stage.title", "Stage"]])") != std::string::npos);
    CHECK(screens->content.find(R"((orderPage.data?.["stage.title"] ?? null))") != std::string::npos);
}

TEST_CASE("a field's label is every screen's, unless one says its own, and a person's column shows their picture beside their name") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-labels";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work at / {\n"
                         "entity order {\n\tnumber  serial  key  \"#\"\n\ttitle  text\n\tpacked_by  list of user  \"Packed by\"\n\tauthor  user  = me\n}\n"
                         "command order::update\n"
                         "view orders {\n\teach order {\n\t\tnumber  title  packed_by  packed_by.name  packed_by.picture  author.name  author.picture\n\t}\n}\n"
                         "view order_page per order {\n\tnumber  title  packed_by  packed_by.name\n}\n"
                         "screen \"Orders\" /orders {\n\ttable orders {\n\t\tnumber  title  packed_by  author \"Opened by\"\n\t}\n}\n"
                         "screen \"Order\" /orders/:order {\n\tdetails order_page {\n\t\tnumber\n\t\tpacked_by.name\n\t}\n\tform order::update {\n\t\ttitle  packed_by\n\t}\n}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(columns={{ number: "#", title: "Title", "packed_by.name": "Packed by", "author.name": "Opened by" }})") != std::string::npos);
    CHECK(tsx.find(R"(people={{ "packed_by.name": "packed_by.picture", "author.name": "author.picture" }})") != std::string::npos);
    CHECK(tsx.find(R"({ name: "packed_by", label: "Packed by")") != std::string::npos);
    CHECK(tsx.find(R"([["number", "#"], ["packed_by.name", "Packed by"]])") != std::string::npos);
}

TEST_CASE("a header's menu goes down the side of every screen under its path, beside what each shows") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-header-menu";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "import one\nproject shop {\n\ttitle  \"Shop\"\n}\n"
                         "namespace work at / {\n"
                         "header /settings {\n\tmenu {\n\t\tlink /settings \"General\"\n\t\tlink /settings/people \"People\"\n\t}\n}\n"
                         "screen \"Settings\" /settings {\n\ttext \"general\"\n}\n"
                         "screen \"People\" /settings/people {\n\ttext \"people\"\n}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    const std::string menu = R"(<Menu links={[{ to: "/settings", label: "General" }, { to: "/settings/people", label: "People" }]}>)";
    for (const char* shown : {"general", "people"}) {
        std::size_t text = tsx.find(std::string(">") + shown + "<");
        REQUIRE(text != std::string::npos);
        std::size_t opened = tsx.rfind(menu, text), closed = tsx.find("</Menu>", text);
        CHECK(opened != std::string::npos);
        CHECK(closed != std::string::npos);
        CHECK(tsx.rfind("= screen(", text) < opened);
    }
}

TEST_CASE("a screen can be untitled and at several addresses, and a header sits above every screen under its path") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-addresses";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "import one\nproject shop {\n\ttitle  \"Shop\"\n}\n"
                         "namespace work at / {\n"
                         "header /orders {\n\ttext \"Orders above\"\n}\n"
                         "screen \"Orders\" /orders {\n\ttext \"all\"\n}\n"
                         "screen /orders/:order\n\t/orders/:order/proof {\n\ttext \"one\"\n}\n"
                         "screen \"Help\" /help {\n\ttext \"help\"\n}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    // Each address is its own screen, with the same items, and an untitled one shows
    // the site's title and isn't in the navigation.
    CHECK(tsx.find(R"({ title: "Shop", route: "/orders/:order" })") != std::string::npos);
    CHECK(tsx.find(R"({ title: "Shop", route: "/orders/:order/proof" })") != std::string::npos);
    CHECK(tsx.find(R"({ title: "Help", route: "/help", nav: "Help" })") != std::string::npos);
    // The header's items come first on every screen under /orders, and on no other.
    std::size_t above = 0;
    for (std::size_t at = tsx.find("Orders above"); at != std::string::npos; at = tsx.find("Orders above", at + 1)) ++above;
    CHECK(above == 3);
    std::size_t help = tsx.find("route: \"/help\"");
    REQUIRE(help != std::string::npos);
    CHECK(tsx.find("Orders above", help) == std::string::npos);
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
                         "entity stage {\n\tboard  board  required  key\n\tname  text  required  key\n\ttitle  text\n\tposition  number\n}\n"
                         "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\tboard  board\n\tstage  stage\n\tauthor  user = me\n\tpriority  enum { low  high }\n}\n"
                         "command project::update\n"
                         "view project_page per project {\n\tname\n\tsummary\n"
                         "\tboards = each board in project {\n\t\ttitle\n\t}\n"
                         "\tissues = each issue in project {\n\t\tboard  stage  stage.title  stage.position\n\t}\n}\n"
                         "screen \"{project_page.name}\" /:project {\n"
                         "\theading {\n\t\tproject::update \"Edit project\" icon edit\n\t\tform project::update \"Save\" {\n\t\t\tname\n\t\t}\n\t}\n"
                         "\tsubtitle \"{project_page.summary}\"\n"
                         "\tcards project_page.boards link /:project/boards/:board {\n\t\ttitle\n\t\ttally project_page.issues by board and stage\n"
                         "\t\tfilter \"Opened by me\" author == me\n\t\tfilter \"High priority\" priority == priority::high  color red\n"
                         "\t\tfilter \"Changed this week\" updated_at > 7 days ago  color green\n\t}\n"
                         "}\n"
                         "screen \"Board\" /:project/boards/:board {\n\tsection \"Wiki\" color violet {\n\t\ttext \"hi\"\n\t}\n"
                         "\ttable project_page.boards {\n\t\tcolor teal\n\t\ttitle\n\t}\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Subtitle><Markdown view={projectPage} field="summary" plain /></Subtitle>)") != std::string::npos);
    CHECK(tsx.find(R"( button opener="Edit project" icon="edit" )") != std::string::npos);
    CHECK(tsx.find(R"(<Cards view={projectPage} list="boards" columns={{ title: "Title" }} link="/:project/boards/:board" keyed={["project"]} named={{ project: 1 }})") == std::string::npos);
    CHECK(tsx.find(R"(tally={{ view: projectPage, list: "issues", by: "board", and: "stage", shown: "stage.title", order: "stage.position", noun: "issues" }})") != std::string::npos);
    CHECK(tsx.find(R"(filters={[{ label: "Opened by me", query: { author: "me" } }, { label: "High priority", query: { priority: "high" }, hue: "red" }, )"
                   R"({ label: "Changed this week", query: { "updated_at>": "-7d" }, hue: "green" }]})") != std::string::npos);
    CHECK(tsx.find(R"(<Section title="Wiki" hue="violet">)") != std::string::npos);
    CHECK(tsx.find(R"( hue="teal")") != std::string::npos);
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
                         "view issue_page per issue {\n\tassignees\n}\n"
                         "screen \"Issue\" /issues/:issue {\n"
                         "\tissue::take \"Assign to me\" when !(issue_page.assignees has me)\n"
                         "\tissue::drop \"Unassign me\" when issue_page.assignees has me\n"
                         "}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                         "view issue_page per issue {\n\ttitle\n\tstatus\n}\n"
                         "screen \"Issue\" /issues/:issue {\n"
                         "\tissue::update \"Edit\"\n\tform issue::update {\n\t\ttitle\n\t}\n\tissue::close\n\tissue::reopen\n"
                         "\ttext \"{issue_page.title}\"\n\tissue::close\n"
                         "}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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

TEST_CASE("a button with its form draws as the button and the form written apart") {
    namespace fs = std::filesystem;
    auto screen = [](const std::string& name, const std::string& buttons) {
        fs::path dir = fs::temp_directory_path() / name;
        fs::remove_all(dir);
        fs::create_directories(dir);
        platform::write_file((dir / "main.one").string(),
                             "namespace tracker {\n"
                             "entity issue {\n\ttitle  text\n\tbody  markdown\n}\n"
                             "command issue::update\n"
                             "view issue_page per issue {\n\ttitle\n\tbody\n}\n"
                             "screen \"Issue\" /issues/:issue {\n" + buttons + "\ttext \"{issue_page.title}\"\n}\n"
                             "}\n");
        auto files = generate_at(dir.string());
        const auto* screens = find(files, "src/screens/main.tsx");
        REQUIRE(screens != nullptr);
        std::string tsx = screens->content;
        fs::remove_all(dir);
        return tsx;
    };
    auto apart = screen("uione-form-apart", "\tissue::update \"Edit\"\n\tform issue::update \"Save\" {\n\t\ttitle  body \"Description\"\n\t}\n");
    auto with = screen("uione-form-with", "\tissue::update \"Edit\" form \"Save\" {\n\t\ttitle  body \"Description\"\n\t}\n");
    CHECK(apart.find("<Form command=\"tracker::issue::update\"") != std::string::npos);
    CHECK(with == apart);
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
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
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
                         "import one\nproject p {\n\tsignin github\n\tsignin microsoft\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto app_files = generate_at(dir.string());
    auto app = find(app_files, "src/app.tsx");
    REQUIRE(app != nullptr);
    CHECK(app->content.find(R"(import { firebaseSource, github as signInWithGitHub, microsoft as signInWithMicrosoft } from "@uione/react/firebase";)") !=
          std::string::npos);
    CHECK(app->content.find("authentication: [signInWithGitHub, signInWithMicrosoft] });") != std::string::npos);
    CHECK(app->content.find("google") == std::string::npos);
    CHECK(app->content.find("authentication: false") == std::string::npos);
    fs::remove_all(dir);

    // A project that names no way offers none.
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "import one\nproject p {\n\tui radix\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto none_files = generate_at(dir.string());
    auto none = find(none_files, "src/app.tsx");
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
                         "command project::create {\n\tby anyone signed in\n}\n"
                         "view all {\n\teach project {\n\t\tname\n\t}\n}\n"
                         "view page per project {\n\tname\n}\n"
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

TEST_CASE("a timeline can say what it's of, and a row can open what it points at") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-news-links";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work at / {\n"
                         "entity project {\n\tslug  text  required  unique  key\n\tname  text\n}\n"
                         "entity issue history {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\ttitle  text\n\tassignees  list of user\n}\n"
                         "entity link {\n\tproject  project  required\n\tfrom  issue  required\n\tto  issue  required\n}\n"
                         "view news per user {\n\tchanges = each change in issue where assignees has me {\n\t\tproject  issue  issue.title  field  before  after  created_at\n\t}\n}\n"
                         "view issue_page per issue {\n\ttitle\n\tlinks = each link where from == issue.id {\n\t\tto  to.title\n\t}\n}\n"
                         "screen \"Home\" / {\n\ttimeline news.changes \"What's new\" link /:project/:issue\n}\n"
                         "screen \"{issue_page.title}\" /:project/:issue {\n\ttable issue_page.links link /:project/:to {\n\t\tto.title \"Issue\"\n\t}\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Timeline view={news} list="changes" subject={["project", "issue.title"]} title="What's new" link="/:project/:issue" keyed={["project"]})") !=
          std::string::npos);
    CHECK(tsx.find(R"(list="links" link="/:project/:to" keyed={["project"]})") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a copy holds the lists a screen shows, not those only its buttons go by") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-copy-shown";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work {\n"
                         "entity stage {\n\ttitle  text\n}\n"
                         "entity step {\n\tfrom  stage\n\tto  stage\n\ttitle  text\n}\n"
                         "entity issue {\n\ttitle  text\n\tstage  stage\n}\n"
                         "entity comment {\n\tissue  issue\n\tbody  text\n}\n"
                         "command issue::move {\n\tchanges stage\n}\n"
                         "view issue_page per issue {\n\ttitle\n\tstage\n"
                         "\tsteps = each step {\n\t\tfrom  to  title\n\t}\n"
                         "\tcomments = each comment in issue {\n\t\tbody\n\t}\n}\n"
                         "screen \"{issue_page.title}\" /issues/:issue {\n\tissue::move along issue_page.steps\n\ttable issue_page.comments {\n\t\tbody\n\t}\n"
                         "\tcopy issue_page \"Copy issue\"\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(lists={[["comments", "Comments", "rows", ["body"]]]})") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a person's news marks what came since they looked, and a header's badge counts it beside the app's name") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-unread";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "import one\nproject tracker {\n\tui  radix\n}\nheader {\n\tbadge news.unread\n}\n"
                         "namespace work at / {\n"
                         "entity issue history {\n\ttitle  text\n\tassignees  list of user\n}\n"
                         "entity reader {\n\tperson  user  key  = me\n\tseen_at  date\n}\n"
                         "command reader::create {\n\tseen_at = now\n}\n"
                         "view news per user {\n\tchanges = each change in issue where assignees has me {\n\t\tissue  field  before  after  created_at\n\t}\n"
                         "\tseen = first(reader where person == me).seen_at\n\tunread = count(changes where created_at > seen)\n}\n"
                         "screen \"Home\" / {\n\ttimeline news.changes \"What's new\" {\n\t\tnew when created_at > news.seen\n\t\ton open dispatch reader::create\n\t}\n}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    // What's new is a condition on each row, reading other views as they were when the
    // page opened, and on open runs once it opens with something new in it.
    CHECK(screens->content.find(R"(title="What's new" fresh={{ views: [news], test: (row, read) => (ordered(row["created_at"]) > ordered(read[0]?.["seen"])) }} onOpen="work::reader::create")") != std::string::npos);
    const auto* site = find(files, "src/app.tsx");
    REQUIRE(site != nullptr);
    CHECK(site->content.find(R"(badges: [{ view: "work::news", value: "unread" }])") != std::string::npos);
}

TEST_CASE("a link and a toolbar's button can be drawn as icons, and a subtitle sits outside a layout") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-icons";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work {\n"
                         "entity issue {\n\ttitle  text\n}\ncommand issue::create\n"
                         "view issues {\n\teach issue {\n\t\ttitle\n\t}\n}\n"
                         "screen \"Workflow\" /workflow {\n\ttext \"a\"\n}\n"
                         "screen \"Issues\" /issues {\n\tlayout two_columns\n\tsubtitle \"All of them\"\n"
                         "\tmain {\n\t\ttable issues {\n\t\t\ttitle\n\t\t\tissue::create \"New issue\" icon add\n\t\t}\n\t\tform issue::create {\n\t\t\ttitle\n\t\t}\n\t}\n"
                         "\tside {\n\t\tlink /workflow \"Workflow\" icon workflow\n\t}\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    fs::remove_all(dir);
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Link to="/work/workflow" icon="workflow">Workflow</Link>)") != std::string::npos);
    CHECK(tsx.find(R"(opener="New issue" icon="add")") != std::string::npos);
    CHECK(tsx.find("<Subtitle>") != std::string::npos);
    CHECK(tsx.find("<Subtitle>") < tsx.find("<Layout"));
}

TEST_CASE("a table keeps only some rows, rows are tinted by a choice, and a box finds across lists") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-only-tint-find";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace work at / {\n"
                         "enum priority {\n\tcritical \"Critical\"\n\tnormal \"Normal\"\n\tlow \"Low\"\n}\n"
                         "entity project {\n\tslug  text  required  unique  key\n}\n"
                         "entity issue {\n\tproject  project  required  key\n\tnumber  serial  per project  key\n\ttitle  text\n\tpriority  priority  required = priority::normal\n}\n"
                         "entity milestone {\n\tproject  project  required\n\ttitle  text\n\tdue  date\n}\n"
                         "command issue::create\n"
                         "view project_page per project {\n"
                         "\tissues = each issue in project {\n\t\tnumber  title  priority\n\t}\n"
                         "\tmilestones = each milestone in project {\n\t\ttitle  due\n\t}\n}\n"
                         "view issue_page per issue {\n\ttitle\n\tpriority\n}\n"
                         "screen \"Project\" /:project {\n"
                         "\tfind \"Search this project\" {\n\t\tproject_page.issues \"Issues\" link /:project/:issue by number title\n\t}\n"
                         "\ttable project_page.milestones {\n\t\tonly due <= 2 weeks from now\n\t\ttitle\n\t}\n"
                         "\ttable project_page.issues link /:project/:issue {\n\t\ttint by priority\n\t\ttitle\n\t\tissue::create \"New issue\"\n\t}\n"
                         "\tform issue::create {\n\t\ttitle  priority\n\t}\n}\n"
                         "screen \"Issue\" /:project/:issue {\n\tdetails issue_page {\n\t\ttint by priority\n\t\tpriority\n\t}\n}\n"
                         "}\n");
    auto screens_files = generate_at(dir.string());
    const auto* screens = find(screens_files, "src/screens/main.tsx");
    fs::remove_all(dir);
    REQUIRE(screens != nullptr);
    const auto& tsx = screens->content;
    CHECK(tsx.find(R"(<Find label="Search this project" sources={[{ view: projectPage, list: "issues", label: "Issues", link: "/:project/:issue", keyed: ["project"], by: ["number", "title"] }]} />)") != std::string::npos);
    CHECK(tsx.find(R"(only={{ "due<=": "+2w" }})") != std::string::npos);
    CHECK(tsx.find(R"(tint={{ field: "priority", order: ["critical", "normal", "low"] }})") != std::string::npos);
    CHECK(tsx.find(R"(<Details view={issuePage} fields={[["priority", "Priority"]]} choices={{ priority: Object.fromEntries([["critical", "Critical"], ["normal", "Normal"], ["low", "Low"]]) }} tint={{ field: "priority", order: ["critical", "normal", "low"] }} />)") != std::string::npos);
    CHECK(tsx.find(R"(start: "normal", required: true)") != std::string::npos);
}

TEST_CASE("a form of only what a command is sent besides its fields acts on the entity its address names") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-form-confirm";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace shop {\n"
                         "entity shop {\n\tslug  text  required  unique  key\n}\n"
                         "command shop::delete {\n\tinput address text\n\trequire address == slug  \"type its address\"\n}\n"
                         "screen \"Settings\" /:shop/settings {\n"
                         "\tform shop::delete \"Delete it\" {\n\t\taddress \"Type its address\"\n\t}\n"
                         "}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Form command="shop::shop::delete" fields={[{ name: "address", label: "Type its address" }]} id={shopId} after="/" submit="Delete it")") != std::string::npos);
    CHECK(screens->content.find(R"(const shopId = useParam("shop");)") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a form deleting what its address names goes to the page it's under, or the nearest page above") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-delete-goes";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "entity project {\n\tslug  text  required  unique  key\n}\n"
                         "entity board {\n\tproject  project  required  key\n\tname  text  required  key\n}\n"
                         "entity milestone {\n\tproject  project  required  key\n\tname  text  required  key\n}\n"
                         "command board::delete {\n\tinput typed text\n\trequire typed == name  \"type its name\"\n}\n"
                         "command milestone::delete {\n\tinput typed text\n\trequire typed == name  \"type its name\"\n}\n"
                         "screen \"Project\" /:project {\n\ttext \"Boards\"\n}\n"
                         "screen \"Roadmap\" /:project/roadmap {\n\ttext \"Milestones\"\n}\n"
                         "screen \"Board\" /:project/boards/:board {\n"
                         "\tform board::delete \"Delete it\" {\n\t\ttyped \"Type its name\"\n\t}\n"
                         "}\n"
                         "screen \"Milestone\" /:project/milestones/:milestone {\n"
                         "\tunder /:project/roadmap \"Roadmap\"\n"
                         "\tform milestone::delete \"Delete it\" {\n\t\ttyped \"Type its name\"\n\t}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(id={boardId} after="/:project" submit="Delete it")") != std::string::npos);
    CHECK(screens->content.find(R"(id={milestoneId} after="/:project/roadmap" submit="Delete it")") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a track of a table and a board switched between is drawn once above the switch, both keeping to it") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-track";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace desk at / {\n"
                         "entity release {\n\ttitle  text\n\tdue  date\n}\n"
                         "entity column {\n\ttitle  text\n}\n"
                         "entity ticket {\n\ttitle  text\n\trelease  release\n\tcolumn  column\n}\n"
                         "view desk public {\n"
                         "\treleases = each release {\n\t\torder by due\n\t\ttitle  due\n\t}\n"
                         "\tcolumns = each column {\n\t\ttitle\n\t}\n"
                         "\ttickets = each ticket {\n\t\ttitle  release  column\n\t}\n"
                         "}\n"
                         "screen \"Desk\" /desk {\n"
                         "\ttable desk.tickets {\n\t\ttrack release over desk.releases {\n\t\t\tdue \"Ships\"\n\t\t}\n\t\ttitle\n\t}\n"
                         "\tboard desk.tickets by column over desk.columns {\n\t\ttitle\n\t}\n"
                         "}\n"
                         "}\n");
    auto files = generate_at(dir.string());
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    // Each keeps to it without drawing it; the switch draws it, its dots from the board's columns.
    std::string track = R"( track={{ field: "release", view: desk, list: "releases", shown: "title", columns: { due: "Ships" }, above: true }})";
    CHECK(screens->content.find("<Table view={desk} list=\"tickets\" columns={{ title: \"Title\" }}" + track) != std::string::npos);
    CHECK(screens->content.find("columns={{ title: \"Title\" }}" + track + " />") != std::string::npos);
    CHECK(screens->content.find(R"( track={{ field: "release", view: desk, list: "releases", shown: "title", columns: { due: "Ships" }, above: true, by: "column", over: desk, overList: "columns" }} view={desk} list="tickets">)") !=
          std::string::npos);
    CHECK(screens->content.find("shown: true") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a file is uploaded from a form, and shown as a link to it") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-files";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "namespace press {\nentity order {\n\ttitle  text\n\tproof  file\n\tpictures  list of file\n}\n"
                         "command order::create\n"
                         "view order_page per order {\n\ttitle\n\tproof\n\tpictures\n}\n"
                         "screen \"Order\" /orders/:order {\n\tdetails order_page {\n\t\tproof\n\t\tpictures\n\t}\n"
                         "\tform order::create {\n\t\ttitle\n\t\tproof\n\t\tpictures\n\t}\n}\n}\n");
    auto generated = generate_at(dir.string());
    const auto* screens = find(generated, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(fields={["title", { name: "proof", type: "file" }, { name: "pictures", type: "files" }]})") != std::string::npos);
    CHECK(screens->content.find(R"(<Details view={orderPage} fields={[["proof", "Proof"], ["pictures", "Pictures"]]} files={["proof", "pictures"]} />)") !=
          std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a landing page's hero and sections are drawn by its design") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-landing";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(),
                         "screen \"Home\" / landing grid {\n\thero \"Fast.\" {\n\t\ttext \"Very.\"\n\t}\n\tsection \"One\" {\n\t\ttext \"A thing.\"\n\t}\n}\n");
    auto generated = generate_at(dir.string());
    const auto* screens = find(generated, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find("<Landing design=\"grid\">\n      <Hero title=\"Fast.\">") != std::string::npos);
    CHECK(screens->content.find("</Section>\n    </Landing>") != std::string::npos);
    fs::remove_all(dir);
}

// A section said with when is there only while it holds, like a log while it has lines.
TEST_CASE("a section with when is drawn only while it holds") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-section-when";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(entity note {
	text  text  required
}
view desk public {
	logged = count(notes)
	notes = each note {
		text
	}
}
screen "Desk" /desk {
	section "Log" when desk.logged > 0 {
		table desk.notes {
			text
		}
	}
}
)");
    auto generated = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(generated, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CAPTURE(screens->content);
    // A view's values aren't typed, so tsc refuses a bare > on one.
    CHECK(screens->content.find(R"({desk.status === "live" && ((ordered((desk.data?.["logged"] ?? null)) > ordered(0))) && (<>)") != std::string::npos);
    CHECK(screens->content.find("ordered") < screens->content.find("export"));
    CHECK(screens->content.find("<Section title=\"Log\">") != std::string::npos);
}

// Cards take a view of one list as a table does, like every public project.
TEST_CASE("cards show a view of one list") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-cards-bare";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(entity shelf {
	name  text  required
	about  text
}
view shelves public {
	each shelf {
		name  about
	}
}
screen "Shelves" /shelves {
	cards shelves {
		name
		about
	}
}
)");
    auto generated = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(generated, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CAPTURE(screens->content);
    CHECK(screens->content.find(R"(<Cards view={shelves} list="rows")") != std::string::npos);
}

// Pages set as a document, like a license, are drawn as one, and only then is the
// theme's document font loaded.
TEST_CASE("markdown pages set as a document") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-document";
    fs::remove_all(dir);
    fs::create_directories(dir / "license");
    platform::write_file((dir / "license" / "license.md").string(), "## Terms\n\nFree software.\n");
    platform::write_file((dir / "main.one").string(), R"(namespace license {
	screen "License" / {
		markdown "license/*.md" as document
	}
}
)");
    auto generated = generate_at(dir.string());
    const auto* screens = find(generated, "src/screens/main.tsx");
    const auto* index = find(generated, "index.html");
    const auto* theme = find(generated, "src/theme.css");
    REQUIRE(screens != nullptr);
    REQUIRE(index != nullptr);
    CHECK(screens->content.find(R"(<Pages base="/license" pages={pages} document />)") != std::string::npos);
    CHECK(index->content.find("family=Libre+Caslon+Text:ital,wght@0,400;0,700;1,400") != std::string::npos);
    if (theme) CHECK(theme->content.find("--font-document: \"Libre Caslon Text\"") != std::string::npos);

    // Without it, the font isn't asked for.
    platform::write_file((dir / "main.one").string(), R"(namespace license {
	screen "License" / {
		markdown "license/*.md"
	}
}
)");
    auto plain = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* plain_index = find(plain, "index.html");
    REQUIRE(plain_index != nullptr);
    CHECK(plain_index->content.find("Caslon") == std::string::npos);
}

// The header's navigation puts the top bar in its order, by the addresses of each
// namespace's screens there; the rest follow.
TEST_CASE("the top bar takes the header's order") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-navigation";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(header {
	navigation projects docs
}
namespace about {
	screen "About" / {
		text "Who we are"
	}
}
namespace docs {
	screen "Docs" /:page {
		text "How it works"
	}
}
namespace projects at / {
	screen "Projects" /projects {
		text "Every project"
	}
	screen "Settings" /:project/settings {
		text "A project's settings"
	}
}
)");
    auto generated = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* app = find(generated, "src/app.tsx");
    REQUIRE(app != nullptr);
    CAPTURE(app->content);
    CHECK(app->content.find(R"(navigation: ["/projects", "/docs"])") != std::string::npos);
}

// A find box finds in a view of one list too, like every public project.
TEST_CASE("find looks in a view of one list") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-find-bare";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(entity shelf {
	name  text  required
}
view shelves public {
	each shelf {
		name
	}
}
screen "Shelves" /shelves {
	find "Find a shelf" {
		shelves "Shelves" by name
	}
}
)");
    auto generated = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(generated, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CAPTURE(screens->content);
    CHECK(screens->content.find(R"({ view: shelves, list: "rows", label: "Shelves")") != std::string::npos);
}

// A file pasted or dropped on a canvas makes a card as a create form on its screen
// would, and a card shows the first image its picture field holds.
TEST_CASE("a canvas makes cards of files pasted on it, and shows their pictures") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-canvas-paste";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), R"(entity wall {
	title  text  required
}
entity note {
	wall   wall  required
	title  text  required
	files  list of file
	x  number
	y  number
	w  number
	h  number
}
command note::create
view wall_page per wall public {
	title
	notes = each note in wall {
		title  files  x  y  w  h
	}
}
screen "Wall" /walls/:wall {
	canvas wall_page.notes {
		paste note::create into files
		picture files
		title
	}
}
)");
    auto generated = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* screens = find(generated, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CAPTURE(screens->content);
    CHECK(screens->content.find(R"(paste={{ command: "note::create", into: "files", many: true, named: "title", given: { wall: wallId } }} picture="files")") != std::string::npos);
}
