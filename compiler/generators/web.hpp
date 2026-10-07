// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes the React app for a project: a Vite project whose screens are built on
// @uione/react and drawn by the component set the project picks with `ui`.
//
// Each .one file with screens becomes one file under src/screens, and each screen
// becomes one screen(...) in it. Everything a screen does is in @uione/react, so a
// generated screen is only the components that make it up, about as long as the
// .one it came from (decision 0002).
//
// Screens read their views live from Firestore and send commands to the Go backend
// that `one build` writes next to the app, through @uione/react/firebase.

#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "code/stream.hpp"
#include "generators/markdown.hpp"
#include "generators/roles.hpp"
#include "language/ast.hpp"
#include "language/names.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace one::generators {

    struct output_file {
        std::string path;  // inside the web folder, like src/app.tsx
        std::string content;
        bool executable = false;  // a script, to be run as it is
        std::vector<std::optional<code::source>> sources = {};  // where each line came from
    };

    inline output_file file(std::string path, const code::stream& written, bool executable = false) {
        return {std::move(path), written.str(), executable, written.sources()};
    }

    namespace web_detail {

        // Text as HTML, with what would be markup written as entities.
        inline std::string html_escape(std::string_view text) {
            std::string out;
            for (char c : text) {
                switch (c) {
                    case '&': out += "&amp;"; break;
                    case '<': out += "&lt;"; break;
                    case '>': out += "&gt;"; break;
                    case '"': out += "&quot;"; break;
                    default: out += c;
                }
            }
            return out;
        }

        // A string as JavaScript source: "like this", with quotes and backslashes escaped.
        inline std::string js_string(std::string_view text) {
            std::string out = "\"";
            for (char c : text) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out += c;
                }
            }
            return out + "\"";
        }

        // Text as JSX children. Braces and angle brackets would be read as code, so
        // they're written as string expressions.
        inline std::string jsx_text(std::string_view text) {
            std::string out;
            for (char c : text) {
                if (c == '{' || c == '}' || c == '<' || c == '>') {
                    out += "{\"";
                    out += c;
                    out += "\"}";
                } else {
                    out += c;
                }
            }
            return out;
        }

        // A JavaScript name from a .one name or a title: sort_title and "Sort title"
        // both become sortTitle.
        // What a way of signing in is called in an app: signInWithGitHub for github.
        inline std::string sign_in_with(std::string_view method) {
            if (method == "github") return "signInWithGitHub";
            std::string name(method);
            name[0] = static_cast<char>(name[0] - 'a' + 'A');
            return "signInWith" + name;
        }

        inline std::string js_name(std::string_view name) {
            std::string snake = language::to_snake_case(name);
            std::string out;
            bool upper = false;
            for (char c : snake) {
                bool letter_or_digit = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
                if (!letter_or_digit) {
                    upper = !out.empty();
                    continue;
                }
                out += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
                upper = false;
            }
            if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out = "screen" + out;
            return out;
        }

        // A readable label, the same way @uione/react makes one: created_at becomes
        // "Created at".
        inline std::string label(std::string_view name) {
            std::string out;
            for (char c : name) out += c == '_' ? ' ' : c;
            if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
            return out;
        }

        inline std::string join(const std::string& ns, const std::string& name) {
            if (ns.empty()) return name;
            if (name.empty()) return ns;
            return ns + "::" + name;
        }

        // The written name of a value, like book.title or waitlist::signups.
        inline std::string text_of(const language::expression& e) {
            if (auto* name = std::get_if<language::name_expression>(&e.node)) return name->name.text();
            if (auto* member = std::get_if<language::member_expression>(&e.node)) return text_of(*member->object) + "." + member->member;
            return "";
        }

        inline bool is_identifier(std::string_view key) {
            if (key.empty() || (key[0] >= '0' && key[0] <= '9')) return false;
            return std::all_of(key.begin(), key.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
        }

    } // namespace web_detail

    class web_generator {
    public:
        // `files` have parsed and checked cleanly. `project_dir` is the folder they came
        // from, and `out_dir` is where the web app goes, so imports between the two can
        // be worked out.
        web_generator(const std::vector<language::file>& files, std::string project_dir, std::string out_dir)
            : files_(files), project_dir_(std::move(project_dir)), out_dir_(std::move(out_dir)), held_(roles_of(files).held) {
            for (const auto& f : files_) {
                indexing_ = f.path;
                index("", f.declarations);
            }
        }

        std::vector<output_file> generate() {
            std::vector<output_file> out;
            std::vector<screen_import> screens;
            for (const auto& f : files_) {
                std::vector<found_screen> found;
                find_screens("", f.declarations, found);
                if (found.empty()) continue;
                std::string stem = std::filesystem::path(f.path).stem().string();
                out.push_back(file("src/screens/" + stem + ".tsx", screen_file(f, found, screens, stem)));
            }
            for (auto& f : component_files()) out.push_back(std::move(f));
            out.push_back(file("package.json", package_json()));
            out.push_back(file("tsconfig.json", tsconfig_json()));
            out.push_back(file("index.html", index_html()));
            out.push_back(file("vite.config.ts", vite_config()));
            out.push_back(file("src/main.tsx", main_tsx()));
            out.push_back(file("src/app.tsx", app_tsx(screens)));
            for (const auto& pages : pages_) out.push_back(file("src/pages/" + pages.name + ".generated.ts", pages_module(pages)));
            if (!icon_.empty()) out.push_back(file("public/icon.svg", icon_svg()));
            // Served as they are: Vite copies public/ into the site unchanged.
            if (!served_.empty()) {
                for (const auto& name : platform::files_under(served_)) {
                    output_file f{"public/" + name, platform::read_file(served_ + "/" + name).value_or(""), false, {}};
                    f.sources.assign(static_cast<std::size_t>(std::count(f.content.begin(), f.content.end(), '\n')), served_source_);
                    out.push_back(std::move(f));
                }
            }
            return out;
        }

    private:
        using stream = code::stream;

        const std::vector<language::file>& files_;
        std::string project_dir_;
        std::string out_dir_;
        std::string name_ = "app";
        std::vector<held_roles> held_;
        std::string screen_title_;
        int in_block_ = 0;  // how deep in heroes and sections the items being written are  // the screen being written's title, as written, like "#{issue_page.number} {issue_page.title}"  // the roles held within something, and what grants each command
        std::string title_;  // the name shown at the top of every page, when it isn't the project's
        std::string ui_ = "radix";
        std::vector<std::string> authentication_;  // the ways people sign in, as the project names them: google, github, microsoft
        bool analytics_ = false;  // whether visitors are counted, with Firebase Analytics, once they agree
        bool has_project_ = false;  // a project block, which says whether people sign in at all
        std::map<std::string, std::map<std::string, const language::entity_declaration*>> entities_;
        std::map<std::string, std::map<std::string, const language::view_declaration*>> views_;
        std::map<std::string, std::set<std::string>> commands_;  // namespace to entity::command
        std::set<std::string> open_;  // commands anyone may run, signed in or not, in full
        std::vector<std::string> personal_;  // views with one document per person
        // A set of markdown pages, one per file, shown by a screen like /docs/:page.
        struct page_set {
            std::string pattern;  // the files, like docs/*.md
            std::string name;     // the generated module's name, from the address: docs
            code::source source;  // the item that names them
        };
        std::vector<page_set> pages_;
        std::string docs_base_;  // the address the screen being written keeps its pages under, like /docs

        // Where generated lines come from. Files every app has, like package.json, come
        // from the project block; a screen's lines come from the screen and its items.
        std::string indexing_;           // the file being indexed
        code::source project_;           // the project block, or fixed without one
        std::string icon_;               // the app's icon, an .svg file, when the project names one
        code::source icon_source_;       // the setting that names it
        std::string served_;             // a folder whose files are served as they are, when the project names one
        code::source served_source_;     // the setting that names it
        std::string screen_path_;        // the file whose screens are being written
        std::string route_;              // the address of the screen being written, like /projects/:project
        int item_line_ = 0;              // the screen item being written
        std::map<std::string, std::string> components_;    // hand-written components, by name, to their file
        std::map<std::string, std::string> dependencies_;  // npm packages they need, from components/package.json

        struct found_screen {
            std::string ns;
            const language::screen_declaration* screen;
            language::location where;
        };

        struct screen_import {
            std::string name, module;
            code::source from;
        };

        void index(const std::string& ns, const std::vector<language::declaration>& declarations) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    if (n->at) prefixes_[web_detail::join(ns, n->name)] = *n->at == "/" ? "" : *n->at;
                    index(web_detail::join(ns, n->name), n->declarations);
                } else if (auto* p = std::get_if<language::project_declaration>(&d.node)) {
                    name_ = p->name;
                    has_project_ = true;
                    project_ = {indexing_, d.where.line};
                    for (const auto& s : p->settings) {
                        if (s.key == "ui") ui_ = s.value;
                        if (s.key == "title") title_ = s.value;
                        if (s.key == "authentication") authentication_.push_back(s.value);
                        if (s.key == "analytics") analytics_ = s.value == "google";
                        if (s.key == "serve") {
                            std::string dir = std::filesystem::path(indexing_).parent_path().string();
                            served_ = platform::resolve(dir.empty() ? "." : dir, s.value);
                            served_source_ = {indexing_, s.where.line};
                        }
                        if (s.key == "icon") {
                            std::string dir = std::filesystem::path(indexing_).parent_path().string();
                            icon_ = platform::resolve(dir.empty() ? "." : dir, s.value);
                            icon_source_ = {indexing_, s.where.line};
                        }
                    }
                } else if (auto* e = std::get_if<language::entity_declaration>(&d.node)) {
                    entities_[ns][e->name] = e;
                } else if (auto* v = std::get_if<language::view_declaration>(&d.node)) {
                    views_[ns][v->name] = v;
                    if (v->per == "user") personal_.push_back(web_detail::join(ns, v->name));
                } else if (auto* c = std::get_if<language::command_declaration>(&d.node)) {
                    commands_[ns].insert(c->name.text());
                    for (const auto& s : c->body) {
                        auto* p = std::get_if<language::permission_statement>(&s.node);
                        if (p && p->permission.text() == "anyone") open_.insert(web_detail::join(ns, c->name.text()));
                    }
                }
            }
        }

        void find_screens(const std::string& ns, const std::vector<language::declaration>& declarations, std::vector<found_screen>& found) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    find_screens(web_detail::join(ns, n->name), n->declarations, found);
                } else if (auto* s = std::get_if<language::screen_declaration>(&d.node)) {
                    found.push_back({ns, s, d.where});
                }
            }
        }

        // What a name written inside a namespace refers to, in full. A command written
        // as entity::action, or a view written by its name alone, is in the same
        // namespace; one written with its namespace stays as written.
        static std::string full_command(const std::string& ns, const language::qualified_name& name) {
            return name.parts.size() == 2 ? web_detail::join(ns, name.text()) : name.text();
        }
        // The view per entity that holds every field an update form asks for.
        std::optional<std::string> edit_view(const std::string& ns, const language::entity_declaration& entity,
                                             const language::form_item& form) const {
            auto scope = views_.find(ns);
            if (scope == views_.end()) return std::nullopt;
            for (const auto& [name, view] : scope->second) {
                if (!view->per || *view->per != entity.name) continue;
                bool holds = std::all_of(form.fields.begin(), form.fields.end(), [&](const language::form_field& f) {
                    return std::any_of(view->values.begin(), view->values.end(),
                                       [&](const language::view_value& v) { return v.name && *v.name == f.name; });
                });
                if (holds) return web_detail::join(ns, name);
            }
            return std::nullopt;
        }

        // The entity a view has one document per, like book, if it's a view per entity.
        std::optional<std::string> per_entity(const std::string& full) const {
            std::size_t cut = full.rfind("::");
            std::string ns = cut == std::string::npos ? "" : full.substr(0, cut);
            std::string name = cut == std::string::npos ? full : full.substr(cut + 2);
            auto scope = views_.find(ns);
            if (scope == views_.end()) return std::nullopt;
            auto view = scope->second.find(name);
            if (view == scope->second.end() || !view->second->per || *view->second->per == "user") return std::nullopt;
            return *view->second->per;
        }

        static std::string full_view(const std::string& ns, const std::string& name) {
            return name.find("::") == std::string::npos ? web_detail::join(ns, name) : name;
        }

        // Where a namespace's screens are: under its name, like /docs, unless it says
        // otherwise with at, like namespace studio at /.
        std::map<std::string, std::string> prefixes_;

        std::string prefix_of(const std::string& ns) const {
            if (ns.empty()) return "";
            if (auto it = prefixes_.find(ns); it != prefixes_.end()) return it->second;
            std::size_t cut = ns.rfind("::");
            std::string outer = cut == std::string::npos ? "" : ns.substr(0, cut);
            return prefix_of(outer) + "/" + ns.substr(cut == std::string::npos ? 0 : cut + 2);
        }

        std::string full_route(const std::string& ns, const std::string& route) const {
            std::string prefix = prefix_of(ns);
            if (prefix.empty()) return route;
            return route == "/" ? prefix : prefix + route;
        }

        bool entity_named(const std::string& ns, const std::string& name) const {
            auto scope = entities_.find(ns);
            return scope != entities_.end() && scope->second.contains(name);
        }

        // Whether a route has a parameter: /projects/:project has project.
        // The key's parts before the last, when an address names an entity by them:
        // owner, in /:owner/:project, for a project keyed owner and slug. Empty when
        // the address names it by its whole id, or it has a key of one part.
        std::vector<std::string> keyed_by(const std::string& ns, const std::string& entity, const std::string& route) const {
            auto scope = entities_.find(ns);
            if (scope == entities_.end() || !scope->second.contains(entity)) return {};
            std::vector<std::string> keys;
            for (const auto& f : scope->second.at(entity)->fields) {
                if (f.key) keys.push_back(f.name);
            }
            if (keys.size() < 2) return {};
            keys.pop_back();
            for (const auto& key : keys) {
                if (!names_parameter(route, key)) return {};
            }
            return keys;
        }

        static bool names_parameter(const std::string& route, const std::string& name) {
            std::string wanted = "/:" + name;
            for (std::size_t at = route.find(wanted); at != std::string::npos; at = route.find(wanted, at + 1)) {
                std::size_t end = at + wanted.size();
                if (end == route.size() || route[end] == '/') return true;
            }
            return false;
        }

        const language::entity_declaration* entity_of_command(const std::string& full) const {
            auto cut = full.rfind("::");
            if (cut == std::string::npos) return nullptr;
            auto entity_end = cut;
            auto entity_start = full.rfind("::", entity_end - 1);
            std::string ns = entity_start == std::string::npos ? "" : full.substr(0, entity_start);
            std::string entity = full.substr(entity_start == std::string::npos ? 0 : entity_start + 2,
                                             entity_end - (entity_start == std::string::npos ? 0 : entity_start + 2));
            auto scope = entities_.find(ns);
            if (scope == entities_.end()) return nullptr;
            auto e = scope->second.find(entity);
            return e == scope->second.end() ? nullptr : e->second;
        }

        // The entity a view lists, and the commands on it, so a table column naming one
        // of those commands becomes an action on every row.
        // The entity a view's list is of: book, for the shelf's each book.
        const language::entity_declaration* listed(const std::string& ns, const std::string& view, const std::optional<std::string>& list) const {
            auto scope = views_.find(ns);
            if (scope == views_.end()) return nullptr;
            auto v = scope->second.find(view);
            if (v == scope->second.end()) return nullptr;
            auto each = std::find_if(v->second->each.begin(), v->second->each.end(), [&](const auto& e) { return e.name == list; });
            if (each == v->second->each.end() || each->changes) return nullptr;  // a change has no commands, and no people of its own
            auto* source = std::get_if<language::name_expression>(&each->source->node);
            if (!source || source->name.parts.size() != 1) return nullptr;
            auto entities = entities_.find(ns);
            if (entities == entities_.end()) return nullptr;
            auto e = entities->second.find(source->name.parts[0]);
            return e == entities->second.end() ? nullptr : e->second;
        }

        std::optional<std::string> row_command(const std::string& ns, const std::string& view, const std::optional<std::string>& list,
                                               const std::string& column) const {
            const language::entity_declaration* entity = listed(ns, view, list);
            if (!entity) return std::nullopt;
            std::string command = entity->name + "::" + column;
            auto commands = commands_.find(ns);
            if (commands == commands_.end() || !commands->second.contains(command)) return std::nullopt;
            return web_detail::join(ns, command);
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
        };

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
                screen_title_ = s->title_is_name ? "" : s->title;
                docs_base_ = route.substr(0, route.find("/:"));  // where its pages live, if it has any
                stream items;
                auto from_items = items.from(f.path, where.line);
                items.open("<>");
                screen_items(items, parts, ns, s->items, s->items);
                items.close("</>");

                std::string title = s->title_is_name ? web_detail::label(s->title) : s->title;
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
                if (!ns.empty() && !needs_parameter) info += ", nav: " + web_detail::js_string(title);
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
                std::string name = web_detail::js_name(found.size() == 1 ? stem : titling.empty() ? s->title : word_of_route);
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
                    if (!parts.views.empty()) components.insert("useView");
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
        std::string title_parts(screen_parts& parts, const std::string& ns, std::string_view text) {
            std::string out;
            auto add = [&](const std::string& part) { out += (out.empty() ? "" : ", ") + part; };
            std::size_t at = 0;
            while (at < text.size()) {
                std::size_t open = text.find('{', at);
                std::size_t close = open == std::string_view::npos ? open : text.find('}', open);
                if (close == std::string_view::npos) {
                    add(web_detail::js_string(text.substr(at)));
                    break;
                }
                if (open > at) add(web_detail::js_string(text.substr(at, open - at)));
                std::string_view value = text.substr(open + 1, close - open - 1);
                std::size_t dot = value.rfind('.');
                if (dot == std::string_view::npos) {
                    add(web_detail::js_string(text.substr(open, close - open + 1)));
                } else {
                    std::string view = full_view(ns, std::string(value.substr(0, dot)));
                    add("[" + view_variable(parts, view) + ", " + web_detail::js_string(value.substr(dot + 1)) + "]");
                }
                at = close + 1;
            }
            return out;
        }

        // Text with {view.field} in it: the plain parts as they are, and each value as
        // a <Live> reading that view.
        std::string live_text(screen_parts& parts, const std::string& ns, std::string_view text) {
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
                std::size_t dot = value.rfind('.');
                if (dot == std::string_view::npos) {
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
            for (const auto& item : items) {
                if (auto* form = std::get_if<language::form_item>(&item.node)) {
                    for (const auto& c : form->commands) {
                        if (c.text() == command.text()) return true;
                    }
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    if (has_form_for(block->items, command)) return true;
                }
            }
            return false;
        }

        static const language::button_item* button_for(const std::vector<language::screen_item>& items, const language::qualified_name& command) {
            for (const auto& item : items) {
                if (auto* button = std::get_if<language::button_item>(&item.node)) {
                    if (button->command.text() == command.text()) return button;
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    if (auto* found = button_for(block->items, command)) return found;
                }
            }
            return nullptr;
        }

        // Whether the person reading may run a command a role grants, on a page whose
        // address names where the role is held, like the project in
        // /:project/issues/:issue: one of the roles granting it, held there. Nothing,
        // when no role grants it or the page doesn't say where.
        std::string allowed(screen_parts& parts, const std::string& ns, const std::string& command) {
            for (const auto& held : held_) {
                auto granting = held.granting.find(command);
                if (granting == held.granting.end()) continue;
                if (!names_parameter(route_, held.within) || !entity_named(ns, held.within)) continue;
                parts.params.insert(held.within);
                parts.components.insert("holds");
                std::string roles;
                for (const auto& role : granting->second) roles += (roles.empty() ? "" : ", ") + web_detail::js_string(role);
                return " allowed={holds(" + view_variable(parts, held.view) + ", " + web_detail::js_string(held.field) + ", " +
                       web_detail::js_name(held.within + "_id") + ", [" + roles + "])}";
            }
            return "";
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

        std::string condition_of(screen_parts& parts, const std::string& ns, const language::expression& e, std::set<std::string>& read) {
            using language::token_kind;
            if (auto* binary = std::get_if<language::binary_expression>(&e.node)) {
                // A list has a value, like assignees has me.
                if (binary->op == token_kind::has) {
                    parts.components.insert("listHas");
                    return "listHas(" + condition_of(parts, ns, *binary->left, read) + ", " + condition_of(parts, ns, *binary->right, read) + ")";
                }
                std::string op = binary->op == token_kind::equal ? "===" : binary->op == token_kind::not_equal ? "!=="
                               : binary->op == token_kind::logical_and ? "&&" : binary->op == token_kind::logical_or ? "||"
                               : binary->op == token_kind::less ? "<" : binary->op == token_kind::greater ? ">"
                               : binary->op == token_kind::less_equal ? "<=" : ">=";
                return "(" + condition_of(parts, ns, *binary->left, read) + " " + op + " " + condition_of(parts, ns, *binary->right, read) + ")";
            }
            if (auto* unary = std::get_if<language::unary_expression>(&e.node)) return "!" + condition_of(parts, ns, *unary->operand, read);
            if (auto* member = std::get_if<language::member_expression>(&e.node)) {
                auto* object = std::get_if<language::name_expression>(&member->object->node);
                std::string variable = view_variable(parts, full_view(ns, object->name.text()));
                read.insert(variable);
                return "(" + variable + ".data?.[" + web_detail::js_string(member->member) + "] ?? null)";
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

        void screen_items(stream& out, screen_parts& parts, const std::string& ns,
                          const std::vector<language::screen_item>& items, const std::vector<language::screen_item>& screen,
                          std::size_t first = 0) {
            // Buttons one after another sit in a row: a command's own, and a form's that
            // opens it, and so do links. A command's line whose form draws its button is
            // passed over.
            auto presses = [&](const language::screen_item& it) {
                if (std::holds_alternative<language::content_link>(it.node)) return in_block_ == 0;  // a hero or a section lays out its own
                if (auto* b = std::get_if<language::button_item>(&it.node)) return !has_form_for(screen, b->command);
                if (auto* f = std::get_if<language::form_item>(&it.node)) return button_for(screen, f->commands.front()) != nullptr;
                return std::holds_alternative<language::copy_item>(it.node);
            };
            auto passed = [&](const language::screen_item& it) {
                auto* b = std::get_if<language::button_item>(&it.node);
                return b && has_form_for(screen, b->command);
            };
            bool in_row = false;
            for (std::size_t at = first; at < items.size(); ++at) {
                const auto& item = items[at];
                if (in_row && !presses(item) && !passed(item)) {
                    out.close("</Actions>");
                    in_row = false;
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
                    parts.components.insert("Menu");
                    std::string links;
                    for (const auto& inside : menu->items) {
                        auto* link = std::get_if<language::content_link>(&inside.node);
                        if (!link) continue;
                        std::string target = link->target.starts_with("/") ? full_route(ns, link->target) : link->target;
                        links += (links.empty() ? "" : ", ") + std::string("{ to: ") + web_detail::js_string(target) + ", label: " +
                                 web_detail::js_string(link->label) + " }";
                    }
                    out.open("<Menu links={[" + links + "]}>");
                    screen_items(out, parts, ns, items, screen, at + 1);
                    out.close("</Menu>");
                    return;
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    bool hero = block->type == language::content_block::kind::hero;
                    std::string tag = hero ? "Hero" : "Section";
                    parts.components.insert(tag);
                    std::string id = block->anchor ? " id=" + web_detail::js_string(*block->anchor) : "";
                    out.open("<" + tag + " title=" + web_detail::js_string(block->title) + id + ">");
                    ++in_block_;
                    screen_items(out, parts, ns, block->items, screen);
                    --in_block_;
                    out.close("</" + tag + ">");
                } else if (auto* text = std::get_if<language::content_text>(&item.node)) {
                    content(out, parts, ns, *text);
                } else if (auto* link = std::get_if<language::content_link>(&item.node)) {
                    parts.components.insert("Link");
                    std::string target = link->namespace_name ? full_route(link->namespace_name->text(), "/")
                                         : link->target.starts_with("/") ? full_route(ns, link->target)
                                                                                         : link->target;
                    out.line("<Link to=" + web_detail::js_string(target) + ">" + web_detail::jsx_text(link->label) + "</Link>");
                } else if (auto* copy = std::get_if<language::copy_item>(&item.node)) {
                    this->copy(out, parts, ns, *copy);
                } else if (auto* thread = std::get_if<language::thread_item>(&item.node)) {
                    parts.components.insert("Thread");
                    out.line("<Thread view={" + view_variable(parts, full_view(ns, thread->view.text())) + "} list=" + web_detail::js_string(thread->list) + " />");
                } else if (auto* timeline = std::get_if<language::timeline_item>(&item.node)) {
                    parts.components.insert("Timeline");
                    out.line("<Timeline view={" + view_variable(parts, full_view(ns, timeline->view.text())) + "} list=" + web_detail::js_string(timeline->list) + " />");
                } else if (auto* table = std::get_if<language::table_item>(&item.node)) {
                    this->table(out, parts, ns, *table);
                } else if (auto* form = std::get_if<language::form_item>(&item.node)) {
                    this->form(out, parts, ns, *form, button_for(screen, form->commands.front()));
                } else if (auto* confirm = std::get_if<language::confirm_item>(&item.node)) {
                    parts.components.insert("Confirm");
                    out.line("<Confirm command=" + web_detail::js_string(full_command(ns, confirm->command)) +
                             " question=" + web_detail::js_string(confirm->message) + " />");
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
                    out.line("<Command name=" + web_detail::js_string(command) + id + label + when + allowed(parts, ns, command) + " />");
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

        void content(stream& out, screen_parts& parts, const std::string& ns, const language::content_text& text) {
            if (text.type == language::content_text::kind::text) {
                parts.components.insert("Text");
                out.line("<Text>" + live_text(parts, ns, text.value) + "</Text>");
            } else if (text.type == language::content_text::kind::code) {
                namespace fs = std::filesystem;
                std::string source = platform::resolve(project_dir_, text.value);
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
                    std::size_t dot = inside.rfind('.');
                    std::string view = full_view(ns, inside.substr(0, dot));
                    parts.components.insert("Markdown");
                    out.line("<Markdown view={" + view_variable(parts, view) + "} field=" + web_detail::js_string(inside.substr(dot + 1)) + " />");
                    return;
                }
                // Named for where the pages live: /docs is docs, /guides/api is guides-api.
                std::string name = docs_base_.empty() ? "pages" : docs_base_.substr(1);
                std::replace(name.begin(), name.end(), '/', '-');
                pages_.push_back({text.value, name, {screen_path_, item_line_}});
                parts.optional_page = true;
                parts.imports.push_back("import pages from \"../pages/" + name + ".generated\";");
                parts.components.insert("Pages");
                out.line("<Pages base=" + web_detail::js_string(docs_base_) + " pages={pages} />");
            }
        }

        // A button copying everything a view holds as Markdown, in the order the view
        // says it: each value but the ones the page's title shows, a markdown one as it
        // was written, then each list, as a conversation, a timeline of changes, or
        // rows. The ids of people, which mean nothing pasted elsewhere, are left out.
        void copy(stream& out, screen_parts& parts, const std::string& ns, const language::copy_item& copy) {
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
            std::string fields, lists;
            if (view) {
                const language::entity_declaration* entity = nullptr;
                if (view->per) {
                    if (auto scope = entities_.find(view_ns); scope != entities_.end()) {
                        if (auto found = scope->second.find(*view->per); found != scope->second.end()) entity = found->second;
                    }
                }
                for (const auto& value : view->values) {
                    std::string key = value.name ? *value.name : web_detail::text_of(*value.value);
                    if (screen_title_.find("{" + name + "." + key + "}") != std::string::npos) continue;
                    std::string kind;
                    auto* member = std::get_if<language::member_expression>(&value.value->node);
                    if (member && entity) {
                        for (const auto& f : entity->fields) {
                            if (f.name != member->member || !f.type) continue;
                            if (f.type->text() == "markdown") kind = "markdown";
                            if (f.list && f.type->text() == "user") kind = "people";
                        }
                    }
                    if (kind == "people") continue;
                    fields += (fields.empty() ? "" : ", ") + std::string("[") + web_detail::js_string(key) + ", " + web_detail::js_string(web_detail::label(key)) +
                              (kind.empty() ? "" : ", " + web_detail::js_string(kind)) + "]";
                }
                for (const auto& each : view->each) {
                    if (!each.name) continue;
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
            out.line("<Copy view={" + variable + "}" + label + " fields={[" + fields + "]} lists={[" + lists + "]} />");
        }

        void table(stream& out, screen_parts& parts, const std::string& ns, const language::table_item& table) {
            parts.components.insert("Table");
            std::string view = full_view(ns, table.view.text());
            std::string columns;
            std::string actions;
            std::string pictures;
            std::string labels;
            std::string shown;  // a choice column's values, as they're shown
            const language::entity_declaration* entity = listed(ns, table.view.text(), table.list);
            for (const auto& column : table.columns) {
                std::string key = web_detail::text_of(*column.value);
                // member.picture, where member is a person: shown as their picture.
                if (entity && key.ends_with(".picture") && key.find('.') == key.rfind('.')) {
                    std::string person = key.substr(0, key.find('.'));
                    for (const auto& f : entity->fields) {
                        if (f.name == person && f.type && f.type->text() == "user") {
                            pictures += (pictures.empty() ? "" : ", ") + web_detail::js_string(key);
                        }
                    }
                }
                if (!column.label && key.find('.') == std::string::npos) {
                    if (auto command = row_command(ns, table.view.text(), table.list, key)) {
                        actions += (actions.empty() ? "" : ", ") + web_detail::js_string(*command);
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
                std::string name = key.substr(key.rfind('.') == std::string::npos ? 0 : key.rfind('.') + 1);
                std::string label = column.label ? *column.label : web_detail::label(name);
                columns += (columns.empty() ? "" : ", ") + (web_detail::is_identifier(key) ? key : web_detail::js_string(key)) + ": " + web_detail::js_string(label);
            }
            std::string line = "<Table view={" + view_variable(parts, view) + "}";
            if (table.list) line += " list=" + web_detail::js_string(*table.list);
            if (table.link) {
                std::string target = full_route(ns, *table.link);
                line += " link=" + web_detail::js_string(target);
                // What it opens is named by its key's parts, like /:owner/:project.
                std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
                auto keyed = keyed_by(ns, last, target);
                if (!keyed.empty()) {
                    std::string names;
                    for (const auto& key : keyed) names += (names.empty() ? "" : ", ") + web_detail::js_string(key);
                    line += " keyed={[" + names + "]}";
                }
            }
            line += " columns={{ " + columns + " }}";
            if (!actions.empty()) line += " actions={[" + actions + "]}";
            if (!pictures.empty()) line += " pictures={[" + pictures + "]}";
            if (!shown.empty()) line += " choices={{ " + shown + " }}";
            if (!labels.empty()) line += " labels={[" + labels + "]}";
            if (table.by) line += " by=" + web_detail::js_string(*table.by);
            out.line(line + " />");
        }

        // A choice field's choices, each with how it's shown: [["mit", "MIT"], ...].
        static std::string choice_options(const language::field& f) {
            std::string options;
            for (std::size_t i = 0; i < f.choices.size(); ++i) {
                std::string shown = i < f.choice_labels.size() && !f.choice_labels[i].empty() ? f.choice_labels[i] : web_detail::label(f.choices[i]);
                options += (options.empty() ? "" : ", ") + std::string("[") + web_detail::js_string(f.choices[i]) + ", " + web_detail::js_string(shown) + "]";
            }
            return "[" + options + "]";
        }

        void form(stream& out, screen_parts& parts, const std::string& ns, const language::form_item& form, const language::button_item* button) {
            parts.components.insert("Form");
            std::string command = full_command(ns, form.commands.front());
            const language::entity_declaration* entity = entity_of_command(command);
            std::string fields;
            for (const auto& f : form.fields) {
                std::string type = "text";
                std::string choices;
                if (entity) {
                    for (const auto& field : entity->fields) {
                        if (field.name == f.name && field.type && field.type->parts.size() == 1) {
                            const auto& t = field.type->parts[0];
                            if (t == "email" || t == "date" || t == "number" || t == "markdown" || t == "boolean") type = t;
                            if (field.list) type = "list";
                        }
                        // A choice is picked from its choices, not typed.
                        if (field.name == f.name && !field.choices.empty()) {
                            type = "choice";
                            choices = ", choices: " + choice_options(field);
                            // A new one starts on the field's own starting choice.
                            if (auto* start = field.initial ? std::get_if<language::name_expression>(&field.initial->node) : nullptr) {
                                choices += ", start: " + web_detail::js_string(start->name.parts.back());
                            }
                        }
                    }
                }
                std::string spec = type == "text" && !f.hint && !f.label
                    ? web_detail::js_string(f.name)
                    : "{ name: " + web_detail::js_string(f.name) + (f.label ? ", label: " + web_detail::js_string(*f.label) : "") +
                          (type == "text" ? "" : ", type: " + web_detail::js_string(type)) + choices +
                          (f.hint ? ", hint: " + web_detail::js_string(*f.hint) : "") + " }";
                fields += (fields.empty() ? "" : ", ") + spec;
            }
            // An update starts from the entity's page view and acts on the entity the
            // address names (the checker makes sure both are there).
            std::string edit;
            if (entity && command.substr(command.rfind("::") + 2) != "create") {
                if (auto view = edit_view(ns, *entity, form)) {
                    edit = " from={" + view_variable(parts, *view) + "} id={" + web_detail::js_name(entity->name + "_id") + "}";
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
            for (const auto& [package, wanted] : dependencies_) dependencies.emplace(package, wanted);
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
            out.open("<html lang=\"en\">");
            out.open("<head>");
            out.line("<meta charset=\"utf-8\" />");
            out.line("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />");
            out.line("<title>" + web_detail::html_escape(title_.empty() ? name_ : title_) + "</title>");
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
            out.close("});");
            return out;
        }

        stream main_tsx() const {
            stream out;
            auto from = out.from(project_.path, project_.line);
            out.generated_from(source_name());
            out.line("import { createRoot } from \"react-dom/client\";");
            out.line("import \"@uione/" + ui_ + "/styles.css\";");
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
                out.line("export const site = { name: " + web_detail::js_string(title_.empty() ? name_ : title_) + icon + ", screens: [" + names + "], ui: " + ui_ +
                         ", data" + offered + (analytics_ ? ", analytics" : "") + " };");
            }
            out.line();
            out.open("export default function Site() {");
            out.line("return <App {...site} />;");
            out.close("}");
            return out;
        }

        stream pages_module(const page_set& pages) const {
            namespace fs = std::filesystem;
            fs::path glob = platform::resolve(project_dir_, pages.pattern);
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

    // The web app for a project whose files have parsed and checked cleanly.
    inline std::vector<output_file> generate_web(const std::vector<language::file>& files, const std::string& project_dir,
                                                 const std::string& out_dir) {
        return web_generator(files, project_dir, out_dir).generate();
    }

} // namespace one::generators
