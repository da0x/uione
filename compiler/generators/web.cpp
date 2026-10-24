// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "generators/web.hpp"

#include <format>
#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "code/stream.hpp"
#include "generators/javascript.hpp"
#include "generators/markdown.hpp"
#include "generators/output.hpp"
#include "generators/roles.hpp"
#include "generators/screens.hpp"
#include "generators/theme.hpp"
#include "language/ast.hpp"
#include "language/names.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace one::generators {

    namespace web_detail {
        using screens::join;
        using screens::label;
        using screens::text_of;
    } // namespace web_detail

    class web_generator {
    public:
        // `files` have parsed and checked cleanly. `project_dir` is the folder they came
        // from, and `out_dir` is where the web app goes, so imports between the two can
        // be worked out.
        web_generator(const std::vector<language::file>& files, std::string project_dir, std::string out_dir)
            : files_(files), model_(files), project_dir_(std::move(project_dir)), out_dir_(std::move(out_dir)) {}

        std::vector<output_file> generate() {
            std::vector<output_file> out;
            std::vector<screen_import> screens;
            for (const auto& f : files_) {
                std::vector<found_screen> found = model_.screens_in(f);
                if (found.empty()) continue;
                std::string stem = std::filesystem::path(f.path).stem().string();
                out.push_back(file("src/screens/" + stem + ".tsx", screen_file(f, found, screens, stem)));
            }
            for (const auto& f : files_) {
                if (auto found = footer_in("", f.declarations); found.second) {
                    out.push_back(file("src/footer.tsx", footer_file(f, found.first, *found.second)));
                }
            }
            for (auto& f : component_files()) out.push_back(std::move(f));
            out.push_back(file("package.json", package_json()));
            out.push_back(file("tsconfig.json", tsconfig_json()));
            out.push_back(file("index.html", index_html()));
            out.push_back(file("vite.config.ts", vite_config()));
            out.push_back(file("src/main.tsx", main_tsx()));
            out.push_back(file("src/theme.css", theme_css()));
            out.push_back(file("src/app.tsx", app_tsx(screens)));
            for (const auto& pages : pages_) out.push_back(file("src/pages/" + pages.name + ".generated.ts", pages_module(pages)));
            if (!icon_.empty()) out.push_back(file("public/icon.svg", icon_svg()));
            // Served as they are: Vite copies public/ into the site unchanged.
            if (!served_.empty()) {
                for (const auto& name : platform::files_under(served_)) {
                    output_file f{"public/" + name, platform::read_file(served_ + "/" + name).value_or(""), false, {}};
                    // Every line is the served folder's, the last too when no newline ends it,
                    // as none ends an image.
                    std::size_t lines = static_cast<std::size_t>(std::count(f.content.begin(), f.content.end(), '\n'));
                    if (!f.content.empty() && f.content.back() != '\n') ++lines;
                    f.sources.assign(lines, served_source_);
                    out.push_back(std::move(f));
                }
            }
            return out;
        }

    private:
        using stream = code::stream;

        const std::vector<language::file>& files_;
        screens::model model_;  // what the screens mean, which this writes as React
        std::string project_dir_;
        std::string out_dir_;
        std::string screen_title_;  // the screen being written's title, as written, like "#{issue_page.number} {issue_page.title}"
        int in_block_ = 0;  // how deep in heroes and sections the items being written are
        // The project, as the model indexed it.
        const std::string& name_ = model_.project.name;
        const std::string& title_ = model_.project.title;
        const std::string& theme_ = model_.project.theme;
        const bool& display_ = model_.project.display;
        const std::string& appearance_ = model_.project.appearance;
        const std::string& layout_ = model_.project.layout;
        const std::string& ui_ = model_.project.ui;
        const std::vector<std::string>& authentication_ = model_.project.authentication;
        const bool& analytics_ = model_.project.analytics;
        const bool& has_project_ = model_.project.has_project;
        const code::source& project_ = model_.project.project;
        const std::string& icon_ = model_.project.icon;
        const code::source& icon_source_ = model_.project.icon_source;
        const std::string& served_ = model_.project.served;
        const code::source& served_source_ = model_.project.served_source;
        const std::map<std::string, std::map<std::string, const language::entity_declaration*>>& entities_ = model_.entities;
        const std::map<std::string, std::map<std::string, const language::view_declaration*>>& views_ = model_.views;
        const std::map<std::string, std::set<std::string>>& commands_ = model_.commands;
        const std::map<std::string, const language::command_declaration*>& command_nodes_ = model_.command_nodes;
        const std::set<std::string>& open_ = model_.open;
        const std::vector<std::string>& personal_ = model_.personal;
        const std::vector<language::field> nothing_;  // the fields of no entity
        // In a switch: the track every way the list is shown keeps to.
        const language::track_setting* group_track_ = nullptr;
        bool footer_ = false;  // whether the site has a footer, in src/footer.tsx
        std::string footer_layout_;  // bar or columns
        // A set of markdown pages, one per file, shown by a screen like /docs/:page.
        struct page_set {
            std::string pattern;  // the files, like docs/*.md
            std::string name;     // the generated module's name, from the address: docs
            code::source source;  // the item that names them
        };
        std::vector<page_set> pages_;
        bool documents_ = false;  // whether a page is set as a document, so its font is loaded
        std::string docs_base_;  // the address the screen being written keeps its pages under, like /docs

        // Where generated lines come from. Files every app has, like package.json, come
        // from the project block; a screen's lines come from the screen and its items.
        std::string screen_path_;        // the file whose screens are being written
        std::string route_;              // the address of the screen being written, like /projects/:project
        std::string under_;              // the page it's under, when it says one, like /:project/roadmap
        int item_line_ = 0;              // the screen item being written
        std::map<std::string, std::string> components_;    // hand-written components, by name, to their file
        std::map<std::string, std::string> dependencies_;  // npm packages they need, from components/package.json

        using found_screen = screens::found_screen;

        struct screen_import {
            std::string name, module;
            code::source from;
        };

        // What the model knows, asked as this writes.
        static std::string full_command(const std::string& ns, const language::qualified_name& name) { return screens::full_command(ns, name); }
        static std::string full_view(const std::string& ns, const std::string& name) { return screens::full_view(ns, name); }
        static bool names_parameter(const std::string& route, const std::string& name) { return screens::names_parameter(route, name); }
        std::set<std::string> inputs_of(const std::string& command) const { return model_.inputs_of(command); }
        std::optional<std::string> edit_view(const std::string& ns, const language::entity_declaration& entity, const language::form_item& form) const {
            return model_.edit_view(ns, entity, form);
        }
        std::optional<std::string> per_entity(const std::string& full) const { return model_.per_entity(full); }
        bool people_view(const std::string& full) const { return model_.people_view(full); }
        std::string full_route(const std::string& ns, const std::string& route) const { return model_.full_route(ns, route); }
        bool entity_named(const std::string& ns, const std::string& name) const { return model_.entity_named(ns, name); }
        std::vector<std::string> keyed_by(const std::string& ns, const std::string& entity, const std::string& route) const {
            return model_.keyed_by(ns, entity, route);
        }
        const language::entity_declaration* entity_of_command(const std::string& full) const { return model_.entity_of_command(full); }
        const language::entity_declaration* listed(const std::string& ns, const std::string& view, const std::optional<std::string>& list) const {
            return model_.listed(ns, view, list);
        }
        std::vector<std::string> listed_columns(const std::string& ns, const std::string& view, const std::string& list) const {
            return model_.listed_columns(ns, view, list);
        }
        std::optional<std::string> row_command(const std::string& ns, const std::string& view, const std::optional<std::string>& list,
                                               const std::string& column) const {
            return model_.row_command(ns, view, list, column);
        }

        // The parameters of a link that name something keyed by several parts, as
        // Link takes them: named={{ board: 2 }}.
        std::string named_by(const std::string& ns, const std::string& route) const {
            std::string named;
            for (const auto& [name, keys] : model_.named_by(ns, route)) named += (named.empty() ? "" : ", ") + name + ": " + std::to_string(keys);
            return named.empty() ? "" : " named={{ " + named + " }}";
        }

        // hand-written components

        // A component's name in JSX, which starts with a capital: project_workbench is
        // ProjectWorkbench.
        static std::string component_tag(const std::string& name) {
            std::string tag = web_detail::js_name(name);
            if (!tag.empty() && tag[0] >= 'a' && tag[0] <= 'z') tag[0] = static_cast<char>(tag[0] - 'a' + 'A');
            return tag;
        }

        void use_component(const std::string& name) {
            std::string file = language::component_file(screen_path_, name);
            components_.emplace(name, file);
            auto manifest = platform::read_file((std::filesystem::path(file).parent_path() / "package.json").string());
            if (manifest) {
                for (const auto& [package, version] : dependencies_in(*manifest)) dependencies_.emplace(package, version);
            }
        }

        // The "dependencies" of a package.json: each package's name and version. Only
        // that one object is read, and only its strings, which is all it holds.
        static std::vector<std::pair<std::string, std::string>> dependencies_in(const std::string& json) {
            std::vector<std::pair<std::string, std::string>> found;
            auto key = json.find("\"dependencies\"");
            if (key == std::string::npos) return found;
            auto open = json.find('{', key);
            auto close = json.find('}', open);
            if (open == std::string::npos || close == std::string::npos) return found;
            std::vector<std::string> strings;
            for (std::size_t at = open; at < close;) {
                auto start = json.find('"', at);
                if (start == std::string::npos || start > close) break;
                auto end = json.find('"', start + 1);
                if (end == std::string::npos || end > close) break;
                strings.push_back(json.substr(start + 1, end - start - 1));
                at = end + 1;
            }
            for (std::size_t i = 0; i + 1 < strings.size(); i += 2) found.emplace_back(strings[i], strings[i + 1]);
            return found;
        }

        // A component's file, copied into the app as it is: it's the project's own code.
        std::vector<output_file> component_files() const {
            std::vector<output_file> out;
            for (const auto& [name, path] : components_) {
                auto content = platform::read_file(path);
                if (!content) continue;  // the checker said so already
                output_file f{"src/components/" + name + ".tsx", *content, false, {}};
                f.sources.assign(static_cast<std::size_t>(std::count(content->begin(), content->end(), '\n')), code::source{});
                out.push_back(std::move(f));
            }
            // What the components share, like a module of hooks two of them import,
            // comes along too: every other .ts, .tsx and .css file in their folder and
            // the folders inside it.
            std::set<std::string> copied;
            for (const auto& f : out) copied.insert(f.path);
            std::set<std::filesystem::path> folders;
            for (const auto& [name, path] : components_) folders.insert(std::filesystem::path(path).parent_path());
            for (const auto& folder : folders) {
                std::error_code error;
                std::vector<std::filesystem::path> found;
                for (auto it = std::filesystem::recursive_directory_iterator(folder, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                    if (it->is_directory() && (it->path().filename() == "node_modules" || it->path().filename().string().starts_with("."))) {
                        it.disable_recursion_pending();
                        continue;
                    }
                    auto extension = it->path().extension().string();
                    if (it->is_regular_file() && (extension == ".ts" || extension == ".tsx" || extension == ".css")) found.push_back(it->path());
                }
                std::sort(found.begin(), found.end());
                for (const auto& file : found) {
                    std::string relative = std::filesystem::relative(file, folder).generic_string();
                    std::string name = "src/components/" + relative;
                    if (copied.contains(name)) continue;
                    auto content = platform::read_file(file.string());
                    if (!content) continue;
                    output_file f{name, *content, false, {}};
                    f.sources.assign(static_cast<std::size_t>(std::count(content->begin(), content->end(), '\n')), code::source{});
                    copied.insert(name);
                    out.push_back(std::move(f));
                }
            }
            return out;
        }

        // one screen file

        struct screen_parts {
            std::set<std::string> components;
            std::set<std::string> params;  // route parameters items read, like project
            std::vector<std::pair<std::string, std::string>> views;  // variable, full view name
            std::vector<std::string> imports;                        // extra import lines
            bool optional_page = false;
            bool viewer = false;  // a button's when reads who's reading, as me
            // It shows something its address says after its own, like a tab, a page of
            // rows, or a board rather than a table: /neotrac/boards/main/board/in_progress.
            bool shown_in_path = false;
        };

        // The pages above a screen, nearest last, each by its address and its title,
        // as Crumbs takes them.
        std::string crumbs(screen_parts& parts, const std::string& ns, const language::screen_declaration& screen, const std::string& here) {
            std::vector<std::pair<std::size_t, std::string>> above;
            for (const auto& c : model_.crumbs(ns, screen, here)) {
                std::string spoken = title_parts(parts, c.title);
                std::string fill;
                for (const auto& f : c.fill) {
                    fill += (fill.empty() ? "" : ", ") + f.parameter + ": [" + view_variable(parts, f.view) + ", " + web_detail::js_string(f.field) + ", " +
                            std::to_string(f.keys) + "]";
                }
                above.emplace_back(c.depth, "{ to: " + web_detail::js_string(c.to) + ", title: [" + spoken + "]" + (fill.empty() ? "" : ", fill: { " + fill + " }") + " }");
            }
            std::sort(above.begin(), above.end());
            if (above.empty()) return "";
            std::string items;
            for (const auto& [_, item] : above) items += (items.empty() ? "" : ", ") + item;
            parts.components.insert("Crumbs");
            return "<Crumbs items={[" + items + "]} />";
        }

        stream screen_file(const language::file& f, const std::vector<found_screen>& found, std::vector<screen_import>& screens,
                           const std::string& stem) {
            std::set<std::string> components{"screen"};
            std::vector<std::string> imports;
            stream bodies;
            screen_path_ = f.path;
            for (const auto& [ns, s, where] : found) {
                auto from_screen = bodies.from(f.path, where.line);
                screen_parts parts;
                std::string route = full_route(ns, s->route);
                route_ = route;
                under_ = s->under ? full_route(ns, *s->under) : "";
                screen_title_ = s->title_is_name ? "" : s->title;
                docs_base_ = route.substr(0, route.find("/:"));  // where its pages live, if it has any
                stream items;
                auto from_items = items.from(f.path, where.line);
                items.open("<>");
                if (std::string trail = crumbs(parts, ns, *s, route); !trail.empty()) items.line(trail);
                // What a header says above every screen under its path, like a project's own;
                // a menu in it goes down the side, with what the screen shows beside it.
                std::string menu;
                for (const auto& [header_ns, header] : model_.headers_over(route)) {
                    std::size_t before = header->items.size();
                    for (std::size_t at = 0; at < header->items.size(); ++at) {
                        auto* block = std::get_if<language::content_block>(&header->items[at].node);
                        if (block && block->type == language::content_block::kind::menu && before == header->items.size()) {
                            before = at;
                            menu = menu_of(parts, header_ns, *block);
                        }
                    }
                    if (before > 0) screen_items(items, parts, header_ns, header->items, header->items, 0, before);
                }
                if (!menu.empty()) items.open(menu);
                // Laid out in regions, like main and side: as the screen puts its items
                // in them, or with everything in main.
                // What sits beside the title, like Edit project.
                for (const auto& item : s->items) {
                    auto* block = std::get_if<language::content_block>(&item.node);
                    if (!block || block->type != language::content_block::kind::heading) continue;
                    parts.components.insert("Heading");
                    items.open("<Heading>");
                    screen_items(items, parts, ns, block->items, s->items);
                    items.close("</Heading>");
                }
                std::string layout = s->layout ? *s->layout : layout_;
                std::vector<const language::content_block*> regions;
                for (const auto& item : s->items) {
                    auto* block = std::get_if<language::content_block>(&item.node);
                    if (block && block->type == language::content_block::kind::region) regions.push_back(block);
                }
                // A subtitle goes under the title, outside the regions of a screen laid out in them.
                if (!regions.empty()) {
                    for (std::size_t at = 0; at < s->items.size(); ++at) {
                        auto* text = std::get_if<language::content_text>(&s->items[at].node);
                        if (text && text->subtitle) screen_items(items, parts, ns, s->items, s->items, at, 1);
                    }
                }
                if (s->landing) {
                    // Its hero, its sections and its call to action, drawn in its design.
                    parts.components.insert("Landing");
                    items.open("<Landing design=" + web_detail::js_string(*s->landing) + ">");
                    screen_items(items, parts, ns, s->items, s->items);
                    items.close("</Landing>");
                } else if (layout == "single" && regions.empty()) {
                    screen_items(items, parts, ns, s->items, s->items);
                } else {
                    parts.components.insert("Layout");
                    parts.components.insert("Region");
                    items.open("<Layout name=" + web_detail::js_string(layout) + ">");
                    if (regions.empty()) {
                        items.open("<Region name=\"main\">");
                        screen_items(items, parts, ns, s->items, s->items);
                        items.close("</Region>");
                    }
                    for (const auto* region : regions) {
                        items.open("<Region name=" + web_detail::js_string(region->title) + ">");
                        screen_items(items, parts, ns, region->items, s->items);
                        items.close("</Region>");
                    }
                    items.close("</Layout>");
                }
                if (!menu.empty()) items.close("</Menu>");
                items.close("</>");

                // An untitled screen shows the site's title, as one titled with its name does.
                std::string title = s->title_is_name ? web_detail::label(s->title) : s->title.empty() ? (title_.empty() ? name_ : title_) : s->title;
                // A title that shows live values, like "#{issue_page.number} {issue_page.title}",
                // is set by the screen once they've arrived; until then it has none.
                std::string titling;
                if (!s->title_is_name && title.find('{') != std::string::npos) {
                    titling = "useTitle([" + title_parts(parts, ns, title) + "]);";
                    components.insert("useTitle");
                    title = "";
                }
                if (parts.optional_page && route.ends_with("/:page")) route += "?";
                std::string info = "{ title: " + web_detail::js_string(title) + ", route: " + web_detail::js_string(route);
                // A screen that needs a parameter, like /books/:book, opens from a link to
                // one particular book, so it isn't listed in the navigation.
                bool needs_parameter = false;
                for (std::size_t at = route.find("/:"); at != std::string::npos; at = route.find("/:", at + 1)) {
                    std::size_t end = route.find('/', at + 1);
                    if (route.substr(at, end == std::string::npos ? std::string::npos : end - at).back() != '?') needs_parameter = true;
                }
                // The app's own name is its home, which its logo already links to.
                if (!ns.empty() && !needs_parameter && title != name_ && !s->title.empty()) info += ", nav: " + web_detail::js_string(title);
                if (parts.shown_in_path) info += ", shown: true";
                info += " }";

                // A file with one screen names it after the file (home.one gives home);
                // with several, each is named after its title, or when they share it,
                // like two titled after the app, after the last word of its address
                // (/:project/settings/deployments gives deployments). A name another
                // file's screen already has is numbered.
                // A title made of live values names nothing, so its screen is named after
                // its address instead.
                std::string word_of_route = stem;
                for (std::size_t start = 0; start < route.size();) {
                    std::size_t end = route.find('/', start + 1);
                    std::string part = route.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
                    if (!part.empty() && part[0] != ':') word_of_route = part;
                    start = end == std::string::npos ? route.size() : end;
                }
                std::string name = web_detail::js_name(found.size() == 1 ? stem : titling.empty() && !s->title.empty() ? s->title : word_of_route);
                bool shared = std::count_if(found.begin(), found.end(), [&](const found_screen& other) {
                                  return other.screen->title == s->title;
                              }) > 1;
                if (found.size() > 1 && shared) {
                    std::string word = stem;
                    for (std::size_t start = 0; start < route.size();) {
                        std::size_t end = route.find('/', start + 1);
                        std::string part = route.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
                        if (!part.empty() && part[0] != ':') word = part;
                        start = end == std::string::npos ? route.size() : end;
                    }
                    name = web_detail::js_name(word);
                }
                auto taken = [&](const std::string& n) {
                    return std::any_of(screens.begin(), screens.end(), [&](const screen_import& i) { return i.name == n; });
                };
                const std::string base = name;
                for (int n = 2; taken(name); ++n) name = base + std::to_string(n);
                screens.push_back({name, "./screens/" + stem, {f.path, where.line}});
                components.insert(parts.components.begin(), parts.components.end());
                for (const auto& line : parts.imports) {
                    if (std::find(imports.begin(), imports.end(), line) == imports.end()) imports.push_back(line);
                }

                if (!bodies.str().empty()) bodies.line();
                for (const auto& [variable, view] : parts.views) {
                    if (auto per = per_entity(view)) parts.params.insert(*per);
                }
                if (parts.views.empty() && parts.params.empty() && !parts.viewer) {
                    bodies.open("export const " + name + " = screen(" + info + ", () => (");
                    bodies.embed(items);
                    bodies.close("));");
                } else {
                    for (const auto& [variable, view] : parts.views) {
                        components.insert(!people_view(view) ? "useView" : names_parameter(route, "user") ? "usePerson" : "usePeople");
                    }
                    bodies.open("export const " + name + " = screen(" + info + ", () => {");
                    // A view per entity is read for the entity the address names, and a
                    // form can send it.
                    for (const auto& param : parts.params) {
                        components.insert("useParam");
                        // An entity the address names by its key's parts, like
                        // /:owner/:project, is read by the id those parts make.
                        auto keyed = keyed_by(ns, param, route);
                        if (!keyed.empty()) {
                            components.insert("keyOf");
                            std::string parts_list;
                            for (const auto& key : keyed) parts_list += "useParam(" + web_detail::js_string(key) + "), ";
                            bodies.line("const " + web_detail::js_name(param + "_id") + " = keyOf([" + parts_list + "useParam(" +
                                        web_detail::js_string(param) + ")]);");
                            continue;
                        }
                        bodies.line("const " + web_detail::js_name(param + "_id") + " = useParam(" + web_detail::js_string(param) + ");");
                    }
                    for (const auto& [variable, view] : parts.views) {
                        // Everyone who has signed up is asked for a page at a time, or
                        // one person, whom the address names.
                        if (people_view(view) && names_parameter(route, "user")) {
                            components.insert("useParam");
                            bodies.line("const " + variable + " = usePerson(" + web_detail::js_string(view) + ", useParam(\"user\"));");
                            continue;
                        }
                        if (people_view(view)) {
                            bodies.line("const " + variable + " = usePeople(" + web_detail::js_string(view) + ");");
                            continue;
                        }
                        auto per = per_entity(view);
                        bodies.line("const " + variable + " = useView(" + web_detail::js_string(view) +
                                    (per ? ", " + web_detail::js_name(*per + "_id") : std::string()) + ");");
                    }
                    if (!titling.empty()) bodies.line(titling);
                    // Who's reading, for a button's when that says me; nobody, signed out.
                    if (parts.viewer) {
                        components.insert("useAuth");
                        bodies.line("const viewer = useAuth()?.person?.uid ?? null;");
                    }
                    bodies.open("return (");
                    bodies.embed(items);
                    bodies.close(");");
                    bodies.close("});");
                }
            }

            stream out;
            out.generated_from(std::filesystem::path(f.path).filename().string());
            {
                auto fixed = out.fixed();  // what the screens below need, gathered from all of them
                std::string names;
                for (const auto& c : components) names += (names.empty() ? "" : ", ") + c;
                out.line("import { " + names + " } from \"@uione/react\";");
                for (const auto& line : imports) out.line(line);
            }
            out.line();
            out.embed(bodies);
            return out;
        }

        // The site's footer, and the namespace it's declared in.
        static std::pair<std::string, const language::declaration*> footer_in(const std::string& ns, const std::vector<language::declaration>& declarations) {
            for (const auto& d : declarations) {
                if (std::holds_alternative<language::footer_declaration>(d.node)) return {ns, &d};
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    if (auto found = footer_in(web_detail::join(ns, n->name), n->declarations); found.second) return found;
                }
            }
            return {"", nullptr};
        }

        // header { badge news.unread }: its badges, wherever it's declared.
        static void header_badges(const std::vector<language::declaration>& declarations, std::vector<language::header_badge>& out) {
            for (const auto& d : declarations) {
                if (auto* h = std::get_if<language::header_declaration>(&d.node)) out.insert(out.end(), h->badges.begin(), h->badges.end());
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) header_badges(n->declarations, out);
            }
        }

        // src/footer.tsx: what's at the foot of every page, drawn with the items a
        // screen has.
        stream footer_file(const language::file& f, const std::string& ns, const language::declaration& declared) {
            const auto& footer = std::get<language::footer_declaration>(declared.node);
            footer_ = true;
            footer_layout_ = footer.layout;
            screen_parts parts;
            screen_path_ = f.path;
            route_ = "";
            stream items;
            auto from_items = items.from(f.path, declared.where.line);
            items.open("<>");
            ++in_block_;  // its links are laid out by the footer, like a section's
            screen_items(items, parts, ns, footer.items, footer.items);
            --in_block_;
            items.close("</>");
            stream out;
            auto from_footer = out.from(f.path, declared.where.line);
            out.generated_from(std::filesystem::path(f.path).filename().string());
            {
                auto fixed = out.fixed();
                std::string names;
                for (const auto& c : parts.components) names += (names.empty() ? "" : ", ") + c;
                if (!names.empty()) out.line("import { " + names + " } from \"@uione/react\";");
                for (const auto& line : parts.imports) out.line(line);
            }
            out.line();
            out.open("export function SiteFooter() {");
            out.open("return (");
            out.embed(items);
            out.close(");");
            out.close("}");
            return out;
        }

        template <typename F>
        static void for_each_line(const std::string& text, F each) {
            std::size_t at = 0;
            while (at < text.size()) {
                std::size_t end = text.find('\n', at);
                each(std::string_view(text).substr(at, end - at));
                at = end + 1;
            }
        }

        std::string view_variable(screen_parts& parts, const std::string& full) {
            for (const auto& [variable, view] : parts.views) {
                if (view == full) return variable;
            }
            std::string variable = web_detail::js_name(full.substr(full.rfind(':') == std::string::npos ? 0 : full.rfind(':') + 1));
            parts.views.emplace_back(variable, full);
            return variable;
        }

        // A title with {view.field} in it, as useTitle takes it: the plain parts as
        // strings, and each value as the view it's read from and its field.
        std::string title_parts(screen_parts& parts, const std::vector<screens::title_part>& title) {
            std::string out;
            for (const auto& part : title) {
                out += (out.empty() ? "" : ", ") +
                       (part.live() ? "[" + view_variable(parts, part.view) + ", " + web_detail::js_string(part.field) + "]" : web_detail::js_string(part.text));
            }
            return out;
        }
        std::string title_parts(screen_parts& parts, const std::string& ns, std::string_view text) { return title_parts(parts, model_.title_parts(ns, text)); }

        // Where a link in words goes: the address as it's written, or, with
        // {view.field} in it, like /organizations/{project_page.organization}, each
        // value read from its view as the page shows it.
        std::string address(screen_parts& parts, const std::string& ns, std::string_view to) {
            std::string whole = to.starts_with("/") ? full_route(ns, std::string(to)) : std::string(to);
            if (whole.find('{') == std::string::npos) return web_detail::js_string(whole);
            std::string out = "{`";
            for (const auto& part : model_.title_parts(ns, whole)) {
                if (part.live()) {
                    out += "${encodeURIComponent(String(" + view_variable(parts, part.view) + ".data?.[" + web_detail::js_string(part.field) + "] ?? \"\"))}";
                    continue;
                }
                for (char c : part.text) out += c == '`' || c == '\\' || c == '$' ? std::string("\\") + c : std::string(1, c);
            }
            return out + "`}";
        }

        // Text with {view.field} in it: the plain parts as they are, and each value as
        // a <Live> reading that view.
        std::string live_text(screen_parts& parts, const std::string& ns, std::string_view text) {
            // Words linked as in Markdown, [Ada Lovelace](https://...), each a link.
            for (std::size_t open = text.find('['); open != std::string_view::npos; open = text.find('[', open + 1)) {
                std::size_t middle = text.find("](", open);
                std::size_t close = middle == std::string_view::npos ? middle : text.find(')', middle);
                if (close == std::string_view::npos || text.substr(open, middle - open).find(']') != std::string_view::npos) continue;
                std::string_view to = text.substr(middle + 2, close - middle - 2);
                if (!(to.starts_with("https://") || to.starts_with("http://") || to.starts_with("/") || to.starts_with("#"))) continue;
                parts.components.insert("Link");
                return live_text(parts, ns, text.substr(0, open)) + "<Link to=" + address(parts, ns, to) + ">" +
                       live_text(parts, ns, text.substr(open + 1, middle - open - 1)) + "</Link>" + live_text(parts, ns, text.substr(close + 1));
            }
            std::string out;
            std::size_t at = 0;
            while (at < text.size()) {
                std::size_t open = text.find('{', at);
                std::size_t close = open == std::string_view::npos ? open : text.find('}', open);
                if (close == std::string_view::npos) {
                    out += web_detail::jsx_text(text.substr(at));
                    break;
                }
                out += web_detail::jsx_text(text.substr(at, open - at));
                std::string_view value = text.substr(open + 1, close - open - 1);
                std::size_t dot = value.find('.');  // a view's name has none; its value may, like stage.title
                // The year it's read in, and what the site was built from.
                if (value == "year" || value == "build.version" || value == "build.commit") {
                    parts.components.insert("Built");
                    out += "<Built value=" + web_detail::js_string(value == "year" ? "year" : value.substr(6)) + " />";
                } else if (dot == std::string_view::npos) {
                    out += web_detail::jsx_text(text.substr(open, close - open + 1));
                } else {
                    std::string view = full_view(ns, std::string(value.substr(0, dot)));
                    parts.components.insert("Live");
                    out += "<Live view={" + view_variable(parts, view) + "} field=" +
                           web_detail::js_string(value.substr(dot + 1)) + " />";
                }
                at = close + 1;
            }
            return out;
        }

        static bool has_form_for(const std::vector<language::screen_item>& items, const language::qualified_name& command) {
            return screens::model::has_form_for(items, command);
        }
        static const language::form_item* form_for(const std::string& ns, const std::vector<language::screen_item>& items, const std::string& command) {
            return screens::model::form_for(ns, items, command);
        }
        bool on_rows(const std::string& ns, const std::vector<language::screen_item>& items, const std::string& command) const {
            return model_.on_rows(ns, items, command);
        }
        static const language::button_item* button_for(const std::vector<language::screen_item>& items, const language::qualified_name& command) {
            return screens::model::button_for(items, command);
        }

        // Whether the person reading may run a command, as a button's allowed: allows
        // for a project's own roles, holds for those it declares.
        std::string allowed(screen_parts& parts, const std::string& ns, const std::string& command) {
            auto may = model_.allowed(ns, route_, command);
            if (!may) return "";
            parts.params.insert(may->within);
            if (may->by_record()) {
                parts.components.insert("allows");
                return " allowed={allows(" + view_variable(parts, may->view) + ", " + web_detail::js_string(may->field) + ", " +
                       web_detail::js_name(may->within + "_id") + ", " + web_detail::js_string(may->command) + ", " + web_detail::js_string(may->allows) + ")}";
            }
            parts.components.insert("holds");
            std::string roles;
            for (const auto& role : may->roles) roles += (roles.empty() ? "" : ", ") + web_detail::js_string(role);
            return " allowed={holds(" + view_variable(parts, may->view) + ", " + web_detail::js_string(may->field) + ", " +
                   web_detail::js_name(may->within + "_id") + ", [" + roles + "])}";
        }

        // A button's when, as JavaScript reading the page's views: false until every
        // view it reads has arrived, so a button that may not apply isn't shown early.
        std::string condition(screen_parts& parts, const std::string& ns, const language::expression& e) {
            std::set<std::string> read;
            std::string test = condition_of(parts, ns, e, read);
            std::string ready;
            for (const auto& variable : read) ready += variable + ".status === \"live\" && ";
            return ready + "(" + test + ")";
        }

        // x == none, or none != x: the side that isn't none, when the other is. A field
        // holds none when it's missing, null, empty text or an empty list, which
        // isNone asks, so the generated code doesn't compare with null alone.
        std::optional<const language::expression*> against_none(screen_parts& parts, const language::binary_expression& binary) {
            using language::token_kind;
            if (binary.op != token_kind::equal && binary.op != token_kind::not_equal) return std::nullopt;
            auto none = [](const language::expression& side) {
                auto* name = std::get_if<language::name_expression>(&side.node);
                return name && name->name.text() == "none";
            };
            if (!none(*binary.left) && !none(*binary.right)) return std::nullopt;
            parts.components.insert("isNone");
            return none(*binary.right) ? binary.left.get() : binary.right.get();
        }

        std::string none_test(const language::binary_expression& binary, const std::string& other) {
            return std::string(binary.op == language::token_kind::not_equal ? "!" : "") + "isNone(" + other + ")";
        }

        std::string condition_of(screen_parts& parts, const std::string& ns, const language::expression& e, std::set<std::string>& read) {
            using language::token_kind;
            if (auto* binary = std::get_if<language::binary_expression>(&e.node)) {
                // A list has a value, like assignees has me.
                if (binary->op == token_kind::has) {
                    parts.components.insert("listHas");
                    return "listHas(" + condition_of(parts, ns, *binary->left, read) + ", " + condition_of(parts, ns, *binary->right, read) + ")";
                }
                if (auto other = against_none(parts, *binary)) return none_test(*binary, condition_of(parts, ns, **other, read));
                // A view's values aren't typed, so an order is taken as the timeline's is:
                // logged > 0 is (ordered(…logged…) > ordered(0)), and none is before everything.
                if (binary->op == token_kind::less || binary->op == token_kind::greater ||
                    binary->op == token_kind::less_equal || binary->op == token_kind::greater_equal) {
                    parts.components.insert("ordered");
                    std::string op = binary->op == token_kind::less ? "<" : binary->op == token_kind::greater ? ">" : binary->op == token_kind::less_equal ? "<=" : ">=";
                    return "(ordered(" + condition_of(parts, ns, *binary->left, read) + ") " + op + " ordered(" + condition_of(parts, ns, *binary->right, read) + "))";
                }
                std::string op = binary->op == token_kind::equal ? "===" : binary->op == token_kind::not_equal ? "!=="
                               : binary->op == token_kind::logical_and ? "&&" : "||";
                return "(" + condition_of(parts, ns, *binary->left, read) + " " + op + " " + condition_of(parts, ns, *binary->right, read) + ")";
            }
            if (auto* unary = std::get_if<language::unary_expression>(&e.node)) return "!" + condition_of(parts, ns, *unary->operand, read);
            if (auto* member = std::get_if<language::member_expression>(&e.node)) {
                // order_page.stage.title: the view, then its value by its name, dots and all.
                std::string value = member->member;
                const language::expression* object = member->object.get();
                while (auto* inner = std::get_if<language::member_expression>(&object->node)) {
                    value = inner->member + "." + value;
                    object = inner->object.get();
                }
                auto* view = std::get_if<language::name_expression>(&object->node);
                if (!view) return "false";
                std::string variable = view_variable(parts, full_view(ns, view->name.text()));
                read.insert(variable);
                return "(" + variable + ".data?.[" + web_detail::js_string(value) + "] ?? null)";
            }
            if (auto* name = std::get_if<language::name_expression>(&e.node)) {
                const std::string word = name->name.text();
                if (word == "true" || word == "false") return word;
                if (word == "none") return "null";
                if (word == "me") {
                    parts.viewer = true;
                    return "viewer";
                }
                return web_detail::js_string(name->name.parts.back());  // a choice, like status::open
            }
            if (auto* literal = std::get_if<language::literal_expression>(&e.node)) {
                return literal->type == language::literal_expression::kind::number ? literal->value : web_detail::js_string(literal->value);
            }
            return "false";
        }

        // A timeline's new when, as JavaScript reading a row and the other views it
        // names, which the timeline keeps as they were when the page opened:
        // created_at > news.seen is (ordered(row["created_at"]) > ordered(read[0]?.["seen"])).
        // Times are compared as times, and none is before everything.
        std::string fresh_condition(screen_parts& parts, const std::string& ns, const language::expression& e, std::vector<std::string>& views) {
            using language::token_kind;
            auto value = [&](const language::expression& operand) -> std::string {
                if (auto* member = std::get_if<language::member_expression>(&operand.node)) {
                    auto* object = std::get_if<language::name_expression>(&member->object->node);
                    std::string variable = view_variable(parts, full_view(ns, object->name.text()));
                    auto at = std::find(views.begin(), views.end(), variable);
                    if (at == views.end()) at = views.insert(views.end(), variable);
                    return "read[" + std::to_string(at - views.begin()) + "]?.[" + web_detail::js_string(member->member) + "]";
                }
                if (auto* name = std::get_if<language::name_expression>(&operand.node); name && name->name.parts.size() == 1) {
                    const std::string word = name->name.text();
                    if (word != "true" && word != "false" && word != "none" && word != "me") return "row[" + web_detail::js_string(word) + "]";
                }
                return "";
            };
            if (auto* binary = std::get_if<language::binary_expression>(&e.node)) {
                bool order = binary->op == token_kind::less || binary->op == token_kind::greater ||
                             binary->op == token_kind::less_equal || binary->op == token_kind::greater_equal;
                if (order) {
                    parts.components.insert("ordered");
                    std::string op = binary->op == token_kind::less ? "<" : binary->op == token_kind::greater ? ">" : binary->op == token_kind::less_equal ? "<=" : ">=";
                    auto side = [&](const language::expression& operand) {
                        std::string read = value(operand);
                        return "ordered(" + (read.empty() ? fresh_condition(parts, ns, operand, views) : read) + ")";
                    };
                    return "(" + side(*binary->left) + " " + op + " " + side(*binary->right) + ")";
                }
                if (binary->op == token_kind::has) {
                    parts.components.insert("listHas");
                    return "listHas(" + fresh_condition(parts, ns, *binary->left, views) + ", " + fresh_condition(parts, ns, *binary->right, views) + ")";
                }
                if (auto other = against_none(parts, *binary)) return none_test(*binary, fresh_condition(parts, ns, **other, views));
                std::string op = binary->op == token_kind::equal ? "===" : binary->op == token_kind::not_equal ? "!=="
                               : binary->op == token_kind::logical_and ? "&&" : "||";
                return "(" + fresh_condition(parts, ns, *binary->left, views) + " " + op + " " + fresh_condition(parts, ns, *binary->right, views) + ")";
            }
            if (auto* unary = std::get_if<language::unary_expression>(&e.node)) return "!" + fresh_condition(parts, ns, *unary->operand, views);
            if (std::string read = value(e); !read.empty()) return "(" + read + " ?? null)";
            return row_condition(parts, e);
        }

        // A row's button's when, as JavaScript reading the row: person != me is
        // ((row["person"] ?? null) !== viewer).
        std::string row_condition(screen_parts& parts, const language::expression& e) {
            using language::token_kind;
            if (auto* binary = std::get_if<language::binary_expression>(&e.node)) {
                if (binary->op == token_kind::has) {
                    parts.components.insert("listHas");
                    return "listHas(" + row_condition(parts, *binary->left) + ", " + row_condition(parts, *binary->right) + ")";
                }
                if (auto other = against_none(parts, *binary)) return none_test(*binary, row_condition(parts, **other));
                std::string op = binary->op == token_kind::equal ? "===" : binary->op == token_kind::not_equal ? "!=="
                               : binary->op == token_kind::logical_and ? "&&" : binary->op == token_kind::logical_or ? "||"
                               : binary->op == token_kind::less ? "<" : binary->op == token_kind::greater ? ">"
                               : binary->op == token_kind::less_equal ? "<=" : ">=";
                return "(" + row_condition(parts, *binary->left) + " " + op + " " + row_condition(parts, *binary->right) + ")";
            }
            if (auto* unary = std::get_if<language::unary_expression>(&e.node)) return "!" + row_condition(parts, *unary->operand);
            if (auto* literal = std::get_if<language::literal_expression>(&e.node)) {
                return literal->type == language::literal_expression::kind::number ? literal->value : web_detail::js_string(literal->value);
            }
            if (auto* name = std::get_if<language::name_expression>(&e.node); name && name->name.parts.size() == 2) {
                return web_detail::js_string(name->name.parts.back());  // a choice, like role::maintainer
            }
            const std::string word = web_detail::text_of(e);
            if (word == "true" || word == "false") return word;
            if (word == "none") return "null";
            if (word == "me") {
                parts.viewer = true;
                return "viewer";
            }
            return "(row[" + web_detail::js_string(word) + "] ?? null)";
        }

        // menu { link /:project/settings "General" ... }: its links, down the side.
        std::string menu_of(screen_parts& parts, const std::string& ns, const language::content_block& menu) {
            parts.components.insert("Menu");
            std::string links;
            for (const auto& inside : menu.items) {
                auto* link = std::get_if<language::content_link>(&inside.node);
                if (!link) continue;
                std::string target = link->target.starts_with("/") ? full_route(ns, link->target) : link->target;
                links += (links.empty() ? "" : ", ") + std::string("{ to: ") + web_detail::js_string(target) + ", label: " +
                         web_detail::js_string(link->label) + " }";
            }
            return "<Menu links={[" + links + "]}>";
        }

        void screen_items(stream& out, screen_parts& parts, const std::string& ns,
                          const std::vector<language::screen_item>& items, const std::vector<language::screen_item>& screen,
                          std::size_t first = 0, std::size_t count = std::string::npos) {
            // Buttons one after another sit in a row: a command's own, and a form's that
            // opens it, and so do links. A command's line whose form draws its button is
            // passed over.
            auto presses = [&](const language::screen_item& it) {
                if (std::holds_alternative<language::content_link>(it.node)) return in_block_ == 0;  // a hero or a section lays out its own
                if (auto* b = std::get_if<language::button_item>(&it.node)) return !has_form_for(screen, b->command);
                if (auto* f = std::get_if<language::form_item>(&it.node)) {
                    return button_for(screen, f->commands.front()) != nullptr && !on_rows(ns, screen, full_command(ns, f->commands.front()));
                }
                return std::holds_alternative<language::copy_item>(it.node);
            };
            auto passed = [&](const language::screen_item& it) {
                if (auto* f = std::get_if<language::form_item>(&it.node)) return on_rows(ns, screen, full_command(ns, f->commands.front()));
                auto* b = std::get_if<language::button_item>(&it.node);
                return b && has_form_for(screen, b->command);
            };
            bool in_row = false;
            std::size_t end = count == std::string::npos ? items.size() : std::min(items.size(), first + count);
            for (std::size_t at = first; at < end; ++at) {
                const auto& item = items[at];
                if (in_row && !presses(item) && !passed(item)) {
                    out.close("</Actions>");
                    in_row = false;
                }
                // Lists of the same rows one after another, a table, a board, a pipeline or a
                // whiteboard, are one, switched between: each person's own pick, or else the
                // one whose when holds, like a board's style, or the first.
                if (auto here = shown_list(item)) {
                    std::size_t last = at;
                    while (last + 1 < end) {
                        auto next = shown_list(items[last + 1]);
                        if (!next || next->view != here->view || next->list != here->list) break;
                        ++last;
                    }
                    if (last > at) {
                        parts.components.insert("Switched");
                        const language::table_item* beside = nullptr;
                        for (std::size_t i = at; i <= last; ++i) {
                            if (auto* t = std::get_if<language::table_item>(&items[i].node); t && !beside) beside = t;
                        }
                        // One track for them all, the first that says one, drawn once above
                        // the switch, its pick kept to whichever way it's shown.
                        const language::track_setting* shared = nullptr;
                        for (std::size_t i = at; i <= last && !shared; ++i) {
                            if (auto* t = std::get_if<language::table_item>(&items[i].node); t && t->track) shared = &*t->track;
                            if (auto* b = std::get_if<language::board_item>(&items[i].node); b && b->track) shared = &*b->track;
                        }
                        group_track_ = shared;
                        std::string names, icons, start;
                        for (std::size_t i = at; i <= last; ++i) {
                            auto one = shown_list(items[i]);
                            names += (names.empty() ? "" : ", ") + web_detail::js_string(one->name);
                            std::string icon = one->name == "Whiteboard" ? "whiteboard" : one->name == "Pipeline" ? "pipeline" : one->name == "Board" ? "board" : "table";
                            icons += (icons.empty() ? "" : ", ") + web_detail::js_string(icon);
                            if (items[i].when) start += "(" + condition(parts, ns, *items[i].when) + ") ? " + std::to_string(i - at) + " : ";
                        }
                        std::string words = web_detail::label(here->list);
                        if (!words.empty()) words[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(words[0])));
                        out.open("<Switched id=" + web_detail::js_string(full_view(ns, here->view) + "." + here->list) + " label=" + web_detail::js_string("Show " + words + " as") +
                                 " options={[" + names + "]} icons={[" + icons + "]}" + (start.empty() ? "" : " start={" + start + "-1}") +
                                 (shared ? track_on(parts, ns, shared, track_stages(parts, ns, items, at, last)) + " view={" + view_variable(parts, full_view(ns, here->view)) +
                                               "} list=" + web_detail::js_string(here->list)
                                         : "") +
                                 ">");
                        for (std::size_t i = at; i <= last; ++i) {
                            const auto& one = items[i];
                            if (auto* t = std::get_if<language::table_item>(&one.node)) this->table(out, parts, ns, *t, screen);
                            else if (auto* b = std::get_if<language::board_item>(&one.node)) this->board(out, parts, ns, *b, screen, beside);
                            else if (auto* c = std::get_if<language::canvas_item>(&one.node)) this->canvas(out, parts, ns, *c);
                            else if (auto* l = std::get_if<language::pipeline_item>(&one.node)) this->pipeline(out, parts, ns, *l);
                        }
                        out.close("</Switched>");
                        group_track_ = nullptr;
                        at = last;
                        continue;
                    }
                }
                if (!in_row && presses(item)) {
                    std::size_t run = 0;
                    for (std::size_t next = at; next < items.size() && (presses(items[next]) || passed(items[next])); ++next) run += presses(items[next]);
                    if (run > 1) {
                        parts.components.insert("Actions");
                        out.open("<Actions>");
                        in_row = true;
                    }
                }
                auto from_item = out.from(screen_path_, item.where.line);
                item_line_ = item.where.line;
                // A menu's links go down the side, with everything after it beside them.
                if (auto* menu = std::get_if<language::content_block>(&item.node); menu && menu->type == language::content_block::kind::menu) {
                    out.open(menu_of(parts, ns, *menu));
                    screen_items(out, parts, ns, items, screen, at + 1);
                    out.close("</Menu>");
                    return;
                }
                if (auto* block = std::get_if<language::content_block>(&item.node); block && block->type == language::content_block::kind::heading) {
                    continue;  // drawn on the title row, before the rest
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    bool hero = block->type == language::content_block::kind::hero;
                    std::string tag = hero ? "Hero" : "Section";
                    parts.components.insert(tag);
                    std::string id = block->anchor ? " id=" + web_detail::js_string(*block->anchor) : "";
                    if (!block->hue.empty()) id += " hue=" + web_detail::js_string(block->hue);
                    if (block->icon) id += " icon=" + web_detail::js_string(*block->icon);
                    if (block->steps) id += " steps";
                    shown_while(out, parts, ns, item, [&] {
                        out.open("<" + tag + " title=" + web_detail::js_string(block->title) + id + ">");
                        ++in_block_;
                        screen_items(out, parts, ns, block->items, screen);
                        --in_block_;
                        out.close("</" + tag + ">");
                    });
                } else if (auto* text = std::get_if<language::content_text>(&item.node)) {
                    content(out, parts, ns, *text);
                } else if (auto* link = std::get_if<language::content_link>(&item.node)) {
                    std::string target = link->namespace_name ? full_route(link->namespace_name->text(), "/")
                                         : link->target.starts_with("/") ? full_route(ns, link->target)
                                                                                         : link->target;
                    std::string icon = link->icon ? " icon=" + web_detail::js_string(*link->icon) : "";
                    if (!link->built.empty()) {
                        // What the site was built from: its release's notes, or its commit.
                        parts.components.insert("BuiltLink");
                        out.line("<BuiltLink to=" + web_detail::js_string(link->built) + ">" + live_text(parts, ns, link->label) + "</BuiltLink>");
                    } else {
                        parts.components.insert("Link");
                        out.line("<Link to=" + web_detail::js_string(target) + icon + ">" + live_text(parts, ns, link->label) + "</Link>");
                    }
                } else if (auto* details = std::get_if<language::details_item>(&item.node)) {
                    this->details(out, parts, ns, *details);
                } else if (auto* copy = std::get_if<language::copy_item>(&item.node)) {
                    this->copy(out, parts, ns, *copy, screen);
                } else if (std::holds_alternative<language::themes_item>(item.node)) {
                    // Themes from a palette, tried on the whole app.
                    parts.components.insert("Themes");
                    out.line("<Themes />");
                } else if (auto* thread = std::get_if<language::thread_item>(&item.node)) {
                    parts.components.insert("Thread");
                    out.line("<Thread view={" + view_variable(parts, full_view(ns, thread->view.text())) + "} list=" + web_detail::js_string(thread->list) + " />");
                } else if (auto* box = std::get_if<language::find_item>(&item.node)) {
                    // A box finding rows of several lists, each with where it opens.
                    parts.components.insert("Find");
                    std::string sources;
                    for (const auto& source : box->sources) {
                        std::string one = "{ view: " + view_variable(parts, full_view(ns, source.view.text())) + ", list: " + web_detail::js_string(source.list.empty() ? "rows" : source.list) +
                                          ", label: " + web_detail::js_string(source.label);
                        if (source.link) {
                            std::string target = full_route(ns, *source.link);
                            one += ", link: " + web_detail::js_string(target);
                            std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                            auto keyed = keyed_by(ns, last, target);
                            if (!keyed.empty()) {
                                std::string names;
                                for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                                one += ", keyed: [" + names + "]";
                            }
                        }
                        // Words in what the rows don't hold, the server finds.
                        std::optional<std::string> list;
                        if (!source.list.empty()) list = source.list;
                        if (!model_.searched_by(ns, source.view.text(), list, source.by).empty()) {
                            std::string full = full_view(ns, source.view.text());
                            auto per = per_entity(full);
                            one += ", search: { view: " + web_detail::js_string(full) + (per ? ", subject: " + web_detail::js_name(*per + "_id") : std::string()) + " }";
                        }
                        std::string by;
                        for (const auto& name : source.by) by += (by.empty() ? "" : ", ") + web_detail::js_string(name);
                        sources += (sources.empty() ? "" : ", ") + one + ", by: [" + by + "] }";
                    }
                    out.line("<Find label=" + web_detail::js_string(box->label) + " sources={[" + sources + "]} />");
                } else if (auto* timeline = std::get_if<language::timeline_item>(&item.node)) {
                    parts.components.insert("Timeline");
                    std::string line = "<Timeline view={" + view_variable(parts, full_view(ns, timeline->view.text())) + "} list=" + web_detail::js_string(timeline->list);
                    // What each change was to, named by the list's own columns, like an
                    // issue's number and title, and opened by its link.
                    // A column whose fields are listed too, like issue beside issue.number,
                    // is there for the link, not to be read.
                    std::string subject;
                    auto columns = listed_columns(ns, timeline->view.text(), timeline->list);
                    for (const auto& column : columns) {
                        static const std::set<std::string> said{"field", "before", "after", "action", "created_by.name", "created_at"};
                        bool through = std::any_of(columns.begin(), columns.end(), [&](const std::string& other) { return other.starts_with(column + "."); });
                        if (!said.contains(column) && !through) subject += (subject.empty() ? "" : ", ") + web_detail::js_string(column);
                    }
                    if (!subject.empty()) line += " subject={[" + subject + "]}";
                    if (timeline->title) line += " title=" + web_detail::js_string(*timeline->title);
                    if (timeline->fresh) {
                        std::vector<std::string> views;
                        std::string test = fresh_condition(parts, ns, *timeline->fresh, views);
                        std::string read;
                        for (const auto& v : views) read += (read.empty() ? "" : ", ") + v;
                        line += " fresh={{ views: [" + read + "], test: (row, read) => " + test + " }}";
                    }
                    if (timeline->opened) line += " onOpen=" + web_detail::js_string(full_command(ns, *timeline->opened));
                    if (timeline->link) {
                        std::string target = full_route(ns, *timeline->link);
                        line += " link=" + web_detail::js_string(target);
                        // What it opens is named by its key's parts, like /:project/issues/:issue.
                        std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                        auto keyed = keyed_by(ns, last, target);
                        if (!keyed.empty()) {
                            std::string names;
                            for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                            line += " keyed={[" + names + "]}";
                        }
                        line += named_by(ns, target);
                    }
                    out.line(line + " />");
                } else if (auto* table = std::get_if<language::table_item>(&item.node)) {
                    shown_while(out, parts, ns, item, [&] { this->table(out, parts, ns, *table, screen); });
                } else if (auto* grid = std::get_if<language::grid_item>(&item.node)) {
                    this->grid(out, parts, ns, *grid, screen);
                } else if (auto* cards = std::get_if<language::cards_item>(&item.node)) {
                    shown_while(out, parts, ns, item, [&] { this->cards(out, parts, ns, *cards); });
                } else if (auto* chart = std::get_if<language::chart_item>(&item.node)) {
                    shown_while(out, parts, ns, item, [&] { this->chart(out, parts, ns, *chart); });
                } else if (auto* monitor = std::get_if<language::monitor_item>(&item.node)) {
                    shown_while(out, parts, ns, item, [&] { this->monitor(out, parts, ns, *monitor); });
                } else if (auto* board = std::get_if<language::board_item>(&item.node)) {
                    shown_while(out, parts, ns, item, [&] { this->board(out, parts, ns, *board, screen); });
                } else if (auto* canvas = std::get_if<language::canvas_item>(&item.node)) {
                    shown_while(out, parts, ns, item, [&] { this->canvas(out, parts, ns, *canvas); });
                } else if (auto* pipeline = std::get_if<language::pipeline_item>(&item.node)) {
                    shown_while(out, parts, ns, item, [&] { this->pipeline(out, parts, ns, *pipeline); });
                } else if (auto* form = std::get_if<language::form_item>(&item.node)) {
                    if (on_rows(ns, screen, full_command(ns, form->commands.front()))) continue;  // each row's button opens it
                    this->form(out, parts, ns, *form, button_for(screen, form->commands.front()));
                } else if (auto* confirm = std::get_if<language::confirm_item>(&item.node)) {
                    parts.components.insert("Confirm");
                    out.line("<Confirm command=" + web_detail::js_string(full_command(ns, confirm->command)) +
                             " question=" + web_detail::js_string(confirm->message) + " />");
                } else if (auto* button = std::get_if<language::button_item>(&item.node); button && button->along) {
                    steps(out, parts, ns, *button);
                } else if (auto* button = std::get_if<language::button_item>(&item.node)) {
                    if (has_form_for(screen, button->command)) continue;  // the form draws its own button
                    parts.components.insert("Command");
                    // A command on the entity the page's address names, like closing the
                    // issue at /projects/:project/issues/:issue, acts on that one.
                    std::string command = full_command(ns, button->command);
                    std::string id;
                    const language::entity_declaration* entity = entity_of_command(command);
                    if (entity && command.substr(command.rfind("::") + 2) != "create" && names_parameter(route_, entity->name) && entity_named(ns, entity->name)) {
                        parts.params.insert(entity->name);
                        id = " id={" + web_detail::js_name(entity->name + "_id") + "}";
                    }
                    std::string label = button->label ? " label=" + web_detail::js_string(*button->label) : "";
                    std::string when = button->when ? " when={" + condition(parts, ns, *button->when) + "}" : "";
                    std::string icon = button->icon ? " icon=" + web_detail::js_string(*button->icon) : "";
                    out.line("<Command name=" + web_detail::js_string(command) + id + label + icon + when + allowed(parts, ns, command) + " />");
                } else if (auto* component = std::get_if<language::component_item>(&item.node)) {
                    std::string tag = component_tag(component->name);
                    std::string line = "import " + tag + " from \"../components/" + component->name + "\";";
                    if (std::find(parts.imports.begin(), parts.imports.end(), line) == parts.imports.end()) parts.imports.push_back(line);
                    use_component(component->name);
                    out.line("<" + tag + " />");
                }
            }
            if (in_row) out.close("</Actions>");
        }

        // issue::move along project_page.steps: a button for each step from where the
        // entity is now, as a view per it on the screen says, that the person's roles
        // may take; pressing one moves it to the step's to.
        void steps(stream& out, screen_parts& parts, const std::string& ns, const language::button_item& button) {
            std::string command = full_command(ns, button.command);
            const language::entity_declaration* entity = entity_of_command(command);
            const language::command_declaration* declared = command_nodes_.count(command) ? command_nodes_.at(command) : nullptr;
            std::string field;
            if (declared) {
                for (const auto& st : declared->body) {
                    if (auto* c = std::get_if<language::changes_statement>(&st.node); c && !c->fields.empty()) field = c->fields.front();
                }
            }
            if (!entity || field.empty()) return;
            parts.components.insert("Steps");
            parts.params.insert(entity->name);
            // Where it is now: a view per the entity, holding the field.
            std::string current, here;
            if (auto scope = views_.find(ns); scope != views_.end()) {
                for (const auto& [name, view] : scope->second) {
                    if (!view->per || *view->per != entity->name) continue;
                    for (const auto& value : view->values) {
                        if ((value.name ? *value.name : web_detail::text_of(*value.value)) == field) current = view_variable(parts, web_detail::join(ns, name));
                    }
                    // What it's called where it is, like stage_title = issue.stage.title.
                    for (const auto& value : view->values) {
                        if (value.name && web_detail::text_of(*value.value) == entity->name + "." + field + ".title") here = *value.name;
                    }
                }
            }
            std::string list = view_variable(parts, full_view(ns, button.along->text()));
            std::string line = "<Steps command=" + web_detail::js_string(command) + " id={" + web_detail::js_name(entity->name + "_id") + "} field=" +
                               web_detail::js_string(field) + " current={" + (current.empty() ? "undefined" : current) + "} steps={" + list + "} list=" +
                               web_detail::js_string(button.along_list);
            if (!here.empty()) line += " here=" + web_detail::js_string(here);
            // Whose roles may take each step: a list of the project's roles on it.
            const language::view_declaration* view = nullptr;
            if (auto scope = views_.find(ns); scope != views_.end()) {
                if (auto it = scope->second.find(button.along->parts.back()); it != scope->second.end()) view = it->second;
            }
            for (std::size_t at = 0; view && at < view->each.size(); ++at) {
                const auto& each = view->each[at];
                if (!each.name || *each.name != button.along_list) continue;
                for (const auto& row : each.rows) {
                    std::string column = row.name ? *row.name : web_detail::text_of(*row.value);
                    if (column == "title") line += " shown=\"title\"";
                    if (column == "to.title") line += " to=\"to.title\"";
                    if (column == "from.title") line += " from=\"from.title\"";
                }
            }
            for (const auto& [name, value] : held_by(parts, ns, button)) line += " " + name + "=" + (value.starts_with("\"") ? value : "{" + value + "}");
            out.line(line + " />");
        }

        // Whose roles may take each step a button goes along, each as a property and
        // its JavaScript.
        std::vector<std::pair<std::string, std::string>> held_by(screen_parts& parts, const std::string& ns, const language::button_item& button) {
            std::vector<std::pair<std::string, std::string>> props;
            for (const auto& h : model_.held_by(ns, route_, button)) {
                parts.params.insert(h.within);
                props = {{"held", web_detail::js_string(h.held)},
                         {"roles", view_variable(parts, h.view)},
                         {"within", web_detail::js_name(h.within + "_id")},
                         {"place", web_detail::js_string(h.place)},
                         {"role", web_detail::js_string(h.role)}};
            }
            return props;
        }

        // board project_page.issues by stage over project_page.stages: the list's rows
        // as cards in the over list's columns, showing what its block names, and with
        // a move, dragged along the steps the person's roles may take.
        // Beside a table of the same list, it takes the table's toolbar and search when it
        // has none of its own.
        // A pipeline: its cards' rows by their stage, its stages, and the fields its lanes
        // and each kind's path are by.
        void pipeline(stream& out, screen_parts& parts, const std::string& ns, const language::pipeline_item& c) {
            parts.components.insert("Pipeline");
            const language::entity_declaration* entity = listed(ns, c.view.text(), c.list);
            std::string columns;
            for (const auto& column : c.columns) {
                auto column_shown = shown_as(ns, c.view.text(), c.list, entity, web_detail::text_of(*column.value), column.label);
                const std::string& key = column_shown.key;
                columns += (columns.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": " + web_detail::js_string(column_shown.label);
            }
            bool titled = false;
            for (const auto& column : listed_columns(ns, c.over.text(), c.over_list)) titled = titled || column == "title";
            std::string line = "<Pipeline view={" + view_variable(parts, full_view(ns, c.view.text())) + "} list=" + web_detail::js_string(*c.list) + " by=" + web_detail::js_string(c.by) +
                               " over={" + view_variable(parts, full_view(ns, c.over.text())) + "} overList=" + web_detail::js_string(c.over_list) +
                               " shown=" + (titled ? "\"title\"" : "\"name\"") + " columns={{ " + columns + " }} lanes=" + web_detail::js_string(c.lanes) +
                               " path=" + web_detail::js_string(c.path);
            if (!c.status_field.empty()) line += " statusBy=" + web_detail::js_string(c.status_field);
            if (std::string waits = model_.waits_on(ns, entity); !waits.empty()) line += " waitsOn=" + web_detail::js_string(waits);
            if (group_track_) line += track_on(parts, ns, nullptr);
            if (c.link) {
                std::string target = full_route(ns, *c.link);
                line += " link=" + web_detail::js_string(target);
                std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                auto keyed = keyed_by(ns, last, target);
                if (!keyed.empty()) {
                    std::string names;
                    for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                    line += " keyed={[" + names + "]}";
                }
                line += named_by(ns, target);
            }
            out.line(line + " />");
        }

        // A canvas: its cards' rows, where they're kept, the command placing them, the
        // grid they snap to, and how those not placed yet are laid out.
        void canvas(stream& out, screen_parts& parts, const std::string& ns, const language::canvas_item& c) {
            parts.components.insert("Canvas");
            const language::entity_declaration* entity = listed(ns, c.view.text(), c.list);
            std::string columns, pictures, people, labels, shown;
            for (const auto& column : c.columns) {
                std::string key = web_detail::text_of(*column.value);
                auto column_shown = shown_as(ns, c.view.text(), c.list, entity, key, column.label);
                if (column_shown.picture) people += (people.empty() ? "" : ", ") + web_detail::js_string(column_shown.key) + ": " + web_detail::js_string(*column_shown.picture);
                key = column_shown.key;
                columns += (columns.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": " + web_detail::js_string(column_shown.label);
                if (key.ends_with(".picture")) pictures += (pictures.empty() ? "" : ", ") + web_detail::js_string(key);
                for (const auto& f : entity ? entity->fields : nothing_) {
                    if (f.name != key) continue;
                    if (f.list && f.type && f.type->text() == "text") labels += (labels.empty() ? "" : ", ") + web_detail::js_string(key);
                    if (!f.choices.empty()) {
                        shown += (shown.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": Object.fromEntries(" + choice_options(f) + ")";
                    }
                }
            }
            auto number = [](double n) { return std::format("{}", n); };
            std::string line = "<Canvas view={" + view_variable(parts, full_view(ns, c.view.text())) + "} list=" + web_detail::js_string(*c.list) + " columns={{ " + columns + " }}";
            for (const auto& [name, field] : {std::pair{"x", c.x}, std::pair{"y", c.y}, std::pair{"w", c.w}, std::pair{"h", c.h}}) {
                if (field != name) line += std::string(" ") + name + "=" + web_detail::js_string(field);
            }
            if (!c.kind.empty()) line += " kind=" + web_detail::js_string(c.kind);
            if (c.place) line += " place=" + web_detail::js_string(full_command(ns, *c.place));
            if (c.grid > 0) line += " grid={{ step: " + number(c.grid) + ", x: " + number(c.origin_x) + ", y: " + number(c.origin_y) + " }}";
            if (!c.by.empty()) {
                line += " by=" + web_detail::js_string(c.by);
                if (!c.over.parts.empty()) line += " over={" + view_variable(parts, full_view(ns, c.over.text())) + "} overList=" + web_detail::js_string(c.over_list);
            }
            line += tint_on(ns, c.view, c.list, std::nullopt);
            if (!c.regions.empty()) line += " regions=" + web_detail::js_string(c.regions);
            if (c.move) line += " move=" + web_detail::js_string(full_command(ns, c.move->command));
            if (c.resize) line += " resize=" + web_detail::js_string(full_command(ns, *c.resize));
            // A pasted file makes a card as a create form on this screen would: what the
            // address names given, the file in its field, named from the file.
            if (const auto* card = listed(ns, c.view.text(), c.list); c.paste && card) {
                std::string given, named;
                bool many = false;
                for (const auto& field : card->fields) {
                    if (field.name == c.paste_into) many = field.list;
                    if ((field.name == "title" || field.name == "name") && named.empty() && field.type && field.type->text() == "text") named = field.name;
                    if (!field.type || field.type->parts.size() != 1 || field.name == c.paste_into) continue;
                    const std::string& points = field.type->parts[0];
                    if (!names_parameter(route_, points) || !entity_named(ns, points)) continue;
                    parts.params.insert(points);
                    given += (given.empty() ? "" : ", ") + field.name + ": " + web_detail::js_name(points + "_id");
                }
                line += " paste={{ command: " + web_detail::js_string(full_command(ns, *c.paste)) + ", into: " + web_detail::js_string(c.paste_into) +
                        (many ? ", many: true" : "") + (named.empty() ? "" : ", named: " + web_detail::js_string(named)) +
                        (given.empty() ? "" : ", given: { " + given + " }") + " }}";
            }
            if (!c.picture.empty()) line += " picture=" + web_detail::js_string(c.picture);
            if (group_track_) line += track_on(parts, ns, nullptr);
            if (c.link) {
                std::string target = full_route(ns, *c.link);
                line += " link=" + web_detail::js_string(target);
                std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                auto keyed = keyed_by(ns, last, target);
                if (!keyed.empty()) {
                    std::string names;
                    for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                    line += " keyed={[" + names + "]}";
                }
                line += named_by(ns, target);
            }
            if (!pictures.empty()) line += " pictures={[" + pictures + "]}";
            if (!people.empty()) line += " people={{ " + people + " }}";
            if (!shown.empty()) line += " choices={{ " + shown + " }}";
            if (!labels.empty()) line += " labels={[" + labels + "]}";
            out.line(line + " />");
        }

        // A list a screen shows, a table, a board, a pipeline or a canvas: its view, its
        // list, and what it's called in a switch between them.
        struct shown {
            std::string view, list, name;
        };
        static std::optional<shown> shown_list(const language::screen_item& item) {
            if (auto* t = std::get_if<language::table_item>(&item.node); t && t->list) return shown{t->view.text(), *t->list, "Table"};
            if (auto* b = std::get_if<language::board_item>(&item.node); b && b->list) return shown{b->view.text(), *b->list, "Board"};
            if (auto* l = std::get_if<language::pipeline_item>(&item.node); l && l->list) return shown{l->view.text(), *l->list, "Pipeline"};
            if (auto* c = std::get_if<language::canvas_item>(&item.node); c && c->list) return shown{c->view.text(), *c->list, "Whiteboard"};
            return std::nullopt;
        }

        // An element drawn only while its when holds, like a board while its board's style is kanban.
        template <typename Draw>
        void shown_while(stream& out, screen_parts& parts, const std::string& ns, const language::screen_item& item, Draw draw) {
            if (!item.when) {
                draw();
                return;
            }
            out.open("{" + condition(parts, ns, *item.when) + " && (<>");
            draw();
            out.close("</>)}");
        }

        void board(stream& out, screen_parts& parts, const std::string& ns, const language::board_item& board, const std::vector<language::screen_item>& screen,
                   const language::table_item* beside = nullptr) {
            parts.components.insert("Board");
            const language::entity_declaration* entity = listed(ns, board.view.text(), board.list);
            std::string columns, pictures, people, labels, shown;
            for (const auto& column : board.columns) {
                std::string key = web_detail::text_of(*column.value);
                if (key.find("::") != std::string::npos) continue;  // a button in its toolbar
                auto column_shown = shown_as(ns, board.view.text(), board.list, entity, key, column.label);
                if (column_shown.picture) people += (people.empty() ? "" : ", ") + web_detail::js_string(column_shown.key) + ": " + web_detail::js_string(*column_shown.picture);
                key = column_shown.key;
                columns += (columns.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": " +
                           web_detail::js_string(column_shown.label);
                if (key.ends_with(".picture")) pictures += (pictures.empty() ? "" : ", ") + web_detail::js_string(key);
                for (const auto& f : entity ? entity->fields : nothing_) {
                    if (f.name != key) continue;
                    if (f.list && f.type && f.type->text() == "text") labels += (labels.empty() ? "" : ", ") + web_detail::js_string(key);
                    if (!f.choices.empty()) {
                        shown += (shown.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": Object.fromEntries(" +
                                 choice_options(f) + ")";
                    }
                }
            }
            parts.shown_in_path = true;
            // Stages written in the code are its columns, and its workflow's steps the
            // moves, given as views that are always there.
            bool written = board.over.parts.empty();
            auto stages = written && entity ? model_.stages_written(ns, *entity, board.by) : screens::model::written_stages{};
            std::string over;
            if (written) {
                std::string listed, steps;
                for (const auto& [id, title] : stages.stages) {
                    listed += (listed.empty() ? "" : ", ") + std::string("{ id: ") + web_detail::js_string(id) + ", title: " + web_detail::js_string(title) + " }";
                }
                over = "{ status: \"live\", data: { stages: [" + listed + "] } }";
            } else {
                over = view_variable(parts, full_view(ns, board.over.text()));
            }
            std::string line = "<Board view={" + view_variable(parts, full_view(ns, board.view.text())) + "} list=" + web_detail::js_string(*board.list) +
                               " by=" + web_detail::js_string(board.by) + " over={" + over + "} overList=" + web_detail::js_string(written ? "stages" : board.over_list);
            if (written) line += " shown=\"title\"";
            for (const auto& column : written ? std::vector<std::string>{} : listed_columns(ns, board.over.text(), board.over_list)) {
                if (column == "title") line += " shown=\"title\"";
            }
            // Columns kept as records say their chime and status in their own fields.
            if (!board.chime_field.empty()) line += " notifyBy={{ field: " + web_detail::js_string(board.chime_field) + ", has: " + web_detail::js_string(board.chime_word) + " }}";
            if (!board.status_field.empty()) line += " statusBy=" + web_detail::js_string(board.status_field);
            if (std::string waits = model_.waits_on(ns, entity); !waits.empty()) line += " waitsOn=" + web_detail::js_string(waits);
            if (!stages.statuses.empty()) {
                std::string statuses;
                for (const auto& [stage, status] : stages.statuses) statuses += (statuses.empty() ? "" : ", ") + stage + ": " + web_detail::js_string(status);
                line += " statuses={{ " + statuses + " }}";
            }
            if (!stages.notify.empty()) {
                std::string notify;
                for (const auto& stage : stages.notify) notify += (notify.empty() ? "" : ", ") + web_detail::js_string(stage);
                line += " notify={[" + notify + "]}";
            }
            line += " columns={{ " + columns + " }}";
            line += tint_on(ns, board.view, board.list, board.tint);
            // A board beside a table, shown in its stead, takes the table's track.
            line += track_on(parts, ns, board.track ? &*board.track : beside && beside->track ? &*beside->track : nullptr);
            if (board.link) {
                std::string target = full_route(ns, *board.link);
                line += " link=" + web_detail::js_string(target);
                std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                auto keyed = keyed_by(ns, last, target);
                if (!keyed.empty()) {
                    std::string names;
                    for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                    line += " keyed={[" + names + "]}";
                }
                line += named_by(ns, target);
            }
            if (!pictures.empty()) line += " pictures={[" + pictures + "]}";
            if (!people.empty()) line += " people={{ " + people + " }}";
            if (!shown.empty()) line += " choices={{ " + shown + " }}";
            if (!labels.empty()) line += " labels={[" + labels + "]}";
            const std::vector<std::string>& search = board.search.empty() && beside ? beside->search : board.search;
            if (!search.empty()) {
                std::string fields;
                for (const auto& f : search) fields += (fields.empty() ? "" : ", ") + web_detail::js_string(f);
                line += " search={[" + fields + "]}";
            }
            std::string tools = toolbar(parts, ns, board.columns, screen);
            if (tools.empty() && beside) tools = toolbar(parts, ns, beside->columns, screen);
            line += tools;
            if (board.move && !board.move->along) {
                std::string steps;
                for (const auto& [from, to] : stages.steps) {
                    steps += (steps.empty() ? "" : ", ") + std::string("{ from: ") + web_detail::js_string(from) + ", to: " + web_detail::js_string(to) + " }";
                }
                line += " move={{ command: " + web_detail::js_string(full_command(ns, board.move->command)) + ", steps: { status: \"live\", data: { steps: [" + steps +
                        "] } }, list: \"steps\" }}";
            } else if (board.move) {
                std::string move = "command: " + web_detail::js_string(full_command(ns, board.move->command)) + ", steps: " +
                                   view_variable(parts, full_view(ns, board.move->along->text())) + ", list: " + web_detail::js_string(board.move->along_list);
                for (const auto& [name, value] : held_by(parts, ns, *board.move)) move += ", " + name + ": " + value;
                line += " move={{ " + move + " }}";
            }
            // Cards dragged into order within their column, each drop setting the field
            // with an update, as a table's rows are.
            if (board.reorder && entity) {
                std::string command = web_detail::join(ns, entity->name + "::update");
                std::string allowed = this->allowed(parts, ns, command);
                line += " reorder={{ command: " + web_detail::js_string(command) + ", field: " + web_detail::js_string(*board.reorder) +
                        (allowed.empty() ? "" : ", allowed: " + allowed.substr(10, allowed.size() - 11)) + " }}";
            }
            out.line(line + " />");
        }

        void content(stream& out, screen_parts& parts, const std::string& ns, const language::content_text& text) {
            // The short line over a title, and a front page's one sentence, which its design places.
            if (text.type == language::content_text::kind::overline || text.type == language::content_text::kind::statement) {
                std::string tag = text.type == language::content_text::kind::overline ? "Overline" : "Statement";
                parts.components.insert(tag);
                out.line("<" + tag + ">" + live_text(parts, ns, text.value) + "</" + tag + ">");
                return;
            }
            if (text.type == language::content_text::kind::text) {
                parts.components.insert("Text");
                // Shown only while its when holds, like who implemented an issue, once someone has.
                if (text.when) {
                    out.line("{" + condition(parts, ns, *text.when) + " && <Text>" + live_text(parts, ns, text.value) + "</Text>}");
                    return;
                }
                out.line("<Text>" + live_text(parts, ns, text.value) + "</Text>");
            } else if (text.type == language::content_text::kind::code) {
                namespace fs = std::filesystem;
                std::string source = beside_screen(text.value);
                fs::path path(source);
                std::string binding = web_detail::js_name(path.stem() == "main" ? path.parent_path().filename().string() : path.stem().string());
                std::string language = path.extension() == ".one" ? "uione" : path.extension().string().substr(path.extension().empty() ? 0 : 1);
                std::string from = platform::relative_import((fs::path(out_dir_) / "src/screens").string(), source);
                parts.imports.push_back("import " + binding + " from " + web_detail::js_string(from + "?raw") + ";");
                parts.components.insert("Code");
                out.line("<Code lang=" + web_detail::js_string(language) + " source={" + binding + "} />");
            } else {
                if (text.value.starts_with("{") && text.value.ends_with("}")) {
                    // A markdown field of a view, like {book_page.summary}, shown rendered.
                    std::string inside = text.value.substr(1, text.value.size() - 2);
                    std::size_t dot = inside.find('.');
                    std::string view = full_view(ns, inside.substr(0, dot));
                    parts.components.insert("Markdown");
                    std::string markdown = "<Markdown view={" + view_variable(parts, view) + "} field=" + web_detail::js_string(inside.substr(dot + 1)) + (text.subtitle ? " plain" : "") + " />";
                    // A subtitle, under the title, as running words.
                    if (text.subtitle) {
                        parts.components.insert("Subtitle");
                        markdown = "<Subtitle>" + markdown + "</Subtitle>";
                    }
                    out.line(markdown);
                    return;
                }
                // A subtitle in words of its own, like "Everything at a glance".
                if (text.subtitle) {
                    parts.components.insert("Subtitle");
                    out.line("<Subtitle>" + live_text(parts, ns, text.value) + "</Subtitle>");
                    return;
                }
                // Named for where the pages live: /docs is docs, /guides/api is guides-api.
                std::string name = docs_base_.empty() ? "pages" : docs_base_.substr(1);
                std::replace(name.begin(), name.end(), '/', '-');
                pages_.push_back({text.value, name, {screen_path_, item_line_}});
                parts.optional_page = true;
                parts.imports.push_back("import pages from \"../pages/" + name + ".generated\";");
                parts.components.insert("Pages");
                if (text.document) documents_ = true;
                out.line("<Pages base=" + web_detail::js_string(docs_base_) + " pages={pages}" + (text.document ? " document" : "") + " />");
            }
        }

        // A view's values, each beside what it is: a choice as it's shown, like In
        // progress, and a list of words each on its own.
        void details(stream& out, screen_parts& parts, const std::string& ns, const language::details_item& details) {
            parts.components.insert("Details");
            std::string full = full_view(ns, details.view.text());
            std::size_t split = full.rfind("::");
            std::string view_ns = split == std::string::npos ? "" : full.substr(0, split);
            std::string name = split == std::string::npos ? full : full.substr(split + 2);
            const language::view_declaration* view = nullptr;
            if (auto scope = views_.find(view_ns); scope != views_.end()) {
                if (auto found = scope->second.find(name); found != scope->second.end()) view = found->second;
            }
            const language::entity_declaration* entity = nullptr;
            if (view && view->per) {
                if (auto scope = entities_.find(view_ns); scope != entities_.end()) {
                    if (auto found = scope->second.find(*view->per); found != scope->second.end()) entity = found->second;
                }
            }
            std::string fields, shown, labels, files;
            for (const auto& f : details.fields) {
                std::string key = web_detail::text_of(*f.value);
                // Called as it says, or as its field is everywhere: assignees.name as
                // assignees is.
                std::optional<std::string> label = f.label;
                std::string first = key.substr(0, key.find('.')), rest = key.find('.') == std::string::npos ? "" : key.substr(key.find('.') + 1);
                for (const auto& field : entity ? entity->fields : nothing_) {
                    if (!label && field.name == first && (rest.empty() || rest == "name" || rest == "title") && field.label) label = field.label;
                }
                fields += (fields.empty() ? "" : ", ") + std::string("[") + web_detail::js_string(key) + ", " +
                          web_detail::js_string(label ? *label : web_detail::label(key)) + "]";
                if (!view || !entity) continue;
                for (const auto& value : view->values) {
                    if ((value.name ? *value.name : web_detail::text_of(*value.value)) != key) continue;
                    auto* member = std::get_if<language::member_expression>(&value.value->node);
                    if (!member) continue;
                    for (const auto& field : entity->fields) {
                        if (field.name != member->member) continue;
                        if (!field.choices.empty()) {
                            shown += (shown.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) +
                                     ": Object.fromEntries(" + choice_options(field) + ")";
                        }
                        if (field.list && field.type && field.type->text() == "text") labels += (labels.empty() ? "" : ", ") + web_detail::js_string(key);
                        // A file, or a list of them, is a link to each.
                        if (field.type && field.type->text() == "file") files += (files.empty() ? "" : ", ") + web_detail::js_string(key);
                    }
                }
            }
            std::string line = "<Details view={" + view_variable(parts, full) + "} fields={[" + fields + "]}";
            if (!shown.empty()) line += " choices={{ " + shown + " }}";
            if (!labels.empty()) line += " labels={[" + labels + "]}";
            if (!files.empty()) line += " files={[" + files + "]}";
            // tint by priority: a value of the view holding a choice of the entity's.
            if (details.tint && view && entity) {
                for (const auto& value : view->values) {
                    if ((value.name ? *value.name : web_detail::text_of(*value.value)) != *details.tint) continue;
                    auto* member = std::get_if<language::member_expression>(&value.value->node);
                    for (const auto& field : entity->fields) {
                        if (member && field.name == member->member) line += tint_of(*details.tint, field);
                    }
                }
            }
            out.line(line + " />");
        }

        // A button copying everything a view holds as Markdown, in the order the view
        // says it: each value but the ones the page's title shows, a markdown one as it
        // was written, then each list, as a conversation, a timeline of changes, or
        // rows. The ids of people, which mean nothing pasted elsewhere, are left out.
        // What a screen calls its values where it shows or asks for them: a details
        // line's label, like implementer "Implemented by", or a form field's, like
        // body "Description". The first one written wins.
        // Only the details of the same view count, and only the forms for the entity
        // it's per, not a comment's on the same page.
        static void labels_on(const std::vector<language::screen_item>& items, const std::string& view, const std::string& entity,
                              std::map<std::string, std::string>& out) {
            for (const auto& item : items) {
                if (auto* details = std::get_if<language::details_item>(&item.node)) {
                    if (details->view.text() != view) continue;
                    for (const auto& f : details->fields) {
                        if (f.label) out.emplace(web_detail::text_of(*f.value), *f.label);
                    }
                } else if (auto* form = std::get_if<language::form_item>(&item.node)) {
                    bool on = std::any_of(form->commands.begin(), form->commands.end(), [&](const language::qualified_name& c) {
                        return c.parts.size() >= 2 && c.parts[c.parts.size() - 2] == entity;
                    });
                    if (!on) continue;
                    for (const auto& f : form->fields) {
                        if (f.label) out.emplace(f.name, *f.label);
                    }
                } else if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    labels_on(block->items, view, entity, out);
                }
            }
        }

        // The lists of a view a screen shows to be read: in a table, a thread, a
        // timeline, cards or a board. A list only buttons are worked out from, like the
        // steps an issue's Move buttons go along, isn't one.
        static void lists_on(const std::vector<language::screen_item>& items, const std::string& view, std::set<std::string>& out) {
            auto take = [&](const language::qualified_name& named, const std::optional<std::string>& list) {
                if (named.text() == view && list) out.insert(*list);
            };
            for (const auto& item : items) {
                if (auto* table = std::get_if<language::table_item>(&item.node)) take(table->view, table->list);
                else if (auto* thread = std::get_if<language::thread_item>(&item.node)) take(thread->view, thread->list);
                else if (auto* timeline = std::get_if<language::timeline_item>(&item.node)) take(timeline->view, timeline->list);
                else if (auto* cards = std::get_if<language::cards_item>(&item.node)) take(cards->view, cards->list);
                else if (auto* monitor = std::get_if<language::monitor_item>(&item.node)) take(monitor->view, monitor->list);
                else if (auto* board = std::get_if<language::board_item>(&item.node)) take(board->view, board->list);
                else if (auto* canvas = std::get_if<language::canvas_item>(&item.node)) take(canvas->view, canvas->list);
                else if (auto* pipeline = std::get_if<language::pipeline_item>(&item.node)) take(pipeline->view, pipeline->list);
                else if (auto* block = std::get_if<language::content_block>(&item.node)) lists_on(block->items, view, out);
            }
        }

        // The field a value reads, through what the entity points at: issue.status is
        // the issue's status, and issue.project.lifecycle the project's lifecycle.
        const language::field* field_through(const std::string& ns, const language::entity_declaration* from, const language::member_expression& m) {
            std::vector<std::string> path{m.member};
            for (auto* at = std::get_if<language::member_expression>(&m.object->node); at; at = std::get_if<language::member_expression>(&at->object->node)) {
                path.insert(path.begin(), at->member);
            }
            for (std::size_t i = 0; from && i < path.size(); ++i) {
                const language::field* found = nullptr;
                for (const auto& f : from->fields) {
                    if (f.name == path[i]) found = &f;
                }
                if (!found || i + 1 == path.size()) return found;
                from = nullptr;
                if (found->type) {
                    if (auto scope = entities_.find(ns); scope != entities_.end()) {
                        if (auto e = scope->second.find(found->type->text()); e != scope->second.end()) from = e->second;
                    }
                }
            }
            return nullptr;
        }

        void copy(stream& out, screen_parts& parts, const std::string& ns, const language::copy_item& copy,
                  const std::vector<language::screen_item>& screen) {
            parts.components.insert("Copy");
            std::string full = full_view(ns, copy.view.text());
            std::size_t split = full.rfind("::");
            std::string view_ns = split == std::string::npos ? "" : full.substr(0, split);
            std::string name = split == std::string::npos ? full : full.substr(split + 2);
            const language::view_declaration* view = nullptr;
            if (auto scope = views_.find(view_ns); scope != views_.end()) {
                if (auto found = scope->second.find(name); found != scope->second.end()) view = found->second;
            }
            std::string variable = view_variable(parts, full);
            std::string fields, lists, shown;
            if (view) {
                const language::entity_declaration* entity = nullptr;
                if (view->per) {
                    if (auto scope = entities_.find(view_ns); scope != entities_.end()) {
                        if (auto found = scope->second.find(*view->per); found != scope->second.end()) entity = found->second;
                    }
                }
                std::map<std::string, std::string> called;
                labels_on(screen, copy.view.text(), view->per.value_or(""), called);
                for (const auto& value : view->values) {
                    std::string key = value.name ? *value.name : web_detail::text_of(*value.value);
                    if (screen_title_.find("{" + name + "." + key + "}") != std::string::npos) continue;
                    std::string kind;
                    auto* member = std::get_if<language::member_expression>(&value.value->node);
                    if (const language::field* f = member && entity ? field_through(view_ns, entity, *member) : nullptr) {
                        if (f->type && f->type->text() == "markdown") kind = "markdown";
                        // A person, or another entity, is held by its id, which means
                        // nothing pasted; its name or title is a value of its own, like
                        // implementer or stage_title.
                        if (f->type && f->type->text() == "user") kind = "people";
                        if (f->type && entities_.count(view_ns) && entities_.at(view_ns).count(f->type->text())) kind = "people";
                        // A choice as it's shown, like In progress.
                        if (!f->choices.empty()) {
                            shown += (shown.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) +
                                     ": Object.fromEntries(" + choice_options(*f) + ")";
                        }
                    }
                    if (kind == "people") continue;
                    auto label = called.find(key);
                    fields += (fields.empty() ? "" : ", ") + std::string("[") + web_detail::js_string(key) + ", " +
                              web_detail::js_string(label != called.end() ? label->second : web_detail::label(key)) +
                              (kind.empty() ? "" : ", " + web_detail::js_string(kind)) + "]";
                }
                std::set<std::string> shown_lists;
                lists_on(screen, copy.view.text(), shown_lists);
                for (const auto& each : view->each) {
                    if (!each.name || !shown_lists.contains(*each.name)) continue;
                    std::vector<std::string> columns;
                    for (const auto& row : each.rows) columns.push_back(row.name ? *row.name : web_detail::text_of(*row.value));
                    auto has = [&](const std::string& c) { return std::find(columns.begin(), columns.end(), c) != columns.end(); };
                    std::string kind = each.changes ? "changes" : has("body") && has("author.name") ? "thread" : "rows";
                    std::string shown;
                    for (const auto& c : columns) shown += (shown.empty() ? "" : ", ") + web_detail::js_string(c);
                    lists += (lists.empty() ? "" : ", ") + std::string("[") + web_detail::js_string(*each.name) + ", " +
                             web_detail::js_string(web_detail::label(*each.name)) + ", " + web_detail::js_string(kind) + ", [" + shown + "]]";
                }
            }
            std::string label = copy.label ? " label=" + web_detail::js_string(*copy.label) : "";
            std::string choices = shown.empty() ? "" : " choices={{ " + shown + " }}";
            out.line("<Copy view={" + variable + "}" + label + " fields={[" + fields + "]} lists={[" + lists + "]}" + choices + " />");
        }

        // cards project_page.boards link /:project/boards/:board { ... }: a card for each
        // row, its tally counted from another list, and its filters as links that open
        // it filtered, each a field and the value it's to hold.
        void chart(stream& out, screen_parts& parts, const std::string& ns, const language::chart_item& chart) {
            parts.components.insert("Chart");
            std::string series;
            for (const auto& c : chart.series) {
                series += (series.empty() ? "" : ", ") + std::string("{ path: ") + web_detail::js_string(c.text()) + ", title: " + web_detail::js_string(c.title) +
                          (c.unit.empty() ? "" : ", unit: " + web_detail::js_string(c.unit)) + " }";
            }
            out.line("<Chart view={" + view_variable(parts, full_view(ns, chart.view.text())) + "} series={[" + series + "]} />");
        }

        void monitor(stream& out, screen_parts& parts, const std::string& ns, const language::monitor_item& monitor) {
            parts.components.insert("Monitor");
            const language::entity_declaration* entity = listed(ns, monitor.view.text(), monitor.list);
            std::string line = "<Monitor view={" + view_variable(parts, full_view(ns, monitor.view.text())) + "} list=" + web_detail::js_string(monitor.list);
            if (monitor.title != "title") line += " title=" + web_detail::js_string(monitor.title);
            line += " status=" + web_detail::js_string(monitor.status);
            if (!monitor.doing.empty()) line += " doing=" + web_detail::js_string(monitor.doing);
            if (!monitor.since.empty()) line += " since=" + web_detail::js_string(monitor.since);
            if (!monitor.step.empty()) line += " step=" + web_detail::js_string(monitor.step) + " steps=" + web_detail::js_string(monitor.steps);
            if (!monitor.seen.empty()) line += " seen=" + web_detail::js_string(monitor.seen) + " seenSeconds={" + std::to_string(monitor.seen_seconds) + "}";
            if (!monitor.charts.empty()) {
                std::string charts;
                for (const auto& c : monitor.charts) {
                    charts += (charts.empty() ? "" : ", ") + std::string("{ path: ") + web_detail::js_string(c.text()) + ", title: " + web_detail::js_string(c.title) +
                              (c.unit.empty() ? "" : ", unit: " + web_detail::js_string(c.unit)) + " }";
                }
                line += " charts={[" + charts + "]}";
            }
            // A status with choices reads as each choice's label, like Standby.
            for (const auto& f : entity ? entity->fields : nothing_) {
                if (f.name == monitor.status && !f.choices.empty()) line += " choices={{ " + f.name + ": Object.fromEntries(" + choice_options(f) + ") }}";
            }
            if (monitor.link) {
                std::string target = full_route(ns, *monitor.link);
                line += " link=" + web_detail::js_string(target);
                std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                auto keyed = keyed_by(ns, last, target);
                if (!keyed.empty()) {
                    std::string names;
                    for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                    line += " keyed={[" + names + "]}";
                }
                line += named_by(ns, target);
            }
            out.line(line + " />");
        }

        void cards(stream& out, screen_parts& parts, const std::string& ns, const language::cards_item& cards) {
            parts.components.insert("Cards");
            const language::entity_declaration* entity = listed(ns, cards.view.text(), cards.list);
            std::string columns, pictures, people, labels, shown;
            for (const auto& column : cards.columns) {
                std::string key = web_detail::text_of(*column.value);
                auto column_shown = shown_as(ns, cards.view.text(), cards.list, entity, key, column.label);
                if (column_shown.picture) people += (people.empty() ? "" : ", ") + web_detail::js_string(column_shown.key) + ": " + web_detail::js_string(*column_shown.picture);
                key = column_shown.key;
                columns += (columns.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": " +
                           web_detail::js_string(column_shown.label);
                if (key.ends_with(".picture")) pictures += (pictures.empty() ? "" : ", ") + web_detail::js_string(key);
                for (const auto& f : entity ? entity->fields : nothing_) {
                    if (f.name != key) continue;
                    if (f.list && f.type && f.type->text() == "text") labels += (labels.empty() ? "" : ", ") + web_detail::js_string(key);
                    if (!f.choices.empty()) shown += (shown.empty() ? "" : ", ") + key + ": Object.fromEntries(" + choice_options(f) + ")";
                }
            }
            std::string line = "<Cards view={" + view_variable(parts, full_view(ns, cards.view.text())) + "} list=" + web_detail::js_string(cards.list.value_or("rows")) +
                               " columns={{ " + columns + " }}";
            if (cards.hide_empty) line += " hideEmpty";
            if (cards.link) {
                std::string target = full_route(ns, *cards.link);
                line += " link=" + web_detail::js_string(target);
                std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                auto keyed = keyed_by(ns, last, target);
                if (!keyed.empty()) {
                    std::string names;
                    for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                    line += " keyed={[" + names + "]}";
                }
                line += named_by(ns, target);
            }
            if (!pictures.empty()) line += " pictures={[" + pictures + "]}";
            if (!people.empty()) line += " people={{ " + people + " }}";
            if (!shown.empty()) line += " choices={{ " + shown + " }}";
            if (!labels.empty()) line += " labels={[" + labels + "]}";
            if (cards.tally) {
                std::string tally = "view: " + view_variable(parts, full_view(ns, cards.tally->text())) + ", list: " + web_detail::js_string(cards.tally_list) +
                                    ", by: " + web_detail::js_string(cards.tally_by) + ", and: " + web_detail::js_string(cards.tally_and);
                auto counted = listed_columns(ns, cards.tally->text(), cards.tally_list);
                auto has = [&](const std::string& column) { return std::find(counted.begin(), counted.end(), column) != counted.end(); };
                if (has(cards.tally_and + ".title")) tally += ", shown: " + web_detail::js_string(cards.tally_and + ".title");
                if (has(cards.tally_and + ".position")) tally += ", order: " + web_detail::js_string(cards.tally_and + ".position");
                if (has(cards.tally_and + ".status")) tally += ", status: " + web_detail::js_string(cards.tally_and + ".status");
                tally += ", noun: " + web_detail::js_string(cards.tally_list);  // issues
                line += " tally={{ " + tally + " }}";
            }
            if (!cards.filters.empty()) {
                std::string filters;
                for (const auto& f : cards.filters) {
                    filters += (filters.empty() ? "" : ", ") + std::string("{ label: ") + web_detail::js_string(f.label) + ", query: " + filter_query(*f.condition) +
                               (f.hue.empty() ? "" : ", hue: " + web_detail::js_string(f.hue)) + " }";
                }
                line += " filters={[" + filters + "]}";
            }
            out.line(line + " />");
        }

        // A filter as the address says it, like { author: "me" }, or, for a time,
        // { "updated_at>": "-7d" }: after seven days ago.
        static std::string filter_query(const language::expression& condition) {
            auto* b = std::get_if<language::binary_expression>(&condition.node);
            if (!b) return "{}";
            std::string field = web_detail::text_of(*b->left);
            switch (b->op) {
                case language::token_kind::less: field += "<"; break;
                case language::token_kind::greater: field += ">"; break;
                case language::token_kind::less_equal: field += "<="; break;
                case language::token_kind::greater_equal: field += ">="; break;
                case language::token_kind::not_equal: field += "!"; break;  // anything but it
                default: break;
            }
            std::string value;
            if (auto* named = std::get_if<language::name_expression>(&b->right->node)) {
                value = named->name.parts.back();  // me, or a choice like priority::high
            } else if (auto* literal = std::get_if<language::literal_expression>(&b->right->node)) {
                value = literal->value;
            }
            return "{ " + (web_detail::is_identifier(field) ? field : web_detail::js_string(field)) + ": " + web_detail::js_string(value) + " }";
        }

        // A toolbar's buttons: each command named in a table's or a board's block, like
        // issue::create "New issue", opening its form when the screen has one.
        std::string toolbar(screen_parts& parts, const std::string& ns, const std::vector<language::table_column>& columns, const std::vector<language::screen_item>& screen) {
            std::string tools;
            for (const auto& column : columns) {
                auto* named = std::get_if<language::name_expression>(&column.value->node);
                if (!named || named->name.parts.size() != 2) continue;
                std::string command = full_command(ns, named->name);
                language::button_item button{named->name, column.label, nullptr, std::nullopt, "", column.icon, {}};
                stream jsx("\t");
                if (const language::form_item* form = form_for(ns, screen, command)) {
                    this->form(jsx, parts, ns, *form, &button);
                } else {
                    parts.components.insert("Command");
                    std::string label = column.label ? " label=" + web_detail::js_string(*column.label) : "";
                    jsx.line("<Command name=" + web_detail::js_string(command) + label + allowed(parts, ns, command) + " />");
                }
                std::string line = jsx.str();
                while (!line.empty() && (line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
                tools += line;
            }
            return tools.empty() ? "" : " tools={<>" + tools + "</>}";
        }

        // grid project_page.steps by from and to over project_page.stages: a cell for
        // each pair, opening the entity's create where there's nothing, and its update
        // and delete where there's something, each as its form on the screen asks.
        void grid(stream& out, screen_parts& parts, const std::string& ns, const language::grid_item& grid, const std::vector<language::screen_item>& screen) {
            std::string tag = grid.drawn ? "Diagram" : "Grid";
            parts.components.insert(tag);
            const language::entity_declaration* entry = listed(ns, grid.view.text(), grid.list);
            std::string line = "<" + tag + " view={" + view_variable(parts, full_view(ns, grid.view.text())) + "} list=" + web_detail::js_string(*grid.list) +
                               " from=" + web_detail::js_string(grid.from) + " to=" + web_detail::js_string(grid.to);
            if (grid.cell) line += " cell=" + web_detail::js_string(web_detail::text_of(*grid.cell));
            line += " over={" + view_variable(parts, full_view(ns, grid.over.text())) + "} overList=" + web_detail::js_string(grid.over_list);
            for (const auto& column : listed_columns(ns, grid.over.text(), grid.over_list)) {
                if (column == "title") line += " shown=\"title\"";
            }
            auto command = [&](const std::string& action, bool given) -> std::string {
                if (!entry) return "";
                std::string full = web_detail::join(ns, entry->name + "::" + action);
                auto commands = commands_.find(ns);
                if (commands == commands_.end() || !commands->second.contains(entry->name + "::" + action)) return "";
                std::string spec = "{ name: " + web_detail::js_string(full);
                if (const language::form_item* form = form_for(ns, screen, full)) {
                    // The row and column are given by the cell, so its form doesn't ask them.
                    language::form_item asked;
                    asked.commands = form->commands;
                    for (const auto& f : form->fields) {
                        if (given && (f.name == grid.from || f.name == grid.to)) continue;
                        language::form_field copy;
                        copy.where = f.where;
                        copy.name = f.name;
                        copy.hint = f.hint;
                        copy.label = f.label;
                        asked.fields.push_back(std::move(copy));
                    }
                    spec += ", fields: [" + form_fields(parts, ns, asked) + "]";
                    if (form->submit) spec += ", submit: " + web_detail::js_string(*form->submit);
                }
                // What the page's address names, like the project, is sent with a create.
                if (given) {
                    std::string sent;
                    for (const auto& field : entry->fields) {
                        if (!field.type || field.type->parts.size() != 1 || field.name == grid.from || field.name == grid.to) continue;
                        const std::string& points = field.type->parts[0];
                        if (!names_parameter(route_, points) || !entity_named(ns, points)) continue;
                        parts.params.insert(points);
                        sent += (sent.empty() ? "" : ", ") + field.name + ": " + web_detail::js_name(points + "_id");
                    }
                    if (!sent.empty()) spec += ", given: { " + sent + " }";
                }
                std::string allowed = this->allowed(parts, ns, full);
                if (!allowed.empty()) spec += ", allowed: " + allowed.substr(10, allowed.size() - 11);
                return spec + " }";
            };
            if (auto c = command("create", true); !c.empty()) line += " create={" + c + "}";
            if (auto c = command("update", false); !c.empty()) line += " update={" + c + "}";
            if (auto c = command("delete", false); !c.empty()) line += " remove={" + c + "}";
            out.line(line + " />");
        }

        using shown_column = screens::shown_column;
        shown_column shown_as(const std::string& ns, const std::string& view, const std::optional<std::string>& list,
                              const language::entity_declaration* entity, const std::string& written, const std::optional<std::string>& said) const {
            return model_.shown_as(ns, view, list, entity, written, said);
        }

        void table(stream& out, screen_parts& parts, const std::string& ns, const language::table_item& table, const std::vector<language::screen_item>& screen) {
            parts.components.insert("Table");
            std::string view = full_view(ns, table.view.text());
            std::string columns;
            std::string actions;
            std::string pictures;
            std::string people;  // a person's column, by their name, and their picture beside it
            std::string labels;
            std::string shown;  // a choice column's values, as they're shown
            const language::entity_declaration* entity = listed(ns, table.view.text(), table.list);
            for (const auto& column : table.columns) {
                std::string key = web_detail::text_of(*column.value);
                if (key.find("::") != std::string::npos) continue;  // a button in its toolbar
                // member.picture, where member is a person: shown as their picture.
                if (entity && key.ends_with(".picture") && key.find('.') == key.rfind('.')) {
                    std::string person = key.substr(0, key.find('.'));
                    for (const auto& f : entity->fields) {
                        if (f.name == person && f.type && f.type->text() == "user") {
                            pictures += (pictures.empty() ? "" : ", ") + web_detail::js_string(key);
                        }
                    }
                }
                // A command, as a button on each row: named as the table says, like
                // delete "Remove", and only for whoever's role lets them press it.
                if (key.find('.') == std::string::npos) {
                    if (auto command = row_command(ns, table.view.text(), table.list, key)) {
                        std::string allowed = this->allowed(parts, ns, *command);
                        std::string action = web_detail::js_string(*command);
                        // A form for it on the screen asks first, opened from the row.
                        const language::form_item* asks = form_for(ns, screen, *command);
                        if (column.label || !allowed.empty() || column.when || asks) {
                            action = "{ name: " + action;
                            if (column.label) action += ", label: " + web_detail::js_string(*column.label);
                            // allowed={holds(...)} as a property: allowed: holds(...)
                            if (!allowed.empty()) action += ", allowed: " + allowed.substr(10, allowed.size() - 11);
                            if (column.when) action += ", when: (row) => " + row_condition(parts, *column.when);
                            if (asks) {
                                action += ", form: { fields: [" + form_fields(parts, ns, *asks) + "]";
                                if (asks->submit) action += ", submit: " + web_detail::js_string(*asks->submit);
                                action += " }";
                            }
                            action += " }";
                        }
                        actions += (actions.empty() ? "" : ", ") + action;
                        continue;
                    }
                }
                // A list of words, like an issue's labels, each shown on its own.
                if (entity && key.find('.') == std::string::npos) {
                    for (const auto& f : entity->fields) {
                        if (f.name == key && f.list && f.type && f.type->text() == "text") labels += (labels.empty() ? "" : ", ") + web_detail::js_string(key);
                    }
                }
                if (entity && key.find('.') == std::string::npos) {
                    for (const auto& f : entity->fields) {
                        if (f.name == key && !f.choices.empty()) {
                            shown += (shown.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) +
                                     ": Object.fromEntries(" + choice_options(f) + ")";
                        }
                    }
                }
                auto column_shown = shown_as(ns, table.view.text(), table.list, entity, key, column.label);
                if (column_shown.picture) people += (people.empty() ? "" : ", ") + web_detail::js_string(column_shown.key) + ": " + web_detail::js_string(*column_shown.picture);
                key = column_shown.key;
                columns += (columns.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": " + web_detail::js_string(column_shown.label);
            }
            std::string line = "<Table view={" + view_variable(parts, view) + "}";
            if (people_view(view) && !names_parameter(route_, "user")) line += " more={" + view_variable(parts, view) + ".more}";
            if (table.list) line += " list=" + web_detail::js_string(*table.list);
            if (table.link) {
                std::string target = full_route(ns, *table.link);
                line += " link=" + web_detail::js_string(target);
                // What it opens is named by its key's parts, like /:owner/:project.
                std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                // Or by a field of the rows that points at it, like a link's to in
                // /:project/:to, which opens an issue.
                if (const auto* rows = listed(ns, table.view.text(), table.list)) {
                    for (const auto& f : rows->fields) {
                        if (f.name == last && f.type && f.type->parts.size() == 1 && entity_named(ns, f.type->parts[0])) last = f.type->parts[0];
                    }
                }
                auto keyed = keyed_by(ns, last, target);
                if (!keyed.empty()) {
                    std::string names;
                    for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                    line += " keyed={[" + names + "]}";
                }
                line += named_by(ns, target);
            }
            // Tabs by a list's records, like stages: each one's id and title, in order.
            if (table.by && table.by_over) {
                std::string tabs_shown = "title";
                auto columns_over = listed_columns(ns, table.by_over->text(), table.by_over_list);
                if (std::find(columns_over.begin(), columns_over.end(), "title") == columns_over.end()) tabs_shown = "name";
                parts.components.insert("listChoices");
                std::string tabs = (web_detail::is_identifier(*table.by) ? *table.by : web_detail::js_string(*table.by)) + ": Object.fromEntries(listChoices(" +
                                   view_variable(parts, full_view(ns, table.by_over->text())) + ", " + web_detail::js_string(table.by_over_list) + ", " +
                                   web_detail::js_string(tabs_shown) + "))";
                shown += (shown.empty() ? "" : ", ") + tabs;
            }
            line += " columns={{ " + columns + " }}";
            if (!actions.empty()) line += " actions={[" + actions + "]}";
            if (!pictures.empty()) line += " pictures={[" + pictures + "]}";
            if (!people.empty()) line += " people={{ " + people + " }}";
            if (!shown.empty()) line += " choices={{ " + shown + " }}";
            if (!labels.empty()) line += " labels={[" + labels + "]}";
            if (table.by) line += " by=" + web_detail::js_string(*table.by);
            if (!table.search.empty()) {
                std::string fields;
                for (const auto& f : table.search) fields += (fields.empty() ? "" : ", ") + web_detail::js_string(f);
                line += " search={[" + fields + "]}";
            }
            if (table.sort) line += " sort=" + web_detail::js_string((table.sort_descending ? "-" : "") + *table.sort);
            if (table.page) line += " page={" + std::to_string(*table.page) + "}";
            if (table.page || table.by) parts.shown_in_path = true;
            if (table.only) line += " only={" + filter_query(*table.only) + "}";
            line += tint_on(ns, table.view, table.list, table.tint);
            line += track_on(parts, ns, table.track ? &*table.track : nullptr);
            if (!table.hue.empty()) line += " hue=" + web_detail::js_string(table.hue);
            if (table.hide_empty) line += " hideEmpty";
            if (table.download) line += " download=" + web_detail::js_string(*table.download);
            line += toolbar(parts, ns, table.columns, screen);
            // Rows dragged into order, each drop setting the field with an update.
            if (table.reorder && entity) {
                std::string command = web_detail::join(ns, entity->name + "::update");
                std::string allowed = this->allowed(parts, ns, command);
                line += " reorder={{ command: " + web_detail::js_string(command) + ", field: " + web_detail::js_string(*table.reorder) +
                        (allowed.empty() ? "" : ", allowed: " + allowed.substr(10, allowed.size() - 11)) + " }}";
            }
            out.line(line + " />");
        }

        // track={{ field: "milestone", view: boardPage, list: "milestones", shown: "title",
        // columns: { due: "Due" } }}: a list's records as cards in a row, picking one
        // keeping the rows whose field is it, which is in the page's path.
        // In a switch, the switch's track, whichever way it's shown, drawn above it, with
        // `more` said of it too, like its stages.
        std::string track_on(screen_parts& parts, const std::string& ns, const language::track_setting* setting, const std::string& more = "") {
            if (group_track_) setting = group_track_;
            if (!setting) return "";
            parts.shown_in_path = true;
            auto track = model_.track_of(ns, *setting);
            std::string columns;
            for (const auto& [key, label] : track.columns) {
                columns += (columns.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": " + web_detail::js_string(label);
            }
            return " track={{ field: " + web_detail::js_string(track.field) + ", view: " + view_variable(parts, track.view) +
                   ", list: " + web_detail::js_string(track.list) + ", shown: " + web_detail::js_string(track.shown) + ", columns: { " + columns + " }" +
                   (setting->hide_empty ? ", hideEmpty: true" : "") + (setting->picked_last ? ", pickedLast: true" : "") + (group_track_ ? ", above: true" : "") + more + " }}";
        }

        // What a switch's track works its dots out from: the stages of its board, or
        // else of its pipeline, as more of the track's settings.
        std::string track_stages(screen_parts& parts, const std::string& ns, const std::vector<language::screen_item>& items, std::size_t from, std::size_t to) {
            for (std::size_t i = from; i <= to; ++i) {
                if (auto* b = std::get_if<language::board_item>(&items[i].node)) {
                    std::string out = ", by: " + web_detail::js_string(b->by);
                    if (b->over.parts.empty()) {
                        const language::entity_declaration* entity = listed(ns, b->view.text(), b->list);
                        if (!entity) return "";
                        auto stages = model_.stages_written(ns, *entity, b->by);
                        std::string listed, statuses;
                        for (const auto& [id, title] : stages.stages) {
                            listed += (listed.empty() ? "" : ", ") + std::string("{ id: ") + web_detail::js_string(id) + ", title: " + web_detail::js_string(title) + " }";
                        }
                        for (const auto& [stage, status] : stages.statuses) statuses += (statuses.empty() ? "" : ", ") + stage + ": " + web_detail::js_string(status);
                        out += ", over: { status: \"live\", data: { stages: [" + listed + "] } }, overList: \"stages\"";
                        if (!statuses.empty()) out += ", statuses: { " + statuses + " }";
                    } else {
                        out += ", over: " + view_variable(parts, full_view(ns, b->over.text())) + ", overList: " + web_detail::js_string(b->over_list);
                        if (!b->status_field.empty()) out += ", statusBy: " + web_detail::js_string(b->status_field);
                    }
                    return out;
                }
            }
            for (std::size_t i = from; i <= to; ++i) {
                if (auto* l = std::get_if<language::pipeline_item>(&items[i].node)) {
                    std::string out = ", by: " + web_detail::js_string(l->by) + ", over: " + view_variable(parts, full_view(ns, l->over.text())) +
                                      ", overList: " + web_detail::js_string(l->over_list);
                    if (!l->status_field.empty()) out += ", statusBy: " + web_detail::js_string(l->status_field);
                    return out;
                }
            }
            return "";
        }

        // tint={{ field: "priority", order: [...] }}: the field's choices, the first
        // the most urgent, which color what shows it.
        static std::string tint_of(const screens::tint& tint) {
            std::string order;
            for (const auto& c : tint.order) order += (order.empty() ? "" : ", ") + web_detail::js_string(c);
            return " tint={{ field: " + web_detail::js_string(tint.field) + ", order: [" + order + "] }}";
        }
        static std::string tint_of(const std::string& name, const language::field& f) { return tint_of(screens::model::tint_of(name, f)); }

        // A list's tint, from the field of its rows' entity that the block names.
        std::string tint_on(const std::string& ns, const language::qualified_name& view, const std::optional<std::string>& list, const std::optional<std::string>& tint) {
            auto found = model_.tint_on(ns, view, list, tint);
            return found ? tint_of(*found) : "";
        }

        // A choice field's choices, each with how it's shown: [["mit", "MIT"], ...].
        static std::string choice_options(const std::vector<std::pair<std::string, std::string>>& choices) {
            std::string options;
            for (const auto& [value, shown] : choices) {
                options += (options.empty() ? "" : ", ") + std::string("[") + web_detail::js_string(value) + ", " + web_detail::js_string(shown) + "]";
            }
            return "[" + options + "]";
        }
        static std::string choice_options(const language::field& f) { return choice_options(screens::model::choice_options(f)); }

        // A list of an entity on the screen, as listChoices takes it: projectPage,
        // "roles", "title"; for people, their name and who they are.
        std::string listing(screen_parts& parts, const screens::listing& list) {
            if (!list.within.empty()) parts.params.insert(list.within);
            return view_variable(parts, list.view) + ", " + web_detail::js_string(list.list) + ", " + web_detail::js_string(list.shown) +
                   (list.person ? ", " + web_detail::js_string(*list.person) : "");
        }

        // What a form asks for, as the Form component takes it: each field with its
        // kind and choices.
        std::string form_fields(screen_parts& parts, const std::string& ns, const language::form_item& form) {
            std::string fields;
            for (const auto& f : model_.form_fields(ns, route_, form)) {
                std::string choices;
                for (const auto& looked : f.looked_at) {
                    parts.components.insert("listChoices");
                    listing(parts, looked);
                }
                if (f.from) {
                    choices = ", choices: listChoices(" + listing(parts, *f.from) + ")";
                } else if (f.options) {
                    choices = ", choices: " + choice_options(*f.options);
                    if (f.start) choices += ", start: " + web_detail::js_string(*f.start);
                    if (f.required) choices += ", required: true";
                } else if (f.range) {
                    choices = ", range: [" + f.range->from + ", " + f.range->to + ", " + f.range->by + "]";
                    if (f.start) choices += ", start: " + web_detail::js_string(*f.start);
                }
                std::string spec = f.type == "text" && !f.hint && !f.label
                    ? web_detail::js_string(f.name)
                    : "{ name: " + web_detail::js_string(f.name) + (f.label ? ", label: " + web_detail::js_string(*f.label) : "") +
                          (f.type == "text" ? "" : ", type: " + web_detail::js_string(f.type)) + choices +
                          (f.hint ? ", hint: " + web_detail::js_string(*f.hint) : "") + " }";
                fields += (fields.empty() ? "" : ", ") + spec;
            }
            return fields;
        }

        void form(stream& out, screen_parts& parts, const std::string& ns, const language::form_item& form, const language::button_item* button) {
            parts.components.insert("Form");
            std::string command = full_command(ns, form.commands.front());
            const language::entity_declaration* entity = entity_of_command(command);
            std::string fields = form_fields(parts, ns, form);
            // An update starts from the entity's page view and acts on the entity the
            // address names (the checker makes sure both are there).
            std::string edit;
            if (entity && command.substr(command.rfind("::") + 2) != "create") {
                auto inputs = inputs_of(command);
                bool only_inputs = std::all_of(form.fields.begin(), form.fields.end(), [&](const auto& f) { return inputs.contains(f.name); });
                if (only_inputs && names_parameter(route_, entity->name)) {
                    // Nothing to start from, like a typed confirmation: only which one.
                    parts.params.insert(entity->name);
                    edit = " id={" + web_detail::js_name(entity->name + "_id") + "}";
                } else if (auto view = edit_view(ns, *entity, form)) {
                    edit = " from={" + view_variable(parts, *view) + "} id={" + web_detail::js_name(entity->name + "_id") + "}";
                }
                // Once what the address names is deleted, its page has nothing to show, so
                // the form goes to the page above it: /:project/settings to /.
                if (command.substr(command.rfind("::") + 2) == "delete" && names_parameter(route_, entity->name)) {
                    edit += " after=" + web_detail::js_string(model_.after_delete(route_, under_, entity->name));
                }
            }
            // A create form on a screen whose address names what the entity points at,
            // like an issue made on /projects/:project, sends that without asking.
            std::string given;
            if (entity && command.substr(command.rfind("::") + 2) == "create") {
                for (const auto& field : entity->fields) {
                    if (!field.type || field.type->parts.size() != 1) continue;
                    const std::string& points = field.type->parts[0];
                    bool asked = std::any_of(form.fields.begin(), form.fields.end(), [&](const auto& f) { return f.name == field.name; });
                    if (asked || !names_parameter(route_, points) || !entity_named(ns, points)) continue;
                    parts.params.insert(points);
                    given += (given.empty() ? "" : ", ") + field.name + ": " + web_detail::js_name(points + "_id");
                }
            }
            if (!given.empty()) given = " given={{ " + given + " }}";
            std::string submit = form.submit ? " submit=" + web_detail::js_string(*form.submit) : "";
            // Everything but a command anyone may run needs its person signed in, so a
            // form asks someone who isn't to sign in, rather than taking what they type.
            std::string authenticated = open_.contains(command) ? "" : " authenticated";
            // The button that opens it says what its line on the screen says, and shows
            // while its when holds.
            std::string opens;
            if (button) {
                opens = " button";
                if (button->label) opens += " opener=" + web_detail::js_string(*button->label);
                if (button->icon) opens += " icon=" + web_detail::js_string(*button->icon);
                if (button->when) opens += " when={" + condition(parts, ns, *button->when) + "}";
            }
            out.line("<Form command=" + web_detail::js_string(command) + " fields={[" + fields + "]}" + edit + given + submit + opens + authenticated + allowed(parts, ns, command) +
                     " />");
        }

        // the files around the screens

        stream package_json() const {
            stream out;
            auto from = out.from(project_.path, project_.line);
            out.open("{");
            out.line("\"name\": " + web_detail::js_string(name_ + "-web") + ",");
            out.line("\"version\": \"0.0.0\",");
            out.line("\"description\": \"Generated by one. Do not edit.\",");
            out.line("\"private\": true,");
            out.line("\"type\": \"module\",");
            out.open("\"scripts\": {");
            out.line("\"dev\": \"vite\",");
            out.line("\"build\": \"tsc --noEmit && vite build\",");
            out.line("\"preview\": \"vite preview\"");
            out.close("},");
            // What every app needs, and what its hand-written components need besides.
            std::map<std::string, std::string> dependencies{
                {"@uione/" + ui_, std::string(version)}, {"@uione/react", std::string(version)}, {"firebase", "^12.19.0"},
                {"react", "^19.3.0"},                    {"react-dom", "^19.3.0"},                 {"react-router", "^7.18.4"}};
            // uione's own packages are the compiler's version, whatever the components'
            // package.json says, since they're released together: an editor checking
            // with an older compiler would call what's new a mistake.
            for (const auto& [package, wanted] : dependencies_) dependencies.emplace(package, package.starts_with("@uione/") ? std::string(version) : wanted);
            // Or as they're built in a checkout of uione, to try what isn't released.
            if (auto libraries = platform::libraries_from()) {
                for (auto& [package, wanted] : dependencies) {
                    if (package.starts_with("@uione/")) wanted = "file:" + (std::filesystem::path(*libraries) / "packages" / package.substr(7)).string();
                }
            }
            out.open("\"dependencies\": {");
            std::size_t n = 0;
            for (const auto& [package, wanted] : dependencies) {
                out.line(web_detail::js_string(package) + ": " + web_detail::js_string(wanted) + (++n < dependencies.size() ? "," : ""));
            }
            out.close("},");
            out.open("\"devDependencies\": {");
            out.line("\"@types/react\": \"^19.3.0\",");
            out.line("\"@types/react-dom\": \"^19.3.0\",");
            out.line("\"@vitejs/plugin-react\": \"^5.2.0\",");
            out.line("\"typescript\": \"~5.9.3\",");
            out.line("\"vite\": \"^7.3.6\"");
            out.close("}");
            out.close("}");
            return out;
        }

        static stream tsconfig_json() {
            stream out;
            auto fixed = out.fixed();  // the same in every app
            out.open("{");
            out.open("\"compilerOptions\": {");
            out.line("\"target\": \"ES2022\",");
            out.line("\"module\": \"ESNext\",");
            out.line("\"moduleResolution\": \"bundler\",");
            out.line("\"lib\": [\"ES2022\", \"DOM\", \"DOM.Iterable\"],");
            out.line("\"jsx\": \"react-jsx\",");
            out.line("\"strict\": true,");
            out.line("\"skipLibCheck\": true,");
            out.line("\"noEmit\": true,");
            out.line("\"types\": [\"vite/client\"]");
            out.close("},");
            out.line("\"include\": [\"src\"]");
            out.close("}");
            return out;
        }

        stream index_html() const {
            stream out;
            auto from = out.from(project_.path, project_.line);
            out.line("<!doctype html>");
            out.line("<!-- Generated by one. Do not edit. -->");
            // Square corners square the chips too, from the first paint.
            auto site = look();
            std::string attributes;
            // A section's color, like violet, in the shades of the built-in theme it's from.
            if (site.base == "papercolor") attributes += " data-palette=\"papercolor\"";
            if (site.corners == 0) attributes += " data-corners=\"square\"";
            // Light or dark from the first paint, which a visitor's own pick replaces.
            if (!appearance_.empty()) attributes += " data-appearance=\"" + appearance_ + "\" data-theme=\"" + appearance_ + "\"";
            out.open("<html lang=\"en\"" + attributes + ">");
            out.open("<head>");
            out.line("<meta charset=\"utf-8\" />");
            out.line("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />");
            out.line("<title>" + web_detail::html_escape(title_.empty() ? name_ : title_) + "</title>");
            if (std::string fonts = theme::fonts_link(site, documents_); !fonts.empty()) {
                out.line("<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin />");
                out.line("<link rel=\"stylesheet\" href=\"" + fonts + "\" />");
            }
            if (!icon_.empty()) {
                auto from_icon = out.from(icon_source_.path, icon_source_.line);
                out.line("<link rel=\"icon\" type=\"image/svg+xml\" href=\"/icon.svg\" />");
            }
            out.close("</head>");
            out.open("<body>");
            out.line("<div id=\"root\"></div>");
            out.line("<script type=\"module\" src=\"/src/main.tsx\"></script>");
            out.close("</body>");
            out.close("</html>");
            return out;
        }

        // The icon the project names, copied as it is. Vite serves what's in public/
        // at the root, so it's /icon.svg in the app.
        stream icon_svg() const {
            stream out;
            auto from = out.from(icon_source_.path, icon_source_.line);
            std::string svg = platform::read_file(icon_).value_or("");
            if (svg.ends_with('\n')) svg.pop_back();
            out.line(svg);
            return out;
        }

        stream vite_config() const {
            stream out;
            auto from = out.from(project_.path, project_.line);
            out.generated_from(source_name());
            out.line("import react from \"@vitejs/plugin-react\";");
            out.line("import { defineConfig } from \"vite\";");
            out.line();
            out.line("// Commands go to the Go backend, which listens on port 8081.");
            out.open("export default defineConfig({");
            out.line("plugins: [react()],");
            out.line("server: { proxy: { \"/api\": \"http://localhost:8081\" } },");
            // The compiler starts its worker from a file beside its own, which Vite's
            // prebundling for development would leave behind.
            if (dependencies_.contains("@uione/compiler")) out.line("optimizeDeps: { exclude: [\"@uione/compiler\"] },");
            out.close("});");
            return out;
        }

        // The site's theme, harbor unless it says, as the variables its component set
        // draws with.
        theme::look look() const {
            return theme::resolve(files_, theme_.empty() ? "harbor" : theme_).value_or(theme::resolve(files_, "harbor").value_or(theme::look{}));
        }

        stream theme_css() const {
            stream out;
            auto from = out.from(project_.path, project_.line);
            std::string text = theme::css(look());
            std::size_t at = 0;
            for (std::size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) out.line(text.substr(at, end - at));
            return out;
        }

        stream main_tsx() const {
            stream out;
            auto from = out.from(project_.path, project_.line);
            out.generated_from(source_name());
            out.line("import { createRoot } from \"react-dom/client\";");
            out.line("import \"@uione/" + ui_ + "/styles.css\";");
            out.line("import \"./theme.css\";");
            out.line("import Site from \"./app\";");
            out.line();
            out.line("createRoot(document.getElementById(\"root\")!).render(<Site />);");
            return out;
        }

        stream app_tsx(const std::vector<screen_import>& screens) const {
            stream out;
            auto from = out.from(project_.path, project_.line);
            out.generated_from(source_name());
            out.line("import { App } from \"@uione/react\";");
            {
                // Each way of signing in is its own module, so the app has only the ones
                // the project names, each named for what it does, so no screen's name
                // takes it.
                std::vector<std::string> imported;
                for (const auto& method : authentication_) imported.push_back(method + " as " + web_detail::sign_in_with(method));
                if (analytics_) imported.push_back("firebaseAnalytics");
                imported.push_back("firebaseSource");
                std::sort(imported.begin(), imported.end());
                std::string list;
                for (const auto& name : imported) list += (list.empty() ? "" : ", ") + name;
                out.line("import { " + list + " } from \"@uione/react/firebase\";");
            }
            out.line("import { " + ui_ + " } from \"@uione/" + ui_ + "\";");
            if (footer_) out.line("import { SiteFooter } from \"./footer\";");
            std::string names;
            for (const auto& screen : screens) {
                auto from_screen = out.from(screen.from.path, screen.from.line);
                out.line("import { " + screen.name + " } from " + web_detail::js_string(screen.module) + ";");
                names += (names.empty() ? "" : ", ") + screen.name;
            }
            out.line();
            out.line("// Views are read live from Firestore, and commands go to the Go backend. While");
            out.line("// developing, both are the local emulators. A production build uses the real");
            out.line("// project, with the settings the deploy writes to .env.production.");
            out.open("const local = {");
            // Signing in opens a window, which Firebase only does with an auth domain,
            // even with the emulator answering in its place.
            out.line("config: { projectId: \"demo-uione\", apiKey: \"demo\", authDomain: \"demo-uione.firebaseapp.com\" },");
            out.line("emulators: { firestore: \"localhost:8080\", auth: \"localhost:9099\" },");
            out.close("};");
            out.open("const cloud = {");
            out.open("config: {");
            out.line("projectId: import.meta.env.VITE_FIREBASE_PROJECT_ID,");
            out.line("apiKey: import.meta.env.VITE_FIREBASE_API_KEY,");
            out.line("appId: import.meta.env.VITE_FIREBASE_APP_ID,");
            out.line("authDomain: import.meta.env.VITE_FIREBASE_AUTH_DOMAIN,");
            if (analytics_) out.line("measurementId: import.meta.env.VITE_FIREBASE_MEASUREMENT_ID,");
            out.close("},");
            out.close("};");
            std::string personal;
            if (!personal_.empty()) {
                std::string list;
                for (const auto& v : personal_) list += (list.empty() ? "" : ", ") + web_detail::js_string(v);
                personal = ", personal: [" + list + "]";
            }
            // The ways people sign in, in the order the project names them; Google when
            // it doesn't say.
            std::string authentication;
            for (const auto& method : authentication_) authentication += (authentication.empty() ? "" : ", ") + web_detail::sign_in_with(method);
            if (!authentication.empty()) authentication = ", authentication: [" + authentication + "]";
            out.line("const data = firebaseSource({ ...(import.meta.env.DEV ? local : cloud)" + personal + authentication + " });");
            if (analytics_) {
                out.line("// Visitors are counted once they agree, and only where the deploy found the");
                out.line("// project linked to Google Analytics, which gives it a measurement ID.");
                out.line("const analytics = !import.meta.env.DEV && cloud.config.measurementId ? firebaseAnalytics(cloud.config) : undefined;");
            }
            out.line();
            std::string icon;
            if (!icon_.empty()) icon = ", icon: \"/icon.svg\"";
            {
                auto from_icon = icon_.empty() ? out.from(project_.path, project_.line) : out.from(icon_source_.path, icon_source_.line);
                // A project that names no way of signing in offers none.
                std::string offered = has_project_ && authentication_.empty() ? ", authentication: false" : "";

                // The header's badges, beside the name, each a value of a view, by the view's
                // full name, wherever it's declared.
                std::string badges;
                std::vector<language::header_badge> shown;
                for (const auto& f : files_) header_badges(f.declarations, shown);
                for (const auto& badge : shown) {
                    std::string full = badge.view;
                    for (const auto& [ns, declared] : views_) {
                        if (declared.contains(badge.view)) full = web_detail::join(ns, badge.view);
                    }
                    badges += std::string(badges.empty() ? "" : ", ") + "{ view: " + web_detail::js_string(full) + ", value: " + web_detail::js_string(badge.value) + " }";
                }
                if (!badges.empty()) badges = ", badges: [" + badges + "]";
                // What it was built from, which a footer can show: the uione release, and
                // the commit and repository, as the deploy found them.
                std::string footer = ", build: { version: " + web_detail::js_string(std::string(one::version)) +
                                     ", commit: import.meta.env.VITE_UIONE_COMMIT, repository: import.meta.env.VITE_UIONE_REPOSITORY }";
                if (footer_) footer += ", footer: { layout: " + web_detail::js_string(footer_layout_) + " as const, content: SiteFooter }";
                // The top bar's order, when the header gives one.
                std::string navigation;
                for (const auto& address : model_.navigation(files_)) navigation += (navigation.empty() ? "" : ", ") + web_detail::js_string(address);
                if (!navigation.empty()) footer += ", navigation: [" + navigation + "]";
                out.line("export const site = { name: " + web_detail::js_string(title_.empty() ? name_ : title_) + icon + ", screens: [" + names + "], ui: " + ui_ +
                         ", data" + offered + (analytics_ ? ", analytics" : "") + badges + footer + (display_ ? ", display: true" : "") + " };");
            }
            out.line();
            out.open("export default function Site() {");
            out.line("return <App {...site} />;");
            out.close("}");
            return out;
        }

        // A file an item names, like code "examples/main.one" or markdown "docs/*.md", is
        // found beside the .one file the item is in, as components and backends are.
        std::string beside(const std::string& one_file, const std::string& path) const {
            std::string folder = one_file.empty() ? project_dir_ : std::filesystem::path(one_file).parent_path().string();
            return platform::resolve(folder.empty() ? "." : folder, path);
        }
        std::string beside_screen(const std::string& path) const { return beside(screen_path_, path); }

        stream pages_module(const page_set& pages) const {
            namespace fs = std::filesystem;
            fs::path glob = beside(pages.source.path, pages.pattern);
            std::string extension = glob.extension().string();
            stream out;
            auto from = out.from(pages.source.path, pages.source.line);
            out.generated_from(pages.pattern);
            out.line("import type { DocPage } from \"@uione/react\";");
            out.line();
            out.open("const pages: DocPage[] = [");
            for (const auto& path : platform::files_in(glob.parent_path().string(), extension)) {
                std::string markdown = platform::read_file(path).value_or("");
                std::string slug = markdown_slug(fs::path(path).stem().string());
                out.open("{");
                out.line("slug: " + web_detail::js_string(slug) + ",");
                out.line("title: " + web_detail::js_string(markdown_title(markdown, slug)) + ",");
                out.line("html: " + web_detail::js_string(markdown_to_html(markdown)) + ",");
                out.close("},");
            }
            out.close("];");
            out.line();
            out.line("export default pages;");
            return out;
        }

        std::string source_name() const {
            std::string folder = std::filesystem::path(project_dir_).lexically_normal().filename().string();
            if (folder.empty()) folder = std::filesystem::path(project_dir_).lexically_normal().parent_path().filename().string();
            return folder + "/";
        }

    };

    std::vector<output_file> generate_web(const std::vector<language::file>& files, const std::string& project_dir,
                                          const std::string& out_dir) {
        return web_generator(files, project_dir, out_dir).generate();
    }

} // namespace one::generators
