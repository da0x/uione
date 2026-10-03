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
            : files_(files), project_dir_(std::move(project_dir)), out_dir_(std::move(out_dir)) {
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
        std::string ui_ = "radix";
        std::string signin_;  // how people sign in, when the project says: google or github
        bool has_project_ = false;  // a project block, which says whether people sign in at all
        std::map<std::string, std::map<std::string, const language::entity_declaration*>> entities_;
        std::map<std::string, std::map<std::string, const language::view_declaration*>> views_;
        std::map<std::string, std::set<std::string>> commands_;  // namespace to entity::command
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
                        if (s.key == "signin") signin_ = s.value;
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
            return out;
        }

        // one screen file

        struct screen_parts {
            std::set<std::string> components;
            std::set<std::string> params;  // route parameters items read, like project
            std::vector<std::pair<std::string, std::string>> views;  // variable, full view name
            std::vector<std::string> imports;                        // extra import lines
            bool optional_page = false;
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
                docs_base_ = route.substr(0, route.find("/:"));  // where its pages live, if it has any
                stream items;
                auto from_items = items.from(f.path, where.line);
                items.open("<>");
                screen_items(items, parts, ns, s->items, s->items);
                items.close("</>");

                std::string title = s->title_is_name ? web_detail::label(s->title) : s->title;
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
                // with several, each is named after its title.
                std::string name = web_detail::js_name(found.size() == 1 ? stem : s->title);
                screens.push_back({name, "./screens/" + stem, {f.path, where.line}});
                components.insert(parts.components.begin(), parts.components.end());
                for (const auto& line : parts.imports) {
                    if (std::find(imports.begin(), imports.end(), line) == imports.end()) imports.push_back(line);
                }

                if (!bodies.str().empty()) bodies.line();
                for (const auto& [variable, view] : parts.views) {
                    if (auto per = per_entity(view)) parts.params.insert(*per);
                }
                if (parts.views.empty() && parts.params.empty()) {
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

        static bool has_button_for(const std::vector<language::screen_item>& items, const language::qualified_name& command) {
            for (const auto& item : items) {
                if (auto* button = std::get_if<language::button_item>(&item.node)) {
                    if (button->command.text() == command.text()) return true;
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    if (has_button_for(block->items, command)) return true;
                }
            }
            return false;
        }

        void screen_items(stream& out, screen_parts& parts, const std::string& ns,
                          const std::vector<language::screen_item>& items, const std::vector<language::screen_item>& screen) {
            for (const auto& item : items) {
                auto from_item = out.from(screen_path_, item.where.line);
                item_line_ = item.where.line;
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    bool hero = block->type == language::content_block::kind::hero;
                    std::string tag = hero ? "Hero" : "Section";
                    parts.components.insert(tag);
                    std::string id = block->anchor ? " id=" + web_detail::js_string(*block->anchor) : "";
                    out.open("<" + tag + " title=" + web_detail::js_string(block->title) + id + ">");
                    screen_items(out, parts, ns, block->items, screen);
                    out.close("</" + tag + ">");
                } else if (auto* text = std::get_if<language::content_text>(&item.node)) {
                    content(out, parts, ns, *text);
                } else if (auto* link = std::get_if<language::content_link>(&item.node)) {
                    parts.components.insert("Link");
                    std::string target = link->namespace_name ? full_route(link->namespace_name->text(), "/")
                                         : link->target.starts_with("/") ? full_route(ns, link->target)
                                                                                         : link->target;
                    out.line("<Link to=" + web_detail::js_string(target) + ">" + web_detail::jsx_text(link->label) + "</Link>");
                } else if (auto* table = std::get_if<language::table_item>(&item.node)) {
                    this->table(out, parts, ns, *table);
                } else if (auto* form = std::get_if<language::form_item>(&item.node)) {
                    this->form(out, parts, ns, *form, has_button_for(screen, form->commands.front()));
                } else if (auto* confirm = std::get_if<language::confirm_item>(&item.node)) {
                    parts.components.insert("Confirm");
                    out.line("<Confirm command=" + web_detail::js_string(full_command(ns, confirm->command)) +
                             " question=" + web_detail::js_string(confirm->message) + " />");
                } else if (auto* button = std::get_if<language::button_item>(&item.node)) {
                    if (has_form_for(screen, button->command)) continue;  // the form draws its own button
                    parts.components.insert("Command");
                    out.line("<Command name=" + web_detail::js_string(full_command(ns, button->command)) + " />");
                } else if (auto* component = std::get_if<language::component_item>(&item.node)) {
                    std::string tag = component_tag(component->name);
                    std::string line = "import " + tag + " from \"../components/" + component->name + "\";";
                    if (std::find(parts.imports.begin(), parts.imports.end(), line) == parts.imports.end()) parts.imports.push_back(line);
                    use_component(component->name);
                    out.line("<" + tag + " />");
                }
            }
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

        void table(stream& out, screen_parts& parts, const std::string& ns, const language::table_item& table) {
            parts.components.insert("Table");
            std::string view = full_view(ns, table.view.text());
            std::string columns;
            std::string actions;
            std::string pictures;
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

        void form(stream& out, screen_parts& parts, const std::string& ns, const language::form_item& form, bool button) {
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
                            if (t == "email" || t == "date" || t == "number" || t == "markdown") type = t;
                            if (field.list) type = "list";
                        }
                        // A choice is picked from its choices, not typed.
                        if (field.name == f.name && !field.choices.empty()) {
                            type = "choice";
                            choices = ", choices: " + choice_options(field);
                            // A new one starts on the field's own starting choice.
                            if (auto* start = field.initial ? std::get_if<language::name_expression>(&field.initial->node) : nullptr) {
                                choices += ", start: " + web_detail::js_string(start->name.text());
                            }
                        }
                    }
                }
                std::string spec = type == "text" && !f.hint
                    ? web_detail::js_string(f.name)
                    : "{ name: " + web_detail::js_string(f.name) + (type == "text" ? "" : ", type: " + web_detail::js_string(type)) + choices +
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
            out.line("<Form command=" + web_detail::js_string(command) + " fields={[" + fields + "]}" + edit + given + (button ? " button" : "") + " />");
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
            out.line("<title>" + name_ + "</title>");
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
            out.line("import { firebaseSource } from \"@uione/react/firebase\";");
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
            out.line("config: { projectId: \"demo-uione\", apiKey: \"demo\" },");
            out.line("emulators: { firestore: \"localhost:8080\", auth: \"localhost:9099\" },");
            out.close("};");
            out.open("const cloud = {");
            out.open("config: {");
            out.line("projectId: import.meta.env.VITE_FIREBASE_PROJECT_ID,");
            out.line("apiKey: import.meta.env.VITE_FIREBASE_API_KEY,");
            out.line("appId: import.meta.env.VITE_FIREBASE_APP_ID,");
            out.line("authDomain: import.meta.env.VITE_FIREBASE_AUTH_DOMAIN,");
            out.close("},");
            out.close("};");
            std::string personal;
            if (!personal_.empty()) {
                std::string list;
                for (const auto& v : personal_) list += (list.empty() ? "" : ", ") + web_detail::js_string(v);
                personal = ", personal: [" + list + "]";
            }
            // Google is the default, so only another way of signing in is written down.
            std::string signin = signin_ == "github" ? ", signin: \"github\" as const" : "";
            out.line("const data = firebaseSource({ ...(import.meta.env.DEV ? local : cloud)" + personal + signin + " });");
            out.line();
            std::string icon;
            if (!icon_.empty()) icon = ", icon: \"/icon.svg\"";
            {
                auto from_icon = icon_.empty() ? out.from(project_.path, project_.line) : out.from(icon_source_.path, icon_source_.line);
                // A project that names no way of signing in offers none.
                std::string offered = has_project_ && signin_.empty() ? ", signin: false" : "";
                out.line("export const site = { name: " + web_detail::js_string(name_) + icon + ", screens: [" + names + "], ui: " + ui_ +
                         ", data" + offered + " };");
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
