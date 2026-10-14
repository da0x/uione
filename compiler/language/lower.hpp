// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// What a project says in short, said in full before it's checked, so the checker and
// the generators see one form:
//     each issue in project          each issue where project == project.id
//     each change in issue in project    each change of issue where project == project.id
//     me, in a view                  user.id, the person reading it
//     define role maintainer "Maintainer" in project { ... }
//                                    the role and member entities each project's roles
//                                    are kept in, and the roles it starts with

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <type_traits>
#include <variant>
#include <string>
#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"
#include "language/parser.hpp"

namespace one::language {

    namespace lowering {

        inline expression_ptr name(std::string text, location where) {
            auto e = std::make_unique<expression>();
            e->where = where;
            e->node = name_expression{qualified_name{{std::move(text)}, where}};
            return e;
        }

        inline expression_ptr member(expression_ptr object, std::string field, location where) {
            auto e = std::make_unique<expression>();
            e->where = where;
            e->node = member_expression{std::move(object), std::move(field)};
            return e;
        }

        inline expression_ptr binary(token_kind op, expression_ptr left, expression_ptr right, location where) {
            auto e = std::make_unique<expression>();
            e->where = where;
            e->node = binary_expression{op, std::move(left), std::move(right)};
            return e;
        }

        // me, the person reading, as views have long said it: user.id.
        inline void reader(expression_ptr& e) {
            if (!e) return;
            if (auto* n = std::get_if<name_expression>(&e->node); n && n->name.parts == std::vector<std::string>{"me"}) {
                location where = e->where;
                e = member(name("user", where), "id", where);
                return;
            }
            std::visit(
                [](auto& node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, member_expression>) reader(node.object);
                    else if constexpr (std::is_same_v<T, call_expression>) {
                        reader(node.callee);
                        for (auto& a : node.arguments) reader(a);
                    } else if constexpr (std::is_same_v<T, where_expression>) {
                        reader(node.source);
                        reader(node.condition);
                    } else if constexpr (std::is_same_v<T, unary_expression>) reader(node.operand);
                    else if constexpr (std::is_same_v<T, binary_expression>) {
                        reader(node.left);
                        reader(node.right);
                    } else if constexpr (std::is_same_v<T, list_expression>) {
                        for (auto& item : node.items) reader(item);
                    } else if constexpr (std::is_same_v<T, match_expression>) {
                        reader(node.subject);
                        for (auto& arm : node.arms) reader(arm.value);
                        if (node.otherwise) reader(node.otherwise);
                    }
                },
                e->node);
        }

        // a book, an issue.
        inline std::string a(const std::string& word) {
            return (std::string("aeiou").find(word.empty() ? 'x' : word[0]) == std::string::npos ? "a " : "an ") + word;
        }

        struct lowerer {
            const std::string* path = nullptr;
            diagnostics& out;
            std::map<std::string, const entity_declaration*> entities;

            void error(location where, std::string message) { out.push_back({*path, where, std::move(message)}); }

            void index(const std::vector<declaration>& declarations) {
                for (const auto& d : declarations) {
                    if (const auto* e = std::get_if<entity_declaration>(&d.node)) entities.emplace(e->name, e);
                    else if (const auto* ns = std::get_if<namespace_declaration>(&d.node)) index(ns->declarations);
                }
            }

            // The one field of an entity that points at another, by its type.
            const field* pointing(const std::string& from, const std::string& at, location where) {
                auto found = entities.find(from);
                if (found == entities.end()) return nullptr;  // the checker says so
                std::vector<const field*> fields;
                for (const auto& f : found->second->fields) {
                    if (!f.list && f.type && !f.type->parts.empty() && f.type->parts.back() == at) fields.push_back(&f);
                }
                if (fields.size() == 1) return fields.front();
                if (fields.empty()) {
                    error(where, from + " has no field pointing at " + a(at) + ", so no " + from + " is in one");
                } else {
                    std::string names;
                    for (const auto* f : fields) names += (names.empty() ? "" : " and ") + f->name;
                    error(where, from + " points at " + a(at) + " by " + names + "; say which with where, like where " +
                                     fields.front()->name + " == " + at + ".id");
                }
                return nullptr;
            }

            void each(const view_declaration& view, view_each& list) {
                if (list.within.empty()) return;
                auto* source = std::get_if<name_expression>(&list.source->node);
                if (!source || source->name.parts.size() != 1) {
                    error(list.within.front().second, "in follows the name of what's listed, like each issue in project");
                    return;
                }
                const std::string& listed = source->name.parts.front();
                if (list.within.size() > 1) {
                    error(list.within[1].second, "a list is in one thing, like each issue in project");
                    return;
                }
                const auto& [in, where] = list.within.front();
                if (view.per != in) {
                    error(where, "view " + view.name + " is " + (view.per ? "per " + *view.per : "for everyone") +
                                     ", so it has no " + in + " for its rows to be in");
                    return;
                }
                const field* f = pointing(listed, in, where);
                if (!f) return;
                // A change's rows hold the fields of what changed, so the same field picks them.
                auto test = binary(token_kind::equal, name(f->name, where), member(name(in, where), "id", where), where);
                list.condition = list.condition
                                     ? binary(token_kind::logical_and, std::move(test), std::move(list.condition), where)
                                     : std::move(test);
                list.within.clear();
            }

            // each change in issue, in a view per issue: that issue's own changes.
            void history(const view_declaration& view, view_each& list) {
                if (!list.changes || !list.within.empty()) return;
                auto* source = std::get_if<name_expression>(&list.source->node);
                if (!source || source->name.parts.size() != 1 || view.per != source->name.parts.front()) return;
                const std::string& of = source->name.parts.front();
                location where = list.source->where;
                auto test = binary(token_kind::equal, name(of, where), member(name(of, where), "id", where), where);
                list.condition = list.condition
                                     ? binary(token_kind::logical_and, std::move(test), std::move(list.condition), where)
                                     : std::move(test);
            }

            // where project == project.id, in a view per project, said as each issue in
            // project; and each change in issue where issue == issue.id, in a view per
            // issue, as each change in issue alone.
            void said_in_full(const view_declaration& view, const view_each& list) {
                if (!list.condition || !list.within.empty() || !view.per || *view.per == "user" || list.where_length == 0) return;
                auto* test = std::get_if<binary_expression>(&list.condition->node);
                if (!test || test->op != token_kind::equal) return;
                auto* left = std::get_if<name_expression>(&test->left->node);
                auto* right = std::get_if<member_expression>(&test->right->node);
                if (!left || !right || left->name.parts.size() != 1 || right->member != "id") return;
                auto* of = std::get_if<name_expression>(&right->object->node);
                if (!of || of->name.parts != std::vector<std::string>{*view.per}) return;
                auto* source = std::get_if<name_expression>(&list.source->node);
                if (!source || source->name.parts.size() != 1) return;
                const std::string& listed = source->name.parts.front();
                const std::string& in = *view.per;
                if (list.changes && listed == in && left->name.parts.front() == in) {
                    location before = list.where_word;
                    --before.column;
                    out.push_back({*path, list.where_word, "each change in " + in + " is " + in + "'s own changes, in a view per " + in,
                                   fix{before, list.where_length + 1, ""}});
                    return;
                }
                auto found = entities.find(listed);
                if (found == entities.end()) return;
                const field* only = nullptr;
                int count = 0;
                for (const auto& f : found->second->fields) {
                    if (!f.list && f.type && !f.type->parts.empty() && f.type->parts.back() == in) only = &f, ++count;
                }
                if (count != 1 || only->name != left->name.parts.front()) return;
                out.push_back({*path, list.where_word, "write in " + in + " for the rows in the view's " + in,
                               fix{list.where_word, list.where_length, "in " + in}});
            }

            // user.id in a view, said as commands and screens say it: me.
            void reading(const expression_ptr& e) {
                if (!e) return;
                if (auto* m = std::get_if<member_expression>(&e->node); m && m->member == "id") {
                    if (auto* n = std::get_if<name_expression>(&m->object->node); n && n->name.parts == std::vector<std::string>{"user"}) {
                        out.push_back({*path, m->object->where, "write me for the person reading", fix{m->object->where, 7, "me"}});
                        return;
                    }
                }
                std::visit(
                    [this](const auto& node) {
                        using T = std::decay_t<decltype(node)>;
                        if constexpr (std::is_same_v<T, member_expression>) reading(node.object);
                        else if constexpr (std::is_same_v<T, call_expression>) {
                            reading(node.callee);
                            for (const auto& a : node.arguments) reading(a);
                        } else if constexpr (std::is_same_v<T, where_expression>) {
                            reading(node.source);
                            reading(node.condition);
                        } else if constexpr (std::is_same_v<T, unary_expression>) reading(node.operand);
                        else if constexpr (std::is_same_v<T, binary_expression>) {
                            reading(node.left);
                            reading(node.right);
                        } else if constexpr (std::is_same_v<T, list_expression>) {
                            for (const auto& item : node.items) reading(item);
                        } else if constexpr (std::is_same_v<T, match_expression>) {
                            reading(node.subject);
                            for (const auto& arm : node.arms) reading(arm.value);
                            if (node.otherwise) reading(node.otherwise);
                        }
                    },
                    e->node);
            }

            void lower(std::vector<declaration>& declarations) {
                for (auto& d : declarations) {
                    if (auto* ns = std::get_if<namespace_declaration>(&d.node)) {
                        lower(ns->declarations);
                    } else if (auto* v = std::get_if<view_declaration>(&d.node)) {
                        for (auto& value : v->values) reading(value.value), reader(value.value);
                        reading(v->public_when), reader(v->public_when);
                        for (auto& person : v->reader_people) reading(person), reader(person);
                        for (auto& list : v->each) {
                            said_in_full(*v, list);
                            reading(list.condition);
                            for (auto& key : list.order) reading(key);
                            for (auto& row : list.rows) reading(row.value);
                            reader(list.condition);
                            for (auto& key : list.order) reader(key);
                            for (auto& row : list.rows) reader(row.value);
                            history(*v, list);
                            each(*v, list);
                        }
                    }
                }
            }
        };

        // define role ... in project: the roles each project starts with, kept as role
        // records, and member records giving people them, which the language
        // declares beside them, as a project that wrote them out would have:
        //
        //     entity role {
        //         project  project  required  key
        //         name     text     required  key = slug(title)
        //         title    text     required
        //         may      list of permission
        //     }
        //     entity member {
        //         project  project  required  key
        //         person   user     required  key
        //         role     role     required  key
        //     }
        //     roles role per project from member { maintainer "Maintainer" { ... } }
        struct definer {
            diagnostics& out;
            struct place {
                std::vector<declaration>* declarations = nullptr;
                const std::string* path = nullptr;
                std::vector<const define_role_declaration*> roles;
            };
            std::map<std::string, place> by_namespace;

            void find(std::vector<declaration>& declarations, const std::string& ns, const std::string& path) {
                for (auto& d : declarations) {
                    if (auto* n = std::get_if<namespace_declaration>(&d.node)) {
                        find(n->declarations, ns.empty() ? n->name : ns + "::" + n->name, path);
                    } else if (auto* r = std::get_if<define_role_declaration>(&d.node)) {
                        auto& at = by_namespace[ns];
                        if (!at.declarations) at.declarations = &declarations, at.path = &path;
                        at.roles.push_back(r);
                    }
                }
            }

            static bool declares(const std::vector<file>& files, const std::string& name) {
                std::function<bool(const std::vector<declaration>&)> any = [&](const std::vector<declaration>& ds) {
                    for (const auto& d : ds) {
                        if (auto* e = std::get_if<entity_declaration>(&d.node); e && e->name == name) return true;
                        if (auto* n = std::get_if<namespace_declaration>(&d.node); n && any(n->declarations)) return true;
                    }
                    return false;
                };
                for (const auto& f : files) {
                    if (any(f.declarations)) return true;
                }
                return false;
            }

            void define(const std::vector<file>& files) {
                // A namespace inside another first, so adding to the outer one moves nothing
                // still to be added to.
                for (auto it = by_namespace.rbegin(); it != by_namespace.rend(); ++it) {
                    auto& at = it->second;
                    const define_role_declaration& first = *at.roles.front();
                    bool fine = true;
                    for (const auto* r : at.roles) {
                        if (r->in != first.in) {
                            out.push_back({*at.path, r->in_where, "every role is in the same thing, " + first.in + ", which each has its own of"});
                            fine = false;
                        }
                    }
                    bool services = std::any_of(at.roles.begin(), at.roles.end(), [](const auto* r) { return r->services; });
                    std::vector<const char*> owns{"role", "member"};
                    if (services) owns.push_back("service");
                    for (const char* own : owns) {
                        if (declares(files, own)) {
                            out.push_back({*at.path, first.where, std::string("define role keeps roles as the language's own ") + own +
                                                                      " records; take out entity " + own + ", which it declares"});
                            fine = false;
                        }
                    }
                    if (!fine) continue;
                    const std::string& in = first.in;
                    std::string text = "entity role {\n\t" + in + "  " + in + "  required  key\n\tname  text  required  key = slug(title)\n"
                                       "\ttitle  text  required\n\tmay  list of permission\n}\n"
                                       "entity member {\n\t" + in + "  " + in + "  required  key\n\tperson  user  required  key\n"
                                       "\trole  role  required  key\n}\n"
                                       "roles role per " + in + " from member {\n";
                    // A role for services brings the project's service accounts: each
                    // named by its title, holding one role, and what's shown of its key.
                    if (services) {
                        text = "entity service history {\n\t" + in + "  " + in + "  required  key\n\tname  text  required  key = slug(title)\n"
                               "\ttitle  text  required\n\trole  role  required\n\tkey_start  text\n\tkey_made  date\n\tkey_used  date\n}\n" + text;
                    }
                    for (const auto* r : at.roles) {
                        text += "\t" + r->name + " \"\" {\n";
                        for (const auto& p : r->permissions) text += "\t\t" + p.text() + "\n";
                        text += "\t}\n";
                    }
                    text += "}\n";
                    diagnostics ignored;
                    file made = parse("<define role>", text, ignored);
                    // Said where the roles are, so what's wrong with one is shown there.
                    for (auto& d : made.declarations) {
                        d.where = first.where;
                        if (auto* e = std::get_if<entity_declaration>(&d.node)) {
                            for (auto& f : e->fields) f.where = first.where;
                        } else if (auto* roles = std::get_if<roles_declaration>(&d.node)) {
                            roles->defined = true;
                            roles->entity_where = roles->per_where = first.in_where;
                            for (std::size_t i = 0; i < roles->defaults.size() && i < at.roles.size(); ++i) {
                                roles->defaults[i].where = at.roles[i]->where;
                                roles->defaults[i].title = at.roles[i]->title;
                                roles->defaults[i].permissions = at.roles[i]->permissions;
                                roles->defaults[i].services = at.roles[i]->services;
                            }
                        }
                    }
                    for (auto& d : made.declarations) at.declarations->push_back(std::move(d));
                }
            }
        };

        // define workflow issue by stage { worked by worked_by  steps step }, said in
        // full, as a project that wrote it out would have: the lease on the entity,
        //
        //     held_by     user
        //     held_until  date
        //
        // the commands that claim it, renew and release the claim, and move it along a
        // step, and a view for each role of what it works, read by the project's people:
        //
        //     command issue::claim {
        //         require held(stage.worked_by)  "this stage isn't worked by a role you hold"
        //         require held_by == none || held_by == me || held_until < now  "someone else holds it"
        //         held_by = me
        //         held_until = 10 minutes from now
        //     }
        //     view issue_work per role { readers member  items = each issue where stage.worked_by has role.id { … } }
        struct workflower {
            diagnostics& out;
            struct found {
                define_workflow_declaration* workflow;
                std::vector<declaration>* declarations;
                const std::string* path;
                std::string ns;
            };
            std::vector<found> workflows;
            std::vector<std::tuple<const define_agent_declaration*, const std::string*, std::string>> agents;  // each, its file and namespace
            std::set<std::string> for_services;  // namespace::role of the roles services may hold
            std::vector<std::pair<view_declaration*, std::string>> views;  // each, and its namespace
            std::map<std::string, entity_declaration*> entities;  // by namespace::name
            std::map<std::string, std::string> scopes;           // a namespace's roles are in this, like project
            std::map<std::string, std::vector<std::string>> roles;  // a namespace's defined roles, by name
            std::map<std::string, const enum_declaration*> enums;   // by namespace::name
            std::set<std::string> commands;                       // namespace::entity::command
            std::set<const define_workflow_declaration*> said_in_full;  // those whose commands are made

            static std::string full(const std::string& ns, const std::string& name) { return ns.empty() ? name : ns + "::" + name; }

            void find(std::vector<declaration>& declarations, const std::string& ns, const std::string& path) {
                for (auto& d : declarations) {
                    if (auto* n = std::get_if<namespace_declaration>(&d.node)) {
                        find(n->declarations, full(ns, n->name), path);
                    } else if (auto* w = std::get_if<define_workflow_declaration>(&d.node)) {
                        workflows.push_back({w, &declarations, &path, ns});
                    } else if (auto* e = std::get_if<entity_declaration>(&d.node)) {
                        entities[full(ns, e->name)] = e;
                    } else if (auto* r = std::get_if<define_role_declaration>(&d.node)) {
                        scopes.emplace(ns, r->in);
                        roles[ns].push_back(r->name);
                        if (r->services) for_services.insert(full(ns, r->name));
                    } else if (auto* agent = std::get_if<define_agent_declaration>(&d.node)) {
                        agents.emplace_back(agent, &path, ns);
                    } else if (auto* v = std::get_if<view_declaration>(&d.node)) {
                        views.emplace_back(v, ns);
                    } else if (auto* en = std::get_if<enum_declaration>(&d.node)) {
                        enums[full(ns, en->name)] = en;
                    } else if (auto* c = std::get_if<command_declaration>(&d.node)) {
                        commands.insert(full(ns, c->name.text()));
                    }
                }
            }

            static const field* field_named(const entity_declaration& e, const std::string& name) {
                for (const auto& f : e.fields) {
                    if (f.name == name) return &f;
                }
                return nullptr;
            }

            static bool lists_roles(const field* f) { return f && f->list && f->type && f->type->text() == "role"; }

            static std::string a(const std::string& word) {
                return std::string(std::string("aeiou").find(word.front()) == std::string::npos ? "a " : "an ") + word;
            }

            using made_list = std::vector<std::pair<std::vector<declaration>*, declaration>>;
            using say_error = std::function<void(location, std::string)>;

            // Whether the lease and the commands can be added: roles to work it, the
            // entity in what they're roles of, and nothing of its own in their way. The
            // name of what the roles are in, like shop, or nothing.
            std::string claimable(const define_workflow_declaration& w, const entity_declaration& moving, const std::string& ns, const say_error& error) {
                auto scope = scopes.find(ns);
                if (scope == scopes.end()) {
                    error(w.where, "a workflow's stages are worked by roles: define role, like define role maintainer \"Maintainer\" in project { ... }");
                    return "";
                }
                const std::string& in = scope->second;
                if (!field_named(moving, in)) {
                    error(w.where, w.entity + " is worked by a " + in + "'s roles, so it needs its " + in + ": " + in + "  " + in + "  required");
                    return "";
                }
                for (const char* own : {"held_by", "held_until"}) {
                    if (const field* f = field_named(moving, own)) error(f->where, std::string("the workflow keeps who holds ") + a(w.entity) + " in " + own + "; take out this field");
                }
                bool taken = false;
                for (const char* command : {"claim", "renew", "release", "move"}) {
                    if (commands.contains(full(ns, w.entity + "::" + command))) {
                        error(w.where, "the workflow makes " + w.entity + "::" + command + "; take out the one declared");
                        taken = true;
                    }
                }
                if (taken || field_named(moving, "held_by") || field_named(moving, "held_until")) return "";
                return in;
            }

            static std::string lease(const define_workflow_declaration& w) { return std::to_string(w.lease_minutes) + " minutes from now"; }
            static constexpr const char* free = "held_by == none || held_by == me || held_until < now";

            // A claim, given who may make it, and a move, given the steps it may take:
            // the commands every workflow has, and the lease they keep.
            static std::string commanded(const define_workflow_declaration& w, const std::string& claim, const std::string& move, const std::string& waits = "") {
                const std::string& e = w.entity;
                std::string text = "entity lease {\n\theld_by  user\n\theld_until  date\n}\n";
                text += "command " + e + "::claim {\n\trequire \"this " + w.by + " isn't worked by a role you hold\" {\n\t\t" + claim +
                        "\n\t}\n\trequire " + free + "  \"someone else holds it\"\n\theld_by = me\n\theld_until = " + lease(w) + "\n}\n";
                text += "command " + e + "::renew {\n\trequire held_by == me  \"you don't hold it\"\n\theld_until = " + lease(w) + "\n}\n";
                text += "command " + e + "::release {\n\trequire held_by == me  \"you don't hold it\"\n\theld_by = none\n\theld_until = none\n}\n";
                text += "command " + e + "::move {\n\tchanges " + w.by + "\n\trequire \"your role doesn't move " + a(e) +
                        " from there to there\" {\n\t\t" + move + "\n\t}\n" + waits + "\trequire " + free +
                        "  \"someone else holds it\"\n\theld_by = none\n\theld_until = none\n}\n";
                return text;
            }

            // The text said as if the project had written it: the lease's fields on the
            // entity, and the rest beside the workflow.
            void said(const define_workflow_declaration& w, entity_declaration& moving, const std::string& text, std::vector<declaration>* declarations,
                      const std::string& path, made_list& made_here) {
                said_in_full.insert(&w);
                diagnostics ignored;
                file made = parse("<define workflow>", text, ignored);
                for (const auto& d : ignored) out.push_back({path, w.where, "the workflow for " + w.entity + " can't be said in full: " + d.message});
                for (auto& d : made.declarations) {
                    d.where = w.where;
                    if (auto* lease_fields = std::get_if<entity_declaration>(&d.node)) {
                        for (auto& f : lease_fields->fields) {
                            f.where = w.where;
                            moving.fields.push_back(std::move(f));
                        }
                        continue;
                    }
                    made_here.emplace_back(declarations, std::move(d));
                }
            }

            static std::string listed(const std::vector<std::string>& words, const std::string& last = "or") {
                std::string text;
                for (std::size_t i = 0; i < words.size(); ++i) text += (i == 0 ? "" : i + 1 == words.size() ? " " + last + " " : ", ") + words[i];
                return text;
            }

            // proofreader, shipper: the roles of two lines on one.
            static std::string joined(const std::vector<std::string>& first, const std::vector<std::string>& more) {
                std::string text;
                for (const auto* words : {&first, &more}) {
                    for (const auto& word : *words) text += (text.empty() ? "" : ", ") + word;
                }
                return text;
            }

            // [role::proofreader, role::shipper]
            static std::string role_list(const std::vector<std::string>& names) {
                std::string text;
                for (const auto& r : names) text += (text.empty() ? "" : ", ") + std::string("role::") + r;
                return "[" + text + "]";
            }

            // define workflow order by stage { proofing worked by proofreader  proofing -> press }:
            // stages that are the choices of the field it moves along, said in the code.
            // Who works each, and who takes each step, are checked from the code:
            //
            //     command order::claim {
            //         require "this stage isn't worked by a role you hold" {
            //             stage == stage::proofing && held([role::proofreader]) || ...
            //         }
            //     }
            //     command order::move {
            //         require "your role doesn't move an order from there to there" {
            //             was stage == stage::proofing && stage == stage::press && held([role::proofreader]) || ...
            //         }
            //     }
            //     view order_work per role {
            //         items = each order where shop == role.shop && (stage == stage::proofing && role.name == "proofreader" || ...) { … }
            //     }
            // How someone is told a record arrived in a stage: the chime, on the machine
            // of whoever has its board open. Email and text are words for later, sent by
            // the backend once they're made.
            static constexpr std::array<std::string_view, 1> notices{"chime"};
            static constexpr std::array<std::string_view, 2> later{"email", "text"};
            template <typename Wrong>
            static void notified(const std::string& way, location where, Wrong& wrong) {
                if (std::find(notices.begin(), notices.end(), way) != notices.end()) return;
                if (std::find(later.begin(), later.end(), way) != later.end()) {
                    wrong(where, "notify " + way + " isn't sent yet; a stage notifies with a chime, like failed notify chime");
                } else {
                    wrong(where, way + " isn't a way to notify; a stage notifies with a chime, like failed notify chime");
                }
            }

            // depends on packages: a list on the entity of records whose own workflow
            // says where their work is done. The field, or nothing when it isn't one.
            template <typename Wrong>
            std::optional<std::string> depended(const define_workflow_declaration& w, const entity_declaration& moving, const std::string& ns, Wrong& wrong) {
                const field* listed = field_named(moving, w.depends_on);
                const std::string kind = listed && listed->type ? listed->type->text() : "";
                if (!listed || !listed->list || !entities.contains(full(ns, kind))) {
                    wrong(w.depends_on_where, w.entity + " depends on a list of what it waits on, and " + w.depends_on +
                                                  " isn't one of its fields that is, like " + w.depends_on + "  list of " + w.entity);
                    return std::nullopt;
                }
                for (const auto& other : workflows) {
                    if (full(other.ns, other.workflow->entity) != full(ns, kind)) continue;
                    if (other.workflow->done_at.empty()) {
                        wrong(w.depends_on_where, w.entity + " waits on " + a(kind) + " until it's done, so " + kind +
                                                      "'s workflow says which stage that is, like done at " +
                                                      (other.workflow->moves.empty() ? std::string("shipped") : other.workflow->moves.back().to));
                        return std::nullopt;
                    }
                    return w.depends_on;
                }
                wrong(w.depends_on_where, w.entity + " waits on " + a(kind) + " until it's done, and " + kind + " has no workflow saying where that is: define workflow " +
                                              kind + " by stage { … done at a stage }");
                return std::nullopt;
            }

            void written(define_workflow_declaration& w, entity_declaration& moving, const std::vector<std::string>& choices,
                         std::vector<declaration>* declarations, const std::string& path, const std::string& ns, const say_error& error,
                         made_list& made_here) {
                bool fine = true;
                auto wrong = [&](location where, std::string message) {
                    error(where, std::move(message));
                    fine = false;
                };
                const std::string& by = w.by;
                if (!w.worked_by.empty()) {
                    wrong(w.worked_by_where, by + " is a choice, so its stages are said here: who works each, like " + choices.front() +
                                                 " worked by a role; worked by and steps are for stages kept as records");
                }
                if (!w.steps.empty()) {
                    wrong(w.steps_where, by + " is a choice, so the steps between its stages are said here, like " + choices.front() +
                                             " -> " + choices.back() + "; worked by and steps are for stages kept as records");
                }
                const std::vector<std::string>& defined = roles[ns];
                auto stage_known = [&](const std::string& name, location where) {
                    if (std::find(choices.begin(), choices.end(), name) != choices.end()) return true;
                    wrong(where, name + " isn't a stage of " + a(w.entity) + ": its stages are " + listed(choices));
                    return false;
                };
                auto role_known = [&](const std::string& name, location where) {
                    if (std::find(defined.begin(), defined.end(), name) != defined.end()) return;
                    wrong(where, defined.empty() ? "there's no role " + name + ": say it with define role, like define role " + name + " \"…\" in project { ... }"
                                                 : "there's no role " + name + ": the roles are " + listed(defined, "and"));
                };
                std::map<std::string, const workflow_stage*> worked;
                for (const auto& stage : w.stages) {
                    if (!stage_known(stage.name, stage.where)) continue;
                    if (worked.contains(stage.name)) {
                        wrong(stage.where, stage.name + " is said twice; put the roles that work it on one line, like " + stage.name + " worked by " +
                                               joined(worked[stage.name]->roles, stage.roles));
                        continue;
                    }
                    worked[stage.name] = &stage;
                    for (std::size_t i = 0; i < stage.roles.size(); ++i) role_known(stage.roles[i], stage.role_where[i]);
                }
                std::set<std::string> reached;
                std::set<std::pair<std::string, std::string>> taken;
                for (const auto& step : w.moves) {
                    bool from = stage_known(step.from, step.from_where);
                    bool to = stage_known(step.to, step.to_where);
                    for (std::size_t i = 0; i < step.by.size(); ++i) role_known(step.by[i], step.by_where[i]);
                    if (!from || !to) continue;
                    if (step.from == step.to) wrong(step.to_where, "a step goes from one stage to another, and this one stays at " + step.from);
                    else if (!taken.insert({step.from, step.to}).second) wrong(step.from_where, "the step " + step.from + " -> " + step.to + " is said twice");
                    if (!worked.contains(step.from)) {
                        wrong(step.from_where, step.from + " is worked by nobody, so it's an end and no step leaves it; say who works it, like " +
                                                   step.from + " worked by a role");
                    }
                    reached.insert(step.to);
                }
                for (const auto& choice : choices) {
                    if (!worked.contains(choice) && !reached.contains(choice)) {
                        wrong(w.where, by + " " + choice + " is worked by nobody and no step reaches it; say who works it, like " + choice +
                                           " worked by a role, or a step to it");
                    }
                }
                if (w.moves.empty()) wrong(w.where, "say the steps between its stages, one a line, like " + choices.front() + " -> " + choices.back());
                if (!w.done_at.empty()) stage_known(w.done_at, w.done_at_where);
                for (const auto& notice : w.notices) {
                    stage_known(notice.stage, notice.where);
                    for (std::size_t i = 0; i < notice.ways.size(); ++i) notified(notice.ways[i], notice.way_where[i], wrong);
                }
                std::string waits;
                if (!w.depends_on.empty()) {
                    if (auto field = depended(w, moving, ns, wrong)) {
                        // It leaves its first stage only once everything it lists is done.
                        waits = "\trequire waiting(" + *field + ") == none || was " + w.entity + "." + by + " != " + by + "::" + choices.front() +
                                "  \"waiting on {waiting(" + *field + ")}\"\n";
                    }
                }
                std::string in = fine ? claimable(w, moving, ns, error) : "";
                if (in.empty()) return;

                auto stage = [&](const std::string& choice) { return by + " == " + by + "::" + choice; };
                // Whoever may take a step out of a stage works it too: its own roles, then
                // those its steps name.
                std::map<std::string, std::vector<std::string>> working;
                for (const auto& [name, said_stage] : worked) working[name] = said_stage->roles;
                for (const auto& step : w.moves) {
                    auto& roles_of = working[step.from];
                    for (const auto& r : step.by) {
                        if (std::find(roles_of.begin(), roles_of.end(), r) == roles_of.end()) roles_of.push_back(r);
                    }
                }
                std::string claim, move, work;
                for (const auto& choice : choices) {
                    auto it = working.find(choice);
                    if (it == working.end()) continue;
                    claim += (claim.empty() ? "" : " || ") + stage(choice) + " && held(" + role_list(it->second) + ")";
                    for (const auto& r : it->second) work += (work.empty() ? "" : " || ") + stage(choice) + " && role.name == \"" + r + "\"";
                }
                for (const auto& step : w.moves) {
                    const auto& roles_taking = step.by.empty() ? worked[step.from]->roles : step.by;
                    move += (move.empty() ? "" : " || ") + std::string("was ") + stage(step.from) + " && " + stage(step.to) + " && held(" + role_list(roles_taking) + ")";
                }
                std::string text = commanded(w, claim, move, waits);
                // What's worked, with every field of each, and who holds it.
                std::string rows;
                for (const auto& f : moving.fields) rows += "  " + f.name;
                rows += "  held_by  held_by.name  held_until";
                text += "view " + w.entity + "_work per role {\n\treaders member\n\titems = each " + w.entity + " where " + in + " == role." + in +
                        " && (" + work + ") {\n\t\t" + rows.substr(2) + "\n\t}\n}\n";
                said(w, moving, text, declarations, path, made_here);
            }

            // Every list of what a workflow moves says who holds each and until when, so
            // a board can show what's being worked on, as it happens, with nothing said.
            void held_in_views() {
                std::set<std::string> moving;
                for (const auto& w : workflows) {
                    if (said_in_full.contains(w.workflow)) moving.insert(full(w.ns, w.workflow->entity));
                }
                for (auto& [view, ns] : views) {
                    for (auto& each : view->each) {
                        auto* source = each.source ? std::get_if<name_expression>(&each.source->node) : nullptr;
                        if (!source || each.changes || !moving.contains(full(ns, source->name.text()))) continue;
                        for (const char* field : {"held_by", "held_until"}) {
                            bool there = std::any_of(each.rows.begin(), each.rows.end(), [&](const view_value& row) {
                                auto* named = row.value ? std::get_if<name_expression>(&row.value->node) : nullptr;
                                return (row.name && *row.name == field) || (!row.name && named && named->name.text() == field);
                            });
                            if (there) continue;
                            auto value = std::make_unique<expression>();
                            value->where = each.where;
                            value->node = name_expression{qualified_name{{field}, each.where}};
                            each.rows.push_back(view_value{each.where, std::nullopt, std::move(value)});
                        }
                    }
                }
            }

            // What an agent works: stages of workflows written in the code that its role,
            // one for services, works, each moved on by a step the workflow has.
            void agent_works(const define_agent_declaration& agent, const std::string& path, const std::string& ns) {
                auto error = [&](location where, std::string message) { out.push_back({path, where, std::move(message)}); };
                const auto& defined = roles[ns];
                if (std::find(defined.begin(), defined.end(), agent.role) == defined.end()) {
                    error(agent.role_where, "there's no role " + agent.role + ": an agent works as a role for services, like define role " + agent.role +
                                                " \"…\" in project for services { ... }");
                    return;
                }
                if (!for_services.contains(full(ns, agent.role))) {
                    error(agent.role_where, agent.role + " is a role for people; an agent works as a role for services, like define role " + agent.role +
                                                " \"…\" in project for services { ... }");
                    return;
                }
                if (agent.rules.empty()) error(agent.where, "say what the agent does in each stage it works, like on order in printing run \"press\" then printed");
                std::set<std::pair<std::string, std::string>> said;
                for (const auto& rule : agent.rules) {
                    const define_workflow_declaration* flow = nullptr;
                    for (const auto& w : workflows) {
                        if (w.ns == ns && w.workflow->entity == rule.entity) flow = w.workflow;
                    }
                    if (!flow) {
                        error(rule.where, "an agent works what moves by a workflow, and there's no workflow for " + rule.entity +
                                              ": define workflow " + rule.entity + " by stage { … }");
                        continue;
                    }
                    if (!said.insert({rule.entity, rule.stage}).second) {
                        error(rule.stage_where, "what the agent does with " + a(rule.entity) + " in " + rule.stage + " is said twice");
                        continue;
                    }
                    // The roles that work a stage: its own, and those of the steps that leave it.
                    bool works = false;
                    std::set<std::string> stages, steps;
                    for (const auto& stage : flow->stages) {
                        stages.insert(stage.name);
                        if (stage.name == rule.stage && std::find(stage.roles.begin(), stage.roles.end(), agent.role) != stage.roles.end()) works = true;
                    }
                    for (const auto& step : flow->moves) {
                        stages.insert(step.from);
                        stages.insert(step.to);
                        if (step.from != rule.stage) continue;
                        steps.insert(step.to);
                        if (std::find(step.by.begin(), step.by.end(), agent.role) != step.by.end()) works = true;
                    }
                    if (!stages.contains(rule.stage)) {
                        error(rule.stage_where, rule.stage + " isn't a stage of " + a(rule.entity) + "'s workflow");
                        continue;
                    }
                    if (!works) {
                        error(rule.stage_where, agent.role + " doesn't work " + rule.stage + "; say so in the workflow, like " + rule.stage + "  worked by " + agent.role);
                    }
                    auto step_to = [&](const std::string& to, location where, const char* when) {
                        if (steps.contains(to)) return;
                        error(where, "there's no step " + rule.stage + " -> " + to + " for " + a(rule.entity) + " to take " + when +
                                         "; add it to the workflow, or say a stage a step reaches");
                    };
                    step_to(rule.then, rule.then_where, "when the program succeeds");
                    if (!rule.otherwise.empty()) step_to(rule.otherwise, rule.otherwise_where, "when it fails");
                    // {order.title} or {title}: a field of what it works.
                    const entity_declaration* worked = entities.contains(full(ns, rule.entity)) ? entities[full(ns, rule.entity)] : nullptr;
                    for (std::size_t open = rule.program.find('{'); worked && open != std::string::npos; open = rule.program.find('{', open + 1)) {
                        std::size_t close = rule.program.find('}', open);
                        std::string inside = close == std::string::npos ? "" : rule.program.substr(open + 1, close - open - 1);
                        if (inside.starts_with(rule.entity + ".")) inside = inside.substr(rule.entity.size() + 1);
                        if (inside != "id" && !field_named(*worked, inside)) {
                            error(rule.program_where, "the program says in braces a field of " + a(rule.entity) + ", like {" + rule.entity + ".id}, and " +
                                                          (inside.empty() ? std::string("{}") : inside) + " isn't one");
                            break;
                        }
                    }
                }
            }

            void define() {
                made_list made_here;
                for (auto& [w, declarations, path, ns] : workflows) {
                    auto error = [&, path = path](location where, std::string message) { out.push_back({*path, where, std::move(message)}); };
                    entity_declaration* moving = entities.contains(full(ns, w->entity)) ? entities[full(ns, w->entity)] : nullptr;
                    if (!moving) {
                        error(w->where, "a workflow is for an entity, and there's no entity " + w->entity + (ns.empty() ? "" : " in namespace " + ns));
                        continue;
                    }
                    const field* along = field_named(*moving, w->by);
                    // Stages written in the code: the choices of an enum, or of the field itself.
                    const std::vector<std::string>* choices = nullptr;
                    if (along && !along->list) {
                        if (!along->choices.empty()) choices = &along->choices;
                        else if (along->type && enums.contains(full(ns, along->type->text()))) choices = &enums[full(ns, along->type->text())]->choices;
                    }
                    if (choices) {
                        written(*w, *moving, *choices, declarations, *path, ns, error, made_here);
                        continue;
                    }
                    const entity_declaration* stage = along && along->type && entities.contains(full(ns, along->type->text()))
                                                          ? entities[full(ns, along->type->text())] : nullptr;
                    if (!stage || along->list) {
                        error(w->by_where, w->entity + " moves along a field pointing at its stages, and " + w->by + " isn't one of its fields that does");
                        continue;
                    }
                    if (!w->stages.empty() || !w->moves.empty()) {
                        location at = !w->stages.empty() ? w->stages.front().where : w->moves.front().from_where;
                        error(at, "the stages of " + a(w->entity) + " are " + stage->name + " records, so who works each and the steps between them "
                                  "are records too: worked by and steps say where they're kept");
                        continue;
                    }
                    if (!w->depends_on.empty() || !w->done_at.empty() || !w->notices.empty()) {
                        error(!w->depends_on.empty() ? w->depends_on_where : !w->done_at.empty() ? w->done_at_where : w->notices.front().where,
                              "done at, depends on and notify go with stages written in the code, like enum stage { … }, not kept as " + stage->name + " records");
                        continue;
                    }
                    if (w->worked_by.empty()) {
                        error(w->where, "say which field of " + stage->name + " lists the roles that work it, like worked by worked_by");
                        continue;
                    }
                    if (!lists_roles(field_named(*stage, w->worked_by))) {
                        error(w->worked_by_where, stage->name + "'s " + w->worked_by + " isn't a list of roles, like worked_by  list of role");
                        continue;
                    }
                    const entity_declaration* steps = w->steps.empty() || !entities.contains(full(ns, w->steps)) ? nullptr : entities[full(ns, w->steps)];
                    if (w->steps.empty()) {
                        error(w->where, "say which entity's records are the steps between stages, like steps step");
                        continue;
                    }
                    if (!steps) {
                        error(w->steps_where, "there's no entity " + w->steps + " for the workflow's steps");
                        continue;
                    }
                    bool shaped = true;
                    for (const char* end : {"from", "to"}) {
                        const field* f = field_named(*steps, end);
                        if (!f || f->list || !f->type || f->type->text() != stage->name) {
                            error(w->steps_where, "a step goes from one " + stage->name + " to another: " + steps->name + " needs " + end + "  " + stage->name);
                            shaped = false;
                        }
                    }
                    if (!lists_roles(field_named(*steps, "roles"))) {
                        error(w->steps_where, "a step says which roles take it: " + steps->name + " needs roles  list of role");
                        shaped = false;
                    }
                    if (!shaped || claimable(*w, *moving, ns, error).empty()) continue;

                    const std::string& e = w->entity;
                    // Whoever may take a step out of a stage works it too, as its own roles do.
                    std::string text = commanded(*w, "held(" + w->by + "." + w->worked_by + ") || exists(" + steps->name + " where from == " + e + "." + w->by +
                                                         " && held(roles))",
                                                 "exists(" + steps->name + " where from == was " + e + "." + w->by + " && to == " + e + "." + w->by +
                                                     " && held(roles))");
                    // What's worked, with every field of each, its stage, and who holds it.
                    std::string rows;
                    for (const auto& f : moving->fields) rows += "  " + f.name;
                    if (field_named(*stage, "title")) rows += "  " + w->by + ".title";
                    rows += "  " + w->by + "." + w->worked_by + "  held_by  held_by.name  held_until";
                    text += "view " + e + "_work per role {\n\treaders member\n\titems = each " + e + " where " + w->by + "." + w->worked_by +
                            " has role.id {\n\t\t" + rows.substr(2) + "\n\t}\n}\n";
                    said(*w, *moving, text, declarations, *path, made_here);
                }
                // A workflow that can't be said still has its commands, bare, so the roles
                // that allow them say nothing more than what's wrong with it.
                for (auto& [w, declarations, path, ns] : workflows) {
                    if (!entities.contains(full(ns, w->entity)) || said_in_full.contains(w)) continue;
                    std::string text;
                    for (const char* command : {"claim", "renew", "release", "move"}) {
                        if (!commands.contains(full(ns, w->entity + "::" + command))) text += "command " + w->entity + "::" + command + "\n";
                    }
                    diagnostics ignored;
                    file bare = parse("<define workflow>", text, ignored);
                    for (auto& d : bare.declarations) {
                        d.where = w->where;
                        made_here.emplace_back(declarations, std::move(d));
                    }
                }
                for (const auto& [agent, path, ns] : agents) agent_works(*agent, *path, ns);
                held_in_views();
                // Added once every workflow is said, so no entity found above has moved.
                for (auto& [declarations, d] : made_here) declarations->push_back(std::move(d));
            }
        };

    } // namespace lowering

    namespace lowering {

        // An entity's public when, said for a view of it or of what's within it: each of
        // its own fields read through the path from the view's entity, like visibility
        // as order.project.visibility. Nothing when it says more than fields and values.
        inline expression_ptr rooted(const expression& e, const entity_declaration& owner, const std::vector<std::string>& path, location where) {
            if (auto* n = std::get_if<name_expression>(&e.node)) {
                bool own = n->name.parts.size() == 1 &&
                           std::any_of(owner.fields.begin(), owner.fields.end(), [&](const field& f) { return f.name == n->name.parts[0]; });
                if (!own) {
                    auto out = std::make_unique<expression>();
                    out->where = where;
                    out->node = name_expression{qualified_name{n->name.parts, where}};
                    return out;
                }
                expression_ptr out = name(path[0], where);
                for (std::size_t i = 1; i < path.size(); ++i) out = member(std::move(out), path[i], where);
                return member(std::move(out), n->name.parts[0], where);
            }
            if (auto* l = std::get_if<literal_expression>(&e.node)) {
                auto out = std::make_unique<expression>();
                out->where = where;
                out->node = *l;
                return out;
            }
            if (auto* b = std::get_if<binary_expression>(&e.node)) {
                auto left = rooted(*b->left, owner, path, where), right = rooted(*b->right, owner, path, where);
                return left && right ? binary(b->op, std::move(left), std::move(right), where) : nullptr;
            }
            if (auto* u = std::get_if<unary_expression>(&e.node)) {
                auto operand = rooted(*u->operand, owner, path, where);
                if (!operand) return nullptr;
                auto out = std::make_unique<expression>();
                out->where = where;
                out->node = unary_expression{u->op, std::move(operand)};
                return out;
            }
            return nullptr;
        }

        // A condition as words, to compare one with another.
        inline std::string spoken(const expression& e) {
            if (auto* n = std::get_if<name_expression>(&e.node)) return n->name.text();
            if (auto* m = std::get_if<member_expression>(&e.node)) return spoken(*m->object) + "." + m->member;
            if (auto* l = std::get_if<literal_expression>(&e.node)) return "\"" + l->value + "\"";
            if (auto* b = std::get_if<binary_expression>(&e.node)) return "(" + spoken(*b->left) + " " + std::to_string(static_cast<int>(b->op)) + " " + spoken(*b->right) + ")";
            if (auto* u = std::get_if<unary_expression>(&e.node)) return std::to_string(static_cast<int>(u->op)) + spoken(*u->operand);
            return "?";
        }

        // Who may read the views of an entity, and of what's within it, said once on
        // it: a view per project, or per an order whose one field points at its
        // project, reads as the project says, unless it says its own readers. A view
        // whose own lines say just what its entity does has them taken out.
        inline void inherit_readers(std::vector<file>& files, diagnostics& out) {
            std::map<std::string, std::map<std::string, const entity_declaration*>> entities;  // by namespace, then name
            std::function<void(const std::vector<declaration>&, const std::string&)> find = [&](const std::vector<declaration>& ds, const std::string& ns) {
                for (const auto& d : ds) {
                    if (auto* n = std::get_if<namespace_declaration>(&d.node)) find(n->declarations, ns.empty() ? n->name : ns + "::" + n->name);
                    if (auto* e = std::get_if<entity_declaration>(&d.node)) entities[ns][e->name] = e;
                }
            };
            for (const auto& f : files) find(f.declarations, "");
            std::function<void(std::vector<declaration>&, const std::string&, const std::string&)> each = [&](std::vector<declaration>& ds, const std::string& ns, const std::string& path) {
                for (auto& d : ds) {
                    if (auto* n = std::get_if<namespace_declaration>(&d.node)) each(n->declarations, ns.empty() ? n->name : ns + "::" + n->name, path);
                    auto* v = std::get_if<view_declaration>(&d.node);
                    if (!v || !v->per) continue;
                    auto& here = entities[ns];
                    auto per = here.find(*v->per);
                    if (per == here.end()) continue;
                    // The entity that says who reads: the view's own, or the one its one
                    // field of that type points at.
                    const entity_declaration* owner = nullptr;
                    std::vector<std::string> through{*v->per};
                    if (per->second->readers || per->second->public_when) {
                        owner = per->second;
                    } else {
                        for (const auto& f : per->second->fields) {
                            if (!f.type || f.list || f.type->parts.size() != 1) continue;
                            auto it = here.find(f.type->parts[0]);
                            if (it == here.end() || !(it->second->readers || it->second->public_when)) continue;
                            if (owner) {
                                owner = nullptr;  // two ways in, so it's for the view to say
                                break;
                            }
                            owner = it->second;
                            through = {*v->per, f.name};
                        }
                    }
                    if (!owner) continue;
                    expression_ptr public_when = owner->public_when ? rooted(*owner->public_when, *owner, through, d.where) : nullptr;
                    bool says_readers = v->readers || !v->reader_people.empty();
                    if (!says_readers && !v->public_when) {
                        v->readers = owner->readers;
                        v->readers_where = d.where;
                        v->public_when = std::move(public_when);
                        continue;
                    }
                    // Lines that say only what the entity says.
                    bool same_readers = v->readers == owner->readers && v->reader_people.empty();
                    bool same_public = (!v->public_when && !public_when) ||
                                       (v->public_when && public_when && spoken(*v->public_when) == spoken(*public_when));
                    if (!same_readers || !same_public) continue;
                    const std::string why = "who may read view " + v->name + " is said by entity " + owner->name + "; take out its own readers";
                    if (v->readers) out.push_back({path, v->readers_line, why, fix{v->readers_line, v->readers_length, ""}});
                    if (v->public_when) out.push_back({path, v->public_line, why, fix{v->public_line, v->public_length, ""}});
                }
            };
            for (auto& f : files) each(f.declarations, "", f.path);
        }

    } // namespace lowering

    inline void lower(std::vector<file>& files, diagnostics& out) {
        lowering::inherit_readers(files, out);
        lowering::definer roles{out, {}};
        for (auto& f : files) roles.find(f.declarations, "", f.path);
        roles.define(files);
        lowering::workflower workflows{out, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}};
        for (auto& f : files) workflows.find(f.declarations, "", f.path);
        workflows.define();
        lowering::lowerer l{nullptr, out, {}};
        for (const auto& f : files) l.index(f.declarations);
        for (auto& f : files) {
            l.path = &f.path;
            l.lower(f.declarations);
        }
    }

} // namespace one::language
