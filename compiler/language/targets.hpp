// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Targets: the apps one build writes from a project's screens, targets web desktop
// terminal in its project block, the web alone when it says none. Each target says
// here, explicitly, every screen element and setting it draws; a screen using one
// that a target it's built for doesn't draw is an error, naming the element, where
// it is, and the target, so what the compiler answers is always complete. one build
// --targets builds fewer for one run.

#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::language {

    inline constexpr std::array<std::string_view, 3> all_targets{"web", "desktop", "terminal"};

    // Whether a list holds a value.
    template <class List, class Value>
    bool listed(const List& list, const Value& value) {
        return std::ranges::find(list, value) != std::ranges::end(list);
    }

    // One thing a screen can say, and which targets draw it.
    struct drawn_by {
        std::string_view name;  // like board.move
        std::string_view said;  // like "a board's move … along", in an error
        bool web, desktop, terminal;
    };

    // Every element of a screen, and each of their settings, as each target draws it.
    // The web draws everything; the desktop and the terminal gain each as their
    // toolkits' components are made.
    inline constexpr std::array support{
        drawn_by{"text", "text", true, false, false},
        drawn_by{"text.when", "a text's when", true, false, false},
        drawn_by{"code", "code", true, false, false},
        drawn_by{"markdown", "markdown", true, false, false},
        drawn_by{"markdown.pages", "markdown pages from files", true, false, false},
        drawn_by{"subtitle", "a subtitle", true, false, false},
        drawn_by{"link", "a link", true, false, false},
        drawn_by{"link.icon", "a link's icon", true, false, false},
        drawn_by{"link.built", "a link to what the site was built from", true, false, false},
        drawn_by{"hero", "a hero", true, false, false},
        drawn_by{"section", "a section", true, false, false},
        drawn_by{"section.color", "a section's color", true, false, false},
        drawn_by{"heading", "a heading", true, false, false},
        drawn_by{"menu", "a menu", true, false, false},
        drawn_by{"region", "a layout's region", true, false, false},
        drawn_by{"table", "a table", true, false, false},
        drawn_by{"table.link", "a table's link", true, false, false},
        drawn_by{"table.tabs", "a table's tabs, by … over", true, false, false},
        drawn_by{"table.by", "a table split by a choice", true, false, false},
        drawn_by{"table.search", "a table's search", true, false, false},
        drawn_by{"table.sort", "a table's sort", true, false, false},
        drawn_by{"table.page", "a table's pages", true, false, false},
        drawn_by{"table.reorder", "reordering a table", true, false, false},
        drawn_by{"table.hide", "a table's hide when empty", true, false, false},
        drawn_by{"table.only", "a table's only", true, false, false},
        drawn_by{"table.color", "a table's color", true, false, false},
        drawn_by{"table.tint", "a table's tint by", true, false, false},
        drawn_by{"table.track", "a table's track", true, false, false},
        drawn_by{"board", "a board", true, false, false},
        drawn_by{"board.link", "a board's link", true, false, false},
        drawn_by{"board.move", "a board's move … along", true, false, false},
        drawn_by{"board.search", "a board's search", true, false, false},
        drawn_by{"board.tint", "a board's tint by", true, false, false},
        drawn_by{"board.track", "a board's track", true, false, false},
        drawn_by{"board.reorder", "reordering a board", true, false, false},
        drawn_by{"cards", "cards", true, false, false},
        drawn_by{"cards.link", "cards' link", true, false, false},
        drawn_by{"cards.tally", "cards' tally", true, false, false},
        drawn_by{"cards.filter", "cards' filter", true, false, false},
        drawn_by{"details", "details", true, false, false},
        drawn_by{"details.tint", "details' tint by", true, false, false},
        drawn_by{"form", "a form", true, false, false},
        drawn_by{"confirm", "a confirm", true, false, false},
        drawn_by{"button", "a command's button", true, false, false},
        drawn_by{"button.when", "a button's when", true, false, false},
        drawn_by{"button.icon", "a button's icon", true, false, false},
        drawn_by{"button.along", "a command along a view's steps", true, false, false},
        drawn_by{"component", "a component", true, false, false},
        drawn_by{"thread", "a thread", true, false, false},
        drawn_by{"timeline", "a timeline", true, false, false},
        drawn_by{"timeline.link", "a timeline's link", true, false, false},
        drawn_by{"timeline.new", "a timeline's new when", true, false, false},
        drawn_by{"timeline.open", "a timeline's on open", true, false, false},
        drawn_by{"copy", "copy", true, false, false},
        drawn_by{"grid", "a grid", true, false, false},
        drawn_by{"diagram", "a diagram", true, false, false},
        drawn_by{"find", "find", true, false, false},
        drawn_by{"landing", "a landing design", true, false, false},
        drawn_by{"layout", "a screen's layout", true, false, false},
        drawn_by{"under", "a screen's under", true, false, false},
        drawn_by{"header.badge", "the header's badge", true, false, false},
        drawn_by{"header.path", "a header for a path", true, false, false},
        drawn_by{"footer", "the footer", true, false, false},
    };

    inline const drawn_by* support_of(std::string_view name) {
        for (const auto& s : support)
            if (s.name == name) return &s;
        return nullptr;
    }

    inline bool draws(const drawn_by& s, std::string_view target) {
        return target == "web" ? s.web : target == "desktop" ? s.desktop : target == "terminal" ? s.terminal : false;
    }

    // What a project builds: its targets, or the web.
    inline std::vector<std::string> targets_of(const std::vector<file>& files) {
        std::vector<std::string> out;
        for (const auto& f : files) {
            for (const auto& d : f.declarations) {
                auto* p = std::get_if<project_declaration>(&d.node);
                if (!p) continue;
                for (const auto& s : p->settings) {
                    if (s.key == "targets" && !listed(out, s.value)) out.push_back(s.value);
                }
            }
        }
        if (out.empty()) out.push_back("web");
        return out;
    }

    // The targets one build was asked for with --targets, while it checks and builds:
    // fewer than the project says, for one run.
    inline std::optional<std::vector<std::string>>& building_targets() {
        thread_local std::optional<std::vector<std::string>> asked;
        return asked;
    }

    namespace targets_detail {

        struct used {
            std::string_view name;
            location where;
        };

        inline void item(const screen_item& i, std::vector<used>& out);

        inline void items(const std::vector<screen_item>& all, std::vector<used>& out) {
            for (const auto& i : all) item(i, out);
        }

        inline void item(const screen_item& i, std::vector<used>& out) {
            auto use = [&](std::string_view name) { out.push_back({name, i.where}); };
            std::visit(
                [&](const auto& n) {
                    using T = std::decay_t<decltype(n)>;
                    if constexpr (std::is_same_v<T, content_block>) {
                        switch (n.type) {
                            case content_block::kind::hero: use("hero"); break;
                            case content_block::kind::section: use("section"); break;
                            case content_block::kind::menu: use("menu"); break;
                            case content_block::kind::region: use("region"); break;
                            case content_block::kind::heading: use("heading"); break;
                        }
                        if (!n.hue.empty()) use("section.color");
                        items(n.items, out);
                    } else if constexpr (std::is_same_v<T, content_text>) {
                        if (n.subtitle) use("subtitle");
                        else if (n.type == content_text::kind::code) use("code");
                        else if (n.type == content_text::kind::markdown) use(n.value.starts_with("{") ? "markdown" : "markdown.pages");
                        else use("text");
                        if (n.when) use("text.when");
                    } else if constexpr (std::is_same_v<T, content_link>) {
                        use(n.built.empty() ? "link" : "link.built");
                        if (n.icon) use("link.icon");
                    } else if constexpr (std::is_same_v<T, table_item>) {
                        use("table");
                        if (n.link) use("table.link");
                        if (n.by) use(n.by_over ? "table.tabs" : "table.by");
                        if (!n.search.empty()) use("table.search");
                        if (n.sort) use("table.sort");
                        if (n.page) use("table.page");
                        if (n.reorder) use("table.reorder");
                        if (n.hide_empty) use("table.hide");
                        if (n.only) use("table.only");
                        if (!n.hue.empty()) use("table.color");
                        if (n.tint) use("table.tint");
                        if (n.track) use("table.track");
                    } else if constexpr (std::is_same_v<T, board_item>) {
                        use("board");
                        if (n.link) use("board.link");
                        if (n.move) use("board.move");
                        if (!n.search.empty()) use("board.search");
                        if (n.tint) use("board.tint");
                        if (n.track) use("board.track");
                        if (n.reorder) use("board.reorder");
                    } else if constexpr (std::is_same_v<T, cards_item>) {
                        use("cards");
                        if (n.link) use("cards.link");
                        if (n.tally) use("cards.tally");
                        if (!n.filters.empty()) use("cards.filter");
                    } else if constexpr (std::is_same_v<T, details_item>) {
                        use("details");
                        if (n.tint) use("details.tint");
                    } else if constexpr (std::is_same_v<T, form_item>) {
                        use("form");
                    } else if constexpr (std::is_same_v<T, confirm_item>) {
                        use("confirm");
                    } else if constexpr (std::is_same_v<T, button_item>) {
                        use(n.along ? "button.along" : "button");
                        if (n.when) use("button.when");
                        if (n.icon) use("button.icon");
                    } else if constexpr (std::is_same_v<T, component_item>) {
                        use("component");
                    } else if constexpr (std::is_same_v<T, thread_item>) {
                        use("thread");
                    } else if constexpr (std::is_same_v<T, timeline_item>) {
                        use("timeline");
                        if (n.link) use("timeline.link");
                        if (n.fresh) use("timeline.new");
                        if (n.opened) use("timeline.open");
                    } else if constexpr (std::is_same_v<T, copy_item>) {
                        use("copy");
                    } else if constexpr (std::is_same_v<T, grid_item>) {
                        use(n.drawn ? "diagram" : "grid");
                    } else if constexpr (std::is_same_v<T, find_item>) {
                        use("find");
                    }
                },
                i.node);
        }

        // What each declaration of a file says that a target draws, wherever it is.
        inline void declarations(const std::vector<declaration>& all, std::vector<used>& out) {
            for (const auto& d : all) {
                if (auto* n = std::get_if<namespace_declaration>(&d.node)) {
                    declarations(n->declarations, out);
                } else if (auto* s = std::get_if<screen_declaration>(&d.node)) {
                    if (s->landing) out.push_back({"landing", s->landing_where});
                    if (s->layout) out.push_back({"layout", s->layout_where});
                    if (s->under) out.push_back({"under", s->under_where});
                    items(s->items, out);
                } else if (auto* h = std::get_if<header_declaration>(&d.node)) {
                    for (const auto& b : h->badges) out.push_back({"header.badge", b.where});
                    if (h->route) {
                        out.push_back({"header.path", h->route_where});
                        items(h->items, out);
                    }
                } else if (auto* f = std::get_if<footer_declaration>(&d.node)) {
                    out.push_back({"footer", d.where});
                    items(f->items, out);
                }
            }
        }

    }  // namespace targets_detail

    // Every screen element is drawn by every target the project is built for, or it's
    // an error saying which, where, and what to do.
    inline void check_targets(const std::vector<file>& files, diagnostics& out) {
        std::vector<std::string> targets = targets_of(files);
        // One that isn't a target is the settings' error, said once, there.
        std::erase_if(targets, [](const std::string& t) { return !listed(all_targets, t); });
        if (const auto& asked = building_targets()) {
            std::erase_if(targets, [&](const std::string& t) { return !listed(*asked, t); });
        }
        for (const auto& f : files) {
            std::vector<targets_detail::used> said;
            targets_detail::declarations(f.declarations, said);
            // An element, and the settings of it said with it, are one error for each target
            // that can't draw them: what it lacks of that element, said together.
            for (std::size_t at = 0; at < said.size();) {
                std::size_t end = at + 1;
                while (end < said.size() && said[end].where.line == said[at].where.line && said[end].where.column == said[at].where.column) ++end;
                auto lacking_in = [&](std::string_view target) {
                    std::vector<std::string_view> lacking;
                    for (std::size_t i = at; i < end; ++i) {
                        const drawn_by* s = support_of(said[i].name);
                        if (s && !draws(*s, target)) lacking.push_back(s->said);
                    }
                    return lacking;
                };
                std::string drawing;
                for (const auto& t : targets) {
                    if (lacking_in(t).empty()) drawing += (drawing.empty() ? "" : " ") + t;
                }
                for (const auto& t : targets) {
                    auto lacking = lacking_in(t);
                    if (lacking.empty()) continue;
                    std::string what;
                    for (std::size_t i = 0; i < lacking.size(); ++i) {
                        what += std::string(i == 0 ? "" : i + 1 == lacking.size() ? " and " : ", ") + std::string(lacking[i]);
                    }
                    out.push_back({f.path, said[at].where,
                                   what + (lacking.size() == 1 ? " isn't" : " aren't") + " drawn by the " + t + " app yet; take " + t + " out of targets" +
                                       (drawing.empty() ? "" : ", or build with --targets " + drawing)});
                }
                at = end;
            }
        }
    }

}  // namespace one::language
