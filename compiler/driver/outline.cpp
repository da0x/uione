// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/outline.hpp"

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "language/written.hpp"

namespace one::driver {

    namespace outline_detail {

        // The views and entities declared, by name and by namespace::name, for a
        // table to find its rows in.
        struct declared {
            std::map<std::string, const language::view_declaration*> views;
            std::map<std::string, const language::entity_declaration*> entities;
            std::string ns;  // the namespace being outlined

            template <typename T>
            const T* find(const std::map<std::string, const T*>& in, const std::string& name) const {
                if (!ns.empty()) {
                    if (auto at = in.find(ns + "::" + name); at != in.end()) return at->second;
                }
                auto at = in.find(name);
                return at == in.end() ? nullptr : at->second;
            }
        };

        inline void collect(const std::vector<language::declaration>& declarations, const std::string& ns, declared& out) {
            for (const auto& d : declarations) {
                std::string prefix = ns.empty() ? "" : ns + "::";
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) collect(n->declarations, prefix + n->name, out);
                if (auto* v = std::get_if<language::view_declaration>(&d.node)) out.views[prefix + v->name] = v;
                if (auto* e = std::get_if<language::entity_declaration>(&d.node)) out.entities[prefix + e->name] = e;
            }
        }

        inline std::string written(const language::expression& e) {
            if (auto* name = std::get_if<language::name_expression>(&e.node)) return name->name.text();
            if (auto* member = std::get_if<language::member_expression>(&e.node)) return written(*member->object) + "." + member->member;
            return "";
        }

        inline outlined_table table_of(const language::table_item& t, const declared& known) {
            outlined_table out;
            for (const auto& c : t.columns) out.columns.push_back({written(*c.value), c.label.value_or(""), c.label.has_value(), c.where.line, c.when_written});
            out.by = t.by.value_or("");
            out.search = t.search;
            if (t.sort) out.sort = (t.sort_descending ? "-" : "") + *t.sort;
            out.page = t.page.value_or(0);
            out.link = t.link.value_or("");
            const language::view_declaration* view = known.find(known.views, t.view.text());
            if (!view) return out;
            for (const auto& each : view->each) {
                if (each.name != t.list) continue;
                const language::entity_declaration* listed = nullptr;
                if (each.source && !each.changes) {
                    if (auto* name = std::get_if<language::name_expression>(&each.source->node)) listed = known.find(known.entities, name->name.text());
                }
                for (const auto& row : each.rows) {
                    std::string name = row.name ? *row.name : written(*row.value);
                    if (name.empty()) continue;
                    bool choices = false;
                    if (listed && !row.name) {
                        for (const auto& f : listed->fields) choices = choices || (f.name == name && !f.choices.empty());
                    }
                    out.rows.push_back({name, choices});
                }
            }
            return out;
        }

        inline std::string named(const language::qualified_name& view, const std::optional<std::string>& list) {
            return list ? view.text() + "." + *list : view.text();
        }

        inline outlined_item item_of(const language::screen_item& i, const declared& known) {
            outlined_item out;
            out.line = i.where.line;
            std::visit(
                [&](const auto& n) {
                    using T = std::decay_t<decltype(n)>;
                    if constexpr (std::is_same_v<T, language::content_block>) {
                        using kind = language::content_block::kind;
                        out.kind = n.type == kind::region ? "region" : n.type == kind::menu ? "menu" : n.type == kind::hero ? "hero" : n.type == kind::heading ? "heading" : "section";
                        out.subject = n.type == kind::region ? n.title : "";
                        if (n.type != kind::region) out.label = n.title;
                        for (const auto& inner : n.items) out.items.push_back(item_of(inner, known));
                    } else if constexpr (std::is_same_v<T, language::content_text>) {
                        using kind = language::content_text::kind;
                        out.kind = n.type == kind::markdown    ? "markdown"
                                 : n.type == kind::code        ? "code"
                                 : n.type == kind::overline    ? "overline"
                                 : n.type == kind::statement   ? "statement"
                                                               : "text";
                        out.label = n.value;
                    } else if constexpr (std::is_same_v<T, language::content_link>) {
                        out.kind = "link";
                        out.subject = n.target;
                        out.label = n.label;
                    } else if constexpr (std::is_same_v<T, language::table_item>) {
                        out.kind = "table";
                        out.subject = named(n.view, n.list);
                        out.table = table_of(n, known);
                    } else if constexpr (std::is_same_v<T, language::form_item>) {
                        out.kind = "form";
                        for (const auto& c : n.commands) out.subject += (out.subject.empty() ? "" : " ") + c.text();
                        out.label = n.submit.value_or("");
                    } else if constexpr (std::is_same_v<T, language::confirm_item>) {
                        out.kind = "confirm";
                        out.subject = n.command.text();
                        out.label = n.message;
                    } else if constexpr (std::is_same_v<T, language::button_item>) {
                        out.kind = "button";
                        out.subject = n.command.text();
                        out.label = n.label.value_or("");
                    } else if constexpr (std::is_same_v<T, language::component_item>) {
                        out.kind = "component";
                        out.subject = n.name;
                    } else if constexpr (std::is_same_v<T, language::thread_item>) {
                        out.kind = "thread";
                        out.subject = n.view.text() + "." + n.list;
                    } else if constexpr (std::is_same_v<T, language::timeline_item>) {
                        out.kind = "timeline";
                        out.subject = n.view.text() + "." + n.list;
                    } else if constexpr (std::is_same_v<T, language::copy_item>) {
                        out.kind = "copy";
                        out.subject = n.view.text();
                        out.label = n.label.value_or("");
                    } else if constexpr (std::is_same_v<T, language::details_item>) {
                        out.kind = "details";
                        out.subject = n.view.text();
                    } else if constexpr (std::is_same_v<T, language::grid_item>) {
                        out.kind = n.drawn ? "diagram" : "grid";
                        out.subject = named(n.view, n.list);
                    }
                },
                i.node);
            return out;
        }

        inline void screens_in(const std::string& path, const std::vector<language::declaration>& declarations,
                               declared& known, std::vector<outlined_screen>& out) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    std::string outer = known.ns;
                    known.ns = outer.empty() ? n->name : outer + "::" + n->name;
                    screens_in(path, n->declarations, known, out);
                    known.ns = outer;
                }
                auto* s = std::get_if<language::screen_declaration>(&d.node);
                if (!s) continue;
                outlined_screen screen{path, s->title, s->route, d.where.line, s->layout.value_or(""),
                                       s->layout ? s->layout_where.line : 0, {}};
                for (const auto& i : s->items) screen.items.push_back(item_of(i, known));
                out.push_back(std::move(screen));
            }
        }

    } // namespace outline_detail

    std::vector<outlined_screen> screens_of(const std::vector<language::file>& files) {
        std::vector<outlined_screen> out;
        outline_detail::declared known;
        for (const auto& f : files) outline_detail::collect(f.declarations, "", known);
        for (const auto& f : files) outline_detail::screens_in(f.path, f.declarations, known, out);
        return out;
    }

    std::optional<outline> outline_of(const std::vector<language::file>& files) {
        for (const auto& f : files) {
            for (const auto& d : f.declarations) {
                auto* p = std::get_if<language::project_declaration>(&d.node);
                if (!p) continue;
                auto settings = [](const std::vector<language::setting>& from) {
                    std::vector<outline::setting> out;
                    for (const auto& s : from) out.push_back({s.key, s.value, s.where.line});
                    return out;
                };
                outline o{f.path, p->name, d.where.line, p->end.line, settings(p->settings), {}};
                for (const auto& e : p->environments) o.environments.push_back({e.name, e.where.line, e.end.line, settings(e.settings)});
                return o;
            }
        }
        return std::nullopt;
    }

} // namespace one::driver
