// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <algorithm>
#include <compare>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "generators/web.hpp"

// The indexes Firestore needs to read a list kept to its newest rows, newest first,
// as the one library asks it to: for each way the list is picked by, the fields its
// conditions compare, then created_at, newest first. A list read whole needs none.
// A list a find box searches by words needs one too: the same fields, and the words.
// They're written to firestore.indexes.json, and the deploy declares each one, with
// the collections whose records expire, like suspensions after 90 days.

namespace one::generators {

    namespace indexes_detail {

        struct index_field {
            std::string name;
            bool contains = false;  // a list asked whether it holds something, like assignees has user.id
            auto operator<=>(const index_field&) const = default;
        };

        struct index {
            std::string collection;
            std::vector<index_field> fields;
            auto operator<=>(const index&) const = default;
        };

        inline std::string joined(const std::string& ns, const std::string& name) { return ns.empty() ? name : ns + "::" + name; }

        // Every entity, by its namespace and name.
        inline void entities_in(const std::string& ns, const std::vector<language::declaration>& declarations,
                                std::map<std::string, const language::entity_declaration*>& out) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) entities_in(joined(ns, n->name), n->declarations, out);
                if (auto* e = std::get_if<language::entity_declaration>(&d.node)) out[joined(ns, e->name)] = e;
            }
        }

        inline const language::field* field_of(const language::entity_declaration& e, const std::string& name) {
            for (const auto& f : e.fields) {
                if (f.name == name) return &f;
            }
            return nullptr;
        }

        // Whether a list is kept to its newest rows: a limit, and ordered by when each
        // was made, newest first, or not ordered, which is the same.
        inline bool newest_first(const language::view_each& each) {
            if (!each.limit) return false;
            if (each.order.empty()) return true;
            if (each.order.size() != 1) return false;
            auto* minus = std::get_if<language::unary_expression>(&each.order.front()->node);
            return minus && minus->op == language::token_kind::minus && web_detail::text_of(*minus->operand) == "created_at";
        }

        class finder {
        public:
            explicit finder(const std::vector<language::file>& files) {
                for (const auto& f : files) entities_in("", f.declarations, entities_);
                for (const auto& f : files) walk("", f.declarations);
                for (const auto& [ns, source] : finds_) searched(ns, *source);
            }

            std::set<index> found;
            // Collections whose records Firestore deletes once a date in them is past:
            // each suspension define role declares, at its until.
            std::set<std::string> expiring;

        private:
            std::map<std::string, const language::entity_declaration*> entities_;
            std::map<std::string, const language::view_declaration*> views_;
            std::vector<std::pair<std::string, const language::find_source*>> finds_;  // with the namespace it's in

            void items(const std::string& ns, const std::vector<language::screen_item>& all) {
                for (const auto& i : all) {
                    if (auto* block = std::get_if<language::content_block>(&i.node)) items(ns, block->items);
                    if (auto* box = std::get_if<language::find_item>(&i.node)) {
                        for (const auto& source : box->sources) finds_.emplace_back(ns, &source);
                    }
                }
            }

            // A find box's list, when it's found by a searchable field its rows don't
            // hold: the server asks for its rows holding a word, the newest first.
            void searched(const std::string& ns, const language::find_source& source) {
                std::string name = source.view.parts.size() == 1 ? joined(ns, source.view.parts[0]) : source.view.text();
                auto view = views_.find(name);
                if (view == views_.end()) return;
                std::string view_ns = name.rfind("::") == std::string::npos ? "" : name.substr(0, name.rfind("::"));
                for (const auto& each : view->second->each) {
                    if (!(each.name ? *each.name == source.list : source.list.empty()) || each.changes) continue;
                    auto* listed = std::get_if<language::name_expression>(&each.source->node);
                    auto entity = listed ? entities_.find(joined(view_ns, listed->name.parts.back())) : entities_.end();
                    if (entity == entities_.end()) return;
                    bool words = false;
                    for (const auto& by : source.by) {
                        const auto* f = field_of(*entity->second, by);
                        bool shown = std::any_of(each.rows.begin(), each.rows.end(), [&](const language::view_value& row) {
                            return (row.name ? *row.name : web_detail::text_of(*row.value)) == by;
                        });
                        words = words || (f && f->searchable && !shown);
                    }
                    if (!words) return;
                    std::vector<index_field> also{{"search_words", true}};
                    for (const auto& [within, _] : each.within) also.push_back({within, false});
                    list(view_ns, each, also);
                }
            }

            void walk(const std::string& ns, const std::vector<language::declaration>& declarations) {
                for (const auto& d : declarations) {
                    if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) walk(joined(ns, n->name), n->declarations);
                    if (auto* r = std::get_if<language::roles_declaration>(&d.node); r && r->defined) {
                        // A suspension after 90 days, and an audit's records once older than it keeps them.
                        for (const char* own : {"suspension", "audit"}) {
                            if (!entities_.contains(joined(ns, own))) continue;
                            std::string collection = joined(ns, own);
                            for (std::size_t at = collection.find("::"); at != std::string::npos; at = collection.find("::")) collection.replace(at, 2, "_");
                            expiring.insert(collection);
                        }
                    }
                    if (auto* screen = std::get_if<language::screen_declaration>(&d.node)) items(ns, screen->items);
                    auto* v = std::get_if<language::view_declaration>(&d.node);
                    if (!v) continue;
                    views_[joined(ns, v->name)] = v;
                    for (const auto& each : v->each) {
                        if (newest_first(each)) list(ns, each);
                    }
                }
            }

            void list(const std::string& ns, const language::view_each& each, const std::vector<index_field>& also = {}) {
                auto* source = std::get_if<language::name_expression>(&each.source->node);
                if (!source || source->name.parts.size() != 1) return;
                auto entity = entities_.find(joined(ns, source->name.parts[0]));
                if (entity == entities_.end()) return;
                std::string collection = entity->first;
                for (std::size_t at = collection.find("::"); at != std::string::npos; at = collection.find("::")) collection.replace(at, 2, "_");
                if (each.changes) collection += "_history";

                // The conditions joined by &&, one of which may be ways joined by ||.
                std::vector<const language::expression*> parts, ways;
                std::function<void(const language::expression&, std::vector<const language::expression*>&, language::token_kind)> split =
                    [&](const language::expression& x, std::vector<const language::expression*>& into, language::token_kind op) {
                        if (auto* b = std::get_if<language::binary_expression>(&x.node); b && b->op == op) {
                            split(*b->left, into, op);
                            split(*b->right, into, op);
                        } else {
                            into.push_back(&x);
                        }
                    };
                if (each.condition) split(*each.condition, parts, language::token_kind::logical_and);
                std::vector<const language::expression*> rest;
                for (const auto* part : parts) {
                    auto* b = std::get_if<language::binary_expression>(&part->node);
                    if (b && b->op == language::token_kind::logical_or) split(*part, ways, language::token_kind::logical_or);
                    else rest.push_back(part);
                }
                if (ways.empty()) {
                    add(collection, *entity->second, rest, also);
                    return;
                }
                for (const auto* way : ways) {
                    std::vector<const language::expression*> all = rest;
                    split(*way, all, language::token_kind::logical_and);
                    add(collection, *entity->second, all, also);
                }
            }

            // One way's index: the fields compared with == or has, in name order, then when
            // each was made. A != is checked on what comes back, so it needs no field, as
            // is an == none, which the store can't ask of a field that's missing.
            void add(const std::string& collection, const language::entity_declaration& entity, const std::vector<const language::expression*>& conditions,
                     const std::vector<index_field>& also) {
                std::vector<index_field> fields = also;
                for (const auto* part : conditions) {
                    auto* b = std::get_if<language::binary_expression>(&part->node);
                    if (!b || (b->op != language::token_kind::equal && b->op != language::token_kind::has)) continue;
                    if (web_detail::text_of(*b->right) == "none") continue;
                    std::string name;
                    bool has = b->op == language::token_kind::has;
                    if (auto* n = std::get_if<language::name_expression>(&b->left->node); n && n->name.parts.size() == 1) {
                        name = n->name.parts[0];
                    } else if (auto* m = std::get_if<language::member_expression>(&b->left->node)) {
                        std::string object = web_detail::text_of(*m->object);
                        if (object == entity.name) {
                            name = m->member;
                        } else if (field_of(entity, object)) {
                            // Through what it points at, like board.followers: asked as board in
                            // the boards the person follows.
                            name = object;
                            has = false;
                        }
                    }
                    if (name.empty()) continue;
                    index_field f{name, has};
                    if (std::find(fields.begin(), fields.end(), f) == fields.end()) fields.push_back(f);
                }
                if (fields.empty()) return;  // ordered by one field alone, which Firestore indexes itself
                std::sort(fields.begin(), fields.end());
                found.insert({collection, fields});
            }
        };

    } // namespace indexes_detail

    // firestore.indexes.json: each index a list kept to its newest rows needs.
    inline output_file indexes_file(const std::vector<language::file>& files) {
        indexes_detail::finder finder(files);
        std::string out = "{\n  \"indexes\": [";
        bool first = true;
        for (const auto& index : finder.found) {
            out += first ? "\n" : ",\n";
            first = false;
            out += "    { \"collection\": \"" + index.collection + "\", \"fields\": [";
            for (const auto& f : index.fields) {
                out += "{ \"field\": \"" + f.name + "\", " + (f.contains ? "\"array\": \"CONTAINS\"" : "\"order\": \"ASCENDING\"") + " }, ";
            }
            out += "{ \"field\": \"created_at\", \"order\": \"DESCENDING\" }] }";
        }
        out += first ? "]" : "\n  ]";
        // A suspension over 90 days old is final, and goes.
        if (!finder.expiring.empty()) {
            out += ",\n  \"expire\": [";
            bool next = false;
            for (const auto& collection : finder.expiring) {
                out += std::string(next ? "," : "") + "\n    { \"collection\": \"" + collection + "\", \"field\": \"until\" }";
                next = true;
            }
            out += "\n  ]";
        }
        out += "\n}\n";
        output_file f{"firestore.indexes.json", out};
        f.sources.assign(static_cast<std::size_t>(std::count(f.content.begin(), f.content.end(), '\n')), code::source{});
        return f;
    }

} // namespace one::generators
