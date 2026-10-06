// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The checker looks at what a project's .one files mean, after they've parsed. A file
// can be well-formed and still wrong: a name in camelCase, a command on an entity
// nobody declared, a table showing a view that doesn't exist.
//
// It reads a whole project at once, because one file can use what another declares.
// It goes through the files twice: first to collect every declaration by namespace,
// then to check each one against that collection.
//
// Names inside expressions are checked for snake_case, but not yet looked up. That
// needs a model of scopes and types, which will come with the generators.

#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"
#include "language/names.hpp"
#include "platform/files.hpp"

namespace one::language {

    // What a name written in a file means: what it names, said in words, and where
    // that's declared, or the reference's section for one of the language's own.
    // An editor shows it on hover, and goes to it.
    struct meaning {
        std::string path;        // the file the name is written in
        location where;          // where it starts
        std::size_t length = 0;
        std::string says;        // field project of report, a project
        std::string to_path;     // the file it's declared in; empty for the language's own
        location to;
        std::string section;     // the reference's section, like built-in-values
    };
    using meanings = std::vector<meaning>;

    class checker {
    public:
        explicit checker(diagnostics& out, meanings* meant = nullptr) : out_(out), meant_(meant) {}

        void check(const std::vector<file>& files) {
            for (const auto& f : files) {
                path_ = f.path;
                collect("", f.declarations);
            }
            for (const auto& f : files) {
                path_ = f.path;
                verify("", f.declarations);
            }
        }

    private:
        // Where something was declared, for "declared twice" messages.
        struct origin {
            std::string path;
            location where;
        };

        template <typename T>
        struct declared {
            const T* node = nullptr;
            origin from;
        };

        struct scope {
            std::map<std::string, declared<entity_declaration>> entities;
            std::map<std::string, declared<view_declaration>> views;
            std::map<std::string, declared<format_declaration>> formats;
            std::map<std::string, declared<function_declaration>> functions;
            std::map<std::string, origin> commands;  // entity::command
            std::vector<const role_declaration*> roles;
            std::map<std::string, origin> role_names;  // so a role is declared once
        };

        // What a name can mean where it's written.
        struct context {
            std::string ns;
            const entity_declaration* entity = nullptr;  // whose fields are named plainly
            const entity_declaration* row = nullptr;     // the row a value is for, as in book.id
            std::vector<std::string> parameters;         // a function's
            bool reader = false;                         // user.id, the person reading a view
            const entity_declaration* subject = nullptr; // the entity a view per entity is for
            bool hook = false;                           // a webhook's handler, which may create
        };

        diagnostics& out_;
        meanings* meant_ = nullptr;
        std::string path_;
        std::map<std::string, scope> scopes_;  // by namespace, "" is the top level
        std::map<std::string, origin> routes_;
        std::map<const entity_declaration*, entity_declaration> changes_;  // what each history holds
        std::optional<origin> project_;

        void error(location where, std::string message, std::optional<fix> resolved = std::nullopt) {
            out_.push_back({path_, where, std::move(message), std::move(resolved)});
        }

        static std::string join(const std::string& ns, const std::string& name) {
            if (ns.empty()) return name;
            if (name.empty()) return ns;
            return ns + "::" + name;
        }

        static std::string in_namespace(const std::string& ns) {
            return ns.empty() ? "at the top level" : "in namespace " + ns;
        }

        static std::string first_seen(const origin& o) {
            return o.path + ":" + std::to_string(o.where.line);
        }

        // the first pass: collect declarations

        template <typename T>
        void add(std::map<std::string, declared<T>>& table, const std::string& name, const T& node,
                 location where, std::string_view kind, const std::string& ns) {
            auto [it, inserted] = table.try_emplace(name, declared<T>{&node, {path_, where}});
            if (!inserted) {
                error(where, std::string(kind) + " " + name + " is declared twice " + in_namespace(ns) +
                                 "; the first is at " + first_seen(it->second.from));
            }
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

        static bool has_pages(const std::vector<screen_item>& items) {
            for (const auto& item : items) {
                if (auto* block = std::get_if<content_block>(&item.node); block && has_pages(block->items)) return true;
                auto* text = std::get_if<content_text>(&item.node);
                if (text && text->type == content_text::kind::markdown && !text->value.starts_with("{")) return true;
            }
            return false;
        }

        void collect(const std::string& ns, const std::vector<declaration>& declarations) {
            scope& here = scopes_[ns];
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<namespace_declaration>(&d.node)) {
                    if (n->at) {
                        if (!n->at->starts_with("/")) error(d.where, "a namespace is at an address, like / or /docs");
                        prefixes_[join(ns, n->name)] = *n->at == "/" ? "" : *n->at;
                    }
                    collect(join(ns, n->name), n->declarations);
                } else if (auto* e = std::get_if<entity_declaration>(&d.node)) {
                    add(here.entities, e->name, *e, d.where, "entity", ns);
                } else if (auto* v = std::get_if<view_declaration>(&d.node)) {
                    add(here.views, v->name, *v, d.where, "view", ns);
                } else if (auto* f = std::get_if<format_declaration>(&d.node)) {
                    add(here.formats, f->name, *f, d.where, "format", ns);
                } else if (auto* r = std::get_if<role_declaration>(&d.node)) {
                    here.roles.push_back(r);
                    // A second role of the same name would replace the first, dropping its
                    // permissions, so a role's permissions are all in one place.
                    auto [it, inserted] = here.role_names.try_emplace(r->name, origin{path_, d.where});
                    if (!inserted) {
                        error(d.where, "role " + r->name + " is declared twice " + in_namespace(ns) + "; the first is at " +
                                           first_seen(it->second) + ". A role with many permissions lists them in a block, like role " +
                                           r->name + " { ... }");
                    }
                } else if (auto* function = std::get_if<function_declaration>(&d.node)) {
                    add(here.functions, function->name, *function, d.where, "function", ns);
                } else if (auto* c = std::get_if<command_declaration>(&d.node)) {
                    if (c->name.parts.size() != 2) continue;  // reported in the second pass
                    auto [it, inserted] = here.commands.try_emplace(c->name.text(), origin{path_, d.where});
                    if (!inserted) {
                        error(d.where, "command " + c->name.text() + " is declared twice " + in_namespace(ns) +
                                           "; the first is at " + first_seen(it->second));
                    }
                } else if (auto* s = std::get_if<screen_declaration>(&d.node)) {
                    std::vector<std::string> routes{full_route(ns, s->route)};
                    // A screen of markdown pages, like /docs/:page, is at /docs too, where
                    // it shows its first page.
                    if (routes[0].ends_with("/:page") && has_pages(s->items)) routes.push_back(routes[0].substr(0, routes[0].size() - 6));
                    for (const auto& route : routes) {
                        auto [it, inserted] = routes_.try_emplace(route, origin{path_, d.where});
                        if (!inserted) {
                            error(d.where, "two screens are at " + route + "; the other is at " + first_seen(it->second));
                        }
                    }
                } else if (std::holds_alternative<project_declaration>(d.node)) {
                    if (project_) {
                        error(d.where, "a project has one project block; the first is at " + first_seen(*project_));
                    } else {
                        project_ = origin{path_, d.where};
                    }
                }
            }
        }

        // finding what a name refers to

        // The namespaces a name written in `ns` can refer to, nearest first: `ns`
        // itself, then each enclosing namespace, then the top level. A name written
        // with its own namespace, like waitlist::signups, is looked for inside those.
        std::vector<std::string> candidates(const std::string& ns, const std::vector<std::string>& prefix) const {
            std::string written;
            for (const auto& part : prefix) written = join(written, part);
            std::vector<std::string> result;
            std::string at = ns;
            for (;;) {
                result.push_back(join(at, written));
                if (at.empty()) break;
                auto cut = at.rfind("::");
                at = cut == std::string::npos ? "" : at.substr(0, cut);
            }
            return result;
        }

        template <typename T>
        const T* find(const std::string& ns, const qualified_name& name,
                      std::map<std::string, declared<T>> scope::*table) const {
            std::vector<std::string> prefix(name.parts.begin(), name.parts.end() - 1);
            for (const auto& candidate : candidates(ns, prefix)) {
                auto s = scopes_.find(candidate);
                if (s == scopes_.end()) continue;
                auto it = (s->second.*table).find(name.parts.back());
                if (it != (s->second.*table).end()) return it->second.node;
            }
            return nullptr;
        }

        const entity_declaration* find_entity(const std::string& ns, const qualified_name& name) const {
            return find(ns, name, &scope::entities);
        }

        // Says what the name written at `where` means, for an editor.
        void mean(location where, std::size_t length, std::string says, const std::optional<origin>& to = std::nullopt, std::string section = "") {
            if (!meant_ || length == 0) return;
            meant_->push_back({path_, where, length, std::move(says), to ? to->path : "", to ? to->where : location{}, std::move(section)});
        }

        // Where a declaration is, by the table its kind is kept in.
        template <typename T>
        std::optional<origin> origin_of(const T* declaration, std::map<std::string, declared<T>> scope::*table) const {
            for (const auto& [ns, s] : scopes_) {
                for (const auto& [name, d] : s.*table) {
                    if (d.node == declaration) return d.from;
                }
            }
            return std::nullopt;
        }
        std::optional<origin> origin_of(const entity_declaration* e) const { return origin_of(e, &scope::entities); }

        // The entity a field is declared in, and where, or none for one every entity
        // has, like id.
        std::optional<std::pair<const entity_declaration*, origin>> owner_of(const field* f) const {
            for (const auto& [ns, s] : scopes_) {
                for (const auto& [name, d] : s.entities) {
                    for (const auto& g : d.node->fields) {
                        if (&g == f) return std::pair{d.node, origin{d.from.path, g.where}};
                    }
                }
            }
            return std::nullopt;
        }

        void mean_field(location where, const field* f) {
            if (!meant_ || !f) return;
            auto owner = owner_of(f);
            if (!owner) {
                static const std::map<std::string, std::string, std::less<>> kept{
                    {"id", "its id"}, {"created_at", "when it was made"}, {"created_by", "who made it"},
                    {"updated_at", "when it last changed"}, {"updated_by", "who last changed it"}};
                auto it = kept.find(f->name);
                mean(where, f->name.size(), f->name + ", " + (it == kept.end() ? "kept for every entity" : it->second) + ", which every entity has",
                     std::nullopt, "built-in-values");
                return;
            }
            std::string kind;
            if (!f->choices.empty()) {
                for (std::size_t i = 0; i < f->choices.size(); ++i) {
                    kind += (i == 0 ? "" : i + 1 == f->choices.size() ? " or " : ", ") + f->name + "::" + f->choices[i];
                }
            } else if (f->type) {
                kind = (f->list ? "a list of " : "a ") + f->type->text();
            }
            mean(where, f->name.size(), "field " + f->name + " of " + owner->first->name + (kind.empty() ? "" : ", " + kind), owner->second);
        }

        void mean_entity(location where, std::size_t length, const entity_declaration* e) {
            if (e) mean(where, length, "entity " + e->name, origin_of(e));
        }

        // A command is named after its entity (book::create), possibly inside a
        // namespace (waitlist::signup::create). Returns the command's entity, or
        // nothing when there's no such command.
        const entity_declaration* find_command(const std::string& ns, const qualified_name& name) const {
            if (name.parts.size() < 2) return nullptr;
            const auto& entity = name.parts[name.parts.size() - 2];
            std::string command = entity + "::" + name.parts.back();
            std::vector<std::string> prefix(name.parts.begin(), name.parts.end() - 2);
            for (const auto& candidate : candidates(ns, prefix)) {
                auto s = scopes_.find(candidate);
                if (s == scopes_.end() || !s->second.commands.contains(command)) continue;
                auto e = s->second.entities.find(entity);
                return e == s->second.entities.end() ? nullptr : e->second.node;
            }
            return nullptr;
        }

        static const field* find_field(const entity_declaration& entity, std::string_view name) {
            for (const auto& f : entity.fields) {
                if (f.name == name) return &f;
            }
            return nullptr;
        }

        // the second pass: check everything

        void snake(std::string_view name, location where) {
            if (is_snake_case(name)) return;
            std::string fixed = to_snake_case(name);
            if (is_snake_case(fixed)) {
                error(where, "'" + std::string(name) + "' isn't snake_case; write it as " + fixed);
            } else {
                error(where, "'" + std::string(name) + "' isn't snake_case; names are lowercase words "
                             "joined by underscores, starting with a letter");
            }
        }

        void snake(const qualified_name& name) {
            for (const auto& part : name.parts) snake(part, name.where);
        }

        void verify(const std::string& ns, const std::vector<declaration>& declarations) {
            for (const auto& d : declarations) {
                std::visit([&](const auto& node) { verify(ns, d.where, node); }, d.node);
            }
        }

        void verify(const std::string& ns, location where, const namespace_declaration& n) {
            snake(n.name, where);
            verify(join(ns, n.name), n.declarations);
        }

        // Where a project runs goes into generated Go, YAML and shell, so each value has
        // to be the kind of name it says it is, and nothing that could break out of a
        // quote. A project names all three or none, since a deploy needs all of them.
        void verify(const std::string&, location where, const project_declaration& p) {
            static const std::set<std::string, std::less<>> known{"domain", "firebase", "region", "ui", "authentication", "signin", "icon", "serve", "redirect", "title", "one", "analytics"};
            auto only = [](const std::string& value, std::string_view allowed) {
                return !value.empty() && value.find_first_not_of(allowed) == std::string::npos;
            };
            static constexpr std::string_view id = "abcdefghijklmnopqrstuvwxyz0123456789-";
            static constexpr std::string_view host = "abcdefghijklmnopqrstuvwxyz0123456789-.";
            std::vector<std::string> deploy;
            std::set<std::string> methods;  // the ways people sign in, each named once
            auto check_setting = [&](const setting& s, std::vector<std::string>& where_it_runs) {
                if (!known.contains(s.key)) {
                    error(s.where, "'" + s.key + "' isn't a project setting; expected domain, firebase, "
                                   "region, ui, authentication, icon, serve, redirect, title, one or analytics");
                    return;
                }
                if (s.key == "firebase" || s.key == "region" || s.key == "domain") where_it_runs.push_back(s.key);
                // The compiler the project was last checked clean with: one "0.4.0".
                if (s.key == "one" && !std::regex_match(s.value, std::regex(R"(\d+\.\d+\.\d+)"))) {
                    error(s.where, "one names the compiler's version, like one \"0.4.0\"");
                }
                if ((s.key == "firebase" || s.key == "region") && !only(s.value, id)) {
                    error(s.where, s.key + " has to be lowercase letters, digits and dashes, like ui-one or us-east4");
                }
                // How people sign in, one way to a line: authentication google, then
                // authentication github. Earlier versions called it signin, with one way.
                if (s.key == "signin") {
                    error(s.where, "signin is called authentication now, like authentication " + s.value, fix{s.where, s.key.size(), "authentication"});
                }
                if (s.key == "authentication") {
                    if (s.value != "google" && s.value != "github" && s.value != "microsoft") {
                        error(s.where, "authentication is google, github or microsoft, one to a line");
                    } else if (!methods.insert(s.value).second) {
                        error(s.where, "authentication " + s.value + " is named twice");
                    }
                }
                // Visitors counted with Firebase Analytics, once they agree to it.
                if (s.key == "analytics" && s.value != "google") {
                    error(s.where, "analytics is google, for Firebase Analytics");
                }
                if (s.key == "domain" && (!only(s.value, host) || s.value.find('.') == std::string::npos)) {
                    error(s.where, "domain has to be a domain name, like uione.io");
                }
                // The app's icon is an SVG file next to the project's .one files, so it's
                // sharp at any size and part of the project like everything else.
                // Files served as they are, at the site's root: serve "public" puts
                // public/install.sh at /install.sh.
                // redirect "/install.sh" "https://www.uione.io/install.sh": an address of
                // this site that sends whoever asks for it somewhere else.
                if (s.key == "redirect") {
                    if (!s.value.starts_with("/") || s.to.empty() ||
                        !(s.to.starts_with("https://") || s.to.starts_with("/")) ||
                        s.to.find_first_of("\" \\") != std::string::npos || s.value.find_first_of("\" \\") != std::string::npos) {
                        error(s.where, "redirect takes an address of this site and where it goes, like redirect \"/install.sh\" \"https://www.uione.io/install.sh\"");
                    }
                } else if (!s.to.empty()) {
                    error(s.where, s.key + " takes one value");
                }
                if (s.key == "serve") {
                    std::string dir = path_.substr(0, path_.find_last_of('/') == std::string::npos ? 0 : path_.find_last_of('/'));
                    if (!std::filesystem::is_directory(platform::resolve(dir.empty() ? "." : dir, s.value))) {
                        error(s.where, "there's no folder " + s.value + " to serve; it's looked for next to this .one file");
                    }
                }
                if (s.key == "icon") {
                    std::string dir = path_.substr(0, path_.find_last_of('/') == std::string::npos ? 0 : path_.find_last_of('/'));
                    if (!s.value.ends_with(".svg")) {
                        error(s.where, "icon has to be an .svg file, like \"assets/icon.svg\"");
                    } else if (!platform::read_file(platform::resolve(dir.empty() ? "." : dir, s.value))) {
                        error(s.where, "there's no icon file at " + s.value + "; it's looked for next to this .one file");
                    }
                }
            };
            for (const auto& s : p.settings) check_setting(s, deploy);
            auto missing_from = [](const std::vector<std::string>& have) {
                std::string missing;
                for (const char* key : {"firebase", "region", "domain"}) {
                    if (std::find(have.begin(), have.end(), key) == have.end()) missing += (missing.empty() ? "" : " and ") + std::string(key);
                }
                return missing;
            };
            if (p.environments.empty()) {
                if (!deploy.empty() && deploy.size() < 3) {
                    error(where, "a project that says where it runs needs firebase, region and domain; this one has no " + missing_from(deploy));
                }
                return;
            }
            // Each environment is a place the project runs: its own domain, Firebase
            // project and region, or the ones shared outside the environments.
            std::set<std::string> named;
            std::map<std::string, std::string> places;  // each Google Cloud project, and the environment that has it
            auto shared_firebase = [&]() -> std::string {
                for (const auto& s : p.settings) {
                    if (s.key == "firebase") return s.value;
                }
                return "";
            }();
            for (const auto& environment : p.environments) {
                std::string firebase = shared_firebase;
                for (const auto& s : environment.settings) {
                    if (s.key == "firebase") firebase = s.value;
                }
                if (!firebase.empty()) {
                    auto [it, first] = places.emplace(firebase, environment.name);
                    if (!first && it->second != environment.name) {
                        error(environment.where, "environments " + it->second + " and " + environment.name + " both run in the Google Cloud project " +
                                                     firebase + "; each needs its own, so their data and deploys never mix");
                    }
                }
                snake(environment.name, environment.where);
                if (!named.insert(environment.name).second) {
                    error(environment.where, "there are two environments called " + environment.name);
                }
                std::vector<std::string> runs = deploy;
                for (const auto& s : environment.settings) {
                    if (s.key != "domain" && s.key != "firebase" && s.key != "region") {
                        error(s.where, s.key + " is the same in every environment, so it goes outside them; an environment has its own domain, firebase and region");
                        continue;
                    }
                    check_setting(s, runs);
                }
                std::sort(runs.begin(), runs.end());
                runs.erase(std::unique(runs.begin(), runs.end()), runs.end());
                if (runs.size() < 3) {
                    error(environment.where, "environment " + environment.name + " needs firebase, region and domain, its own or shared; it has no " + missing_from(runs));
                }
            }
        }

        void verify(const std::string&, location where, const format_declaration& f) {
            snake(f.name, where);
        }

        void verify(const std::string& ns, location where, const entity_declaration& e) {
            snake(e.name, where);
            std::set<std::string, std::less<>> seen;
            for (const auto& f : e.fields) {
                snake(f.name, f.where);
                if (!seen.insert(f.name).second) {
                    error(f.where, "entity " + e.name + " has two fields called " + f.name);
                }
                for (const auto& choice : f.choices) snake(choice, f.where);
                if (f.type) verify_type(ns, *f.type);
                // A field says what it holds, so nothing has to be worked out from how
                // a command happens to use it.
                if (!f.type && f.choices.empty()) error(f.where, f.name + " needs a type, like date or text");
                if (f.per) verify_serial(ns, e, f);
                if (f.list) verify_list(ns, f);
                if (f.initial && is_serial(f)) {
                    error(f.initial->where, f.name + " is counted, so it doesn't start with a value");
                }
                if (f.after && !find_field(e, *f.after)) {
                    error(f.where, "'after " + *f.after + "' names a field entity " + e.name + " doesn't have");
                }
                if (f.initial) {
                    names_in(*f.initial);
                    // me.username: the signed-in person's GitHub username, like da0x.
                    if (auto* m = std::get_if<member_expression>(&f.initial->node)) {
                        auto* object = std::get_if<name_expression>(&m->object->node);
                        bool username = object && object->name.text() == "me" && m->member == "username";
                        if (!username) {
                            error(f.initial->where, "a field starts as a value, me, me.username or now, not " + written(*f.initial));
                        } else if (!f.type || f.type->text() != "text") {
                            error(f.initial->where, f.name + " starts as me.username, so it's text");
                        }
                    }
                    if (!f.choices.empty()) {
                        auto* start = std::get_if<name_expression>(&f.initial->node);
                        if (start && start->name.parts.size() == 2) {
                            choice_named(*start, f);
                        } else if (start && start->name.parts.size() == 1 &&
                                   std::find(f.choices.begin(), f.choices.end(), start->name.parts[0]) != f.choices.end()) {
                            error(f.initial->where, "write " + f.name + "::" + start->name.parts[0] + "; an enum's choices are named with it",
                                  fix{start->name.where, start->name.parts[0].size(), f.name + "::" + start->name.parts[0]});
                        } else {
                            error(f.initial->where, "field " + f.name + " has to start as one of its choices, like " + f.name + "::" + f.choices[0]);
                        }
                    }
                }
            }
        }

        // labels  list of label: a list holds text, people, or entities' ids. A list
        // isn't a key, a serial, unique, or after anything.
        void verify_list(const std::string& ns, const field& f) {
            const std::string held = f.type ? f.type->text() : "";
            if (held != "text" && held != "user" && !(f.type && find_entity(ns, *f.type))) {
                error(f.where, f.name + " is a list, which holds text, people or entities, like list of label or list of user");
            }
            if (f.key || f.unique || f.after || f.initial) {
                error(f.where, f.name + " is a list, so it can't be a key, unique, after a field, or start with a value");
            }
        }

        // serial per book counts within each book, so book has to be a field that
        // points at one, and every entity has to have one to be counted in.
        void verify_serial(const std::string& ns, const entity_declaration& e, const field& f) {
            const field* parent = find_field(e, *f.per);
            if (!parent || !pointed(ns, *parent)) {
                error(f.where, f.name + " is counted per " + *f.per + ", so entity " + e.name + " needs a field " + *f.per +
                                   " that points at another entity, like " + *f.per + "  " + *f.per + "  required");
            } else if (!parent->required) {
                error(parent->where, f.name + " is counted per " + *f.per + ", so " + *f.per + " has to be required");
            }
        }

        // A key names the entity, and a serial is counted when it's made, so neither
        // is ever changed afterwards.
        static bool is_serial(const field& f) { return f.type && f.type->text() == "serial"; }
        static bool fixed_once_made(const field& f) { return f.key || is_serial(f); }

        static std::string why_fixed(const field& f) {
            if (is_serial(f)) return f.name + " is counted when it's made, so it can't be set";
            return f.name + " is part of what names it, its key, so it can't be changed";
        }

        void verify_type(const std::string& ns, const qualified_name& type) {
            static const std::set<std::string, std::less<>> built_in{"text", "markdown", "email", "slug", "date", "number", "serial", "boolean", "user"};
            snake(type);
            if (type.parts.size() == 1 && built_in.contains(type.parts[0])) {
                mean(type.where, type.parts[0].size(), "built-in type " + type.parts[0], std::nullopt, "built-in-types");
                return;
            }
            if (auto* f = find(ns, type, &scope::formats)) {
                mean(type.where, type.text().size(), "format " + type.text(), origin_of(f, &scope::formats));
                return;
            }
            if (auto* e = find_entity(ns, type)) {
                mean_entity(type.where, type.text().size(), e);
                return;
            }
            if (type.text() == "bool") {
                error(type.where, "the type is the whole word: boolean, not bool");
                return;
            }
            error(type.where, "'" + type.text() + "' isn't a type; use text, markdown, email, date, number, serial, boolean, "
                              "user, a format, or an entity");
        }

        void verify(const std::string& ns, location where, const command_declaration& c) {
            snake(c.name);
            if (c.name.parts.size() != 2) {
                error(c.name.where, "a command is named after its entity, like book::create");
                return;
            }
            const entity_declaration* entity = find_entity(ns, qualified_name{{c.name.parts[0]}, where});
            mean_entity(c.name.where, c.name.parts[0].size(), entity);
            if (!entity) {
                error(c.name.where, "command " + c.name.text() + " is on entity " + c.name.parts[0] +
                                        ", which isn't declared " + in_namespace(ns));
                return;
            }
            statements(ns, c.body, entity, context{ns, entity, nullptr, {}, false});
        }

        void statements(const std::string& ns, const std::vector<statement>& body, const entity_declaration* entity, const context& in) {
            for (const auto& s : body) {
                if (auto* r = std::get_if<require_statement>(&s.node)) {
                    resolve(in, *r->condition);
                } else if (auto* p = std::get_if<permission_statement>(&s.node)) {
                    verify_permission(ns, p->permission, entity);
                } else if (auto* c = std::get_if<clear_statement>(&s.node)) {
                    for (const auto& name : c->fields) {
                        snake(name, s.where);
                        const field* f = entity ? find_field(*entity, name) : nullptr;
                        if (entity && !f) {
                            error(s.where, "'clear " + name + "' names a field entity " + entity->name + " doesn't have");
                        } else if (f && fixed_once_made(*f)) {
                            error(s.where, why_fixed(*f));
                        }
                    }
                } else if (auto* a = std::get_if<assign_statement>(&s.node)) {
                    std::size_t errors = out_.size();
                    const field* target = resolve(in, *a->target);
                    if (target && fixed_once_made(*target)) error(a->target->where, why_fixed(*target));
                    if (target && target->list) {
                        error(a->target->where, target->name + " is a list; add to it or remove from it, like add me to " + target->name);
                    }
                    // With the target wrong, what's assigned to it can't be judged.
                    if (out_.size() == errors) resolve(in, *a->value, target);
                } else if (auto* c = std::get_if<create_statement>(&s.node)) {
                    verify_create(ns, *c, in);
                } else if (auto* l = std::get_if<list_statement>(&s.node)) {
                    snake(l->list, l->list_where);
                    const field* list = entity ? find_field(*entity, l->list) : nullptr;
                    if (!entity) {
                        error(l->list_where, std::string(l->adds ? "add" : "remove") + " goes in a command, which changes things");
                    } else if (!list) {
                        error(l->list_where, "entity " + entity->name + " has no field " + l->list + nearest(l->list, field_names(*entity)));
                    } else if (!list->list) {
                        error(l->list_where, l->list + " isn't a list, so nothing can be " + (l->adds ? "added to" : "removed from") + " it");
                    } else {
                        resolve(in, *l->value, list);
                    }
                } else if (auto* ret = std::get_if<return_statement>(&s.node)) {
                    if (ret->value) resolve(in, *ret->value);
                } else if (auto* i = std::get_if<if_statement>(&s.node)) {
                    resolve(in, *i->condition);
                    statements(ns, i->then_body, entity, in);
                    statements(ns, i->else_body, entity, in);
                }
            }
        }

        // create member { project = id  person = me }: each value is given to a field of
        // what's made, and is worked out where the command runs. Every required field
        // gets a value, unless it starts with one, and a serial is never given one.
        void verify_create(const std::string& ns, const create_statement& c, const context& in) {
            snake(c.entity, c.entity_where);
            if (!in.entity && !in.hook) {
                error(c.entity_where, "create goes in a command, which makes things; a function only works out a value");
                return;
            }
            const entity_declaration* made = find_entity(ns, qualified_name{{c.entity}, c.entity_where});
            if (!made) {
                error(c.entity_where, "there's no entity " + c.entity + " to create " + in_namespace(ns));
                return;
            }
            std::set<std::string, std::less<>> given;
            bool misnamed = false;
            for (const auto& v : c.values) {
                const field* target = find_field(*made, v.name);
                if (!target) {
                    error(v.where, "entity " + made->name + " has no field " + v.name + nearest(v.name, field_names(*made)));
                    names_in(*v.value);
                    misnamed = true;
                    continue;
                }
                if (!given.insert(v.name).second) error(v.where, v.name + " is given a value twice");
                if (is_serial(*target)) error(v.where, why_fixed(*target));
                resolve(in, *v.value, target);
            }
            // A misspelled name is likely the missing one, so it's reported once.
            for (const auto& f : made->fields) {
                if (!misnamed && f.required && !f.initial && !is_serial(f) && !given.contains(f.name)) {
                    error(c.entity_where, "create " + made->name + " needs a value for " + f.name + ", which is required");
                }
            }
        }

        // Who may do something: anyone, authenticated, owner, or a permission named on an
        // entity, like book::view. owner only makes sense for a command whose entity
        // has an owner field. Earlier versions called authenticated signed_in.
        void verify_permission(const std::string& ns, const qualified_name& p, const entity_declaration* entity) {
            snake(p);
            if (p.parts.size() == 1) {
                const auto& word = p.parts[0];
                static const std::map<std::string, std::string, std::less<>> says{
                    {"anyone", "anyone, signed in or not"}, {"authenticated", "anyone signed in"},
                    {"owner", "the person in the entity's owner field"}};
                if (auto it = says.find(word); it != says.end()) {
                    mean(p.where, word.size(), "built-in permission " + word + ": " + it->second, std::nullopt, "command");
                }
                if (word == "signed_in") {
                    error(p.where, "signed_in is called authenticated now", fix{p.where, word.size(), "authenticated"});
                    return;
                }
                if (word == "anyone" || word == "authenticated") return;
                if (word == "owner") {
                    const field* owner = entity ? find_field(*entity, "owner") : nullptr;
                    bool is_user = owner && owner->type && owner->type->text() == "user";
                    if (!is_user) {
                        error(p.where, entity ? "'owner' needs entity " + entity->name + " to have a field "
                                                "'owner user'"
                                              : "'owner' only applies to a command");
                    }
                    return;
                }
                error(p.where, "'" + word + "' isn't a permission; use anyone, authenticated, owner, or one "
                               "named on an entity, like book::view");
                return;
            }
            qualified_name on{{p.parts.begin(), p.parts.end() - 1}, p.where};
            if (!find_entity(ns, on)) {
                error(p.where, "permission " + p.text() + " is on entity " + on.text() +
                                   ", which isn't declared " + in_namespace(ns));
            }
        }

        void verify(const std::string& ns, location where, const view_declaration& v) {
            snake(v.name, where);
            if (v.per && *v.per != "user") {
                snake(*v.per, where);
                if (!find_entity(ns, qualified_name{{*v.per}, where})) {
                    error(where, "view " + v.name + " is per " + *v.per + ", which is neither user nor an "
                                 "entity " + in_namespace(ns));
                }
            }
            // In a view per entity, that entity is named plainly: book.title is the
            // title of the book the document is for.
            const entity_declaration* subject = v.per && *v.per != "user" ? find_entity(ns, qualified_name{{*v.per}, where}) : nullptr;
            context plain{ns, nullptr, nullptr, {}, true, subject};
            for (const auto& value : v.values) verify_view_value(plain, value);
            for (const auto& value : v.values) {
                if (*value.name == "public" || *value.name == "readers" || *value.name == "within") {
                    error(value.where, *value.name + " says who may read a view's document, so it can't name a value");
                }
                auto* call = std::get_if<call_expression>(&value.value->node);
                auto* callee = call ? std::get_if<name_expression>(&call->callee->node) : nullptr;
                if (callee && callee->name.text() == "github_secret" && (v.is_public || v.public_when || !v.readers)) {
                    error(value.where, "view " + v.name + " shows a webhook secret, so only the project's people may read it: "
                                       "give it readers, and don't make it public");
                }
            }
            if (v.readers || !v.reader_people.empty() || v.public_when) verify_access(ns, v, where, subject, plain);
            // A view has at most one list without a name, its rows, and any number
            // with names, each named once.
            std::set<std::string, std::less<>> names;
            for (const auto& value : v.values) names.insert(*value.name);
            bool unnamed = false;
            for (const auto& each : v.each) {
                if (!each.name) {
                    if (unnamed) error(each.where, "view " + v.name + " already has a list without a name; give this one a name, like comments = each ...");
                    unnamed = true;
                    continue;
                }
                snake(*each.name, each.where);
                if (*each.name == "rows") {
                    error(each.where, "rows is what a view's list without a name is called; name this list something else");
                } else if (!names.insert(*each.name).second) {
                    error(each.where, "view " + v.name + " already has something called " + *each.name);
                }
            }
            for (const auto& each : v.each) {
                const entity_declaration* listed = nullptr;
                auto* source = std::get_if<name_expression>(&each.source->node);
                if (!source) {
                    error(each.where, "'each' is followed by the entity whose rows the view lists");
                } else {
                    snake(source->name);
                    listed = find_entity(ns, source->name);
                    if (!listed) {
                        error(source->name.where, "'each " + source->name.text() + "' needs an entity called " +
                                                      source->name.text());
                    } else if (each.changes && !listed->history) {
                        error(source->name.where, "'each change of " + listed->name + "' needs " + listed->name +
                                                      " to keep its history: entity " + listed->name + " history { ... }");
                        listed = nullptr;
                    } else if (each.changes) {
                        listed = &change_of(ns, *listed);
                    }
                }
                context rows{ns, listed, nullptr, {}, true, subject};
                if (each.condition) {
                    if (listed) resolve(rows, *each.condition);
                    else names_in(*each.condition);
                }
                for (const auto& key : each.order) {
                    if (listed) resolve(rows, *key);
                    else names_in(*key);
                }
                for (const auto& row : each.rows) {
                    if (listed) verify_view_value(rows, row);
                    else names_in(*row.value);
                }
            }
        }

        // readers member and public when project.visibility == public: who may read
        // each document of a view per entity. readers names an entity that grants a
        // role within something the view's entity is, or is held within; public when
        // compares one field of the entity, or of what it points at, with a value.
        void verify_access(const std::string& ns, const view_declaration& v, location where, const entity_declaration* subject,
                           const context& plain) {
            if (!subject) {
                error(where, "view " + v.name + " says who may read it, so it needs a document per entity, like per project");
                return;
            }
            if (v.is_public && v.public_when) error(where, "view " + v.name + " is public, so it can't also be public when something holds");
            if (v.readers) {
                snake(*v.readers, v.readers_where);
                const entity_declaration* scope = nullptr;
                if (auto here = scopes_.find(ns); here != scopes_.end()) {
                    for (const auto* r : here->second.roles) {
                        if (r->from == v.readers && r->per) scope = find_entity(ns, qualified_name{{*r->per}, v.readers_where});
                    }
                }
                if (!find_entity(ns, qualified_name{{*v.readers}, v.readers_where})) {
                    error(v.readers_where, "readers " + *v.readers + " names an entity that isn't declared " + in_namespace(ns));
                } else if (!scope) {
                    error(v.readers_where, "readers " + *v.readers + " needs a role that comes from " + *v.readers +
                                               ", like role maintainer per project from " + *v.readers);
                } else if (!held_within(ns, *subject, *scope)) {
                    error(v.readers_where, "readers " + *v.readers + " are people in a " + scope->name + ", but a " + subject->name +
                                               " isn't held within one");
                }
            }
            // readers report.author: the person a field of the entity names, or each
            // person a list of them names.
            for (const auto& people : v.reader_people) {
                auto* m = std::get_if<member_expression>(&people->node);
                auto* object = m ? std::get_if<name_expression>(&m->object->node) : nullptr;
                if (!object || object->name.parts.size() != 1 || object->name.parts[0] != subject->name) {
                    error(people->where, "readers names an entity, like readers member, or the people in a field of the " +
                                             subject->name + ", like readers " + subject->name + ".author");
                    continue;
                }
                auto f = std::find_if(subject->fields.begin(), subject->fields.end(), [&](const field& f) { return f.name == m->member; });
                if (f == subject->fields.end()) {
                    error(people->where, "a " + subject->name + " has no field " + m->member + " for readers to name");
                } else if (!f->type || f->type->text() != "user") {
                    error(people->where, "readers " + subject->name + "." + m->member + " needs a person, but a " + subject->name + "'s " +
                                             m->member + " isn't one");
                }
            }
            if (v.public_when) {
                auto* b = std::get_if<binary_expression>(&v.public_when->node);
                if (!b || b->op != token_kind::equal || !std::holds_alternative<member_expression>(b->left->node)) {
                    error(v.public_when->where, "public when compares a field with a value, like " + subject->name + ".visibility == public");
                    return;
                }
                resolve(plain, *v.public_when);
            }
        }

        // What a change of an entity holds: the entity's id under its own name, what
        // it points at, like its project, and which field changed, what it was before
        // and after, and the command that changed it. Who made it and when are in
        // created_by and created_at, as for anything stored.
        const entity_declaration& change_of(const std::string& ns, const entity_declaration& e) {
            auto [it, made] = changes_.try_emplace(&e);
            if (!made) return it->second;
            entity_declaration& change = it->second;
            change.name = "change";
            field of;
            of.name = e.name;
            of.type = qualified_name{{e.name}, {}};
            change.fields.push_back(std::move(of));
            for (const auto& f : e.fields) {
                if (!f.type || (f.type->text() != "user" && !pointed(ns, f))) continue;
                field copy;
                copy.name = f.name;
                copy.type = f.type;
                copy.list = f.list;
                change.fields.push_back(std::move(copy));
            }
            for (const char* name : {"field", "action", "before", "after"}) {
                field f;
                f.name = name;
                f.type = qualified_name{{"text"}, {}};
                change.fields.push_back(std::move(f));
            }
            return change;
        }

        // Whether an entity is a scope, points at one, or points at something that does.
        bool held_within(const std::string& ns, const entity_declaration& e, const entity_declaration& scope) {
            if (&e == &scope) return true;
            for (const auto& f : e.fields) {
                const entity_declaration* through = pointed(ns, f);
                if (through == &scope) return true;
                if (through && std::any_of(through->fields.begin(), through->fields.end(),
                                           [&](const field& g) { return pointed(ns, g) == &scope; })) {
                    return true;
                }
            }
            return false;
        }

        // The list a table shows: the one it names, like issue_page.comments, or the
        // view's list without a name.
        const view_each* table_list(const view_declaration& view, const std::string& view_name, const table_item& table) {
            for (const auto& each : view.each) {
                if (each.name == table.list) return &each;
            }
            if (table.list) {
                error(table.view.where, "view " + view_name + " has no list called " + *table.list);
            } else if (!view.each.empty()) {
                error(table.view.where, "view " + view_name + "'s lists all have names; say which one, like table " + view_name +
                                            "." + *view.each.front().name);
            }
            return nullptr;
        }

        void verify_view_value(const context& in, const view_value& value) {
            if (value.name) snake(*value.name, value.where);
            resolve(in, *value.value);
        }

        void verify(const std::string& ns, location where, const role_declaration& r) {
            snake(r.name, where);
            for (const auto& p : r.permissions) verify_permission(ns, p, nullptr);
            if (r.per) verify_role_within(ns, r);
        }

        // role maintainer per project from member: a member grants it, by pointing at
        // a project and a person and naming the role in its role field. Each
        // permission it grants is on the project, on something that points at one,
        // like an issue, or on something that points at that, like an issue's
        // comment, so a command can tell which project to look in.
        void verify_role_within(const std::string& ns, const role_declaration& r) {
            const entity_declaration* scope = find_entity(ns, qualified_name{{*r.per}, r.per_where});
            const entity_declaration* member = find_entity(ns, qualified_name{{*r.from}, r.per_where});
            if (!scope) error(r.per_where, "role " + r.name + " is held within " + *r.per + ", which isn't an entity " + in_namespace(ns));
            if (!member) error(r.per_where, "role " + r.name + " comes from " + *r.from + ", which isn't an entity " + in_namespace(ns));
            if (!scope || !member) return;
            bool place = false, person = false, named = false;
            for (const auto& f : member->fields) {
                if (f.required && pointed(ns, f) == scope) place = true;
                if (f.required && f.type && f.type->text() == "user") person = true;
                if (f.name == "role" && std::find(f.choices.begin(), f.choices.end(), r.name) != f.choices.end()) named = true;
            }
            if (!place) {
                error(r.per_where, "role " + r.name + " comes from " + member->name + ", so " + member->name + " needs a required field " +
                                       "pointing at " + scope->name + ", like " + scope->name + "  " + scope->name + "  required");
            }
            if (!person) {
                error(r.per_where, "role " + r.name + " comes from " + member->name + ", so " + member->name + " needs a required field " +
                                       "holding the person, like person  user  required");
            }
            if (!named) {
                error(r.per_where, "role " + r.name + " comes from " + member->name + ", so " + member->name + " needs a field role " +
                                       "with " + r.name + " among its choices, like role  enum " + r.name + " | reader");
            }
            for (const auto& p : r.permissions) {
                if (p.parts.size() < 2) continue;
                const entity_declaration* on = find_entity(ns, qualified_name{{p.parts.begin(), p.parts.end() - 1}, p.where});
                if (!on || on == scope) continue;
                auto points_at_scope = [&](const entity_declaration& e) {
                    return std::any_of(e.fields.begin(), e.fields.end(), [&](const field& f) { return pointed(ns, f) == scope; });
                };
                bool points = points_at_scope(*on) || std::any_of(on->fields.begin(), on->fields.end(), [&](const field& f) {
                                  const entity_declaration* through = pointed(ns, f);
                                  return through && points_at_scope(*through);
                              });
                if (!points) {
                    error(p.where, "role " + r.name + " is held within a " + scope->name + ", but " + on->name + " doesn't point at one, " +
                                       "so there's no " + scope->name + " to look in for " + p.text());
                }
            }
        }

        void verify(const std::string& ns, location where, const function_declaration& f) {
            snake(f.name, where);
            for (const auto& parameter : f.parameters) snake(parameter, where);
            statements(ns, f.body, nullptr, context{ns, nullptr, nullptr, f.parameters, false});
        }

        // A link opens a screen that's there: one written as an address matches a
        // screen's, with any :parameter standing for one part, and a namespace has a
        // screen at its own address.
        void link(const std::string& ns, const std::string& screen_route, const content_link& link, location where) {
            if (link.namespace_name) {
                snake(*link.namespace_name);
                std::string name = link.namespace_name->text();
                if (!scopes_.contains(name)) {
                    error(link.namespace_name->where, "there's no namespace " + name + " for this link to open");
                } else if (!routes_.contains(full_route(name, "/"))) {
                    error(link.namespace_name->where, "namespace " + name + " has no screen at its own address, " +
                                                      full_route(name, "/") + ", for this link to open");
                }
                return;
            }
            if (link.target.starts_with("https://") || link.target.starts_with("http://")) {
                if (!link.target.starts_with("https://") || link.target.find_first_of("\" \\") != std::string::npos) {
                    error(where, "a link to another site is an https:// address, like \"https://uione.io/studio\"");
                }
                return;
            }
            if (!link.target.starts_with("/") && !link.target.starts_with("#")) {
                error(where, "a link goes to an address, like /docs, a place on the page, like #top, or another site, like \"https://uione.io\"");
                return;
            }
            if (!link.target.starts_with("/")) return;
            // Like a screen's address, it's inside the namespace it's written in.
            std::string target = full_route(ns, link.target);
            // Its :parameters are filled from this screen's address, like the project in
            // /projects/:project/reports from the page /projects/:project.
            for (std::size_t at = target.find("/:"); at != std::string::npos; at = target.find("/:", at + 1)) {
                std::size_t end = target.find('/', at + 1);
                std::string name = target.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2);
                if ((screen_route + "/").find("/:" + name + "/") == std::string::npos) {
                    error(where, "this link needs :" + name + ", which this screen's address doesn't have");
                    return;
                }
            }
            auto parts = [](const std::string& route) {
                std::vector<std::string> out;
                for (std::size_t at = 1; at <= route.size();) {
                    std::size_t next = std::min(route.find('/', at), route.size());
                    if (next > at) out.push_back(route.substr(at, next - at));
                    at = next + 1;
                }
                return out;
            };
            auto wanted = parts(target);
            for (const auto& [route, from] : routes_) {
                auto screen = parts(route);
                // A last parameter like :file* takes one part or more: /code/a/b.one.
                bool rest = !screen.empty() && screen.back().starts_with(":") && screen.back().ends_with("*");
                if (rest ? wanted.size() < screen.size() : screen.size() != wanted.size()) continue;
                bool same = true;
                for (std::size_t i = 0; same && i < screen.size(); ++i) {
                    same = screen[i].starts_with(":") || screen[i] == wanted[i];
                }
                if (same) return;
            }
            error(where, "there's no screen at " + target + " for this link to open");
        }

        void verify(const std::string& ns, location where, const screen_declaration& s) {
            if (s.title_is_name) snake(s.title, where);
            if (auto star = s.route.find('*'); star != std::string::npos) {
                auto last = s.route.rfind('/');
                if (star != s.route.size() - 1 || s.route.compare(last, 2, "/:") != 0 || star == last + 2) {
                    error(where, "a * ends the last parameter of an address, taking the rest of it, like /code/:file*");
                }
            }
            screen_items(ns, s.items, full_route(ns, s.route));
            // A title can show what the page does: screen "#{issue_page.number} {issue_page.title}".
            if (!s.title_is_name) verify_live_text(ns, s.title, full_route(ns, s.route), where);
        }

        // A view per entity is shown for one entity at a time, the one the screen's
        // address names, so the screen needs that entity as a parameter of its route.
        // When a button shows: what the page's views say, compared with values, like
        // issue_page.status == status::open, and joined with && and ||.
        void verify_condition(const std::string& ns, const expression& e, const std::string& route) {
            static const std::string how = "a button's when compares a view's fields with values, like issue_page.status == status::open";
            if (auto* binary = std::get_if<binary_expression>(&e.node)) {
                verify_condition(ns, *binary->left, route);
                verify_condition(ns, *binary->right, route);
            } else if (auto* unary = std::get_if<unary_expression>(&e.node); unary && unary->op == token_kind::logical_not) {
                verify_condition(ns, *unary->operand, route);
            } else if (auto* member = std::get_if<member_expression>(&e.node)) {
                auto* object = std::get_if<name_expression>(&member->object->node);
                const view_declaration* view = object ? find(ns, object->name, &scope::views) : nullptr;
                if (!view) {
                    error(e.where, how);
                    return;
                }
                verify_shown(*view, object->name.text(), route, e.where);
                bool has = false;
                for (const auto& v : view->values) {
                    if ((v.name ? *v.name : written(*v.value)) == member->member) has = true;
                }
                if (!has) error(e.where, "view " + object->name.text() + " has no " + member->member + " for the button to read");
            } else if (auto* name = std::get_if<name_expression>(&e.node)) {
                const std::string word = name->name.text();
                if (name->name.parts.size() != 2 && word != "true" && word != "false" && word != "none") error(e.where, how);
            } else if (!std::holds_alternative<literal_expression>(e.node)) {
                error(e.where, how);
            }
        }

        // Live values like {book_page.title}, in a text or a screen's title, each read a
        // view the screen can show.
        void verify_live_text(const std::string& ns, const std::string& text, const std::string& route, location where) {
            for (std::size_t open = text.find('{'); open != std::string::npos; open = text.find('{', open + 1)) {
                std::size_t close = text.find('}', open);
                if (close == std::string::npos) break;
                std::string inside = text.substr(open + 1, close - open - 1);
                std::size_t dot = inside.rfind('.');
                if (dot == std::string::npos) continue;
                qualified_name name{{}, where};
                std::string written = inside.substr(0, dot);
                for (std::size_t at = 0;;) {
                    std::size_t next = written.find("::", at);
                    name.parts.push_back(written.substr(at, next == std::string::npos ? std::string::npos : next - at));
                    if (next == std::string::npos) break;
                    at = next + 2;
                }
                if (const view_declaration* view = find(ns, name, &scope::views)) {
                    verify_shown(*view, name.text(), route, where);
                }
            }
        }

        void verify_shown(const view_declaration& view, const std::string& view_name,
                          const std::string& route, location where) {
            if (!view.per || *view.per == "user") return;
            if (route.find("/:" + *view.per) == std::string::npos) {
                error(where, "view " + view_name + " has one document per " + *view.per + ", so the screen showing it needs :" +
                                 *view.per + " in its route, like /" + *view.per + "s/:" + *view.per);
            }
        }

        void verify_command_use(const std::string& ns, const qualified_name& name) {
            snake(name);
            if (!find_command(ns, name)) {
                error(name.where, "there's no command " + name.text() + " " + in_namespace(ns) +
                                      "; commands are named after their entity, like book::create");
            }
        }

        // How a value is written, like title or book.title, which is also its name in
        // a view's rows.
        static std::string written(const expression& e) {
            if (auto* name = std::get_if<name_expression>(&e.node)) return name->name.text();
            if (auto* member = std::get_if<member_expression>(&e.node)) return written(*member->object) + "." + member->member;
            return "";
        }

        // A table shows a view's rows, so each column is either something the rows
        // hold or a command on the row's entity, like withdraw. A row holds exactly
        // what its view's block lists, so a view never sends a field by accident.
        void verify_column(const std::string& ns, const view_each& each, const std::string& view_name,
                           const table_column& column) {
            std::string key = written(*column.value);
            if (key.empty()) return;
            if (auto* source = std::get_if<name_expression>(&each.source->node); source && key.find('.') == std::string::npos) {
                qualified_name command{{source->name.parts.back(), key}, column.where};
                if (find_command(ns, command)) return;
            }
            for (const auto& row : each.rows) {
                if ((row.name && *row.name == key) || (!row.name && written(*row.value) == key)) return;
            }
            std::string where = each.name ? *each.name : "rows";
            error(column.where, "view " + view_name + " has no " + key + " in its " + where + "; add it to the list's block");
        }

        // A form for anything but create changes one entity that's already there, so it
        // starts from what's stored: its screen names the entity in its address, and a
        // view per that entity holds every field the form asks for. Without that, a
        // field would start empty and the update would store it empty.
        void verify_edit_form(const std::string& ns, const entity_declaration& entity, const form_item& form,
                              const std::string& route) {
            for (const auto& command : form.commands) {
                if (command.parts.back() == "create") continue;
                if (route.find("/:" + entity.name) == std::string::npos) {
                    error(command.where, "form " + command.text() + " changes one " + entity.name + ", so its screen needs :" +
                                             entity.name + " in its route, like /" + entity.name + "s/:" + entity.name);
                    continue;
                }
                bool found = false;
                if (auto scope = scopes_.find(ns); scope != scopes_.end()) {
                    for (const auto& [name, view] : scope->second.views) {
                        if (!view.node->per || *view.node->per != entity.name) continue;
                        bool holds = std::all_of(form.fields.begin(), form.fields.end(), [&](const form_field& f) {
                            return std::any_of(view.node->values.begin(), view.node->values.end(),
                                               [&](const view_value& v) { return v.name && *v.name == f.name; });
                        });
                        found = found || holds;
                    }
                }
                if (!found) {
                    std::string fields;
                    for (const auto& f : form.fields) fields += (fields.empty() ? "" : " ") + f.name;
                    error(command.where, "form " + command.text() + " starts from what's stored, so it needs a view per " +
                                             entity.name + " holding " + fields + ", like " + form.fields.front().name + " = " +
                                             entity.name + "." + form.fields.front().name);
                }
            }
        }

        void screen_items(const std::string& ns, const std::vector<screen_item>& items, const std::string& route) {
            for (const auto& item : items) {
                if (auto* block = std::get_if<content_block>(&item.node)) {
                    if (block->type == content_block::kind::menu) {
                        for (const auto& inside : block->items) {
                            if (!std::holds_alternative<content_link>(inside.node)) {
                                error(inside.where, "a menu holds links, like link /settings \"General\"");
                            }
                        }
                    }
                    screen_items(ns, block->items, route);
                } else if (auto* table = std::get_if<table_item>(&item.node)) {
                    snake(table->view);
                    const view_declaration* view = find(ns, table->view, &scope::views);
                    if (!view) {
                        error(table->view.where, "there's no view " + table->view.text() + " for this table " +
                                                     in_namespace(ns));
                    }
                    const view_each* list = view ? table_list(*view, table->view.text(), *table) : nullptr;
                    for (const auto& column : table->columns) {
                        names_in(*column.value);
                        if (list) verify_column(ns, *list, table->view.text(), column);
                    }
                    if (view) verify_shown(*view, table->view.text(), route, table->view.where);
                    if (table->link) {
                        // The last :parameter is the row's own; any before it come from this
                        // screen's address, like the project in /projects/:project/issues/:issue.
                        std::string target = full_route(ns, *table->link);
                        std::vector<std::string> names;
                        for (std::size_t at = target.find("/:"); at != std::string::npos; at = target.find("/:", at + 1)) {
                            std::size_t end = target.find('/', at + 1);
                            names.push_back(target.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2));
                        }
                        // A row that holds the thing a parameter names, like a report's
                        // project, fills it too.
                        auto row_holds = [&](const std::string& name) {
                            if (!list) return false;
                            for (const auto& row : list->rows) {
                                auto* plain = std::get_if<name_expression>(&row.value->node);
                                if (row.name ? *row.name == name : plain && plain->name.text() == name) return true;
                            }
                            return false;
                        };
                        // Or the key the last names it by, like owner in /:owner/:project
                        // for a project keyed owner and slug.
                        std::vector<std::string> keys;
                        if (!names.empty()) {
                            if (const entity_declaration* opened = find_entity(ns, qualified_name{{names.back()}, table->link_where})) {
                                for (const auto& f : opened->fields) {
                                    if (f.key) keys.push_back(f.name);
                                }
                            }
                        }
                        auto keyed = [&](const std::string& name) {
                            return keys.size() > 1 && std::find(keys.begin(), keys.end() - 1, name) != keys.end() - 1;
                        };
                        std::string missing;
                        for (std::size_t i = 0; i + 1 < names.size(); ++i) {
                            if (route.find("/:" + names[i]) == std::string::npos && !row_holds(names[i]) && !keyed(names[i])) missing = names[i];
                        }
                        if (names.empty()) {
                            error(table->link_where, "a table's link ends with a :parameter, which each row's id fills, like /books/:book");
                        } else if (!missing.empty()) {
                            error(table->link_where, "this table's link needs :" + missing + ", which neither this screen's address nor its rows have");
                        } else if (!routes_.contains(target)) {
                            error(table->link_where, "there's no screen at " + target + " for this table's rows to open");
                        }
                    }
                } else if (auto* link = std::get_if<content_link>(&item.node)) {
                    this->link(ns, route, *link, item.where);
                } else if (auto* text = std::get_if<content_text>(&item.node);
                           text && (text->type == content_text::kind::text || text->value.starts_with("{"))) {
                    verify_live_text(ns, text->value, route, item.where);
                } else if (auto* form = std::get_if<form_item>(&item.node)) {
                    for (const auto& command : form->commands) verify_command_use(ns, command);
                    const entity_declaration* entity = find_command(ns, form->commands.front());
                    bool creates = form->commands.front().parts.back() == "create";
                    for (const auto& f : form->fields) {
                        snake(f.name, f.where);
                        const field* asked = entity ? find_field(*entity, f.name) : nullptr;
                        if (entity && !asked) {
                            error(f.where, "the form asks for " + f.name + ", which isn't a field of entity " +
                                               entity->name);
                        } else if (asked && fixed_once_made(*asked) && !(creates && asked->key && !is_serial(*asked))) {
                            // A create form asks for a key, which names what it makes.
                            error(f.where, why_fixed(*asked));
                        }
                        if (f.value) names_in(*f.value);
                    }
                    if (entity) verify_edit_form(ns, *entity, *form, route);
                } else if (auto* confirm = std::get_if<confirm_item>(&item.node)) {
                    verify_command_use(ns, confirm->command);
                } else if (auto* button = std::get_if<button_item>(&item.node)) {
                    verify_command_use(ns, button->command);
                    if (button->when) verify_condition(ns, *button->when, route);
                } else if (auto* component = std::get_if<component_item>(&item.node)) {
                    snake(component->name, item.where);
                    std::string file = component_file(path_, component->name);
                    if (!platform::read_file(file)) {
                        error(item.where, "component " + component->name + " is drawn by components/" + component->name +
                                              ".tsx beside this file, which isn't there");
                    }
                }
            }
        }

        // The entity a #12 names: numbered within the scope, keyed by the scope and its
        // number, like an issue.
        const entity_declaration* numbered_within(const std::string& ns, const entity_declaration& scope) {
            auto here = scopes_.find(ns);
            if (here == scopes_.end()) return nullptr;
            for (const auto& [name, e] : here->second.entities) {
                std::vector<const field*> keys;
                for (const auto& f : e.node->fields) {
                    if (f.key) keys.push_back(&f);
                }
                if (keys.size() == 2 && pointed(ns, *keys[0]) == &scope && is_serial(*keys[1]) && keys[1]->per == keys[0]->name) {
                    return e.node;
                }
            }
            return nullptr;
        }

        // backend deploy: backend/deploy.go beside this file, Go in the namespace's
        // package, built with the generated code beside it.
        void verify(const std::string& ns, location where, const backend_declaration& b) {
            snake(b.name, where);
            if (ns.empty()) {
                error(where, "backend " + b.name + " is Go in a namespace's package, so it goes inside a namespace");
                return;
            }
            std::string package;
            for (char c : ns) {
                if (c != '_') package += c;
            }
            if (b.name == package) {
                error(where, "backend " + b.name + " would be the same file as the code generated for namespace " + ns +
                                 "; name it for what it does, like deploy");
                return;
            }
            auto text = platform::read_file(backend_file(path_, b.name));
            if (!text) {
                error(where, "backend " + b.name + " is written in backend/" + b.name + ".go beside this file, which isn't there");
                return;
            }
            // Its package clause, past any comments: package studio.
            std::istringstream lines(*text);
            for (std::string line; std::getline(lines, line);) {
                if (line.empty() || line.starts_with("//")) continue;
                if (line != "package " + package) {
                    error(where, "backend/" + b.name + ".go has to start with package " + package + ", the package of namespace " + ns);
                }
                break;
            }
        }

        void verify(const std::string& ns, location where, const webhook_declaration& w) {
            if (w.provider != "github") {
                error(w.provider_where, "'" + w.provider + "' isn't a webhook uione knows; it knows github");
                return;
            }
            if (!w.route.starts_with("/hooks/")) {
                error(where, "a webhook is received under /hooks/, like /hooks/github");
            }
            if (w.scope.empty()) {
                error(where, "webhook github needs to know whose repository it is, like for project by repository");
                return;
            }
            const entity_declaration* scope = find_entity(ns, qualified_name{{w.scope}, w.scope_where});
            if (!scope) {
                error(w.scope_where, "there's no entity " + w.scope + " " + in_namespace(ns));
                return;
            }
            const field* repository = find_field(*scope, w.repository);
            if (!repository || !repository->type || repository->type->text() != "text" || repository->list) {
                error(w.scope_where, "webhook github finds a " + scope->name + " by " + w.repository + ", so " + scope->name +
                                         " needs a text field " + w.repository + ", like " + w.repository + "  text  unique");
            }
            const entity_declaration* issue = numbered_within(ns, *scope);
            if (!issue) {
                error(w.scope_where, "webhook github reads #12 as a " + scope->name + "'s issue 12, so an entity needs keys " +
                                         scope->name + " and a serial per " + scope->name + ", like issue");
            }
            std::set<std::string, std::less<>> seen;
            for (const auto& h : w.handlers) {
                std::vector<std::string> given;
                if (h.event == "commit") given = {"mentioned", "message", "url", "author", "sha"};
                else if (h.event == "pull_request") given = {"mentioned", "title", "url", "author", "number"};
                else {
                    error(h.where, "github sends commit and pull_request, not " + h.event + nearest(h.event, {"commit", "pull_request"}));
                    continue;
                }
                if (!seen.insert(h.event).second) error(h.where, "webhook github handles " + h.event + " twice");
                context in{ns, nullptr, nullptr, given, false, nullptr, true};
                for (const auto& s : h.body) {
                    if (auto* c = std::get_if<create_statement>(&s.node)) verify_create(ns, *c, in);
                    else error(s.where, "a webhook makes things with create; it changes nothing else");
                }
            }
        }

        void verify(const std::string& ns, location where, const picker_declaration& p) {
            snake(p.entity, where);
            snake(p.view, where);
            if (!find_entity(ns, qualified_name{{p.entity}, where})) {
                error(where, "picker " + p.entity + " needs an entity called " + p.entity);
            }
            if (!find(ns, qualified_name{{p.view}, where}, &scope::views)) {
                error(where, "picker " + p.entity + " picks from view " + p.view + ", which isn't declared " +
                                 in_namespace(ns));
            }
        }

        // How far apart two words are, in letters added, dropped or changed.
        static std::size_t distance(std::string_view a, std::string_view b) {
            std::vector<std::size_t> row(b.size() + 1);
            for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
            for (std::size_t i = 1; i <= a.size(); ++i) {
                std::size_t diagonal = row[0];
                row[0] = i;
                for (std::size_t j = 1; j <= b.size(); ++j) {
                    std::size_t above = row[j];
                    row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
                    diagonal = above;
                }
            }
            return row[b.size()];
        }

        // "; did you mean status?", when one of the names it could have been is close.
        static std::string nearest(std::string_view name, const std::vector<std::string>& candidates) {
            std::string best;
            std::size_t best_distance = std::max<std::size_t>(2, name.size() / 3) + 1;
            for (const auto& c : candidates) {
                std::size_t d = distance(name, c);
                if (d < best_distance) {
                    best = c;
                    best_distance = d;
                }
            }
            return best.empty() ? "" : "; did you mean " + best + "?";
        }

        static std::vector<std::string> field_names(const entity_declaration& e) {
            std::vector<std::string> names{"id", "created_at", "created_by", "updated_at", "updated_by"};
            for (const auto& f : e.fields) names.push_back(f.name);
            return names;
        }

        const entity_declaration* pointed(const std::string& ns, const field& f) const {
            if (!f.type) return nullptr;
            return find_entity(ns, *f.type);
        }

        // What a view can show about a person a field holds, like member.name: their
        // id, and the name and picture their sign-in gives. Nothing else about them,
        // such as their email address, ever reaches a view.
        const field* profile_field(const context& in, const field& person, const std::string& member, location where) {
            static const std::vector<field> profile = [] {
                std::vector<field> fields;
                for (const char* name : {"id", "name", "picture", "username"}) {
                    field f;
                    f.name = name;
                    f.type = qualified_name{{"text"}, {}};
                    fields.push_back(std::move(f));
                }
                return fields;
            }();
            if (!in.reader) {
                error(where, "a person's name, picture and username are shown in views; here " + person.name + " is only who they are");
                return nullptr;
            }
            for (const auto& f : profile) {
                if (f.name == member) return &f;
            }
            error(where, "a view can show a person's name, picture and username, not " + person.name + "." + member +
                             nearest(member, {"name", "picture", "username"}));
            return nullptr;
        }

        // A field an entity names, or one every entity has: its id, and when and by
        // whom it was made and last changed.
        const field* field_or_id(const entity_declaration& e, std::string_view name) const {
            static const std::vector<field> record = [] {
                std::vector<field> fields;
                for (auto [name, type] : {std::pair{"id", ""}, {"created_at", "date"}, {"created_by", "user"},
                                          {"updated_at", "date"}, {"updated_by", "user"}}) {
                    field f;
                    f.name = name;
                    if (*type) f.type = qualified_name{{type}, {}};
                    fields.push_back(std::move(f));
                }
                return fields;
            }();
            for (const auto& f : record) {
                if (f.name == name) return &f;
            }
            return find_field(e, name);
        }

        // An enum's choice, written with the enum's name, like visibility::public, where
        // the enum is a field's: the name has to be that field's, and the choice one of
        // its choices.
        void choice_named(const name_expression& n, const field& f) {
            const auto& written = n.name.parts;
            if (written[0] != f.name) {
                error(n.name.where, n.name.text() + " isn't one of " + f.name + "'s choices; they're written " + f.name + "::" +
                                        f.choices[0] + " and so on");
                return;
            }
            if (std::find(f.choices.begin(), f.choices.end(), written[1]) == f.choices.end()) {
                std::string choices;
                for (const auto& c : f.choices) choices += (choices.empty() ? "" : ", ") + f.name + "::" + c;
                error(n.name.where, written[1] + " isn't one of " + f.name + "'s choices, " + choices + nearest(written[1], f.choices));
            }
        }

        // Checks every name in an expression against what it can mean here, and
        // returns the field it stands for, if it's one, so a choice compared with it
        // or assigned to it can be checked too.
        const field* resolve(const context& in, const expression& e, const field* beside = nullptr) {
            static const std::set<std::string, std::less<>> plain{"now", "me", "none", "true", "false"};
            if (auto* n = std::get_if<name_expression>(&e.node)) {
                snake(n->name);
                // An enum's choice, named with its enum: visibility::public.
                if (n->name.parts.size() == 2 && beside && !beside->choices.empty()) {
                    choice_named(*n, *beside);
                    if (auto owner = owner_of(beside)) {
                        mean(n->name.where, n->name.text().size(), "choice " + n->name.parts[1] + " of " + beside->name + ", in " + owner->first->name,
                             owner->second);
                    }
                    return nullptr;
                }
                if (n->name.parts.size() != 1) return nullptr;
                const std::string& name = n->name.parts[0];
                if (!is_snake_case(name)) return nullptr;  // that error says what's wrong already
                if (plain.contains(name)) {
                    static const std::map<std::string, std::string, std::less<>> says{
                        {"now", "now, the time it runs"}, {"me", "me, the person doing it"}, {"none", "none, no value"},
                        {"true", "true"}, {"false", "false"}};
                    mean(n->name.where, name.size(), "built-in value " + says.at(name), std::nullopt, "built-in-values");
                    return nullptr;
                }
                if (std::find(in.parameters.begin(), in.parameters.end(), name) != in.parameters.end()) return nullptr;
                if (beside && std::find(beside->choices.begin(), beside->choices.end(), name) != beside->choices.end()) {
                    error(n->name.where, "write " + beside->name + "::" + name + "; an enum's choices are named with it",
                          fix{n->name.where, name.size(), beside->name + "::" + name});
                    return nullptr;
                }
                if (in.entity) {
                    if (const field* f = field_or_id(*in.entity, name)) {
                        mean_field(n->name.where, f);
                        return f;
                    }
                }
                if (beside && !beside->choices.empty()) {
                    std::string choices;
                    for (const auto& c : beside->choices) choices += (choices.empty() ? "" : ", ") + beside->name + "::" + c;
                    error(n->name.where, name + " isn't one of " + beside->name + "'s choices, " + choices +
                                             nearest(name, beside->choices));
                    return nullptr;
                }
                std::vector<std::string> known(plain.begin(), plain.end());
                known.insert(known.end(), in.parameters.begin(), in.parameters.end());
                if (in.entity) {
                    auto fields = field_names(*in.entity);
                    known.insert(known.end(), fields.begin(), fields.end());
                }
                error(n->name.where, in.entity ? "entity " + in.entity->name + " has no field " + name + nearest(name, known)
                                               : "there's no " + name + " here" + nearest(name, known));
                return nullptr;
            }
            if (auto* m = std::get_if<member_expression>(&e.node)) {
                const field* read = resolve_member(in, e, *m);
                mean_field({e.where.line, e.where.column + 1}, read);  // the member's name starts after its dot
                return read;
            }
            if (auto* call = std::get_if<call_expression>(&e.node)) {
                called(in, *call);
                return nullptr;
            }
            return resolve_rest(in, e);
        }

        // book.title, loan.book.status, first(loan).book: the field a member reads.
        const field* resolve_member(const context& in, const expression& e, const member_expression& member) {
            const member_expression* m = &member;
            {
                location where{e.where.line, e.where.column};
                snake(m->member, where);
                if (!is_snake_case(m->member)) return nullptr;
                // first(...).member: a field of what first finds.
                if (auto* call = std::get_if<call_expression>(&m->object->node)) {
                    const entity_declaration* found = called(in, *call);
                    if (found && !field_or_id(*found, m->member)) {
                        error(where, "entity " + found->name + " has no field " + m->member + nearest(m->member, field_names(*found)));
                    }
                    return found ? field_or_id(*found, m->member) : nullptr;
                }
                // issue.project.visibility: a field of what a field points at.
                if (std::holds_alternative<member_expression>(m->object->node)) {
                    const field* inner = resolve(in, *m->object);
                    if (!inner) return nullptr;
                    const entity_declaration* through = pointed(in.ns, *inner);
                    if (!through) {
                        error(where, inner->name + " isn't another entity, so it has no fields to read");
                        return nullptr;
                    }
                    const field* f = field_or_id(*through, m->member);
                    if (!f) error(where, "entity " + through->name + " has no field " + m->member + nearest(m->member, field_names(*through)));
                    return f;
                }
                auto* object = std::get_if<name_expression>(&m->object->node);
                if (!object || object->name.parts.size() != 1) {
                    names_in(*m->object);
                    return nullptr;
                }
                const std::string& name = object->name.parts[0];
                const entity_declaration* through = nullptr;
                if (name == "user" && in.reader) {
                    if (m->member != "id") error(where, "the person reading a view is only known by user.id");
                    return nullptr;
                }
                if (in.entity && name == in.entity->name) through = in.entity;            // loan.book, in a where on loan
                else if (in.row && name == in.row->name) through = in.row;                // book.id, the row's own
                else if (in.subject && name == in.subject->name) through = in.subject;    // book.title, the view's own book
                if (through) mean_entity(object->name.where, name.size(), through);
                else if (in.entity) {
                    if (const field* f = field_or_id(*in.entity, name)) {
                        mean_field(object->name.where, f);
                        if (f->type && f->type->text() == "user") return profile_field(in, *f, m->member, where);  // member.name
                        through = pointed(in.ns, *f);                                    // book.status, through a loan's book
                        if (!through) {
                            error(object->name.where, name + " isn't another entity, so it has no fields to read");
                            return nullptr;
                        }
                    }
                }
                if (!through) {
                    std::vector<std::string> known;
                    if (in.entity) known = field_names(*in.entity);
                    error(object->name.where, (in.entity ? "entity " + in.entity->name + " has no field " : "there's no ") + name +
                                                  nearest(name, known));
                    return nullptr;
                }
                const field* f = field_or_id(*through, m->member);
                if (!f) error(where, "entity " + through->name + " has no field " + m->member + nearest(m->member, field_names(*through)));
                return f;
            }
        }

        // Conditions, comparisons and the rest, which read fields without being one.
        const field* resolve_rest(const context& in, const expression& e) {
            if (auto* w = std::get_if<where_expression>(&e.node)) {
                where_of(in, *w);
                return nullptr;
            }
            if (auto* u = std::get_if<unary_expression>(&e.node)) {
                resolve(in, *u->operand);
                return nullptr;
            }
            if (auto* b = std::get_if<binary_expression>(&e.node)) {
                std::size_t errors = out_.size();
                const field* left = resolve(in, *b->left);
                if (b->op == token_kind::has && left && !left->list) {
                    error(b->left->where, "has asks a list, and " + left->name + " isn't one");
                    return nullptr;
                }
                bool compares = b->op == token_kind::equal || b->op == token_kind::not_equal || b->op == token_kind::has;
                // With the left side wrong, what it's compared with can't be judged.
                if (compares && out_.size() > errors) return nullptr;
                resolve(in, *b->right, compares ? left : nullptr);
                return nullptr;
            }
            return nullptr;
        }

        // `loan where ...`: the condition is about the entity named before where, and
        // a row's own id may be compared with it. Returns that entity.
        const entity_declaration* where_of(const context& outer, const where_expression& w) {
            auto* source = std::get_if<name_expression>(&w.source->node);
            const entity_declaration* e = source ? find_entity(outer.ns, source->name) : nullptr;
            if (!e) {
                error(w.source->where, "'where' needs an entity before it, like loan where ...");
                return nullptr;
            }
            context inner{outer.ns, e, outer.row ? outer.row : outer.entity, {}, outer.reader, outer.subject};
            if (w.condition) resolve(inner, *w.condition);
            return e;
        }

        // A call: count and first of an entity, starts_with and drop on text, or a
        // function the file declares. Returns the entity count or first is about.
        const entity_declaration* called(const context& in, const call_expression& call) {
            auto* callee = std::get_if<name_expression>(&call.callee->node);
            std::string name = callee ? callee->name.text() : "";
            auto arguments = [&](std::size_t n) {
                if (call.arguments.size() != n) {
                    error(call.callee->where, name + " takes " + std::to_string(n) + (n == 1 ? " argument" : " arguments") + ", not " +
                                                  std::to_string(call.arguments.size()));
                    return false;
                }
                return true;
            };
            if (name == "count" || name == "first") {
                if (!arguments(1)) return nullptr;
                const auto& argument = *call.arguments[0];
                if (auto* w = std::get_if<where_expression>(&argument.node)) return where_of(in, *w);
                auto* entity = std::get_if<name_expression>(&argument.node);
                const entity_declaration* e = entity ? find_entity(in.ns, entity->name) : nullptr;
                if (!e) error(argument.where, name + " needs an entity, like " + name + "(loan)");
                return e;
            }
            // github_secret(project.id): the secret a project pastes into GitHub for
            // its webhook, shown only in a view per that project.
            if (name == "github_secret") {
                if (!arguments(1)) return nullptr;
                auto* m = std::get_if<member_expression>(&call.arguments[0]->node);
                auto* object = m ? std::get_if<name_expression>(&m->object->node) : nullptr;
                if (!in.subject || !object || object->name.text() != in.subject->name || m->member != "id") {
                    error(call.arguments[0]->where, "github_secret takes the id of the project a view is per, like github_secret(project.id)");
                }
                return nullptr;
            }
            if (name == "starts_with" || name == "drop") {
                if (arguments(2)) {
                    for (const auto& a : call.arguments) resolve(in, *a);
                }
                return nullptr;
            }
            if (const function_declaration* function = callee ? find(in.ns, callee->name, &scope::functions) : nullptr) {
                if (arguments(function->parameters.size())) {
                    for (const auto& a : call.arguments) resolve(in, *a);
                }
                return nullptr;
            }
            std::vector<std::string> known{"count", "first", "starts_with", "drop", "github_secret"};
            for (const auto& candidate : candidates(in.ns, {})) {
                auto s = scopes_.find(candidate);
                if (s == scopes_.end()) continue;
                for (const auto& [function, _] : s->second.functions) known.push_back(function);
            }
            error(call.callee->where, "there's no function " + name + nearest(name, known));
            return nullptr;
        }

        void names_in(const expression& e) {
            std::visit(
                [&](const auto& node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, name_expression>) {
                        snake(node.name);
                    } else if constexpr (std::is_same_v<T, member_expression>) {
                        names_in(*node.object);
                        snake(node.member, location{e.where.line, e.where.column + 1});
                    } else if constexpr (std::is_same_v<T, call_expression>) {
                        names_in(*node.callee);
                        for (const auto& argument : node.arguments) names_in(*argument);
                    } else if constexpr (std::is_same_v<T, where_expression>) {
                        names_in(*node.source);
                        names_in(*node.condition);
                    } else if constexpr (std::is_same_v<T, unary_expression>) {
                        names_in(*node.operand);
                    } else if constexpr (std::is_same_v<T, binary_expression>) {
                        names_in(*node.left);
                        names_in(*node.right);
                    }
                },
                e.node);
        }
    };

    // Checks what a project's files mean. Call it only after every file has parsed
    // without errors, since a half-read file would only produce follow-on errors.
    inline void check(const std::vector<file>& files, diagnostics& out) {
        checker(out).check(files);
    }

    // Checks, and says what each name means where it's written.
    inline void check(const std::vector<file>& files, diagnostics& out, meanings& meant) {
        checker(out, &meant).check(files);
    }

} // namespace one::language
