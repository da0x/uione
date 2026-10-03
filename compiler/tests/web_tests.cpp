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

TEST_CASE("a button for a command with a form becomes the form's own button") {
    auto files = generate("/examples/library");
    const auto& content = find(files, "src/screens/main.tsx")->content;
    CHECK(content.find(R"(<Form command="library::book::create" fields={["title", "author", "shelfmark", { name: "summary", type: "markdown" }]} button />)") != std::string::npos);
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
    CHECK(language->content.find("<h2>entity</h2>") != std::string::npos);
    const auto* releases = find(files, "src/pages/releases.generated.ts");
    REQUIRE(releases != nullptr);
    CHECK(releases->content.find(R"(slug: "v0-3-0",)") != std::string::npos);
    CHECK(releases->content.find("<h2>entity</h2>") == std::string::npos);
}

TEST_CASE("the app is told which views have one document per person, and only those") {
    auto app = find(generate("/examples/library"), "src/app.tsx");
    REQUIRE(app != nullptr);
    CHECK(app->content.find("personal: [\"library::mine\"] ") != std::string::npos);
}

TEST_CASE("markdown") {
    CHECK(generators::markdown_to_html("# Title\n\nSome *words*.\n") == "<h1>Title</h1>\n<p>Some <em>words</em>.</p>\n");
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
    CHECK(screens->content.find(R"(<Form command="library::book::update" fields={["title", "author", { name: "summary", type: "markdown" }]} from={bookPage} id={bookId} button />)") !=
          std::string::npos);
}


TEST_CASE("a table shows the list of a view it names") {
    auto files = generate("/examples/library");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Table view={bookPage} list="loans" columns={{ number: "Loan", "member.picture": "", "member.name": "Member", lent_at: "Lent", returned_at: "Back" }} pictures={["member.picture"]} />)") !=
          std::string::npos);
}

TEST_CASE("a create form takes what the screen's address names, without asking for it") {
    auto files = generate("/examples/tracker");
    const auto* screens = find(files, "src/screens/main.tsx");
    REQUIRE(screens != nullptr);
    CHECK(screens->content.find(R"(<Form command="tracker::issue::create" fields={["title", { name: "body", type: "markdown" }, { name: "labels", type: "list" }]} given={{ project: projectId }} button />)") !=
          std::string::npos);
    CHECK(screens->content.find(R"(<Form command="tracker::comment::create" fields={[{ name: "body", type: "markdown" }]} given={{ issue: issueId }} button />)") !=
          std::string::npos);
    // On the list of every project, no project is named, so none is given.
    CHECK(screens->content.find(R"(<Form command="tracker::project::create" fields={["slug", "name", "repository"]} button />)") != std::string::npos);
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

TEST_CASE("what a project's components need is added to the app's dependencies") {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(root) / "compiler" / "build" / "component-project";
    fs::remove_all(dir);
    fs::create_directories(dir / "components");
    REQUIRE(platform::write_file((dir / "main.one").string(), "screen \"Editor\" /edit {\n\tcomponent workbench\n}\n"));
    REQUIRE(platform::write_file((dir / "components" / "workbench.tsx").string(), "export default function Workbench() { return null; }\n"));
    REQUIRE(platform::write_file((dir / "components" / "package.json").string(),
                                 "{\n  \"private\": true,\n  \"dependencies\": {\n    \"@uione/editor\": \"0.1.0\",\n    \"react\": \"^19.0.0\"\n  }\n}\n"));
    auto files = generate_at(dir.string());
    fs::remove_all(dir);
    const auto* manifest = find(files, "package.json");
    REQUIRE(manifest != nullptr);
    CHECK(manifest->content.find("\"@uione/editor\": \"0.1.0\",\n") != std::string::npos);
    CHECK(manifest->content.find("\"react\": \"^19.3.0\",\n") != std::string::npos);  // the app's own version wins
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

TEST_CASE("an app whose people sign in with GitHub asks for GitHub") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "uione-github-signin";
    fs::remove_all(dir);
    fs::create_directories(dir);
    platform::write_file((dir / "main.one").string(), "project p {\n\tsignin github\n}\nscreen \"Home\" / {\n\ttext \"hi\"\n}\n");
    auto app = find(generate_at(dir.string()), "src/app.tsx");
    REQUIRE(app != nullptr);
    CHECK(app->content.find(R"(signin: "github" as const)") != std::string::npos);
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
                         "namespace studio {\nentity project {\n\tname  text\n\tlicense  mit \"MIT\" | apache_2_0 \"Apache-2.0\" | none = none\n}\n"
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
