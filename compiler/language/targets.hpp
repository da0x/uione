// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Targets: the apps one build writes from a project's screens, target web desktop
// terminal in its project block, the web alone when it says none. Each target says
// here, explicitly, every screen element and setting it draws; a screen using one
// that a target it's built for doesn't draw is an error, naming the element, where
// it is, and the target, so what the compiler answers is always complete. one build
// --target builds fewer for one run.

#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <span>
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
        std::string_view by;    // the targets drawing it, like "web desktop terminal"
    };

    // Every target draws it: a new target, like ios, draws nothing until it's added to
    // each, so the gallery, which uses everything, says all it lacks.
    inline constexpr std::string_view everywhere = "web desktop terminal";

    // Every element of a screen, and each of their settings, as each target draws it.
    inline constexpr std::array support{
        drawn_by{"text", "text", everywhere},
        drawn_by{"text.when", "a text's when", everywhere},
        drawn_by{"code", "code", everywhere},
        drawn_by{"markdown", "markdown", everywhere},
        drawn_by{"markdown.pages", "markdown pages from files", everywhere},
        drawn_by{"subtitle", "a subtitle", everywhere},
        drawn_by{"link", "a link", everywhere},
        drawn_by{"link.icon", "a link's icon", everywhere},
        drawn_by{"link.built", "a link to what the site was built from", everywhere},
        drawn_by{"link.step", "a link to the record before or after, in a list", everywhere},
        drawn_by{"hero", "a hero", everywhere},
        drawn_by{"section", "a section", everywhere},
        drawn_by{"section.color", "a section's color", everywhere},
        drawn_by{"section.icon", "a section's icon", everywhere},
        drawn_by{"section.steps", "a section as steps", everywhere},
        drawn_by{"overline", "an overline", everywhere},
        drawn_by{"statement", "a statement", everywhere},
        drawn_by{"heading", "a heading", everywhere},
        drawn_by{"menu", "a menu", everywhere},
        drawn_by{"region", "a layout's region", everywhere},
        drawn_by{"layout.workspace", "a workspace, filling the window", everywhere},
        drawn_by{"group", "a toolbar's group", everywhere},
        drawn_by{"options", "a tool that opens its choices, each with what it's for", everywhere},
        drawn_by{"toggle", "a toggle", everywhere},
        drawn_by{"undo", "undo and redo", everywhere},
        drawn_by{"messages", "messages", everywhere},
        drawn_by{"full_screen", "full screen", everywhere},
        drawn_by{"key", "a key", everywhere},
        drawn_by{"pick", "a pick", everywhere},
        drawn_by{"on_open", "what a screen does as it's opened", everywhere},
        drawn_by{"collage", "a collage", everywhere},
        drawn_by{"paper", "a paper", "web desktop"},
        drawn_by{"paper.insert", "a block inserted in a paper", "web desktop"},
        drawn_by{"paper.outline", "a paper's outline", "web desktop"},
        drawn_by{"paper.paste", "pictures pasted and dropped on a paper", "web desktop"},
        drawn_by{"paper.progress", "how far along a paper's been read", "web desktop"},
        drawn_by{"paper.warm", "a paper set on warm paper", "web desktop"},
        drawn_by{"paper.spread", "a paper's pages side by side", "web desktop"},
        drawn_by{"paper.read_only", "a paper read only", "web desktop"},
        drawn_by{"paper.size", "a paper made larger or smaller", "web desktop"},
        drawn_by{"collage.place", "placing a collage's layers", everywhere},
        drawn_by{"collage.snap", "snapping a collage's layers", everywhere},
        drawn_by{"collage.clear", "a clear collage", everywhere},
        drawn_by{"layers", "a collage's layers, each hidden or shown, previewed and ordered", everywhere},
        drawn_by{"collage.over", "buttons over a collage's picked layer", everywhere},
        drawn_by{"collage.add", "adding words to a collage", everywhere},
        drawn_by{"collage.write", "words written on a collage", everywhere},
        drawn_by{"collage.crop", "cropping a collage's picture", "web desktop"},
        drawn_by{"collage.draw", "a collage's layers drawn by a drag", "web desktop"},
        drawn_by{"collage.draw_line", "a collage's lines drawn by a drag", "web desktop"},
        drawn_by{"collage.draw_hand", "a collage's paths drawn by hand", "web desktop"},
        drawn_by{"collage.draw_points", "a collage's paths drawn node by node, as a pen does", "web desktop"},
        drawn_by{"collage.reshape", "a collage's paths reshaped by their nodes", "web desktop"},
        drawn_by{"collage.rotate", "a collage's layers turned by a drag past a corner", "web desktop"},
        drawn_by{"collage.group", "a collage's layers in groups, picked, moved and resized as one", "web desktop"},
        drawn_by{"collage.zoom", "a collage zoomed and moved as it's looked at", "web desktop"},
        drawn_by{"collage.order", "a collage's layers raised and lowered", "web desktop"},
        drawn_by{"collage.paste", "pasting and dropping pictures on a collage", everywhere},
        drawn_by{"collage.screenshot", "a screenshot added to a collage", "web desktop"},
        drawn_by{"pictures.choose", "pictures chosen from the device's files, onto a collage, a paper or cards", "web desktop"},
        drawn_by{"pictures.folder", "a folder of pictures chosen, onto a collage, a paper or cards", "web desktop"},
        drawn_by{"calendar", "a list's rows on the days a date of theirs says, a week side by side", "web desktop terminal"},
        drawn_by{"notify", "a row whose time has come said by the system, whatever screen is open", "web desktop terminal"},
        drawn_by{"notify.buttons", "buttons on what the system says, acting on its row", "web desktop"},
        drawn_by{"calendar.month", "a calendar as a month's grid while a condition holds", "web desktop terminal"},
        drawn_by{"calendar.move", "a calendar's row dragged to another day", "web desktop"},
        drawn_by{"collage.copy", "a collage copied as a picture", "web desktop"},
        drawn_by{"collage.save", "a collage saved as a picture", "web desktop"},
        drawn_by{"collage.svg", "a collage saved or copied as SVG", "web desktop"},
        drawn_by{"collage.preview", "a collage's preview", "web desktop"},
        drawn_by{"collage.match_colors", "matching a collage's colors", "web desktop"},
        drawn_by{"button.given", "a button's given values", everywhere},
        drawn_by{"button.slide", "a button's value along a slider", everywhere},
        drawn_by{"button.color", "a button's value any color, from a color picker", "web desktop"},
        drawn_by{"button.on", "a button's on when", everywhere},
        drawn_by{"button.then", "a button's then open", everywhere},
        drawn_by{"table.empty", "a table's empty", everywhere},
        drawn_by{"table", "a table", everywhere},
        drawn_by{"table.link", "a table's link", everywhere},
        drawn_by{"table.tabs", "a table's tabs, by … over", everywhere},
        drawn_by{"table.by", "a table split by a choice", everywhere},
        drawn_by{"table.search", "a table's search", everywhere},
        drawn_by{"table.sort", "a table's sort", everywhere},
        drawn_by{"table.page", "a table's pages", everywhere},
        drawn_by{"table.reorder", "reordering a table", everywhere},
        drawn_by{"table.picture", "a picture on each table row", "web desktop"},
        drawn_by{"picture", "a picture of a file a value holds", "web desktop"},
        drawn_by{"picture.alone", "a picture alone on its screen, zoomed as it's looked at", "web desktop"},
        drawn_by{"table.hide", "a table's hide when empty", everywhere},
        drawn_by{"table.only", "a table's only", everywhere},
        drawn_by{"table.color", "a table's color", everywhere},
        drawn_by{"table.tint", "a table's tint by", everywhere},
        drawn_by{"table.track", "a table's track", everywhere},
        drawn_by{"board", "a board", everywhere},
        drawn_by{"board.link", "a board's link", everywhere},
        drawn_by{"board.move", "a board's move … along", everywhere},
        drawn_by{"board.search", "a board's search", everywhere},
        drawn_by{"board.tint", "a board's tint by", everywhere},
        drawn_by{"board.track", "a board's track", everywhere},
        drawn_by{"board.reorder", "reordering a board", everywhere},
        drawn_by{"canvas", "a canvas", everywhere},
        drawn_by{"canvas.place", "placing a canvas's cards", everywhere},
        drawn_by{"pipeline", "a pipeline", everywhere},
        drawn_by{"cards", "cards", everywhere},
        drawn_by{"cards.link", "cards' link", everywhere},
        drawn_by{"cards.tally", "cards' tally", everywhere},
        drawn_by{"cards.filter", "cards' filter", everywhere},
        drawn_by{"cards.picture", "a picture on each card", "web desktop"},
        drawn_by{"cards.paste", "pictures pasted and dropped on cards, each made a record", "web desktop"},
        drawn_by{"cards.mosaic", "cards set as a mosaic of their pictures", "web desktop"},
        drawn_by{"cards.by", "cards under a heading for each year, month or day", "web desktop"},
        drawn_by{"chart", "chart", everywhere},
        drawn_by{"monitor", "monitor", everywhere},
        drawn_by{"details", "details", everywhere},
        drawn_by{"details.tint", "details' tint by", everywhere},
        drawn_by{"form", "a form", everywhere},
        drawn_by{"confirm", "a confirm", everywhere},
        drawn_by{"button", "a command's button", everywhere},
        drawn_by{"button.when", "a button's when", everywhere},
        drawn_by{"button.icon", "a button's icon", everywhere},
        drawn_by{"button.along", "a command along a view's steps", everywhere},
        drawn_by{"component", "a component", everywhere},
        drawn_by{"thread", "a thread", everywhere},
        drawn_by{"timeline", "a timeline", everywhere},
        drawn_by{"timeline.link", "a timeline's link", everywhere},
        drawn_by{"timeline.new", "a timeline's new when", everywhere},
        drawn_by{"timeline.open", "a timeline's on open", everywhere},
        drawn_by{"timeline.by", "a timeline of cards' years and months, scrolled to", "web desktop"},
        drawn_by{"copy", "copy", everywhere},
        drawn_by{"themes", "themes", everywhere},
        drawn_by{"grid", "a grid", everywhere},
        drawn_by{"diagram", "a diagram", everywhere},
        drawn_by{"find", "find", everywhere},
        drawn_by{"landing", "a landing design", everywhere},
        drawn_by{"layout", "a screen's layout", everywhere},
        drawn_by{"under", "a screen's under", everywhere},
        drawn_by{"header.badge", "the header's badge", everywhere},
        drawn_by{"header.path", "a header for a path", everywhere},
        drawn_by{"footer", "the footer", everywhere},
        // How a form asks for each field, by its kind.
        drawn_by{"field.text", "a text box", everywhere},
        drawn_by{"field.email", "an email box", everywhere},
        drawn_by{"field.date", "a date box", everywhere},
        drawn_by{"field.number", "a number box", everywhere},
        drawn_by{"field.slider", "a slider", everywhere},
        drawn_by{"field.markdown", "a box to write Markdown in", everywhere},
        drawn_by{"field.boolean", "a checkbox", everywhere},
        drawn_by{"field.list", "a list written separated by commas", everywhere},
        drawn_by{"field.choice", "a choice from a list that opens", everywhere},
        drawn_by{"field.radio", "radio buttons", everywhere},
        drawn_by{"field.pick", "picking one of a list", everywhere},
        drawn_by{"field.choices", "ticking several", everywhere},
        drawn_by{"field.file", "a file to attach", everywhere},
        drawn_by{"field.files", "files to attach", everywhere},
    };

    inline const drawn_by* support_of(std::string_view name, std::span<const drawn_by> table = support) {
        for (const auto& s : table)
            if (s.name == name) return &s;
        return nullptr;
    }

    inline bool draws(const drawn_by& s, std::string_view target) {
        for (std::size_t at = 0; at < s.by.size();) {
            std::size_t end = std::min(s.by.find(' ', at), s.by.size());
            if (s.by.substr(at, end - at) == target) return true;
            at = end + 1;
        }
        return false;
    }

    // What a project builds: its targets, or the web.
    inline std::vector<std::string> targets_of(const std::vector<file>& files) {
        std::vector<std::string> out;
        for (const auto& f : files) {
            for (const auto& d : f.declarations) {
                auto* p = std::get_if<project_declaration>(&d.node);
                if (!p) continue;
                for (const auto& s : p->settings) {
                    if (s.key == "target" && !listed(out, s.value)) out.push_back(s.value);
                }
            }
        }
        if (out.empty()) out.push_back("web");
        return out;
    }

    // The targets one build was asked for with --target, while it checks and builds:
    // fewer than the project says, for one run.
    inline std::optional<std::vector<std::string>>& building_targets() {
        thread_local std::optional<std::vector<std::string>> asked;
        return asked;
    }

    // The targets being checked and built now: the project's, or fewer with --target.
    inline std::vector<std::string> targets_built(const std::vector<file>& files) {
        std::vector<std::string> targets = targets_of(files);
        if (const auto& asked = building_targets()) {
            std::erase_if(targets, [&](const std::string& t) { return !listed(*asked, t); });
        }
        return targets;
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
                            case content_block::kind::group: use("group"); break;
                            case content_block::kind::options: use("options"); break;
                        }
                        if (!n.hue.empty()) use("section.color");
                        if (n.icon && n.type != content_block::kind::options) use("section.icon");
                        if (n.steps) use("section.steps");
                        items(n.items, out);
                    } else if constexpr (std::is_same_v<T, content_text>) {
                        if (n.type == content_text::kind::overline) use("overline");
                        else if (n.type == content_text::kind::statement) use("statement");
                        else if (n.subtitle) use("subtitle");
                        else if (n.type == content_text::kind::code) use("code");
                        else if (n.type == content_text::kind::markdown) use(n.value.starts_with("{") ? "markdown" : "markdown.pages");
                        else use("text");
                        if (n.when) use("text.when");
                    } else if constexpr (std::is_same_v<T, content_link>) {
                        use(!n.step.empty() ? "link.step" : n.built.empty() ? "link" : "link.built");
                        if (n.icon) use("link.icon");
                        if (!n.keys.empty()) use("key");
                    } else if constexpr (std::is_same_v<T, table_item>) {
                        use("table");
                        if (n.link) use("table.link");
                        if (n.by) use(n.by_over ? "table.tabs" : "table.by");
                        if (!n.search.empty()) use("table.search");
                        if (n.sort) use("table.sort");
                        if (n.page) use("table.page");
                        if (n.reorder) use("table.reorder");
                        if (n.picture) use("table.picture");
                        if (n.hide_empty) use("table.hide");
                        if (n.empty) use("table.empty");
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
                    } else if constexpr (std::is_same_v<T, pipeline_item>) {
                        use("pipeline");
                    } else if constexpr (std::is_same_v<T, canvas_item>) {
                        use("canvas");
                        if (n.place) use("canvas.place");
                    } else if constexpr (std::is_same_v<T, cards_item>) {
                        use("cards");
                        if (n.link) use("cards.link");
                        if (n.tally) use("cards.tally");
                        if (!n.filters.empty()) use("cards.filter");
                        if (n.picture) use("cards.picture");
                        if (n.paste) use("cards.paste");
                        if (n.mosaic) use("cards.mosaic");
                        if (n.by) use("cards.by");
                    } else if constexpr (std::is_same_v<T, chart_item>) {
                        use("chart");
                    } else if constexpr (std::is_same_v<T, monitor_item>) {
                        use("monitor");
                    } else if constexpr (std::is_same_v<T, details_item>) {
                        use("details");
                        if (n.tint) use("details.tint");
                    } else if constexpr (std::is_same_v<T, form_item>) {
                        use("form");
                        for (const auto& f : n.fields) {
                            if (f.kind.empty()) continue;
                            // The names it's looked up by live as long as the table.
                            for (const auto& s : support) {
                                if (s.name.substr(0, 6) == "field." && s.name.substr(6) == f.kind) out.push_back({s.name, f.where});
                            }
                        }
                    } else if constexpr (std::is_same_v<T, confirm_item>) {
                        use("confirm");
                    } else if constexpr (std::is_same_v<T, button_item>) {
                        use(n.along ? "button.along" : "button");
                        if (n.when) use("button.when");
                        if (n.icon) use("button.icon");
                        if (!n.keys.empty()) use("key");
                        if (!n.given.empty()) use("button.given");
                        if (std::ranges::any_of(n.given, [](const auto& v) { return v.range.has_value(); })) use("button.slide");
                        if (std::ranges::any_of(n.given, [](const auto& v) { return v.any_color; })) use("button.color");
                        if (n.pressed_when) use("button.on");
                        if (n.then_open) use("button.then");
                    } else if constexpr (std::is_same_v<T, component_item>) {
                        use("component");
                    } else if constexpr (std::is_same_v<T, thread_item>) {
                        use("thread");
                    } else if constexpr (std::is_same_v<T, timeline_item>) {
                        if (n.by) {
                            use("timeline.by");
                            return;
                        }
                        use("timeline");
                        if (n.link) use("timeline.link");
                        if (n.fresh) use("timeline.new");
                        if (n.opened) use("timeline.open");
                    } else if constexpr (std::is_same_v<T, themes_item>) {
                        use("themes");
                    } else if constexpr (std::is_same_v<T, copy_item>) {
                        use("copy");
                    } else if constexpr (std::is_same_v<T, grid_item>) {
                        use(n.drawn ? "diagram" : "grid");
                    } else if constexpr (std::is_same_v<T, find_item>) {
                        use("find");
                    } else if constexpr (std::is_same_v<T, toggle_item>) {
                        use("toggle");
                        if (!n.keys.empty()) use("key");
                    } else if constexpr (std::is_same_v<T, history_item>) {
                        use("undo");
                        if (!n.keys.empty()) use("key");
                    } else if constexpr (std::is_same_v<T, messages_item>) {
                        use("messages");
                    } else if constexpr (std::is_same_v<T, full_screen_item>) {
                        use("full_screen");
                        if (!n.keys.empty()) use("key");
                    } else if constexpr (std::is_same_v<T, pick_item>) {
                        use("pick");
                    } else if constexpr (std::is_same_v<T, layers_item>) {
                        use("layers");
                    } else if constexpr (std::is_same_v<T, collage_item>) {
                        use("collage");
                        if (n.place) use("collage.place");
                        if (n.snap) use("collage.snap");
                        if (n.clear) use("collage.clear");
                        if (n.over) use("collage.over");
                        if (n.add) use("collage.add");
                        if (n.write) use("collage.write");
                        if (n.crop) use("collage.crop");
                        if (!n.draws.empty()) use("collage.draw");
                        if (n.reshape) use("collage.reshape");
                        if (n.rotate) use("collage.rotate");
                        if (n.group) use("collage.group");
                        if (n.order) use("collage.order");
                        if (std::ranges::any_of(n.draws, [](const auto& d) { return d.by == "line"; })) use("collage.draw_line");
                        if (std::ranges::any_of(n.draws, [](const auto& d) { return d.by == "hand"; })) use("collage.draw_hand");
                        if (std::ranges::any_of(n.draws, [](const auto& d) { return d.by == "points"; })) use("collage.draw_points");
                        if (n.paste) use("collage.paste");
                        if (n.preview) use("collage.preview");
                        if (n.match_colors) use("collage.match_colors");
                    } else if constexpr (std::is_same_v<T, picture_item>) {
                        use("picture");
                        if (n.alone) use("picture.alone");
                    } else if constexpr (std::is_same_v<T, paper_item>) {
                        use("paper");
                        if (n.outline) use("paper.outline");
                        if (n.paste) use("paper.paste");
                        if (n.progress) use("paper.progress");
                        if (n.warm) use("paper.warm");
                        if (n.spread) use("paper.spread");
                        if (n.read_only) use("paper.read_only");
                    } else if constexpr (std::is_same_v<T, calendar_item>) {
                        use("calendar");
                        if (n.month) use("calendar.month");
                        if (n.move) use("calendar.move");
                    } else if constexpr (std::is_same_v<T, paper_insert>) {
                        use(n.sizes ? "paper.size" : "paper.insert");
                        if (!n.keys.empty()) use("key");
                    } else if constexpr (std::is_same_v<T, on_open_item>) {
                        use("on_open");
                        for (const auto& inner : n.run) item(inner, out);
                    } else if constexpr (std::is_same_v<T, collage_button>) {
                        using kind = collage_button::kind;
                        use(n.does == kind::crop         ? "collage.crop"
                            : n.does == kind::edit_words ? "collage.write"
                            : n.does == kind::paste      ? "collage.paste"
                            : n.does == kind::screenshot ? "collage.screenshot"
                            : n.does == kind::add_pictures ? "pictures.choose"
                            : n.does == kind::add_folder ? "pictures.folder"
                            : n.does == kind::copy || n.does == kind::cut ? "collage.copy"
                            : n.does == kind::save       ? "collage.save"
                            : n.does == kind::ungroup || n.does == kind::group_picked ? "collage.group"
                            : n.does == kind::raise || n.does == kind::lower ? "collage.order"
                            : n.does == kind::zoom_in || n.does == kind::zoom_out || n.does == kind::zoom_fit ? "collage.zoom"
                                                         : "collage.add");
                        if (n.svg) use("collage.svg");
                        if (!n.keys.empty()) use("key");
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
                    if (s->layout) out.push_back({*s->layout == "workspace" ? "layout.workspace" : "layout", s->layout_where});
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
                } else if (auto* n = std::get_if<notify_declaration>(&d.node)) {
                    out.push_back({"notify", d.where});
                    if (!n->buttons.empty()) out.push_back({"notify.buttons", d.where});
                }
            }
        }

    }  // namespace targets_detail

    // Every screen element is drawn by every target the project is built for, or it's
    // an error saying which, where, and what to do.
    // `table` is what each target draws: the support table, or another for a test.
    inline void check_targets(const std::vector<file>& files, diagnostics& out, std::span<const drawn_by> table = support) {
        std::vector<std::string> targets = targets_built(files);
        // One that isn't a target is the settings' error, said once, there.
        std::erase_if(targets, [](const std::string& t) { return !listed(all_targets, t); });
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
                        const drawn_by* s = support_of(said[i].name, table);
                        if (s && !draws(*s, target)) lacking.push_back(s->said);
                    }
                    return lacking;
                };
                std::string drawing;
                for (const auto& t : targets) {
                    // Between commas, as --target takes them.
                    if (lacking_in(t).empty()) drawing += (drawing.empty() ? "" : ",") + t;
                }
                for (const auto& t : targets) {
                    auto lacking = lacking_in(t);
                    if (lacking.empty()) continue;
                    std::string what;
                    for (std::size_t i = 0; i < lacking.size(); ++i) {
                        what += std::string(i == 0 ? "" : i + 1 == lacking.size() ? " and " : ", ") + std::string(lacking[i]);
                    }
                    out.push_back({f.path, said[at].where,
                                   what + (lacking.size() == 1 ? " isn't" : " aren't") + " drawn by the " + t + " app yet; take " + t + " out of target" +
                                       (drawing.empty() ? "" : ", or build with --target " + drawing)});
                }
                at = end;
            }
        }
    }

}  // namespace one::language
