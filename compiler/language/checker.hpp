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
#include <cmath>
#include <cstdio>
#include <initializer_list>
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
#include "language/library.hpp"
#include "language/parser.hpp"
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

    // What the library says of what's declared on a line of it, from the comments just
    // above, like what a setting is for, or "" when nothing is said.
    inline std::string library_comment(int line) {
        std::vector<std::string_view> lines;
        std::string_view text = library_source;
        for (std::size_t at = 0; at <= text.size();) {
            std::size_t end = text.find('\n', at);
            if (end == std::string_view::npos) end = text.size();
            lines.push_back(text.substr(at, end - at));
            at = end + 1;
        }
        std::string said;
        for (int at = line - 2; at >= 0 && at < static_cast<int>(lines.size()); --at) {
            std::string_view l = lines[static_cast<std::size_t>(at)];
            std::size_t start = l.find_first_not_of(" \t");
            if (start == std::string_view::npos || !l.substr(start).starts_with("//")) break;
            std::string words(l.substr(start + 2));
            if (!words.empty() && words.front() == ' ') words.erase(0, 1);
            said = words + (said.empty() ? "" : " " + said);
        }
        return said;
    }

    class checker {
    public:
        explicit checker(diagnostics& out, meanings* meant = nullptr) : out_(out), meant_(meant) {}

        void check(const std::vector<file>& files) {
            // uione's own library comes first, so what it declares, like the settings a
            // project may say, is there for every file.
            const file& own = library_file();
            std::vector<const file*> all{&own};
            for (const auto& f : files) all.push_back(&f);
            for (const auto* f : all) {
                path_ = f->path;
                place("", f->declarations);
            }
            for (const auto* f : all) {
                path_ = f->path;
                collect("", f->declarations);
            }
            for (const auto* f : all) {
                path_ = f->path;
                give_enums("", f->declarations);
            }
            for (const auto* f : all) {
                path_ = f->path;
                imports_at_ = f->imports_at;
                verify("", f->declarations);
            }
        }

        // uione's own library, read once.
        static const file& library_file() {
            static const file parsed = [] {
                diagnostics ignored;
                return parse(std::string(library_path), library_source, ignored);
            }();
            return parsed;
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
            std::map<std::string, declared<enum_declaration>> enums;
            std::map<std::string, declared<function_declaration>> functions;
            std::map<std::string, origin> commands;  // entity::command
            std::map<std::string, const command_declaration*> command_nodes;  // the same, as declared
            std::vector<const role_declaration*> roles;
            std::vector<const roles_declaration*> defined;  // roles each project defines for itself
            std::map<std::string, origin> role_names;  // so a role is declared once
            std::map<std::string, const settings_declaration*, std::less<>> settings;  // what a block of settings may say, in a library
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
            bool update = false;                         // an update command's body, which may say what it changes
            bool command = false;                        // a command's body, which may ask what a field was and what exists
            // What a command's body makes by name, like create phase { name = "triaged" },
            // by entity: what phase::triaged may name, with a project's starting roles.
            const std::map<std::string, std::set<std::string>>* made = nullptr;
            bool stored = false;                         // a once's, which may name what's already stored, like role::developer
            // A command's inputs, like into in input into phase, named in its body.
            const std::map<std::string, field, std::less<>>* inputs = nullptr;
        };

        diagnostics& out_;
        meanings* meant_ = nullptr;
        std::string path_;
        std::map<std::string, scope> scopes_;  // by namespace, "" is the top level
        std::map<std::string, origin> routes_;
        std::map<const entity_declaration*, entity_declaration> changes_;  // what each history holds
        std::optional<origin> project_;
        const std::vector<screen_item>* screen_ = nullptr;  // the screen being checked, whose tables a form can open from

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
        std::map<std::string, std::set<std::string>> imports_;  // what each file imports, by its path
        location imports_at_;                                    // where the file being verified would say one
        std::map<std::string, std::string> placed_;  // where each namespace's at was first said
        std::set<std::string> conflicted_;
        std::string layout_;  // the project's layout for its screens, when it says one

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

        // Where each namespace is, read before anything else, so a namespace says at
        // once, in any of its files, and its screens in every file are there.
        void place(const std::string& ns, const std::vector<declaration>& declarations) {
            for (const auto& d : declarations) {
                auto* n = std::get_if<namespace_declaration>(&d.node);
                if (!n) continue;
                std::string name = join(ns, n->name);
                if (n->at) {
                    if (!n->at->starts_with("/")) error(d.where, "a namespace is at an address, like / or /docs");
                    std::string prefix = *n->at == "/" ? "" : *n->at;
                    auto [it, added] = prefixes_.emplace(name, prefix);
                    auto [first, placed] = placed_.emplace(name, path_ + ":" + std::to_string(d.where.line));
                    if (!added && it->second != prefix && !conflicted_.contains(name)) {
                        conflicted_.insert(name);
                        error(d.where, "namespace " + n->name + " is at " + (it->second.empty() ? "/" : it->second) + " in " + first->second +
                                           " but at " + *n->at + " here; say where it is once");
                    }
                }
                place(name, n->declarations);
            }
        }

        void collect(const std::string& ns, const std::vector<declaration>& declarations) {
            scope& here = scopes_[ns];
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<namespace_declaration>(&d.node)) {
                    // namespace one is uione's own library.
                    if (ns.empty() && n->name == "one" && path_ != library_path) {
                        error(d.where, "namespace one is uione's own library; name yours something else");
                        continue;
                    }
                    collect(join(ns, n->name), n->declarations);
                } else if (auto* st = std::get_if<settings_declaration>(&d.node)) {
                    here.settings[st->name] = st;
                } else if (auto* im = std::get_if<import_declaration>(&d.node)) {
                    imports_[path_].insert(im->name);
                } else if (auto* e = std::get_if<entity_declaration>(&d.node)) {
                    add(here.entities, e->name, *e, d.where, "entity", ns);
                } else if (auto* v = std::get_if<view_declaration>(&d.node)) {
                    add(here.views, v->name, *v, d.where, "view", ns);
                } else if (auto* f = std::get_if<format_declaration>(&d.node)) {
                    add(here.formats, f->name, *f, d.where, "format", ns);
                } else if (auto* rs = std::get_if<roles_declaration>(&d.node)) {
                    here.defined.push_back(rs);
                } else if (auto* en = std::get_if<enum_declaration>(&d.node)) {
                    add(here.enums, en->name, *en, d.where, "enum", ns);
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
                    here.command_nodes.try_emplace(c->name.text(), c);
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
                } else if (auto* p = std::get_if<project_declaration>(&d.node)) {
                    if (project_) {
                        error(d.where, "a project has one project block; the first is at " + first_seen(*project_));
                    } else {
                        project_ = origin{path_, d.where};
                        for (const auto& setting : p->settings) {
                            if (setting.key == "layout") layout_ = setting.value;
                        }
                    }
                }
            }
        }

        // A field whose type is an enum, like status  status, gets its choices, as if
        // written on it, and the enum's name, which names them: status::open.
        void give_enums(const std::string& ns, const std::vector<declaration>& declarations) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<namespace_declaration>(&d.node)) {
                    give_enums(join(ns, n->name), n->declarations);
                } else if (auto* e = std::get_if<entity_declaration>(&d.node)) {
                    for (const auto& f : e->fields) {
                        if (!f.type || !f.choices.empty()) continue;
                        const enum_declaration* en = find(ns, *f.type, &scope::enums);
                        if (!en) continue;
                        mean(f.type->where, f.type->text().size(), "enum " + f.type->text(), origin_of(en, &scope::enums));
                        if (f.list) {
                            error(f.type->where, "a list of " + en->name + "'s choices isn't supported yet; a field holds one of them");
                            continue;
                        }
                        f.choices = en->choices;
                        f.choice_labels = en->choice_labels;
                        f.enum_name = en->name;
                        f.type.reset();
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
                    kind += (i == 0 ? "" : i + 1 == f->choices.size() ? " or " : ", ") + naming(*f) + "::" + f->choices[i];
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

        void verify(const std::string&, location where, const enum_declaration& e) {
            snake(e.name, where);
            if (e.choices.empty()) error(where, "enum " + e.name + " needs its choices, each with how it's shown, like open \"Open\"");
            for (std::size_t i = 0; i < e.choices.size(); ++i) {
                snake(e.choices[i], e.choice_where[i]);
                if (std::find(e.choices.begin(), e.choices.begin() + static_cast<std::ptrdiff_t>(i), e.choices[i]) != e.choices.begin() + static_cast<std::ptrdiff_t>(i)) {
                    error(e.choice_where[i], e.choices[i] + " is one of " + e.name + "'s choices already");
                }
            }
        }

        // roles role per project from member: role is a project's role, with a field
        // pointing at the project, a name and a title among its keys and fields, and
        // a list of the commands it allows; member points at a project, a person and
        // a role. Each role a project starts with allows commands there are.
        void verify(const std::string& ns, location, const roles_declaration& r) {
            snake(r.entity, r.entity_where);
            const entity_declaration* role = find_entity(ns, qualified_name{{r.entity}, r.entity_where});
            const entity_declaration* scope = find_entity(ns, qualified_name{{r.per}, r.per_where});
            const entity_declaration* member = find_entity(ns, qualified_name{{r.from}, r.per_where});
            if (!role) error(r.entity_where, "a project's roles are records of " + r.entity + ", which isn't an entity " + in_namespace(ns));
            if (!scope) error(r.per_where, "roles belong to " + r.per + ", which isn't an entity " + in_namespace(ns));
            if (!member) error(r.per_where, "roles are given by " + r.from + ", which isn't an entity " + in_namespace(ns));
            if (!role || !scope || !member) return;
            mean_entity(r.entity_where, r.entity.size(), role);
            bool place = false, name = false, title = false;
            int allows = 0;
            for (const auto& f : role->fields) {
                if (f.key && pointed(ns, f) == scope) place = true;
                if (f.name == "name" && f.key) name = true;
                if (f.name == "title") title = true;
                if (f.list && f.type && f.type->text() == "permission") ++allows;
            }
            if (!place) error(r.entity_where, r.entity + " is a role of a " + r.per + ", so it needs a key field pointing at one, like " + r.per + "  " + r.per + "  required  key");
            if (!name) error(r.entity_where, r.entity + " is named within its " + r.per + ", so it needs a key field name, like name  slug  required  key");
            if (!title) error(r.entity_where, r.entity + " is shown by its title, so it needs a field title, like title  text  required");
            if (allows != 1) error(r.entity_where, r.entity + " says what it allows in one list of commands, like may  list of permission");
            bool held_in = false, person = false, given = false;
            for (const auto& f : member->fields) {
                if (pointed(ns, f) == scope) held_in = true;
                if (f.type && f.type->text() == "user") person = true;
                if (pointed(ns, f) == role) given = true;
            }
            if (!held_in || !person || !given) {
                error(r.per_where, r.from + " gives a person a role in a " + r.per + ", so it needs fields pointing at a " + r.per + ", a person and a " +
                                       r.entity + ", like " + r.entity + "  " + r.entity + "  required  key");
            }
            std::set<std::string> seen;
            for (const auto& d : r.defaults) {
                snake(d.name, d.where);
                if (!seen.insert(d.name).second) error(d.where, "every " + r.per + " starts with one " + d.name + "; it's given twice");
                for (const auto& p : d.permissions) {
                    snake(p);
                    // What a project's role allows is running commands, so each is one.
                    auto here = scopes_.find(ns);
                    if (p.parts.size() != 2 || here == scopes_.end() || !here->second.commands.contains(p.text())) {
                        error(p.where, d.name + " allows " + p.text() + ", which isn't a command " + in_namespace(ns));
                    }
                }
            }
        }

        // What create statements make by a name given plainly, by entity, through ifs.
        static void made_by_name(const std::vector<statement>& body, std::map<std::string, std::set<std::string>>& made) {
            for (const auto& st : body) {
                if (auto* c = std::get_if<create_statement>(&st.node)) {
                    for (const auto& v : c->values) {
                        auto* literal = v.value ? std::get_if<literal_expression>(&v.value->node) : nullptr;
                        if (v.name == "name" && literal) made[c->entity].insert(literal->value);
                    }
                } else if (auto* i = std::get_if<if_statement>(&st.node)) {
                    made_by_name(i->then_body, made);
                    made_by_name(i->else_body, made);
                }
            }
        }

        // Whether an entity is named within another by a name, like a project's role or
        // phase: its keys are a field pointing at the other and name.
        bool named_within(const std::string& ns, const entity_declaration& e) {
            int keys = 0;
            bool place = false, name = false;
            for (const auto& f : e.fields) {
                if (!f.key) continue;
                ++keys;
                if (pointed(ns, f)) place = true;
                if (f.name == "name") name = true;
            }
            return keys == 2 && place && name;
        }

        // The roles a field's type names, when its entity is a project's roles, like a
        // member's role: what role::maintainer, a role every project starts with, means.
        const roles_declaration* defined_roles(const std::string& ns, const field& f) {
            if (!f.type) return nullptr;
            for (const auto& space : candidates(ns, {})) {
                auto it = scopes_.find(space);
                if (it == scopes_.end()) continue;
                for (const auto* r : it->second.defined) {
                    if (r->entity == f.type->parts.back()) return r;
                }
            }
            return nullptr;
        }

        void verify(const std::string& ns, location where, const namespace_declaration& n) {
            snake(n.name, where);
            verify(join(ns, n.name), n.declarations);
        }

        // The regions each layout has, in the order it draws them, or none for a layout
        // there isn't.
        static const std::vector<std::string>* regions_of(const std::string& layout) {
            static const std::map<std::string, std::vector<std::string>> layouts{{"single", {"main"}}, {"two_columns", {"main", "side"}}};
            auto found = layouts.find(layout);
            return found == layouts.end() ? nullptr : &found->second;
        }

        static std::string join_words(const std::vector<std::string>& words) {
            std::string out;
            for (std::size_t i = 0; i < words.size(); ++i) out += (i == 0 ? "" : i + 1 == words.size() ? " and " : ", ") + words[i];
            return out;
        }

        // unread news.changes since news.seen: a list of a view per user, and a value of
        // the same view saying when the person last looked.
        void check_unread(const setting& s) {
            auto [view, list] = std::pair{s.value.substr(0, s.value.find('.')), s.value.substr(s.value.find('.') + 1)};
            auto [since_view, since] = std::pair{s.to.substr(0, s.to.find('.')), s.to.substr(s.to.find('.') + 1)};
            const view_declaration* found = nullptr;
            for (const auto& [ns, sc] : scopes_) {
                if (auto it = sc.views.find(view); it != sc.views.end()) found = it->second.node;
            }
            bool listed = found && std::any_of(found->each.begin(), found->each.end(), [&](const view_each& e) { return e.name && *e.name == list; });
            bool held = found && std::any_of(found->values.begin(), found->values.end(), [&](const view_value& v) { return v.name && *v.name == since; });
            if (!found || found->per != std::optional<std::string>{"user"}) {
                error(s.where, "unread counts a list of a view per user, and there's no such view " + view);
            } else if (!listed) {
                error(s.where, "view " + view + " has no list " + list + " to count");
            } else if (since_view != view || !held) {
                error(s.where, "unread counts since a value of view " + view + ", like since " + view + ".seen");
            }
        }

        // What the library says of a setting, like "domain: where it's served".
        static std::string doc_of(int line, const std::string& name) {
            std::string said = library_comment(line);
            return said.empty() ? name : name + ": " + said;
        }

        // How a choice of an enum is shown, like Square.
        static std::string choice_shown(const enum_declaration& e, const std::string& value) {
            for (std::size_t i = 0; i < e.choices.size(); ++i) {
                if (e.choices[i] == value) return i < e.choice_labels.size() && !e.choice_labels[i].empty() ? e.choice_labels[i] : value;
            }
            return value;
        }

        // import one: uione's own library is the one there is.
        void verify(const std::string&, location where, const import_declaration& i) {
            if (i.name != "one") error(where, "there's no library " + i.name + "; uione's own is one, as in import one");
        }

        // What a block of settings may say is declared in a library, like uione's own.
        void verify(const std::string&, location where, const settings_declaration& st) {
            if (path_ != library_path) error(where, "settings " + st.name + " is declared in a library, like uione's own one, which a project imports");
        }

        // How much a color stands out from white, as WCAG measures it: 21 for black, 1
        // for white itself.
        static double contrast_with_white(const std::string& hex) {
            auto channel = [&](std::size_t at) {
                double c = std::stoi(hex.substr(at, 2), nullptr, 16) / 255.0;
                return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            };
            double luminance = 0.2126 * channel(1) + 0.7152 * channel(3) + 0.0722 * channel(5);
            return 1.05 / (luminance + 0.05);
        }

        // Where a project runs goes into generated Go, YAML and shell, so each value has
        // to be the kind of name it says it is, and nothing that could break out of a
        // quote. A project names all three or none, since a deploy needs all of them.
        void verify(const std::string&, location where, const project_declaration& p) {
            auto only = [](const std::string& value, std::string_view allowed) {
                return !value.empty() && value.find_first_not_of(allowed) == std::string::npos;
            };
            static constexpr std::string_view id = "abcdefghijklmnopqrstuvwxyz0123456789-";
            static constexpr std::string_view host = "abcdefghijklmnopqrstuvwxyz0123456789-.";
            // A project's file says it uses uione's own library, which says what a
            // project may say.
            if (!imports_[path_].contains("one")) {
                error(imports_at_, "a project's file says import one, uione's own library, which says what a project may say",
                      fix{imports_at_, 0, "import one\n\n"});
            }
            const scope& library = scopes_["one"];
            auto settings_of = [&](std::string_view name) -> const settings_declaration* {
                auto it = library.settings.find(name);
                return it == library.settings.end() ? nullptr : it->second;
            };
            std::vector<std::string> deploy;
            std::set<std::string> said;  // each setting that isn't a list, said once
            std::set<std::string> listed;  // each value of a list, said once, like signin github
            // A value, checked as its type says: one of an enum's choices, written plainly
            // or in full, or text of a kind, like a version or a domain.
            auto check_value = [&](const setting& s, const field& f, const std::string& value, bool string) {
                std::string type = f.type ? f.type->text() : "text";
                const std::string dir = path_.substr(0, path_.find_last_of('/') == std::string::npos ? 0 : path_.find_last_of('/'));
                if (auto e = library.enums.find(type); e != library.enums.end()) {
                    const auto& choices = e->second.node->choices;
                    bool known = std::find(choices.begin(), choices.end(), value) != choices.end();
                    std::string names;
                    for (std::size_t i = 0; i < choices.size(); ++i) names += (i == 0 ? "" : i + 1 == choices.size() ? " or " : ", ") + choices[i];
                    if (string || !known) {
                        error(s.value_where, s.key + " is " + names + ", written plainly, like " + s.key + " " + choices.front());
                    } else if (!s.qualified.empty() && s.qualified != "one::" + type + "::" + value) {
                        error(s.value_where, s.key + " " + value + " is one::" + type + "::" + value + " in full, or " + value + " plainly");
                    } else {
                        mean(s.value_where, (s.qualified.empty() ? value : s.qualified).size(), s.key + " " + value + ": " + choice_shown(*e->second.node, value),
                             e->second.from, "project");
                    }
                    return;
                }
                if (type == "version" && !std::regex_match(value, std::regex(R"(\d+\.\d+\.\d+)"))) {
                    error(s.where, s.key + " names the compiler's version, like " + s.key + " \"0.7.0\"");
                } else if (type == "slug" && !only(value, id)) {
                    error(s.where, s.key + " has to be lowercase letters, digits and dashes, like ui-one or us-east4");
                } else if (type == "domain" && (!only(value, host) || value.find('.') == std::string::npos)) {
                    error(s.where, s.key + " has to be a domain name, like uione.io");
                } else if (type == "color") {
                    if (!std::regex_match(value, std::regex("#[0-9a-fA-F]{6}"))) {
                        error(s.where, s.key + " is written #rrggbb, like " + s.key + " \"#0f766e\"");
                    } else if (double contrast = contrast_with_white(value); contrast < 4.5) {
                        char shown[8];
                        std::snprintf(shown, sizeof shown, "%.1f", contrast);
                        error(s.where, s.key + " " + value + " is too light to read as a link on a white page (" + shown + ":1, and it needs 4.5:1); choose a darker one");
                    }
                } else if (type == "folder" && !std::filesystem::is_directory(platform::resolve(dir.empty() ? "." : dir, value))) {
                    error(s.where, "there's no folder " + value + " to " + s.key + "; it's looked for next to this .one file");
                } else if (type == "file" && !platform::read_file(platform::resolve(dir.empty() ? "." : dir, value))) {
                    error(s.where, "there's no " + s.key + " file at " + value + "; it's looked for next to this .one file");
                } else if (type == "address" && (!value.starts_with("/") || value.find_first_of("\" \\") != std::string::npos)) {
                    error(s.where, s.key + " takes an address of this site, like \"/install.sh\"");
                } else if (type == "link" && (!(value.starts_with("https://") || (s.key == "redirect" && value.starts_with("/"))) ||
                                              value.find_first_of("\" \\") != std::string::npos)) {
                    error(s.where, s.key + "'s " + f.name + " is an https address, like \"https://www.uione.io\"");
                }
            };
            auto check_setting = [&](const setting& s, const settings_declaration& in, std::vector<std::string>& where_it_runs) {
                // How people sign in was called authentication for a while.
                if (s.key == "authentication") {
                    error(s.where, "authentication is called signin, like signin " + s.value, fix{s.where, s.key.size(), "signin"});
                    return;
                }
                const field* f = nullptr;
                for (const auto& candidate : in.fields) {
                    if (candidate.name == s.key) f = &candidate;
                }
                if (!f) {
                    std::string names;
                    for (std::size_t i = 0; i < in.fields.size(); ++i) {
                        names += (i == 0 ? "" : i + 1 == in.fields.size() ? " or " : ", ") + in.fields[i].name;
                    }
                    error(s.where, "'" + s.key + "' isn't a setting " + (in.name == "project" ? "of a project" : "of an environment") + "; it says " + names);
                    return;
                }
                mean(s.where, s.key.size(), doc_of(f->where.line, s.key), origin{std::string(library_path), f->where}, "project");
                if (s.key == "firebase" || s.key == "region" || s.key == "domain") where_it_runs.push_back(s.key);
                if (!f->list && !said.insert(s.key).second) {
                    error(s.where, s.key + " is said once");
                }
                if (f->list && !listed.insert(s.key + " " + s.value).second) {
                    error(s.where, s.key + " " + s.value + " is named twice");
                }
                std::string type = f->type ? f->type->text() : "text";
                // A setting of several values, like copyright's name and link, in their order.
                if (const settings_declaration* parts = settings_of(type)) {
                    if (type == "unread") {
                        check_unread(s);
                        return;
                    }
                    const field* second = parts->fields.size() > 1 ? &parts->fields[1] : nullptr;
                    if (!parts->fields.empty()) check_value(s, parts->fields[0], s.value, s.is_string);
                    if (second && !s.to.empty()) check_value(s, *second, s.to, true);
                    if (type == "redirect" && s.to.empty()) {
                        error(s.where, "redirect takes an address of this site and where it goes, like redirect \"/install.sh\" \"https://www.uione.io/install.sh\"");
                    }
                    return;
                }
                if (!s.to.empty()) error(s.where, s.key + " takes one value");
                // The icon is an SVG, so it's sharp at any size.
                if (s.key == "icon" && !s.value.ends_with(".svg")) {
                    error(s.where, "icon has to be an .svg file, like \"assets/icon.svg\"");
                    return;
                }
                check_value(s, *f, s.value, s.is_string);
            };
            const settings_declaration* project_settings = settings_of("project");
            const settings_declaration* environment_settings = settings_of("environment");
            if (!project_settings || !environment_settings) return;  // the library is uione's own, and has them
            for (const auto& s : p.settings) check_setting(s, *project_settings, deploy);
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
                said.clear();
                for (const auto& s : environment.settings) {
                    bool own = std::any_of(environment_settings->fields.begin(), environment_settings->fields.end(), [&](const field& f) { return f.name == s.key; });
                    if (!own) {
                        error(s.where, s.key + " is the same in every environment, so it goes outside them; an environment has its own domain, firebase and region");
                        continue;
                    }
                    check_setting(s, *environment_settings, runs);
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
            // entity invitation invites member: one email to match, the member's one
            // person to fill, and everything else the member needs, by the same name.
            if (e.invites) {
                const entity_declaration* made = find_entity(ns, qualified_name{{*e.invites}, e.invites_where});
                std::vector<const field*> emails, people;
                for (const auto& f : e.fields) {
                    if (f.type && f.type->text() == "email" && !f.list) emails.push_back(&f);
                }
                if (made) {
                    for (const auto& f : made->fields) {
                        if (f.type && f.type->text() == "user" && !f.list && !f.initial) people.push_back(&f);
                    }
                }
                if (!made) {
                    error(e.invites_where, "there's no entity " + *e.invites + " " + in_namespace(ns) + " for " + e.name + " to invite to be");
                } else if (emails.size() != 1) {
                    error(e.invites_where, "an invitation is to one email, so " + e.name + " has one email field, like email  email  required");
                } else if (people.size() != 1) {
                    error(e.invites_where, "a " + made->name + " made from an invitation is for one person, so " + made->name + " has one user field that doesn't start as a value, like person  user  required");
                } else if (find_field(e, people[0]->name)) {
                    error(e.invites_where, e.name + " doesn't say who " + people[0]->name + " is; whoever signs in with its email is");
                } else {
                    for (const auto& f : made->fields) {
                        if (&f == people[0] || !f.required || f.initial) continue;
                        const field* same = find_field(e, f.name);
                        if (!same) {
                            error(e.invites_where, "a " + made->name + " needs its " + f.name + ", so " + e.name + " has it too, like " + f.name + "  " +
                                                       (f.type ? f.type->text() : "text"));
                        } else if (!same->type || !f.type || same->type->text() != f.type->text() || same->list != f.list) {
                            error(same->where, e.name + "." + f.name + " becomes " + made->name + "." + f.name + ", so it's the same kind");
                        }
                    }
                }
            }
            // entity comment history of issue: the issue keeps its history, and the
            // comment points at it.
            if (e.history_of) {
                const entity_declaration* parent = find_entity(ns, qualified_name{{*e.history_of}, e.history_of_where});
                bool points = std::any_of(e.fields.begin(), e.fields.end(), [&](const field& f) { return f.type && f.type->text() == *e.history_of && !f.list; });
                if (!parent) {
                    error(e.history_of_where, "there's no entity " + *e.history_of + " " + in_namespace(ns) + " to keep " + e.name + "'s changes");
                } else if (!parent->history) {
                    error(e.history_of_where, "entity " + *e.history_of + " keeps no history to keep " + e.name + "'s in; say entity " + *e.history_of + " history");
                } else if (!points) {
                    error(e.history_of_where, "a " + e.name + " is kept in its " + *e.history_of + "'s history, so it points at one, like " + *e.history_of + "  " + *e.history_of + "  required");
                }
            }
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
                if (f.type && f.type->text() == "permission" && !f.list) {
                    error(f.where, "a role allows several commands, so " + f.name + " is a list of them, like " + f.name + "  list of permission");
                }
                if (f.per) verify_serial(ns, e, f);
                if (f.list) verify_list(ns, f);
                if (f.initial && is_serial(f)) {
                    error(f.initial->where, f.name + " is counted, so it doesn't start with a value");
                }
                if (f.after && !find_field(e, *f.after)) {
                    error(f.where, "'after " + *f.after + "' names a field entity " + e.name + " doesn't have");
                }
                // slug(title): a name made from another of its fields when none is
                // given, like a phase's key from its title.
                if (auto* call = f.initial ? std::get_if<call_expression>(&f.initial->node) : nullptr) {
                    auto* callee = std::get_if<name_expression>(&call->callee->node);
                    auto* from = call->arguments.size() == 1 ? std::get_if<name_expression>(&call->arguments[0]->node) : nullptr;
                    const field* source = from && from->name.parts.size() == 1 ? find_field(e, from->name.parts[0]) : nullptr;
                    if (callee && callee->name.text() == "mentions" && from) {
                        // mentions(body): the people its text names with @username.
                        if (!source || !source->type || (source->type->text() != "text" && source->type->text() != "markdown")) {
                            error(f.initial->where, "mentions finds people named in a text or markdown field of entity " + e.name + ", like mentions(body)");
                        } else if (!f.list || !f.type || f.type->text() != "user") {
                            error(f.initial->where, f.name + " holds the people " + source->name + " mentions, so it's a list of user");
                        } else {
                            mean_field(from->name.where, source);
                        }
                    } else if (!callee || callee->name.text() != "slug" || !from) {
                        error(f.initial->where, "a field starts as a value, me, me.username, now, a name made from another field, like slug(title), or the people a field mentions, like mentions(body)");
                    } else if (!source || !source->type || source->type->text() != "text") {
                        error(f.initial->where, "slug makes a name from a text field of entity " + e.name + ", like slug(title)");
                    } else if (!f.type || (f.type->text() != "text" && f.type->text() != "slug")) {
                        error(f.initial->where, f.name + " starts as a name made from " + source->name + ", so it's text");
                    } else {
                        mean_field(from->name.where, source);
                    }
                } else if (f.initial) {
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
                            error(f.initial->where, "write " + naming(f) + "::" + start->name.parts[0] + "; an enum's choices are named with it",
                                  fix{start->name.where, start->name.parts[0].size(), naming(f) + "::" + start->name.parts[0]});
                        } else {
                            error(f.initial->where, "field " + f.name + " has to start as one of its choices, like " + naming(f) + "::" + f.choices[0]);
                        }
                    }
                }
            }
        }

        // labels  list of label: a list holds text, people, or entities' ids. A list
        // isn't a key, a serial, unique, or after anything.
        void verify_list(const std::string& ns, const field& f) {
            const std::string held = f.type ? f.type->text() : "";
            if (held != "text" && held != "user" && held != "permission" && !(f.type && find_entity(ns, *f.type))) {
                error(f.where, f.name + " is a list, which holds text, people or entities, like list of label or list of user");
            }
            // A list can be the people a field mentions, like mentions(body), and nothing
            // else worked out.
            auto* call = f.initial ? std::get_if<call_expression>(&f.initial->node) : nullptr;
            auto* callee = call ? std::get_if<name_expression>(&call->callee->node) : nullptr;
            bool mentions = callee && callee->name.text() == "mentions";
            if (f.key || f.unique || f.after || (f.initial && !mentions)) {
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
            static const std::set<std::string, std::less<>> built_in{"text", "markdown", "email", "slug", "date", "number", "serial", "boolean", "user", "permission"};
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
            context in{ns, entity, nullptr, {}, false};
            in.update = c.name.parts.back() != "create" && c.name.parts.back() != "delete";
            in.command = true;
            std::map<std::string, std::set<std::string>> made;
            made_by_name(c.body, made);
            in.made = &made;
            std::map<std::string, field, std::less<>> inputs = inputs_of(c);
            in.inputs = &inputs;
            statements(ns, c.body, entity, in);
        }

        // What a command is sent besides its entity's fields: input into phase.
        static std::map<std::string, field, std::less<>> inputs_of(const command_declaration& c) {
            std::map<std::string, field, std::less<>> inputs;
            for (const auto& st : c.body) {
                if (auto* i = std::get_if<input_statement>(&st.node)) {
                    field f;
                    f.name = i->name;
                    f.where = i->name_where;
                    f.type = i->type;
                    inputs.emplace(i->name, std::move(f));
                }
            }
            return inputs;
        }

        // each issue where phase == id, or delete each step where from == id || to ==
        // id: a field of what's picked, its value worked out where the command runs,
        // and for a delete, any of several.
        void verify_pick(const entity_declaration& picked, const expression& where, const context& in, bool any) {
            auto* b = std::get_if<binary_expression>(&where.node);
            if (any && b && b->op == token_kind::logical_or) {
                verify_pick(picked, *b->left, in, any);
                verify_pick(picked, *b->right, in, any);
                return;
            }
            auto* name = b ? std::get_if<name_expression>(&b->left->node) : nullptr;
            if (!b || b->op != token_kind::equal || !name || name->name.parts.size() != 1) {
                error(where.where, std::string("what's picked is by a field's value, like where phase == id") +
                                       (any ? ", or by any of several, like from == id || to == id" : ""));
                return;
            }
            const field* f = field_or_id(picked, name->name.parts[0]);
            if (!f) {
                error(name->name.where, "entity " + picked.name + " has no field " + name->name.parts[0] + nearest(name->name.parts[0], field_names(picked)));
                return;
            }
            mean_field(name->name.where, f);
            resolve(in, *b->right, f);
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
                } else if (auto* c = std::get_if<changes_statement>(&s.node)) {
                    if (!in.update) {
                        error(s.where, "changes goes in a command that changes what's there, like an update or a move, naming the fields it takes");
                    }
                    for (const auto& name : c->fields) {
                        snake(name, s.where);
                        const field* f = entity ? find_field(*entity, name) : nullptr;
                        if (entity && !f) error(s.where, "'changes " + name + "' names a field entity " + entity->name + " doesn't have");
                    }
                } else if (auto* a = std::get_if<assign_statement>(&s.node)) {
                    std::size_t errors = out_.size();
                    const field* target = resolve(in, *a->target);
                    // A once may give what was made before a key existed its key, like the board
                    // a project's phases were in before it had boards.
                    if (target && fixed_once_made(*target) && !in.stored) error(a->target->where, why_fixed(*target));
                    if (target && target->list && !std::holds_alternative<list_expression>(a->value->node)) {
                        error(a->target->where, target->name + " is a list; add to it or remove from it, like add me to " + target->name +
                                                    ", or give it a whole list, like [a, b]");
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
                } else if (auto* input = std::get_if<input_statement>(&s.node)) {
                    snake(input->name, input->name_where);
                    if (!in.command || in.stored) {
                        error(s.where, "input goes in a command, naming what it's sent besides its entity's fields, like input into phase");
                    } else if (entity && find_field(*entity, input->name)) {
                        error(input->name_where, input->name + " is a field of entity " + entity->name + " already; an input is something else it's sent");
                    } else {
                        verify_type(ns, input->type);
                    }
                } else if (auto* each = std::get_if<each_statement>(&s.node)) {
                    snake(each->entity, each->entity_where);
                    const entity_declaration* picked = in.command && !in.stored ? find_entity(ns, qualified_name{{each->entity}, each->entity_where}) : nullptr;
                    if (!in.command || in.stored) {
                        error(s.where, "each in a body goes in a command; a once's steps are each already");
                    } else if (!picked) {
                        error(each->entity_where, "there's no entity " + each->entity + " " + in_namespace(ns));
                    } else {
                        mean_entity(each->entity_where, each->entity.size(), picked);
                        verify_pick(*picked, *each->where, in, false);
                        context inner = in;
                        inner.entity = picked;
                        inner.update = true;
                        statements(ns, each->body, picked, inner);
                    }
                } else if (auto* gone = std::get_if<delete_statement>(&s.node)) {
                    snake(gone->entity, gone->entity_where);
                    const entity_declaration* picked = find_entity(ns, qualified_name{{gone->entity}, gone->entity_where});
                    if (!in.command) {
                        error(s.where, "delete each goes in a command, which changes things");
                    } else if (!picked) {
                        error(gone->entity_where, "there's no entity " + gone->entity + " " + in_namespace(ns));
                    } else {
                        mean_entity(gone->entity_where, gone->entity.size(), picked);
                        verify_pick(*picked, *gone->where, in, true);
                    }
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
            // board::create { ... } runs that command's body too, so it's a create that's declared.
            if (c.command) {
                if (c.command->parts.size() != 2 || c.command->parts[1] != "create") {
                    error(c.command->where, c.command->text() + " isn't a create; a command run here makes something, like " + c.entity + "::create");
                } else {
                    verify_command_use(ns, *c.command);
                }
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

        // Who may do something: anyone, signed_in, owner, or a permission named on an
        // entity, like book::view. owner only makes sense for a command whose entity
        // has an owner field. For a while signed_in was called authenticated.
        void verify_permission(const std::string& ns, const qualified_name& p, const entity_declaration* entity) {
            snake(p);
            if (p.parts.size() == 1) {
                const auto& word = p.parts[0];
                static const std::map<std::string, std::string, std::less<>> says{
                    {"anyone", "anyone, signed in or not"}, {"signed_in", "anyone signed in"},
                    {"owner", "the person in the entity's owner field"}};
                if (auto it = says.find(word); it != says.end()) {
                    mean(p.where, word.size(), "built-in permission " + word + ": " + it->second, std::nullopt, "command");
                }
                if (word == "authenticated") {
                    error(p.where, "authenticated is called signed_in", fix{p.where, word.size(), "signed_in"});
                    return;
                }
                if (word == "anyone" || word == "signed_in") return;
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
                error(p.where, "'" + word + "' isn't a permission; use anyone, signed_in, owner, or one "
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
                    for (const auto* r : here->second.defined) {
                        if (r->from == *v.readers) scope = find_entity(ns, qualified_name{{r->per}, v.readers_where});
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
            // A change can come from something kept in this one's history, like a
            // comment on an issue, and holds what that points at too, like the people
            // it mentions.
            for (const auto& [_, sc] : scopes_) {
                for (const auto& [name, child] : sc.entities) {
                    if (!child.node->history_of || *child.node->history_of != e.name) continue;
                    for (const auto& f : child.node->fields) {
                        bool taken = std::any_of(change.fields.begin(), change.fields.end(), [&](const field& c) { return c.name == f.name; });
                        if (taken || !f.type || (f.type->text() != "user" && !pointed(ns, f))) continue;
                        field copy;
                        copy.name = f.name;
                        copy.type = f.type;
                        copy.list = f.list;
                        change.fields.push_back(std::move(copy));
                    }
                }
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
            // Its layout, and the regions it puts its items in: every item in one, once
            // there are any, and each region one its layout has, once.
            std::string layout = s.layout ? *s.layout : layout_.empty() ? "single" : layout_;
            const std::vector<std::string>* regions = regions_of(layout);
            if (s.layout && !regions) error(s.layout_where, "layout is single or two_columns");
            std::set<std::string> placed;
            bool any_region = false;
            for (const auto& item : s.items) {
                auto* block = std::get_if<content_block>(&item.node);
                if (!block || block->type != content_block::kind::region) continue;
                any_region = true;
                if (regions && std::find(regions->begin(), regions->end(), block->title) == regions->end()) {
                    error(item.where, "layout " + layout + " has no region " + block->title + "; its regions are " + join_words(*regions));
                } else if (!placed.insert(block->title).second) {
                    error(item.where, "region " + block->title + " is here twice; put everything in it in one block");
                }
            }
            if (any_region) {
                for (const auto& item : s.items) {
                    auto* block = std::get_if<content_block>(&item.node);
                    // A heading's buttons and a subtitle's words go under the screen's
                    // title, whatever its layout, so they're outside its regions.
                    auto* text = std::get_if<content_text>(&item.node);
                    if (text && text->subtitle) continue;
                    if (!block || (block->type != content_block::kind::region && block->type != content_block::kind::heading)) {
                        error(item.where, "this screen puts its items in regions, so this goes in one too, like main { ... }");
                    }
                }
            }
            screen_ = &s.items;
            screen_items(ns, s.items, full_route(ns, s.route));
            screen_ = nullptr;
            // A title can show what the page does: screen "#{issue_page.number} {issue_page.title}".
            if (!s.title_is_name) verify_live_text(ns, s.title, full_route(ns, s.route), where);
            if (s.under) verify_under(ns, s);
        }

        // under /:project/boards/:board "{issue_page.board_title}": a page there, what
        // it's called read like a title, and each of its parameters this address
        // doesn't have held by a view per entity this one does, like issue_page.board.
        void verify_under(const std::string& ns, const screen_declaration& s) {
            std::string route = full_route(ns, s.route);
            std::string above = full_route(ns, *s.under);
            if (!routes_.contains(above)) {
                error(s.under_where, "there's no screen at " + above + " for this one to be under");
                return;
            }
            verify_live_text(ns, s.under_title, route, s.under_where);
            for (std::size_t at = above.find("/:"); at != std::string::npos; at = above.find("/:", at + 1)) {
                std::size_t end = above.find('/', at + 1);
                std::string name = above.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2);
                if (route.find("/:" + name) != std::string::npos) continue;
                bool held = false;
                if (auto scope = scopes_.find(ns); scope != scopes_.end()) {
                    for (const auto& [_, view] : scope->second.views) {
                        if (!view.node->per || route.find("/:" + *view.node->per) == std::string::npos) continue;
                        for (const auto& value : view.node->values) held = held || (value.name && *value.name == name);
                    }
                }
                if (!held) {
                    error(s.under_where, "this screen's address has no :" + name + ", so a view per what it shows holds it, like " + name + " = issue." + name);
                }
            }
        }

        // A view per entity is shown for one entity at a time, the one the screen's
        // address names, so the screen needs that entity as a parameter of its route.
        // A thread or a timeline shows one of a view's lists, whose rows hold what it
        // needs: a thread who wrote each and what, a timeline each change of an entity.
        void verify_listing(const std::string& ns, const qualified_name& name, const std::string& list_name, const std::string& route, bool changes,
                            std::initializer_list<std::string_view> needs) {
            snake(name);
            const view_declaration* view = find(ns, name, &scope::views);
            if (!view) {
                error(name.where, "there's no view " + name.text() + " " + in_namespace(ns));
                return;
            }
            verify_shown(*view, name.text(), route, name.where);
            const view_each* list = nullptr;
            for (const auto& each : view->each) {
                if (each.name && *each.name == list_name) list = &each;
            }
            if (!list) {
                error(name.where, "view " + name.text() + " has no list called " + list_name);
                return;
            }
            if (changes && !list->changes) {
                error(name.where, "a timeline shows an entity's changes, so " + list_name + " is each change of an entity, like each change of issue");
                return;
            }
            for (auto need : needs) {
                bool has = false;
                for (const auto& row : list->rows) {
                    if ((row.name ? *row.name : written(*row.value)) == need) has = true;
                }
                if (!has) {
                    error(name.where, std::string(changes ? "a timeline" : "a thread") + " needs " + std::string(need) + " in each of " + list_name + "'s rows");
                }
            }
        }

        // When a button shows: what the page's views say, compared with values, like
        // issue_page.status == status::open, and joined with && and ||. me is whoever
        // is reading, as in issue_page.assignees has me.
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
                if (name->name.parts.size() != 2 && word != "true" && word != "false" && word != "none" && word != "me") error(e.where, how);
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

        // issue::move along project_page.steps: the command changes one field, like
        // phase, and each step in the list goes from one of those to another, so its
        // rows hold from and to.
        void verify_along(const std::string& ns, const button_item& button, const std::string& route) {
            const command_declaration* command = nullptr;
            if (auto here = scopes_.find(ns); here != scopes_.end()) {
                if (auto it = here->second.command_nodes.find(button.command.text()); it != here->second.command_nodes.end()) command = it->second;
            }
            if (!command) return;  // verify_command_use says so
            std::vector<std::string> changed;
            for (const auto& st : command->body) {
                if (auto* c = std::get_if<changes_statement>(&st.node)) changed.insert(changed.end(), c->fields.begin(), c->fields.end());
            }
            if (changed.size() != 1) {
                error(button.command.where, button.command.text() + " goes along steps, so it changes one field, the one they go between, like changes phase");
            }
            const view_declaration* view = find(ns, *button.along, &scope::views);
            if (!view) {
                error(button.along->where, "there's no view " + button.along->text() + " for its steps " + in_namespace(ns));
                return;
            }
            verify_shown(*view, button.along->text(), route, button.along->where);
            for (const auto& each : view->each) {
                if (!each.name || *each.name != button.along_list) continue;
                bool from = false, to = false;
                for (const auto& row : each.rows) {
                    std::string column = row.name ? *row.name : written(*row.value);
                    from = from || column == "from";
                    to = to || column == "to";
                }
                if (!from || !to) {
                    error(button.along->where, "view " + button.along->text() + "'s " + button.along_list + " are steps, so each row holds from and to");
                }
                return;
            }
            error(button.along->where, "view " + button.along->text() + " has no list called " + button.along_list);
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
        // The entity a view's list is of, like issue in each issue where ...
        const entity_declaration* listed_entity(const std::string& ns, const view_each& each) {
            auto* source = std::get_if<name_expression>(&each.source->node);
            return source ? find_entity(ns, source->name) : nullptr;
        }

        void verify_column(const std::string& ns, const view_each& each, const std::string& view_name,
                           const table_column& column) {
            std::string key = written(*column.value);
            if (key.empty()) return;
            // issue::create "New issue": a button in the table's toolbar.
            if (auto* named = std::get_if<name_expression>(&column.value->node); named && named->name.parts.size() == 2) {
                verify_command_use(ns, named->name);
                return;
            }
            if (auto* source = std::get_if<name_expression>(&each.source->node); source && key.find('.') == std::string::npos) {
                qualified_name command{{source->name.parts.back(), key}, column.where};
                if (find_command(ns, command)) {
                    if (column.when) verify_row_condition(each, view_name, *column.when);
                    return;
                }
            }
            if (column.when) error(column.when->where, "only a row's button has a when, like delete \"Remove\" when person != me");
            if (!in_rows(each, key)) {
                std::string where = each.name ? *each.name : "rows";
                error(column.where, "view " + view_name + " has no " + key + " in its " + where + "; add it to the list's block");
            }
        }

        bool in_rows(const view_each& each, const std::string& key) {
            for (const auto& row : each.rows) {
                if ((row.name && *row.name == key) || (!row.name && written(*row.value) == key)) return true;
            }
            return false;
        }

        // A row's button's when reads that row: the fields its list holds, compared
        // with values, like person != me or role == role::maintainer.
        void verify_row_condition(const view_each& each, const std::string& view_name, const expression& e) {
            static const std::string how = "a row's when compares the row's fields with values, like person != me";
            if (auto* binary = std::get_if<binary_expression>(&e.node)) {
                verify_row_condition(each, view_name, *binary->left);
                verify_row_condition(each, view_name, *binary->right);
            } else if (auto* unary = std::get_if<unary_expression>(&e.node); unary && unary->op == token_kind::logical_not) {
                verify_row_condition(each, view_name, *unary->operand);
            } else if (std::holds_alternative<member_expression>(e.node) ||
                       (std::holds_alternative<name_expression>(e.node) && std::get<name_expression>(e.node).name.parts.size() == 1)) {
                std::string key = written(e);
                static const std::set<std::string> values{"true", "false", "none", "me"};
                if (values.contains(key)) return;
                if (!in_rows(each, key)) {
                    std::string where = each.name ? *each.name : "rows";
                    error(e.where, "view " + view_name + " has no " + key + " in its " + where + " for the row's when to read; add it to the list's block");
                }
            } else if (auto* name = std::get_if<name_expression>(&e.node); !(name && name->name.parts.size() == 2) && !std::holds_alternative<literal_expression>(e.node)) {
                error(e.where, how);
            }
        }

        // The list of a table on the screen being checked whose rows each have a button
        // for a command, like phase::update "Rename" on the phases: a form for it opens
        // from the row and starts from it. With its view's name.
        std::pair<const view_each*, std::string> rows_with(const std::string& ns, const std::vector<screen_item>& items, const qualified_name& command) {
            for (const auto& item : items) {
                if (auto* block = std::get_if<content_block>(&item.node)) {
                    if (auto found = rows_with(ns, block->items, command); found.first) return found;
                }
                // A grid's cell opens its entity's update, started from the entry.
                if (auto* grid = std::get_if<grid_item>(&item.node); grid && command.parts.size() == 2 && command.parts[1] == "update") {
                    if (const view_declaration* view = find(ns, grid->view, &scope::views)) {
                        for (const auto& each : view->each) {
                            auto* source = std::get_if<name_expression>(&each.source->node);
                            if (each.name == grid->list && source && source->name.parts.back() == command.parts[0]) return {&each, grid->view.text()};
                        }
                    }
                }
                auto* table = std::get_if<table_item>(&item.node);
                const view_declaration* view = table ? find(ns, table->view, &scope::views) : nullptr;
                if (!view) continue;
                for (const auto& each : view->each) {
                    if (each.name != table->list) continue;
                    auto* source = std::get_if<name_expression>(&each.source->node);
                    if (!source || command.parts.size() != 2 || source->name.parts.back() != command.parts[0]) continue;
                    for (const auto& column : table->columns) {
                        if (written(*column.value) == command.parts[1]) return {&each, table->view.text()};
                    }
                }
            }
            return {nullptr, ""};
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

        // The icons a button or a link can be drawn as; one they don't have would show
        // nothing but its words, so a misspelled one is an error.
        void icon(const std::optional<std::string>& name, location where) {
            static const std::set<std::string, std::less<>> known{"edit", "add", "follow", "following", "workflow"};
            if (name && !known.contains(*name)) error(where, "there's no icon " + *name + "; there are " + join_words({known.begin(), known.end()}));
        }

        // The colors a section or a filter can be drawn in, the library's hues, which
        // each component set draws in its own shade, light and dark.
        void hue(const std::string& name, location where) {
            if (name.empty()) return;
            const scope& library = scopes_["one"];
            auto e = library.enums.find("hue");
            if (e == library.enums.end()) return;
            const auto& choices = e->second.node->choices;
            if (std::find(choices.begin(), choices.end(), name) == choices.end()) {
                error(where, "there's no color " + name + "; there are " + join_words(choices));
                return;
            }
            mean(where, name.size(), "color " + name + ": " + choice_shown(*e->second.node, name), e->second.from, "screen");
        }

        void screen_items(const std::string& ns, const std::vector<screen_item>& items, const std::string& route) {
            for (const auto& item : items) {
                if (auto* b = std::get_if<button_item>(&item.node)) icon(b->icon, b->icon_where);
                if (auto* block = std::get_if<content_block>(&item.node)) hue(block->hue, block->hue_where);
                if (auto* l = std::get_if<content_link>(&item.node)) icon(l->icon, l->icon_where);
                if (auto* t = std::get_if<table_item>(&item.node)) {
                    for (const auto& c : t->columns) icon(c.icon, c.icon_where);
                }
                if (auto* bo = std::get_if<board_item>(&item.node)) {
                    for (const auto& c : bo->columns) icon(c.icon, c.icon_where);
                }
                if (auto* block = std::get_if<content_block>(&item.node)) {
                    if (block->type == content_block::kind::menu) {
                        for (const auto& inside : block->items) {
                            if (!std::holds_alternative<content_link>(inside.node)) {
                                error(inside.where, "a menu holds links, like link /settings \"General\"");
                            }
                        }
                    }
                    screen_items(ns, block->items, route);
                } else if (auto* box = std::get_if<find_item>(&item.node)) {
                    for (const auto& source : box->sources) {
                        const view_each* list = named_list(ns, source.view, source.list, route);
                        if (!list) continue;
                        for (const auto& name : source.by) {
                            if (!in_rows(*list, name)) error(source.where, "view " + source.view.text() + " has no " + name + " in its " + source.list + " to find by; add it to the list's block");
                        }
                        if (source.link && !routes_.contains(full_route(ns, *source.link))) {
                            error(source.link_where, "there's no screen at " + full_route(ns, *source.link) + " for what's found to open");
                        }
                    }
                } else if (auto* details = std::get_if<details_item>(&item.node)) {
                    snake(details->view);
                    const view_declaration* view = find(ns, details->view, &scope::views);
                    if (!view) {
                        error(details->view.where, "there's no view " + details->view.text() + " for these details " + in_namespace(ns));
                    } else {
                        verify_shown(*view, details->view.text(), route, details->view.where);
                        for (const auto& f : details->fields) {
                            std::string key = written(*f.value);
                            bool has = false;
                            for (const auto& v : view->values) has = has || (v.name ? *v.name : written(*v.value)) == key;
                            if (!has) error(f.where, "view " + details->view.text() + " has no " + key + " to show");
                        }
                        if (details->tint && std::none_of(view->values.begin(), view->values.end(), [&](const view_value& v) { return (v.name ? *v.name : written(*v.value)) == *details->tint; })) {
                            error(details->tint_where, "view " + details->view.text() + " has no " + *details->tint + " to tint by");
                        }
                    }
                } else if (auto* copy = std::get_if<copy_item>(&item.node)) {
                    snake(copy->view);
                    if (const view_declaration* view = find(ns, copy->view, &scope::views)) {
                        verify_shown(*view, copy->view.text(), route, copy->view.where);
                    } else {
                        error(copy->view.where, "there's no view " + copy->view.text() + " to copy " + in_namespace(ns));
                    }
                } else if (auto* thread = std::get_if<thread_item>(&item.node)) {
                    verify_listing(ns, thread->view, thread->list, route, false, {"body", "author.name"});
                } else if (auto* timeline = std::get_if<timeline_item>(&item.node)) {
                    verify_listing(ns, timeline->view, timeline->list, route, true, {"field", "before", "after", "created_at"});
                    if (timeline->link && !routes_.contains(full_route(ns, *timeline->link))) {
                        error(timeline->link_where, "there's no screen at " + full_route(ns, *timeline->link) + " for this timeline's changes to open");
                    }
                    if (timeline->since) {
                        const view_declaration* view = find(ns, *timeline->since, &scope::views);
                        bool held = view && std::any_of(view->values.begin(), view->values.end(), [&](const view_value& v) { return v.name && *v.name == timeline->since_field; });
                        if (!held) error(timeline->since_where, "view " + timeline->since->text() + " has no value " + timeline->since_field + " saying when the person last looked");
                    }
                    if (timeline->seen && !find_command(ns, *timeline->seen)) {
                        error(timeline->seen_where, "there's no command " + timeline->seen->text() + " " + in_namespace(ns));
                    }
                    if (timeline->seen && !timeline->since) error(timeline->seen_where, "a timeline runs seen once its new changes are looked at; say since when, like new since news.seen");
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
                    // What the table searches and sorts by is in its rows.
                    auto in_rows = [&](const std::string& name) {
                        if (!list) return true;
                        for (const auto& row : list->rows) {
                            if ((row.name ? *row.name : written(*row.value)) == name) return true;
                        }
                        return false;
                    };
                    for (const auto& name : table->search) {
                        if (!in_rows(name)) error(table->search_where, "the table searches " + name + ", which its rows don't have");
                    }
                    if (table->sort && !in_rows(*table->sort)) error(table->sort_where, "the table is sorted by " + *table->sort + ", which its rows don't have");
                    if (table->page && *table->page < 1) error(table->page_where, "a page has at least one row");
                    if (list) verify_tint(ns, *list, table->view.text(), table->tint, table->tint_where);
                    if (table->only) {
                        verify_filter(*table->only, table->only_where);
                        auto* b = std::get_if<binary_expression>(&table->only->node);
                        auto* n = b ? std::get_if<name_expression>(&b->left->node) : nullptr;
                        if (n && !in_rows(n->name.text())) error(table->only_where, "the table keeps only rows by " + n->name.text() + ", which its rows don't have");
                    }
                    // reorder position: rows dragged into order, which an update sets.
                    if (table->reorder && list) {
                        const entity_declaration* rows_of = listed_entity(ns, *list);
                        const field* f = rows_of ? find_field(*rows_of, *table->reorder) : nullptr;
                        bool ordered = !list->order.empty() && written(*list->order.front()) == *table->reorder;
                        if (!f || !f->type || f->type->text() != "number") {
                            error(table->reorder_where, "a table's rows are put in order by a number field of theirs, like reorder position");
                        } else if (!in_rows(*table->reorder)) {
                            error(table->reorder_where, "the table is put in order by " + *table->reorder + ", which its rows don't have");
                        } else if (!ordered) {
                            error(table->reorder_where, "the list is put in order by " + *table->reorder + ", so it's ordered by it first, like order by " + *table->reorder);
                        } else if (!find_command(ns, qualified_name{{rows_of->name, "update"}, table->reorder_where})) {
                            error(table->reorder_where, "rows are put in order by " + rows_of->name + "::update, which isn't declared " + in_namespace(ns));
                        }
                    }
                    // Tabs by a choice the table shows, like status: one for each of its choices.
                    if (table->by && table->by_over) {
                        // by phase over project_page.phases: a tab for each phase, the rows
                        // holding which they're in.
                        const view_each* tabs = named_list(ns, *table->by_over, table->by_over_list, route);
                        const entity_declaration* rows_of = list ? listed_entity(ns, *list) : nullptr;
                        const field* chosen = rows_of ? find_field(*rows_of, *table->by) : nullptr;
                        const entity_declaration* tab = tabs ? listed_entity(ns, *tabs) : nullptr;
                        if (rows_of && (!chosen || !tab || pointed(ns, *chosen) != tab)) {
                            error(table->by_where, "a table's tabs over " + table->by_over->text() + "." + table->by_over_list + " are by a field pointing at what it lists" +
                                                       (tab ? ", a " + tab->name : ""));
                        } else if (list && !in_rows(*table->by)) {
                            error(table->by_where, "the table's tabs are by " + *table->by + ", which its rows don't have");
                        } else if (tabs && !this->in_rows(*tabs, "title") && !this->in_rows(*tabs, "name")) {
                            error(table->by_over->where, "a table's tabs are called by their title or name, so " + table->by_over->text() + "." + table->by_over_list + " needs one");
                        }
                    } else if (table->by) {
                        bool shown = false;
                        for (const auto& column : table->columns) shown = shown || written(*column.value) == *table->by;
                        const entity_declaration* rows_of = list ? listed_entity(ns, *list) : nullptr;
                        const field* chosen = rows_of ? find_field(*rows_of, *table->by) : nullptr;
                        if (!shown) {
                            error(table->by_where, "the table's tabs are by " + *table->by + ", which it needs as a column too");
                        } else if (!chosen || chosen->choices.empty()) {
                            error(table->by_where, "a table's tabs are by a field with choices, like status, and " + *table->by + " isn't one");
                        }
                    }
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
                        } else if (!routes_.contains(target) && !routes_.contains(pointed_at(ns, list, target, names.back()))) {
                            error(table->link_where, "there's no screen at " + target + " for this table's rows to open");
                        }
                    }
                } else if (auto* grid = std::get_if<grid_item>(&item.node)) {
                    verify_grid(ns, *grid, route);
                } else if (auto* board = std::get_if<board_item>(&item.node)) {
                    verify_board(ns, *board, route);
                } else if (auto* cards = std::get_if<cards_item>(&item.node)) {
                    verify_cards(ns, *cards, route);
                } else if (auto* link = std::get_if<content_link>(&item.node)) {
                    this->link(ns, route, *link, item.where);
                } else if (auto* text = std::get_if<content_text>(&item.node);
                           text && (text->type == content_text::kind::text || text->value.starts_with("{"))) {
                    verify_live_text(ns, text->value, route, item.where);
                    if (text->when) verify_condition(ns, *text->when, route);
                } else if (auto* form = std::get_if<form_item>(&item.node)) {
                    for (const auto& command : form->commands) verify_command_use(ns, command);
                    const entity_declaration* entity = find_command(ns, form->commands.front());
                    bool creates = form->commands.front().parts.back() == "create";
                    // What the command is sent besides its entity's fields, a form asks for too.
                    std::map<std::string, field, std::less<>> inputs;
                    if (auto here = scopes_.find(ns); here != scopes_.end()) {
                        if (auto c = here->second.command_nodes.find(form->commands.front().text()); c != here->second.command_nodes.end()) inputs = inputs_of(*c->second);
                    }
                    for (const auto& f : form->fields) {
                        snake(f.name, f.where);
                        if (inputs.contains(f.name)) continue;
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
                    // One a row's button opens starts from that row, which holds what it asks.
                    auto [rows, view_name] = screen_ ? rows_with(ns, *screen_, form->commands.front()) : std::pair<const view_each*, std::string>{nullptr, ""};
                    if (rows) {
                        for (const auto& f : form->fields) {
                            if (!in_rows(*rows, f.name) && !inputs.contains(f.name)) {
                                error(f.where, "form " + form->commands.front().text() + " opens from each row of " + view_name + ", so the list needs " +
                                                   f.name + "; add it to the list's block");
                            }
                        }
                    } else if (entity) {
                        verify_edit_form(ns, *entity, *form, route);
                    }
                } else if (auto* confirm = std::get_if<confirm_item>(&item.node)) {
                    verify_command_use(ns, confirm->command);
                } else if (auto* button = std::get_if<button_item>(&item.node)) {
                    verify_command_use(ns, button->command);
                    if (button->when) verify_condition(ns, *button->when, route);
                    if (button->along) verify_along(ns, *button, route);
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

        // A view's named list, or nothing, having said why.
        const view_each* named_list(const std::string& ns, const qualified_name& view_name, const std::string& list, const std::string& route) {
            snake(view_name);
            const view_declaration* view = find(ns, view_name, &scope::views);
            if (!view) {
                error(view_name.where, "there's no view " + view_name.text() + " " + in_namespace(ns));
                return nullptr;
            }
            verify_shown(*view, view_name.text(), route, view_name.where);
            for (const auto& each : view->each) {
                if (each.name && *each.name == list) return &each;
            }
            error(view_name.where, "view " + view_name.text() + " has no list called " + list);
            return nullptr;
        }

        // grid project_page.steps by from and to over project_page.phases: each entry
        // names its row and column by fields pointing at what the over list lists, and
        // the rows hold those, what a cell shows, and a title or name for each thing.
        void verify_grid(const std::string& ns, const grid_item& grid, const std::string& route) {
            const view_each* entries = named_list(ns, grid.view, *grid.list, route);
            const view_each* things = named_list(ns, grid.over, grid.over_list, route);
            if (!entries || !things) return;
            const entity_declaration* entry = listed_entity(ns, *entries);
            const entity_declaration* thing = listed_entity(ns, *things);
            if (!entry || !thing) return;
            for (const auto& [name, where] : {std::pair{grid.from, grid.from_where}, std::pair{grid.to, grid.to_where}}) {
                const field* f = find_field(*entry, name);
                if (!f) {
                    error(where, "entity " + entry->name + " has no field " + name + nearest(name, field_names(*entry)));
                } else if (pointed(ns, *f) != thing) {
                    error(where, entry->name + "." + name + " is a row or column of the grid, so it points at a " + thing->name + ", what " +
                                     grid.over.text() + "." + grid.over_list + " lists");
                } else if (!in_rows(*entries, name)) {
                    error(where, "view " + grid.view.text() + " has no " + name + " in its " + *grid.list + "; add it to the list's block");
                }
            }
            if (grid.cell) {
                std::string shown = written(*grid.cell);
                if (!in_rows(*entries, shown)) error(grid.cell->where, "view " + grid.view.text() + " has no " + shown + " in its " + *grid.list + "; add it to the list's block");
            }
            if (!in_rows(*things, "title") && !in_rows(*things, "name")) {
                error(grid.over.where, "a grid's rows and columns are called by their title or name, so " + grid.over.text() + "." + grid.over_list + " needs one");
            }
        }

        // board project_page.issues by phase over project_page.phases: each card names
        // its column by a field pointing at what the over list lists, and the cards hold
        // that field and what they show. A move goes along steps and changes that field.
        void verify_board(const std::string& ns, const board_item& board, const std::string& route) {
            const view_each* cards = named_list(ns, board.view, *board.list, route);
            const view_each* columns = named_list(ns, board.over, board.over_list, route);
            if (!cards || !columns) return;
            const entity_declaration* card = listed_entity(ns, *cards);
            const entity_declaration* column = listed_entity(ns, *columns);
            if (!card || !column) return;
            const field* by = find_field(*card, board.by);
            if (!by) {
                error(board.by_where, "entity " + card->name + " has no field " + board.by + nearest(board.by, field_names(*card)));
            } else if (pointed(ns, *by) != column) {
                error(board.by_where, card->name + "." + board.by + " is a card's column, so it points at a " + column->name + ", what " + board.over.text() + "." +
                                          board.over_list + " lists");
            } else if (!in_rows(*cards, board.by)) {
                error(board.by_where, "view " + board.view.text() + " has no " + board.by + " in its " + *board.list + "; add it to the list's block");
            }
            verify_tint(ns, *cards, board.view.text(), board.tint, board.tint_where);
            if (!in_rows(*columns, "title") && !in_rows(*columns, "name")) {
                error(board.over.where, "a board's columns are called by their title or name, so " + board.over.text() + "." + board.over_list + " needs one");
            }
            if (std::none_of(board.columns.begin(), board.columns.end(), [](const table_column& c) {
                    auto* named = std::get_if<name_expression>(&c.value->node);
                    return !named || named->name.parts.size() != 2;
                })) {
                error(board.view.where, "a board's cards show something, its title first, like title");
            }
            for (const auto& name : board.search) {
                if (!in_rows(*cards, name)) error(board.view.where, "the board searches " + name + ", which its cards don't have");
            }
            for (const auto& shown : board.columns) {
                std::string key = written(*shown.value);
                if (auto* named = std::get_if<name_expression>(&shown.value->node); named && named->name.parts.size() == 2) {
                    verify_command_use(ns, named->name);  // a button in its toolbar
                    continue;
                }
                if (!in_rows(*cards, key)) error(shown.where, "view " + board.view.text() + " has no " + key + " in its " + *board.list + "; add it to the list's block");
            }
            if (board.link && !routes_.contains(full_route(ns, *board.link))) {
                error(board.link_where, "there's no screen at " + full_route(ns, *board.link) + " for this board's cards to open");
            }
            if (board.move) {
                verify_command_use(ns, board.move->command);
                if (board.move->command.parts.size() == 2 && board.move->command.parts[0] != card->name) {
                    error(board.move->command.where, "a board's cards are " + card->name + "s, so they're moved by a command on " + card->name + ", like " + card->name + "::move");
                }
                verify_along(ns, *board.move, route);
            }
        }

        // cards project_page.boards link /:project/boards/:board { ... }: the rows hold
        // what the cards show; a tally counts a list's rows by a field naming the card
        // and another; a filter is one of the cards' rows' fields and the value it holds.
        void verify_cards(const std::string& ns, const cards_item& cards, const std::string& route) {
            const view_each* rows = named_list(ns, cards.view, *cards.list, route);
            if (!rows) return;
            if (cards.columns.empty()) error(cards.view.where, "cards show something, their title first, like title");
            for (const auto& shown : cards.columns) {
                std::string key = written(*shown.value);
                if (!in_rows(*rows, key)) error(shown.where, "view " + cards.view.text() + " has no " + key + " in its " + *cards.list + "; add it to the list's block");
            }
            if (cards.link && !routes_.contains(full_route(ns, *cards.link))) {
                error(cards.link_where, "there's no screen at " + full_route(ns, *cards.link) + " for these cards to open");
            }
            if (cards.tally) {
                if (const view_each* counted = named_list(ns, *cards.tally, cards.tally_list, route)) {
                    for (const auto& field : {cards.tally_by, cards.tally_and}) {
                        if (!in_rows(*counted, field)) {
                            error(cards.tally_where, "view " + cards.tally->text() + " has no " + field + " in its " + cards.tally_list + "; add it to the list's block");
                        }
                    }
                }
            }
            for (const auto& f : cards.filters) {
                verify_filter(*f.condition, f.where);
                hue(f.hue, f.hue_where);
            }
            if (cards.filters.size() > 3) error(cards.filters[3].where, "a card has three filters at most, the ones most used");
        }

        // A link whose last parameter is a field of the rows pointing at something,
        // like a link's to in /:project/:to, opens that thing's screen, /:project/:issue.
        std::string pointed_at(const std::string& ns, const view_each* list, const std::string& target, const std::string& last) {
            const entity_declaration* rows = list ? listed_entity(ns, *list) : nullptr;
            const field* f = rows ? find_field(*rows, last) : nullptr;
            if (!f || !f->type || f->type->parts.size() != 1 || !find_entity(ns, *f->type)) return target;
            return target.substr(0, target.rfind("/:") + 2) + f->type->parts[0] + target.substr(target.rfind("/:") + 2 + last.size());
        }

        // tint by priority: a field of the rows with choices, in the list, which colors
        // each row by how urgent its choice is.
        void verify_tint(const std::string& ns, const view_each& list, const std::string& view, const std::optional<std::string>& tint, location where) {
            if (!tint) return;
            const entity_declaration* rows = listed_entity(ns, list);
            const field* f = rows ? find_field(*rows, *tint) : nullptr;
            if (!f || f->choices.empty()) {
                error(where, "a tint is by a field with choices, like priority, the first the most urgent, and " + *tint + " isn't one");
            } else if (!in_rows(list, *tint)) {
                error(where, "view " + view + " has no " + *tint + " in this list; add it to the list's block");
            }
        }

        // A filter, a card's or a table's only: a field and the value it holds, or a
        // time it's before or after, counted from now.
        void verify_filter(const expression& condition, location where) {
            auto* b = std::get_if<binary_expression>(&condition.node);
            auto* field = b ? std::get_if<name_expression>(&b->left->node) : nullptr;
            auto* literal = b ? std::get_if<literal_expression>(&b->right->node) : nullptr;
            bool time = literal && literal->type == literal_expression::kind::time;
            bool compared = b && (b->op == token_kind::less || b->op == token_kind::greater || b->op == token_kind::less_equal ||
                                  b->op == token_kind::greater_equal);
            if (!b || !(b->op == token_kind::equal || compared) || !field || field->name.parts.size() != 1) {
                error(where, "a filter is a field and the value it holds, like author == me, or a time it's after, like updated_at > 7 days ago");
            } else if (compared != time) {
                error(where, compared ? "a filter compares a time with one counted from now, like updated_at > 7 days ago"
                                      : "a filter keeps a time before or after one, like updated_at > 7 days ago");
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

        // once "2026-10-07 workflows" { each project { ... } }: each step's body is
        // checked as an update's on its entity, and may name what's already stored, as
        // role::developer, as well as what it makes. A where picks by a field's value.
        void verify(const std::string& ns, location where, const once_declaration& o) {
            std::string what = o.signin ? "on signin" : "once \"" + o.name + "\"";
            if (ns.empty()) {
                error(where, what + " changes a namespace's entities, so it goes inside a namespace");
                return;
            }
            if (o.name.empty() && !o.signin) error(where, "a once has a name of its own, like \"2026-10-07 workflows\", so it's known to be done");
            if (o.steps.empty()) error(where, what + " does nothing; give it a step, like each project { ... }");
            for (const auto& step : o.steps) {
                snake(step.entity, step.entity_where);
                const entity_declaration* entity = find_entity(ns, qualified_name{{step.entity}, step.entity_where});
                mean_entity(step.entity_where, step.entity.size(), entity);
                if (!entity) {
                    error(step.entity_where, "there's no entity " + step.entity + " " + in_namespace(ns));
                    continue;
                }
                context in{ns, entity, nullptr, {}, false};
                in.update = true;
                in.command = true;
                in.stored = true;
                std::map<std::string, std::set<std::string>> made;
                made_by_name(step.body, made);
                in.made = &made;
                if (step.where) {
                    auto* b = std::get_if<binary_expression>(&step.where->node);
                    auto* name = b ? std::get_if<name_expression>(&b->left->node) : nullptr;
                    // On signing in, me.email is the email the person's sign-in vouches for.
                    bool email = o.signin && b && written(*b->right) == "me.email";
                    if (!b || b->op != token_kind::equal || !name || name->name.parts.size() != 1) {
                        error(step.where->where, "a once's where picks by a field's value, like where name == \"developer\"");
                    } else if (!email) {
                        resolve(in, *step.where);
                    } else if (!find_field(*entity, name->name.parts[0])) {
                        error(name->name.where, "entity " + entity->name + " has no field " + name->name.parts[0]);
                    }
                }
                if (step.remove && !o.signin) error(step.entity_where, "a once changes what's there; delete each is for on signin");
                statements(ns, step.body, entity, in);
            }
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
            if (written[0] != f.name && written[0] != f.enum_name) {
                error(n.name.where, n.name.text() + " isn't one of " + f.name + "'s choices; they're written " + naming(f) + "::" +
                                        f.choices[0] + " and so on");
                return;
            }
            if (std::find(f.choices.begin(), f.choices.end(), written[1]) == f.choices.end()) {
                std::string choices;
                for (const auto& c : f.choices) choices += (choices.empty() ? "" : ", ") + naming(f) + "::" + c;
                error(n.name.where, written[1] + " isn't one of " + f.name + "'s choices, " + choices + nearest(written[1], f.choices));
            }
        }

        // What a field's choices are written with: its enum's name, or its own.
        static std::string naming(const field& f) { return f.enum_name.empty() ? f.name : f.enum_name; }

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
                // A command a role allows, like issue::create, in its list.
                if (n->name.parts.size() == 2 && beside && beside->type && beside->type->text() == "permission") {
                    auto here = scopes_.find(in.ns);
                    if (here == scopes_.end() || !here->second.commands.contains(n->name.text())) {
                        error(n->name.where, n->name.text() + " isn't a command " + in_namespace(in.ns));
                    }
                    return nullptr;
                }
                // One of a project's own, by its name, like role::maintainer or
                // phase::triaged: what's named by the project and a name.
                if (n->name.parts.size() == 2 && beside && beside->choices.empty()) {
                    if (const entity_declaration* named = pointed(in.ns, *beside)) {
                        if (n->name.parts[0] != named->name) {
                            error(n->name.where, beside->name + " is a " + named->name + ", like " + named->name + "::" + n->name.parts[1]);
                        } else if (!named_within(in.ns, *named)) {
                            error(n->name.where, named->name + "::" + n->name.parts[1] + " names a " + named->name + " within its project by its name, so " +
                                                     named->name + "'s keys are the project and a name, like name  slug  required  key");
                        } else {
                            // One the command makes, or a role every project starts with.
                            std::set<std::string> known;
                            if (in.made) {
                                if (auto it = in.made->find(named->name); it != in.made->end()) known = it->second;
                            }
                            if (const roles_declaration* r = defined_roles(in.ns, *beside)) {
                                for (const auto& d : r->defaults) known.insert(d.name);
                            }
                            if (!in.stored && !known.contains(n->name.parts[1])) {
                                std::string names;
                                for (const auto& k : known) names += (names.empty() ? "" : ", ") + named->name + "::" + k;
                                error(n->name.where, n->name.text() + " isn't a " + named->name + " this command makes or every project starts with" +
                                                         (names.empty() ? "" : "; those are " + names));
                            }
                        }
                        snake(n->name.parts[1], n->name.where);
                        return nullptr;
                    }
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
                if (in.inputs) {
                    if (auto it = in.inputs->find(name); it != in.inputs->end()) return &it->second;
                }
                if (beside && std::find(beside->choices.begin(), beside->choices.end(), name) != beside->choices.end()) {
                    error(n->name.where, "write " + naming(*beside) + "::" + name + "; an enum's choices are named with it",
                          fix{n->name.where, name.size(), naming(*beside) + "::" + name});
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
                    for (const auto& c : beside->choices) choices += (choices.empty() ? "" : ", ") + naming(*beside) + "::" + c;
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
            // [role::maintainer, role::programmer]: each value is one the list holds.
            if (auto* list = std::get_if<list_expression>(&e.node)) {
                if (beside && !beside->list) error(e.where, beside->name + " holds one value, not a list of them");
                for (const auto& item : list->items) resolve(in, *item, beside);
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
                    // issue.implemented_by.name: a person's name, picture or username, or
                    // each one's, like issue.assignees.name.
                    if (inner->type && inner->type->text() == "user") return profile_field(in, *inner, m->member, where);
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
            inner.command = outer.command;
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
            // exists(step where from == was issue.phase && held(roles)): whether
            // there's one, in a command's require.
            if (name == "exists") {
                if (!arguments(1)) return nullptr;
                if (!in.command) error(call.callee->where, "exists goes in a command, like require exists(step where ...)  \"...\"");
                auto* w = std::get_if<where_expression>(&call.arguments[0]->node);
                if (!w) {
                    error(call.arguments[0]->where, "exists takes an entity and where, like exists(step where to == issue.phase)");
                    return nullptr;
                }
                where_of(in, *w);
                return nullptr;
            }
            // was issue.phase: what a field of the command's entity held before it.
            if (name == "was") {
                if (!arguments(1)) return nullptr;
                if (!in.command) error(call.callee->where, "was goes in a command, saying what a field held before it");
                resolve(in, *call.arguments[0]);
                return nullptr;
            }
            // held(roles): whether the person holds one of these project roles.
            if (name == "held") {
                if (!arguments(1)) return nullptr;
                const field* roles = resolve(in, *call.arguments[0]);
                if (roles && (!roles->list || !defined_roles(in.ns, *roles))) {
                    error(call.arguments[0]->where, "held takes a list of a project's roles, like held(roles)");
                }
                return nullptr;
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
            std::vector<std::string> known{"count", "first", "exists", "was", "held", "starts_with", "drop", "github_secret"};
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
