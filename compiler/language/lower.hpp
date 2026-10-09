// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// What a project says in short, said in full before it's checked, so the checker and
// the generators see one form:
//     each issue in project          each issue where project == project.id
//     each change in issue in project    each change of issue where project == project.id
//     me, in a view                  user.id, the person reading it

#include <map>
#include <memory>
#include <type_traits>
#include <variant>
#include <string>
#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"

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

    } // namespace lowering

    inline void lower(std::vector<file>& files, diagnostics& out) {
        lowering::lowerer l{nullptr, out, {}};
        for (const auto& f : files) l.index(f.declarations);
        for (auto& f : files) {
            l.path = &f.path;
            l.lower(f.declarations);
        }
    }

} // namespace one::language
