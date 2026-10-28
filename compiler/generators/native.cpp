// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "generators/native.hpp"

#include <format>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <one/screens/palette.hpp>

#include "code/stream.hpp"
#include "generators/local.hpp"
#include "generators/markdown.hpp"
#include "generators/screens.hpp"
#include "generators/theme.hpp"
#include "generators/native_names.hpp"
#include "generators/output.hpp"
#include "language/ast.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace one::generators {

    class native_generator {
    public:
        // `target` is desktop or terminal.
        native_generator(const std::vector<language::file>& files, std::string project_dir, std::string target)
            : files_(files), model_(files), project_dir_(std::move(project_dir)), target_(std::move(target)) {
            for (const auto& f : files_) find_project(f.declarations, f.path);
        }

        std::vector<output_file> generate() {
            std::vector<output_file> out;
            std::vector<std::string> stems;
            for (const auto& f : files_) {
                auto found = model_.screens_in(f);
                if (found.empty()) continue;
                std::string stem = std::filesystem::path(f.path).stem().string();
                stems.push_back(stem);
                out.push_back(file("src/screens/" + stem + ".cpp", screen_file(f, found, stem)));
            }
            for (const auto& pages : pages_) out.push_back(file("src/pages/" + pages.name + ".hpp", pages_file(pages)));
            // A project that keeps its data here runs its commands and views itself.
            if (model_.project.local) {
                auto local = generate_local(files_);
                for (auto& f : local.files) {
                    local_sources_.push_back(f.path);
                    out.push_back(std::move(f));
                }
                installs_ = std::move(local.installs);
            }
            for (auto& component : component_files()) out.push_back(std::move(component));
            out.push_back(file("src/site.cpp", site_file()));
            out.push_back(file("src/app.hpp", app_hpp(stems)));
            out.push_back(file("src/main.cpp", main_cpp(stems)));
            out.push_back(file("CMakeLists.txt", cmake_lists(stems)));
            // What installing it puts beside it: the project's license, and for the desktop,
            // how the system lists it among its apps, and its icon.
            if (auto license = license_text()) out.push_back(copied("packaging/LICENSE", license->second, license->first));
            if (target_ == "desktop") {
                out.push_back(file("packaging/" + app_id() + ".desktop", desktop_entry()));
                if (!model_.project.icon.empty()) {
                    if (auto svg = platform::read_file(model_.project.icon)) out.push_back(copied("packaging/" + app_id() + ".svg", *svg, model_.project.icon));
                }
                // Fonts the theme brings, #embedded so the app needs nothing beside it.
                for (const auto& [family, path] : theme_look().font_files) {
                    if (auto bytes = platform::read_file(path)) out.push_back(copied("fonts/" + path.substr(path.find_last_of('/') + 1), *bytes, path));
                }
            }
            return out;
        }

    private:
        using stream = code::stream;

        const std::vector<language::file>& files_;
        screens::model model_;
        std::string project_dir_, target_;
        std::string domain_;  // where the app is served, like example.org
        const language::declaration* project_ = nullptr;
        std::string project_path_;
        const std::vector<language::field> no_fields_;
        // In a switch: the track every way the list is shown keeps to, and whose pick it is.
        const language::track_setting* group_track_ = nullptr;
        std::string group_key_;

        // What a screen being written reads: its views, by variable, and the entities
        // its address names, which views per them are read for.
        struct screen_parts {
            std::vector<std::pair<std::string, std::string>> views;  // variable, full view name
            std::set<std::string> params;                           // entities, like project
            std::set<std::string> picks;                            // the picks it reads, like layer
        };
        // The components the screens draw, each with its file: components/name.cpp beside
        // the .one file drawing it.
        std::map<std::string, std::string> components_;
        std::string screen_path_, route_, under_, title_;
        int item_line_ = 0;

        void find_project(const std::vector<language::declaration>& declarations, const std::string& path) {
            for (const auto& d : declarations) {
                if (auto* p = std::get_if<language::project_declaration>(&d.node)) {
                    project_ = &d;
                    project_path_ = path;
                    for (const auto& s : p->settings) {
                        if (s.key == "domain" && domain_.empty()) domain_ = s.value;
                    }
                } else if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    find_project(n->declarations, path);
                }
            }
        }

        static std::string str(std::string_view text) { return native_detail::literal(text); }

        std::string view_variable(screen_parts& parts, const std::string& full) {
            for (const auto& [variable, view] : parts.views) {
                if (view == full) return variable;
            }
            std::string variable = native_detail::identifier(full.substr(full.rfind(':') == std::string::npos ? 0 : full.rfind(':') + 1));
            for (const auto& [taken, _] : parts.views) {
                if (taken == variable) variable += "_";
            }
            parts.views.emplace_back(variable, full);
            return variable;
        }

        std::string id_of(screen_parts& parts, const std::string& entity) {
            parts.params.insert(entity);
            return native_detail::identifier(entity + "_id");
        }

        // A title or words with {view.field} in them, as parts: words("#"), live(issue_page, "number").
        std::string parts_of(screen_parts& parts, const std::vector<screens::title_part>& title) {
            std::string out;
            for (const auto& part : title) {
                // The year it's read in, and what the app was built from.
                std::string named = part.view.substr(part.view.rfind(':') == std::string::npos ? 0 : part.view.rfind(':') + 1);
                // A field of the row picked, like {layer.size}.
                if (part.live() && picks_.contains(named)) {
                    out += (out.empty() ? "" : ", ") + std::string("words(show(field_of(") + picked(parts, named) + ", " + str(part.field) + ")))";
                    continue;
                }
                std::string one = part.live() ? "live(" + view_variable(parts, part.view) + ", " + str(part.field) + ")" : "words(" + str(part.text) + ")";
                if (!part.live() && part.text == "{year}") one = "words(this_year())";
                if (part.live() && named == "build" && (part.field == "version" || part.field == "commit")) {
                    one = "words(build_" + part.field + ")";
                    std::erase_if(parts.views, [&](const auto& v) { return v.second == part.view; });
                }
                out += (out.empty() ? "" : ", ") + one;
            }
            return "{" + out + "}";
        }
        std::string parts_of(screen_parts& parts, const std::string& ns, std::string_view text) { return parts_of(parts, model_.title_parts(ns, text)); }

        // Words a link says, read now: said({...}).
        std::string said_of(screen_parts& parts, const std::string& ns, std::string_view text) {
            if (text.find('{') == std::string_view::npos) return str(text);
            return "said(" + parts_of(parts, ns, text) + ")";
        }

        // Conditions

        // A when, as C++ reading the page's views: false until each view it reads has arrived.
        std::string condition(screen_parts& parts, const std::string& ns, const language::expression& e) {
            std::set<std::string> read;
            std::string test = condition_of(parts, ns, e, read);
            std::string ready;
            for (const auto& variable : read) ready += "ready(" + variable + ") && ";
            return "(" + ready + test + ")";
        }

        static std::string compare(language::token_kind op, const std::string& left, const std::string& right) {
            using language::token_kind;
            switch (op) {
                case token_kind::equal: return "same(" + left + ", " + right + ")";
                case token_kind::not_equal: return "!same(" + left + ", " + right + ")";
                case token_kind::less: return "(compared(" + left + ", " + right + ") < 0)";
                case token_kind::greater: return "(compared(" + left + ", " + right + ") > 0)";
                case token_kind::less_equal: return "(compared(" + left + ", " + right + ") <= 0)";
                case token_kind::greater_equal: return "(compared(" + left + ", " + right + ") >= 0)";
                case token_kind::has: return "has(" + left + ", " + right + ")";
                case token_kind::plus: return "worked('+', " + left + ", " + right + ")";
                case token_kind::minus: return "worked('-', " + left + ", " + right + ")";
                case token_kind::star: return "worked('*', " + left + ", " + right + ")";
                case token_kind::slash: return "worked('/', " + left + ", " + right + ")";
                case token_kind::remainder: return "worked('%', " + left + ", " + right + ")";
                case token_kind::logical_and: return "(" + left + " && " + right + ")";
                default: return "(" + left + " || " + right + ")";
            }
        }

        std::string condition_of(screen_parts& parts, const std::string& ns, const language::expression& e, std::set<std::string>& read) {
            if (auto* binary = std::get_if<language::binary_expression>(&e.node)) {
                return compare(binary->op, condition_of(parts, ns, *binary->left, read), condition_of(parts, ns, *binary->right, read));
            }
            if (auto* unary = std::get_if<language::unary_expression>(&e.node)) return "!" + condition_of(parts, ns, *unary->operand, read);
            // What's picked, like layer: its row, or nothing while there's none.
            if (auto* name = std::get_if<language::name_expression>(&e.node); name && name->name.parts.size() == 1 && picks_.contains(name->name.text())) {
                return picked(parts, name->name.text());
            }
            // A toggle of the screen's, like layers: whether it's on.
            if (auto* name = std::get_if<language::name_expression>(&e.node); name && name->name.parts.size() == 1 && choices_.contains(name->name.text())) {
                return chosen(name->name.text());
            }
            if (auto* name = std::get_if<language::name_expression>(&e.node); name && name->name.parts.size() == 1 && toggles_.contains(name->name.text())) {
                return "p.toggled(" + str(route_ + " " + name->name.text()) + ")";
            }
            if (auto* member = std::get_if<language::member_expression>(&e.node)) {
                std::string field = member->member;
                const language::expression* object = member->object.get();
                while (auto* inner = std::get_if<language::member_expression>(&object->node)) {
                    field = inner->member + "." + field;
                    object = inner->object.get();
                }
                auto* view = std::get_if<language::name_expression>(&object->node);
                if (!view) return "value()";
                // poster.width, in an on open line's where: the row it's asking about.
                if (row_named_ && view->name.text() == *row_named_) return "field_of(row, " + str(field) + ")";
                // screen.width: the screen's, in its own pixels.
                if (view->name.text() == "screen" && !picks_.contains("screen")) return field == "width" ? "value(screen_width())" : "value(screen_height())";
                // layer.size: the picked row's.
                if (picks_.contains(view->name.text())) return "field_of(" + picked(parts, view->name.text()) + ", " + str(field) + ")";
                std::string variable = view_variable(parts, screens::full_view(ns, view->name.text()));
                read.insert(variable);
                return "at(" + variable + ", " + str(field) + ")";
            }
            return plain_value(e, "value()");
        }

        // on open: each line an opening, a row opened or a command run, decided once every
        // view they read is there.
        std::string on_open(screen_parts& parts, const std::string& ns, const std::vector<language::screen_item>& list) {
            std::set<std::string> read;
            std::string lines;
            for (const auto& item : list) {
                auto* o = std::get_if<language::on_open_item>(&item.node);
                if (!o) continue;
                std::string line;
                if (o->route) {
                    std::string variable = view_variable(parts, screens::full_view(ns, o->view.text()));
                    read.insert(variable);
                    line = ".open = " + str(model_.full_route(ns, *o->route)) + ", .view = " + variable + ", .list = " + str(o->list.value_or(""));
                    if (o->where) {
                        const language::entity_declaration* of = model_.listed(ns, o->view.text(), o->list);
                        row_named_ = of ? of->name : std::string("row");
                        line += ", .where = [&](const value& row) { return " + condition_of(parts, ns, *o->where, read) + "; }";
                        row_named_.reset();
                    }
                } else if (auto* b = std::get_if<language::button_item>(&o->run.front().node)) {
                    std::string made = command_of(parts, ns, *b);  // command(p, {...})
                    line = ".run = command_button" + made.substr(std::string("command(p, ").size(), made.size() - std::string("command(p, ").size() - 1);
                    if (b->when) (void)condition_of(parts, ns, *b->when, read);
                    for (const auto& v : b->given) (void)condition_of(parts, ns, *v.value, read);
                }
                // A command's own when is its button's, in its spec.
                if (o->route && item.when) line += ", .when = " + condition_of(parts, ns, *item.when, read);
                lines += (lines.empty() ? "" : ", ") + std::string("opening{") + line + "}";
            }
            std::string ready;
            for (const auto& variable : read) ready += (ready.empty() ? "" : " && ") + std::string("ready(") + variable + ")";
            return "on_open(p, " + (ready.empty() ? std::string("true") : ready) + ", {" + lines + "})";
        }

        // collage poster_page.layers size width height { ... }: its spec, with the buttons
        // over the picked layer as command_buttons it draws itself.
        std::string collage(screen_parts& parts, const std::string& ns, const language::collage_item& c) {
            std::string view = screens::full_view(ns, c.view.text());
            std::string out = "collage(p, {.view = " + view_variable(parts, view) + ", .list = " + str(c.list);
            out += ", .width = " + str(c.width) + ", .height = " + str(c.height);
            std::string kept;
            auto fields = model_.collage_fields(ns, c);
            for (const auto& [part, field] : fields) kept += (kept.empty() ? "" : ", ") + std::string("{") + str(part) + ", " + str(field) + "}";
            out += ", .fields = {" + kept + "}";
            if (c.place) out += ", .place = " + str(screens::full_command(ns, *c.place));
            if (c.reshape) out += ", .reshape = " + str(screens::full_command(ns, *c.reshape));
            if (c.snap) out += ", .snap = true";
            if (c.clear) out += ", .clear = true";
            if (c.match_colors) out += ", .match_colors = " + condition(parts, ns, *c.match_colors);
            if (c.empty) out += ", .empty = " + str(*c.empty);
            if (c.over) out += ", .pick = " + str(route_ + " " + *c.over);
            if (!c.tools.empty()) {
                std::string over;
                for (const auto& tool : c.tools) {
                    auto* b = std::get_if<language::button_item>(&tool.node);
                    if (!b) continue;  // what asks the collage itself goes in its asks
                    std::string made = command_of(parts, ns, *b);  // command(p, {...})
                    std::string spec = made.substr(std::string("command(p, ").size(), made.size() - std::string("command(p, ").size() - 1);
                    over += (over.empty() ? "" : ", ") + std::string("command_button") + spec;
                }
                out += ", .over = {" + over + "}";
            }
            auto per = model_.per_entity(view);
            if (per) out += ", .subject = " + id_of(parts, *per);
            if (workspace_) out += ", .fills = true";
            if (c.add) out += ", .add = " + str(screens::full_command(ns, *c.add));
            if (!c.draws.empty()) {
                std::string draws;
                for (const auto& d : c.draws) {
                    std::string given;
                    for (const auto& v : d.given) {
                        std::set<std::string> read;
                        given += (given.empty() ? "" : ", ") + std::string("{") + str(v.name) + ", " + condition_of(parts, ns, *v.value, read) + "}";
                    }
                    std::vector<std::string> names;
                    if (d.when) language::names_read(*d.when, names);
                    std::string back;
                    for (const auto& name : names) {
                        if (choices_.contains(name)) back += (back.empty() ? "" : ", ") + std::string("{") + str(route_ + " " + name) + ", " + str(choices_.at(name)) + "}";
                    }
                    draws += (draws.empty() ? "" : ", ") + std::string("collage_draw{.command = ") + str(screens::full_command(ns, d.command)) +
                             (given.empty() ? "" : ", .fields = value(std::map<std::string, value>{" + given + "})") + (d.when ? ", .when = " + condition(parts, ns, *d.when) : "") +
                             (d.by ? ", .by = " + str(*d.by) : "") + (back.empty() ? "" : ", .back = {" + back + "}") + "}";
                }
                out += ", .draws = {" + draws + "}";
            }
            if (c.crop) out += ", .crop = " + str(screens::full_command(ns, *c.crop));
            if (c.write) out += ", .write = " + str(screens::full_command(ns, *c.write));
            if (c.remove) out += ", .remove = " + str(screens::full_command(ns, *c.remove));
            // The field a new layer names what the view is per by, like poster.
            if (const language::entity_declaration* layer = model_.listed(ns, c.view.text(), c.list); layer && per) {
                for (const auto& f : layer->fields) {
                    if (f.type && f.type->text() == *per) out += ", .parent = " + str(f.name);
                }
            }
            // Held in layers: the list of them, how an object names its own, how one's made,
            // and the screen's pick of one.
            if (c.in_list) {
                out += ", .layers = " + str(*c.in_list) + ", .layer = " + str(c.in_field);
                if (const language::entity_declaration* group = model_.listed(ns, c.view.text(), *c.in_list)) {
                    out += ", .layer_create = " + str(screens::join(ns, group->name + "::create"));
                    for (const auto& f : group->fields) {
                        if (per && f.type && f.type->text() == *per) out += ", .layer_parent = " + str(f.name);
                    }
                }
                for (const auto& [name, of] : picks_) {
                    if (of.view == view && of.list == *c.in_list) out += ", .layer_pick = " + str(route_ + " " + name);
                }
            }
            out += ", .key = " + str(collage_key(c));
            std::string asks;
            for (const auto& tool : c.tools) {
                auto* does = std::get_if<language::collage_button>(&tool.node);
                if (!does) continue;
                std::string what = language::asked_of(*does, c.over), label = language::label_of(*does);
                asks += (asks.empty() ? "" : ", ") + std::string("collage_ask{.key = ") + str(collage_key(c)) + ", .does = " + str(what) + ", .label = " + str(label) +
                        ", .icon = " + str(does->icon.value_or("")) + (does->keys.empty() ? "" : ", .keys = " + keys_of(does->keys)) + "}";
            }
            if (!asks.empty()) out += ", .asks = {" + asks + "}";
            if (c.paste) out += ", .paste = true";
            bool sized = fields.contains("picture") && fields.contains("picture_width") && fields.contains("picture_height");
            if (const language::entity_declaration* layer = model_.listed(ns, c.view.text(), c.list); layer && sized) out += ", .update = " + str(screens::join(ns, layer->name + "::update"));
            if (c.preview) out += ", .preview = " + str(screens::full_command(ns, *c.preview)) + ", .preview_into = " + str(c.preview_into) + ", .preview_wide = " + std::to_string(c.preview_wide);
            return out + "})";
        }

        // layers drawing_page.layers { ... }: the collage's layers as a list, read as it reads them.
        std::string layers(screen_parts& parts, const std::string& ns, const language::layers_item& l, const language::collage_item& c) {
            std::string view = screens::full_view(ns, c.view.text());
            bool held = c.in_list && *c.in_list == l.list;
            std::string out = "layer_list(p, {.view = " + view_variable(parts, view) + ", .list = " + str(l.list) + ", .width = " + str(c.width) + ", .height = " + str(c.height);
            std::string kept;
            for (const auto& [part, field] : model_.collage_fields(ns, c)) kept += (kept.empty() ? "" : ", ") + std::string("{") + str(part) + ", " + str(field) + "}";
            out += ", .fields = {" + kept + "}";
            if (c.clear) out += ", .clear = true";
            if (held) {
                for (const auto& [name, of] : picks_) {
                    if (of.view == view && of.list == l.list) out += ", .pick = " + str(route_ + " " + name);
                }
            } else if (c.over) {
                out += ", .pick = " + str(route_ + " " + *c.over);
            }
            if (l.hide) out += ", .hide = " + str(screens::full_command(ns, *l.hide));
            if (l.order) out += ", .order = " + str(screens::full_command(ns, *l.order));
            if (l.remove) out += ", .remove = " + str(screens::full_command(ns, *l.remove));
            if (held) {
                out += ", .objects = " + str(c.list) + ", .object_layer = " + str(c.in_field);
                if (c.remove) out += ", .object_remove = " + str(screens::full_command(ns, *c.remove));
                if (c.over) out += ", .object_pick = " + str(route_ + " " + *c.over);
            }
            // A layer's rename, and an object's; where each row is an object, its own.
            const language::entity_declaration* object = model_.listed(ns, c.view.text(), c.list);
            for (const auto& rename : l.renames) {
                std::string command = screens::full_command(ns, rename);
                bool on_object = held && object && model_.entity_of_command(command) == object;
                out += std::string(on_object ? ", .object_rename = " : ", .rename = ") + str(command);
            }
            // A new layer from its header's +, as the collage makes one.
            if (const language::entity_declaration* group = held ? model_.listed(ns, c.view.text(), l.list) : nullptr) {
                std::string create = screens::join(ns, group->name + "::create");
                if (model_.entity_of_command(create)) {
                    out += ", .create = " + str(create);
                    auto per = model_.per_entity(view);
                    for (const auto& f : group->fields) {
                        if (per && f.type && f.type->text() == *per) out += ", .create_parent = " + str(f.name) + ", .subject = " + id_of(parts, *per);
                    }
                }
            }
            return out + "})";
        }

        // calendar planner.tasks by due { ... }: its rows on their days, picked as the
        // screen picks them, and moved by its command.
        std::string calendar(screen_parts& parts, const std::string& ns, const language::calendar_item& c) {
            std::string view = screens::full_view(ns, c.view.text());
            std::string list = c.list.empty() ? "rows" : c.list;
            std::string out = "calendar(p, {.view = " + view_variable(parts, view) + ", .list = " + str(list) + ", .by = " + str(c.by);
            if (c.month) out += ", .month = " + condition(parts, ns, *c.month);
            std::string columns;
            for (const auto& column : c.columns) {
                std::string field = screens::text_of(*column.value);
                columns += (columns.empty() ? "" : ", ") + std::string("{") + str(field) + ", " + str(column.label.value_or(screens::label(field))) + "}";
            }
            out += ", .columns = {" + columns + "}";
            if (c.move) out += ", .move = " + str(screens::full_command(ns, *c.move));
            if (std::string tint = tint_of(ns, c.view, c.list, c.tint); !tint.empty()) out += ", .tint = " + tint;
            for (const auto& [name, of] : picks_) {
                if (of.view == view && of.list == list) out += ", .pick = " + str(route_ + " " + name);
            }
            return out + ", .key = " + str(route_ + " " + list) + "})";
        }

        // paper document_page.blocks { ... }: its blocks' parts as the fields keeping them, its
        // commands, and the screen's pick of its blocks, which the caret's block is.
        std::string paper(screen_parts& parts, const std::string& ns, const language::paper_item& paper) {
            std::string view = screens::full_view(ns, paper.view.text());
            std::string out = "paper(p, {.view = " + view_variable(parts, view) + ", .list = " + str(paper.list);
            std::string kept;
            auto fields = model_.paper_fields(ns, paper);
            for (const auto& [part, field] : fields) kept += (kept.empty() ? "" : ", ") + std::string("{") + str(part) + ", " + str(field) + "}";
            out += ", .fields = {" + kept + "}";
            if (paper.write) {
                std::string write = screens::full_command(ns, *paper.write);
                out += ", .write = " + str(write);
                if (fields.contains("kind") && model_.changes(write, fields.at("kind"))) out += ", .writes_kind = true";
            }
            if (paper.add) out += ", .add = " + str(screens::full_command(ns, *paper.add));
            if (paper.remove) out += ", .remove = " + str(screens::full_command(ns, *paper.remove));
            auto per = model_.per_entity(view);
            if (const language::entity_declaration* block = model_.listed(ns, paper.view.text(), paper.list); block && per) {
                for (const auto& f : block->fields) {
                    if (f.type && f.type->text() == *per) out += ", .parent = " + str(f.name);
                }
            }
            if (per) out += ", .subject = " + id_of(parts, *per);
            for (const auto& [name, picked] : picks_) {
                if (picked.view == view && picked.list == paper.list) out += ", .pick = " + str(route_ + " " + name);
            }
            out += ", .key = " + str(paper_key_);
            if (paper.empty) out += ", .empty = " + str(*paper.empty);
            if (workspace_) out += ", .fills = true";
            if (paper.outline) out += ", .outline = " + (paper.outline_when ? condition(parts, ns, *paper.outline_when) + ", .outline_drawer = true" : std::string("true"));
            if (paper.paste) out += ", .paste = true";
            if (paper.progress) out += ", .progress = true";
            if (paper.warm) out += ", .warm = " + condition(parts, ns, *paper.warm);
            if (paper.spread) out += ", .spread = " + condition(parts, ns, *paper.spread);
            if (paper.read_only) out += ", .read_only = " + condition(parts, ns, *paper.read_only);
            return out + "})";
        }

        // What a collage and its buttons name it by: its pick, or the screen's address.
        // The fields the screen's collage keeps its parts' colors in, and its stroke's width.
        screens::collage_colors collage_colors(const std::string& ns) const {
            screens::collage_colors out;
            if (!collage_) return out;
            for (const auto& [part, field] : model_.collage_fields(ns, *collage_)) {
                if (part == "fill" || part == "color") out.fills.insert(field);
                if (part == "stroke") out.strokes.insert(field);
                if (part == "stroke_width") out.widths.insert(field);
            }
            out.width = collage_->width, out.height = collage_->height;
            return out;
        }

        std::string collage_key(const language::collage_item& c) const { return route_ + " " + (c.over ? *c.over : std::string("collage")); }

        // The picks anywhere in a screen.
        static std::vector<const language::pick_item*> picks_in(const std::vector<language::screen_item>& items) {
            std::vector<const language::pick_item*> out;
            for (const auto& item : items) {
                if (auto* p = std::get_if<language::pick_item>(&item.node)) out.push_back(p);
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    auto inside = picks_in(block->items);
                    out.insert(out.end(), inside.begin(), inside.end());
                }
            }
            return out;
        }

        // A pick the screen reads, as the variable holding its row.
        std::string picked(screen_parts& parts, const std::string& name) {
            parts.picks.insert(name);
            view_variable(parts, picks_.at(name).view);
            return native_detail::identifier("picked_" + name);
        }

        // The toggles anywhere in a screen, by their names.
        static std::set<std::string> toggles_in(const std::vector<language::screen_item>& items) {
            std::set<std::string> out;
            for (const auto& item : items) {
                if (auto* t = std::get_if<language::toggle_item>(&item.node)) out.insert(t->name);
                if (auto* block = std::get_if<language::content_block>(&item.node)) out.merge(toggles_in(block->items));
            }
            return out;
        }

        // The toggles over an enum anywhere in a screen, each by its enum, and the choice
        // it starts at: the first of them the screen shows.
        static std::map<std::string, std::string> choices_in(const std::vector<language::screen_item>& items) {
            std::map<std::string, std::string> out;
            for (const auto& item : items) {
                if (auto* t = std::get_if<language::toggle_item>(&item.node); t && t->choice) out.emplace(t->name, *t->choice);
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    for (auto& [name, first] : choices_in(block->items)) out.emplace(name, first);
                }
            }
            return out;
        }

        // What a toggle over an enum has chosen, like tool::pen, as a value.
        std::string chosen(const std::string& name) const {
            return "value(p.chosen(" + str(route_ + " " + name) + ", " + str(choices_.at(name)) + "))";
        }

        // key ctrl+z key ctrl+y: the keys a line is run by, as a list.
        static std::string keys_of(const std::vector<language::key_chord>& keys) {
            std::string all;
            for (const auto& k : keys) all += (all.empty() ? "" : ", ") + str(k.chord);
            return "std::vector<std::string>{" + all + "}";
        }

        // A literal, a choice, me, none, or a toggle of the screen's, as a value.
        std::string plain_value(const language::expression& e, const std::string& otherwise) {
            if (auto* name = std::get_if<language::name_expression>(&e.node)) {
                const std::string word = name->name.text();
                if (name->name.parts.size() == 1 && choices_.contains(word)) return chosen(word);
                if (name->name.parts.size() == 1 && toggles_.contains(word)) return "value(p.toggled(" + str(route_ + " " + word) + "))";
                if (word == "true" || word == "false") return "value(" + word + ")";
                if (word == "none") return "value()";
                if (word == "me") return "me(p)";
                if (name->name.parts.size() == 2) return "value(" + str(name->name.parts.back()) + ")";  // a choice, like status::open
            }
            if (auto* literal = std::get_if<language::literal_expression>(&e.node)) {
                // A time named, like tomorrow at 9:00, worked out as it's read.
                if (literal->type == language::literal_expression::kind::time) return "time_value(" + str(literal->value) + ")";
                return literal->type == language::literal_expression::kind::number ? "value(" + literal->value + ")" : "value(" + str(literal->value) + ")";
            }
            return otherwise;
        }

        // A row's when, as a function of the row: person != me is !same(field_of(row, "person"), me(p)).
        std::string row_condition(const language::expression& e) {
            if (auto* binary = std::get_if<language::binary_expression>(&e.node)) return compare(binary->op, row_condition(*binary->left), row_condition(*binary->right));
            if (auto* unary = std::get_if<language::unary_expression>(&e.node)) return "!" + row_condition(*unary->operand);
            std::string fixed = plain_value(e, "");
            if (!fixed.empty()) return fixed;
            return "field_of(row, " + str(screens::text_of(e)) + ")";
        }

        // A timeline's new when, of a row and the views it names as they were when the
        // page opened: created_at > news.seen is compared(field_of(row, "created_at"), field_of(read[0], "seen")) > 0.
        std::string fresh_condition(screen_parts& parts, const std::string& ns, const language::expression& e, std::vector<std::string>& views) {
            auto side = [&](const language::expression& operand) -> std::string {
                if (auto* member = std::get_if<language::member_expression>(&operand.node)) {
                    auto* object = std::get_if<language::name_expression>(&member->object->node);
                    if (!object) return "value()";
                    std::string variable = view_variable(parts, screens::full_view(ns, object->name.text()));
                    auto at = std::find(views.begin(), views.end(), variable);
                    if (at == views.end()) at = views.insert(views.end(), variable);
                    return "field_of(read[" + std::to_string(at - views.begin()) + "], " + str(member->member) + ")";
                }
                if (auto* binary = std::get_if<language::binary_expression>(&operand.node)) (void)binary;
                return row_condition(operand);
            };
            if (auto* binary = std::get_if<language::binary_expression>(&e.node)) {
                using language::token_kind;
                bool joined = binary->op == token_kind::logical_and || binary->op == token_kind::logical_or;
                if (joined) return compare(binary->op, fresh_condition(parts, ns, *binary->left, views), fresh_condition(parts, ns, *binary->right, views));
                return compare(binary->op, side(*binary->left), side(*binary->right));
            }
            if (auto* unary = std::get_if<language::unary_expression>(&e.node)) return "!" + fresh_condition(parts, ns, *unary->operand, views);
            return side(e);
        }

        // Roles

        // .allowed = allows(member_roles, "project", project_id, "issue::create", "role.may"), or nothing.
        std::string allowed(screen_parts& parts, const std::string& ns, const std::string& command) {
            auto may = model_.allowed(ns, route_, command);
            if (!may) return "";
            std::string roles = view_variable(parts, may->view), within = id_of(parts, may->within);
            if (may->by_record()) return "allows(" + roles + ", " + str(may->field) + ", " + within + ", " + str(may->command) + ", " + str(may->allows) + ")";
            return "holds(" + roles + ", " + str(may->field) + ", " + within + ", " + native_detail::strings(may->roles) + ")";
        }

        // Choices and columns

        static std::string options_of(const std::vector<std::pair<std::string, std::string>>& choices) {
            std::string out;
            for (const auto& [value, shown] : choices) out += (out.empty() ? "" : ", ") + std::string("{") + str(value) + ", " + str(shown) + "}";
            return "std::vector<one::screens::option>{" + out + "}";
        }

        std::string listing_of(screen_parts& parts, const screens::listing& l) {
            if (!l.within.empty()) parts.params.insert(l.within);
            return "list_choices(" + view_variable(parts, l.view) + ", " + str(l.list) + ", " + str(l.shown) + (l.person ? ", " + str(*l.person) : "") + ")";
        }

        std::string fields_of(screen_parts& parts, const std::string& ns, const language::form_item& form) {
            std::string out;
            for (const auto& f : model_.form_fields(ns, route_, form)) {
                for (const auto& looked : f.looked_at) listing_of(parts, looked);
                std::string one = "{.name = " + str(f.name);
                if (f.label) one += ", .label = " + str(*f.label);
                if (f.type != "text") one += ", .type = " + str(f.type);
                if (f.hint) one += ", .hint = " + str(*f.hint);
                if (f.from) one += ", .choices = " + listing_of(parts, *f.from);
                else if (f.options) one += ", .choices = " + options_of(*f.options);
                if (f.start) one += ", .start = " + str(*f.start);
                if (f.required) one += ", .required = true";
                if (f.range) one += ", .range = {" + f.range->from + ", " + f.range->to + ", " + f.range->by + "}";
                out += (out.empty() ? "" : ", ") + one + "}";
            }
            return "{" + out + "}";
        }

        // The route an item links to, in full, and what names it: .link, .keyed and .named.
        std::string link_settings(const std::string& ns, const std::string& route, const std::optional<std::string>& view = std::nullopt,
                                  const std::optional<std::string>& list = std::nullopt, bool named_too = true) {
            std::string target = model_.full_route(ns, route);
            std::string out = ".link = " + str(target);
            std::string last = target.substr(target.rfind("/:") == std::string::npos ? 0 : target.rfind("/:") + 2);
            // Or by a field of the rows that points at it, like a link's to in /:project/:to.
            if (view) {
                if (const auto* rows = model_.listed(ns, *view, list)) {
                    for (const auto& f : rows->fields) {
                        if (f.name == last && f.type && f.type->parts.size() == 1 && model_.entity_named(ns, f.type->parts[0])) last = f.type->parts[0];
                    }
                }
            }
            auto keyed = model_.keyed_by(ns, last, target);
            if (!keyed.empty()) out += ", .keyed = " + native_detail::strings(keyed);
            auto named = model_.named_by(ns, target);
            if (named_too && !named.empty()) {
                std::string n;
                for (const auto& [name, keys] : named) n += (n.empty() ? "" : ", ") + std::string("{") + str(name) + ", " + std::to_string(keys) + "}";
                out += ", .named = {" + n + "}";
            }
            return out;
        }

        // A choice column's values as they're shown, and a list of words: what a table, a
        // board and details say of an entity's fields.
        std::string choices_entry(const std::string& key, const language::field& f) {
            return "{" + str(key) + ", " + options_of(screens::model::choice_options(f)) + "}";
        }

        std::string tint_of(const std::string& ns, const language::qualified_name& view, const std::optional<std::string>& list, const std::optional<std::string>& field) {
            auto found = model_.tint_on(ns, view, list, field);
            if (!found) return "";
            return "tint_spec{" + str(found->field) + ", " + native_detail::strings(found->order) + "}";
        }

        // A list's track; in a switch, the switch's, whichever way it's shown, its pick
        // shared and drawn above it, with `more` said of it too, like its stages.
        std::string track_of(screen_parts& parts, const std::string& ns, const language::track_setting* setting, const std::string& more = "") {
            if (group_track_) setting = group_track_;
            if (!setting) return "";
            auto t = model_.track_of(ns, *setting);
            std::string columns;
            for (const auto& [key, label] : t.columns) columns += (columns.empty() ? "" : ", ") + std::string("{") + str(key) + ", " + str(label) + "}";
            std::string spec = "track_spec{" + str(t.field) + ", " + view_variable(parts, t.view) + ", " + str(t.list) + ", " + str(t.shown) + ", {" + columns + "}" +
                               (setting->hide_empty ? ", true" : "") + "}";
            std::string sets = setting->picked_last ? " t.picked_last = true;" : "";
            if (group_track_) sets += " t.key = " + str(group_key_) + "; t.above = true;" + more;
            if (sets.empty()) return spec;
            return "[&] { auto t = " + spec + ";" + sets + " return t; }()";
        }

        // What a switch's track works its dots out from: the stages of its board, or
        // else of its pipeline, as statements setting them on t.
        std::string track_stages(screen_parts& parts, const std::string& ns, const std::vector<language::screen_item>& list, std::size_t from, std::size_t to) {
            for (std::size_t i = from; i <= to; ++i) {
                if (auto* b = std::get_if<language::board_item>(&list[i].node)) {
                    std::string out = " t.by = " + str(b->by) + ";";
                    if (b->over.parts.empty()) {
                        const language::entity_declaration* entity = model_.listed(ns, b->view.text(), b->list);
                        if (!entity) return "";
                        auto stages = model_.stages_written(ns, *entity, b->by);
                        std::string listed, statuses;
                        auto quoted = [](const std::string& text) {
                            std::string out = "\"";
                            for (char c : text) out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
                            return out + "\"";
                        };
                        for (const auto& [id, title] : stages.stages) listed += (listed.empty() ? "" : ",") + std::string("{\"id\":") + quoted(id) + ",\"title\":" + quoted(title) + "}";
                        for (const auto& [stage, status] : stages.statuses) statuses += (statuses.empty() ? "" : ", ") + std::string("{") + str(stage) + ", " + str(status) + "}";
                        out += " t.over = state<view_state>(view_state{status::ready, ember::parse_json(" + str("{\"stages\":[" + listed + "]}") + ")}); t.over_list = \"stages\";";
                        if (!statuses.empty()) out += " t.statuses = {" + statuses + "};";
                    } else {
                        out += " t.over = " + view_variable(parts, screens::full_view(ns, b->over.text())) + "; t.over_list = " + str(b->over_list) + ";";
                        if (!b->status_field.empty()) out += " t.status_by = " + str(b->status_field) + ";";
                    }
                    return out;
                }
            }
            for (std::size_t i = from; i <= to; ++i) {
                if (auto* l = std::get_if<language::pipeline_item>(&list[i].node)) {
                    std::string out = " t.by = " + str(l->by) + "; t.over = " + view_variable(parts, screens::full_view(ns, l->over.text())) + "; t.over_list = " + str(l->over_list) + ";";
                    if (!l->status_field.empty()) out += " t.status_by = " + str(l->status_field) + ";";
                    return out;
                }
            }
            return "";
        }

        std::string reorder_of(screen_parts& parts, const std::string& ns, const std::optional<std::string>& field, const language::entity_declaration* entity, bool descending = false) {
            if (!field || !entity) return "";
            std::string command = screens::join(ns, entity->name + "::update");
            std::string may = allowed(parts, ns, command);
            if (descending) return "reorder_spec{" + str(command) + ", " + str(*field) + ", " + (may.empty() ? "true" : may) + ", true}";
            return "reorder_spec{" + str(command) + ", " + str(*field) + (may.empty() ? "" : ", " + may) + "}";
        }

        // Items

        struct written {
            std::string code;
            int line;
        };

        // Buttons for what a table's or a board's block names as commands, like New issue.
        std::string toolbar(screen_parts& parts, const std::string& ns, const std::vector<language::table_column>& columns, const std::vector<language::screen_item>& screen) {
            std::string tools = buttons(parts, ns, columns, screen);
            return tools.empty() ? "" : "[=, &p] { return " + tools + "; }";
        }

        // A list's buttons, like New issue: actions(...), each a command, opening its
        // form where the screen gives one.
        std::string buttons(screen_parts& parts, const std::string& ns, const std::vector<language::table_column>& columns, const std::vector<language::screen_item>& screen) {
            std::vector<std::string> tools;
            for (const auto& column : columns) {
                auto* named = std::get_if<language::name_expression>(&column.value->node);
                if (!named || named->name.parts.size() != 2) continue;
                std::string command = screens::full_command(ns, named->name);
                language::button_item button{named->name, column.label, nullptr, std::nullopt, "", column.icon, {}, {}, {}, nullptr, std::nullopt, {}, std::nullopt};
                if (const language::form_item* f = screens::model::form_for(ns, screen, command)) tools.push_back(form(parts, ns, *f, &button));
                else tools.push_back(command_of(parts, ns, button));
            }
            if (tools.empty()) return "";
            std::string joined;
            for (const auto& t : tools) joined += (joined.empty() ? "" : ", ") + t;
            return "actions(" + joined + ")";
        }

        std::string command_of(screen_parts& parts, const std::string& ns, const language::button_item& button) {
            std::string command = screens::full_command(ns, button.command);
            std::string out = "command(p, {.command = " + str(command) + ", .label = " + str(button.label ? *button.label : screens::label(command.substr(command.rfind("::") + 2)));
            std::string may = allowed(parts, ns, command);
            // A command on the entity the page's address names acts on that one.
            const language::entity_declaration* entity = model_.entity_of_command(command);
            bool named = entity && command.substr(command.rfind("::") + 2) != "create" && screens::names_parameter(route_, entity->name) && model_.entity_named(ns, entity->name);
            // A command on what's picked, like layer::delete, acts on the picked row, and
            // is there only while one is.
            std::string on_pick;
            if (!named && entity && command.substr(command.rfind("::") + 2) != "create") {
                for (const auto& [name, of] : picks_) {
                    if (of.entity == entity->name) {
                        on_pick = picked(parts, name);
                        break;
                    }
                }
            }
            // What it's sent, worked out as the screen's built, and pressed while what it
            // would set is already so.
            std::string given, same;
            for (const auto& v : button.given) {
                std::set<std::string> read;
                std::string value = condition_of(parts, ns, *v.value, read);
                given += (given.empty() ? "" : ", ") + std::string("{") + str(v.name) + ", " + value + "}";
                std::string now;
                // Only a field it stores can already be so; an input never is.
                bool stored = entity && std::ranges::any_of(entity->fields, [&](const language::field& f) { return f.name == v.name; });
                if (!stored) now = "";
                else if (!on_pick.empty()) now = "field_of(" + on_pick + ", " + str(v.name) + ")";
                else if (named) {
                    if (auto holding = model_.view_holding(ns, entity->name, v.name)) now = "at(" + view_variable(parts, *holding) + ", " + str(v.name) + ")";
                }
                same = now.empty() || same == "-" ? "-" : (same.empty() ? "" : same + " && ") + "same(" + now + ", " + value + ")";
            }
            // A create sends what the page's address names, as a create form does.
            if (entity && command.substr(command.rfind("::") + 2) == "create") {
                for (const auto& field : entity->fields) {
                    if (!field.type || field.type->parts.size() != 1) continue;
                    const std::string& points = field.type->parts[0];
                    bool written = std::ranges::any_of(button.given, [&](const auto& v) { return v.name == field.name; });
                    if (written || !screens::names_parameter(route_, points) || !model_.entity_named(ns, points)) continue;
                    given += (given.empty() ? "" : ", ") + std::string("{") + str(field.name) + ", value(" + id_of(parts, points) + ")}";
                }
            }
            if (!given.empty()) out += ", .fields = value(std::map<std::string, value>{" + given + "})";
            if (!may.empty()) out += ", .allowed = " + may;
            if (named) out += ", .id = " + id_of(parts, entity->name);
            if (!on_pick.empty()) out += ", .id = " + on_pick + "[\"id\"].as_string()";
            std::string shown_when = button.when ? condition(parts, ns, *button.when) : "";
            if (!on_pick.empty()) shown_when = shown_when.empty() ? "!none(" + on_pick + ")" : "(!none(" + on_pick + ") && " + shown_when + ")";
            if (!shown_when.empty()) out += ", .when = " + shown_when;
            if (button.icon) out += ", .icon = " + str(*button.icon);
            if (std::string swatch = screens::swatch_of(button, collage_colors(ns)); !swatch.empty()) out += ", .swatch = " + str(swatch);
            if (!button.keys.empty()) out += ", .keys = " + keys_of(button.keys);
            if (button.pressed_when) out += ", .pressed = " + condition(parts, ns, *button.pressed_when);
            else if (!same.empty() && same != "-") out += ", .pressed = " + same;
            if (button.then_open) out += ", .then_open = " + str(model_.full_route(ns, *button.then_open));
            if (button.about) out += ", .about = " + str(*button.about);
            return out + "})";
        }

        std::string form(screen_parts& parts, const std::string& ns, const language::form_item& form_item, const language::button_item* button) {
            std::string command = screens::full_command(ns, form_item.commands.front());
            const language::entity_declaration* entity = model_.entity_of_command(command);
            std::string action = command.substr(command.rfind("::") + 2);
            std::string out = "form(p, {.command = " + str(command) + ", .fields = " + fields_of(parts, ns, form_item);
            if (button) out += ", .button = true";
            // An update starts from the entity's page view and acts on the entity the address names.
            std::string from, id, after;
            if (entity && action != "create") {
                auto inputs = model_.inputs_of(command);
                bool only_inputs = std::all_of(form_item.fields.begin(), form_item.fields.end(), [&](const auto& f) { return inputs.contains(f.name); });
                if (only_inputs && screens::names_parameter(route_, entity->name)) {
                    id = id_of(parts, entity->name);
                } else if (auto view = model_.edit_view(ns, *entity, form_item)) {
                    from = view_variable(parts, *view);
                    id = id_of(parts, entity->name);
                }
                // Once what the address names is deleted, the form goes to the page above it.
                if (action == "delete" && screens::names_parameter(route_, entity->name)) after = model_.after_delete(route_, under_, entity->name);
            }
            if (!from.empty()) out += ", .from = " + from;
            if (!id.empty()) out += ", .id = " + id;
            // A create form on a screen whose address names what the entity points at sends that.
            std::string given;
            if (entity && action == "create") {
                for (const auto& field : entity->fields) {
                    if (!field.type || field.type->parts.size() != 1) continue;
                    const std::string& points = field.type->parts[0];
                    bool asked = std::any_of(form_item.fields.begin(), form_item.fields.end(), [&](const auto& f) { return f.name == field.name; });
                    if (asked || !screens::names_parameter(route_, points) || !model_.entity_named(ns, points)) continue;
                    given += (given.empty() ? "" : ", ") + std::string("{") + str(field.name) + ", " + id_of(parts, points) + "}";
                }
            }
            if (!given.empty()) out += ", .given = {" + given + "}";
            if (form_item.submit) out += ", .submit = " + str(*form_item.submit);
            if (button && button->label) out += ", .opener = " + str(*button->label);
            if (button && button->when) out += ", .when = " + condition(parts, ns, *button->when);
            if (std::string may = allowed(parts, ns, command); !may.empty()) out += ", .allowed = " + may;
            if (!after.empty()) out += ", .after = " + str(after);
            if (button && button->icon) out += ", .icon = " + str(*button->icon);
            if (button && button->then_open) out += ", .then_open = " + str(model_.full_route(ns, *button->then_open));
            return out + "})";
        }

        std::string table(screen_parts& parts, const std::string& ns, const language::table_item& t, const std::vector<language::screen_item>& screen) {
            const language::entity_declaration* entity = model_.listed(ns, t.view.text(), t.list);
            std::string columns, actions, people, labels;
            std::vector<std::string> choices;
            for (const auto& column : t.columns) {
                std::string key = screens::text_of(*column.value);
                if (key.find("::") != std::string::npos) continue;  // a button in its toolbar
                if (key.find('.') == std::string::npos) {
                    if (auto command = model_.row_command(ns, t.view.text(), t.list, key)) {
                        std::string action = "{.command = " + str(*command) + ", .label = " + str(column.label ? *column.label : screens::label(key));
                        if (std::string may = allowed(parts, ns, *command); !may.empty()) action += ", .allowed = " + may;
                        if (column.when) action += ", .when = [&p](const value& row) { return " + row_condition(*column.when) + "; }";
                        if (const language::form_item* asks = screens::model::form_for(ns, screen, *command)) {
                            action += ", .form = " + fields_of(parts, ns, *asks);
                            if (asks->submit) action += ", .submit = " + str(*asks->submit);
                        }
                        if (column.icon) action += ", .icon = " + str(*column.icon);
                        actions += (actions.empty() ? "" : ", ") + action + "}";
                        continue;
                    }
                }
                if (entity && key.find('.') == std::string::npos) {
                    for (const auto& f : entity->fields) {
                        if (f.name != key) continue;
                        if (f.list && f.type && f.type->text() == "text") labels += (labels.empty() ? "" : ", ") + str(key);
                        if (!f.choices.empty()) choices.push_back(choices_entry(key, f));
                    }
                }
                auto shown = model_.shown_as(ns, t.view.text(), t.list, entity, key, column.label);
                if (shown.picture) people += (people.empty() ? "" : ", ") + std::string("{") + str(shown.key) + ", " + str(*shown.picture) + "}";
                columns += (columns.empty() ? "" : ", ") + std::string("{") + str(shown.key) + ", " + str(shown.label) + "}";
            }
            if (t.by && t.by_over) {
                auto over = model_.listed_columns(ns, t.by_over->text(), t.by_over_list);
                std::string shown = std::find(over.begin(), over.end(), "title") != over.end() ? "title" : "name";
                choices.push_back("{" + str(*t.by) + ", list_choices(" + view_variable(parts, screens::full_view(ns, t.by_over->text())) + ", " + str(t.by_over_list) + ", " + str(shown) + ")}");
            }
            std::string out = "table(p, {.view = " + view_variable(parts, screens::full_view(ns, t.view.text()));
            if (t.list) out += ", .list = " + str(*t.list);
            out += ", .columns = {" + columns + "}";
            if (t.link) out += ", " + link_settings(ns, *t.link, t.view.text(), t.list);
            if (!actions.empty()) out += ", .actions = {" + actions + "}";
            if (!people.empty()) out += ", .people = {" + people + "}";
            if (!choices.empty()) {
                std::string c;
                for (const auto& one : choices) c += (c.empty() ? "" : ", ") + one;
                out += ", .choices = {" + c + "}";
            }
            if (!labels.empty()) out += ", .labels = {" + labels + "}";
            if (t.by) out += ", .by = " + str(*t.by);
            if (!t.search.empty()) out += ", .search = " + native_detail::strings(t.search);
            if (t.sort) out += ", .sort = " + str((t.sort_descending ? "-" : "") + *t.sort);
            if (t.page) out += ", .page = " + std::to_string(*t.page);
            if (std::string tools = toolbar(parts, ns, t.columns, screen); !tools.empty()) out += ", .tools = " + tools;
            if (t.hide_empty) out += ", .hide_empty = true";
            if (t.only) out += ", .only = " + only_of(*t.only);
            if (t.download) out += ", .download = " + str(*t.download);
            if (std::string tint = tint_of(ns, t.view, t.list, t.tint); !tint.empty()) out += ", .tint = " + tint;
            if (std::string track = track_of(parts, ns, t.track ? &*t.track : nullptr); !track.empty()) out += ", .track = " + track;
            if (std::string reorder = reorder_of(parts, ns, t.reorder, entity, t.reorder_descending); !reorder.empty()) out += ", .reorder = " + reorder;
            if (!t.hue.empty()) out += ", .color = " + str(t.hue);
            if (t.empty) out += ", .empty = " + str(*t.empty);
            // A row picked with a click, when the screen picks one of this list.
            for (const auto& [name, of] : picks_) {
                if (of.view == screens::full_view(ns, t.view.text()) && of.list == (t.list ? *t.list : "rows")) out += ", .pick = " + str(route_ + " " + name);
            }
            if (t.picture) out += ", .picture = " + str(*t.picture) + (t.picture_sits == "fit" ? ", .fit = true" : "");
            return out + "})";
        }

        // A table's only, as the address's filters are written: due<= +2w is {"due<=", "+2w"}.
        std::string only_of(const language::expression& condition) {
            std::string out;
            std::function<void(const language::expression&)> walk = [&](const language::expression& e) {
                auto* binary = std::get_if<language::binary_expression>(&e.node);
                if (!binary) return;
                if (binary->op == language::token_kind::logical_and) {
                    walk(*binary->left);
                    walk(*binary->right);
                    return;
                }
                using language::token_kind;
                std::string op = binary->op == token_kind::less ? "<" : binary->op == token_kind::greater ? ">" : binary->op == token_kind::less_equal ? "<="
                               : binary->op == token_kind::greater_equal ? ">=" : binary->op == token_kind::not_equal ? "!" : "";
                std::string field = screens::text_of(*binary->left);
                std::string wanted;
                if (auto* literal = std::get_if<language::literal_expression>(&binary->right->node)) wanted = literal->value;
                else wanted = screens::text_of(*binary->right);
                out += (out.empty() ? "" : ", ") + std::string("{") + str(field + op) + ", " + str(wanted) + "}";
            };
            walk(condition);
            return "{" + out + "}";
        }

        std::string board(screen_parts& parts, const std::string& ns, const language::board_item& b, const std::vector<language::screen_item>& screen,
                          const language::table_item* beside = nullptr) {
            const language::entity_declaration* entity = model_.listed(ns, b.view.text(), b.list);
            std::string columns, people, labels;
            for (const auto& column : b.columns) {
                std::string key = screens::text_of(*column.value);
                if (key.find("::") != std::string::npos) continue;
                auto shown = model_.shown_as(ns, b.view.text(), b.list, entity, key, column.label);
                if (shown.picture) people += (people.empty() ? "" : ", ") + std::string("{") + str(shown.key) + ", " + str(*shown.picture) + "}";
                columns += (columns.empty() ? "" : ", ") + std::string("{") + str(shown.key) + ", " + str(shown.label) + "}";
                for (const auto& f : (entity ? entity->fields : no_fields_)) {
                    if (f.name == shown.key && f.list && f.type && f.type->text() == "text") labels += (labels.empty() ? "" : ", ") + str(shown.key);
                }
            }
            // Stages written in the code are its columns, and its workflow's steps the
            // moves, given as views that are always there.
            bool written = b.over.parts.empty();
            auto stages = written && entity ? model_.stages_written(ns, *entity, b.by) : screens::model::written_stages{};
            auto fixed = [&](const std::string& json) { return "state<view_state>(view_state{status::ready, ember::parse_json(" + str(json) + ")})"; };
            auto quoted = [](const std::string& text) {
                std::string out = "\"";
                for (char c : text) out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
                return out + "\"";
            };
            std::string over;
            if (written) {
                std::string listed;
                for (const auto& [id, title] : stages.stages) listed += (listed.empty() ? "" : ",") + std::string("{\"id\":") + quoted(id) + ",\"title\":" + quoted(title) + "}";
                over = fixed("{\"stages\":[" + listed + "]}");
            } else {
                over = view_variable(parts, screens::full_view(ns, b.over.text()));
            }
            std::string out = "board(p, {.view = " + view_variable(parts, screens::full_view(ns, b.view.text())) + ", .list = " + str(*b.list) + ", .by = " + str(b.by) +
                              ", .over = " + over + ", .over_list = " + str(written ? "stages" : b.over_list);
            // A card picked with a click, when the screen picks one of this list.
            std::string picks;
            for (const auto& [name, of] : picks_) {
                if (of.view == screens::full_view(ns, b.view.text()) && of.list == *b.list) picks = route_ + " " + name;
            }
            if (written) out += ", .shown = \"title\"";
            for (const auto& column : written ? std::vector<std::string>{} : model_.listed_columns(ns, b.over.text(), b.over_list)) {
                if (column == "title") out += ", .shown = \"title\"";
            }
            out += ", .columns = {" + columns + "}";
            if (b.link) out += ", " + link_settings(ns, *b.link);
            if (!people.empty()) out += ", .people = {" + people + "}";
            if (!labels.empty()) out += ", .labels = {" + labels + "}";
            if (b.move && !b.move->along) {
                std::string steps;
                for (const auto& [from, to] : stages.steps) steps += (steps.empty() ? "" : ",") + std::string("{\"from\":") + quoted(from) + ",\"to\":" + quoted(to) + "}";
                // With no steps written, a card goes to any column from any other.
                out += ", .move = board_move{.command = " + str(screens::full_command(ns, b.move->command)) + ", .steps = " + fixed("{\"steps\":[" + steps + "]}") +
                       ", .list = \"steps\"" + (steps.empty() ? ", .anywhere = true" : "") + "}";
            } else if (b.move) {
                std::string move = "board_move{.command = " + str(screens::full_command(ns, b.move->command)) + ", .steps = " +
                                   view_variable(parts, screens::full_view(ns, b.move->along->text())) + ", .list = " + str(b.move->along_list);
                auto holders = model_.held_by(ns, route_, *b.move);
                if (!holders.empty()) {
                    const auto& h = holders.back();
                    move += ", .held = " + str(h.held) + ", .roles = " + view_variable(parts, h.view) + ", .within = " + id_of(parts, h.within) + ", .place = " + str(h.place) +
                            ", .role = " + str(h.role);
                }
                out += ", .move = " + move + "}";
            }
            const std::vector<std::string>& search = b.search.empty() && beside ? beside->search : b.search;
            if (!search.empty()) out += ", .search = " + native_detail::strings(search);
            if (std::string tint = tint_of(ns, b.view, b.list, b.tint); !tint.empty()) out += ", .tint = " + tint;
            const language::track_setting* track = b.track ? &*b.track : beside && beside->track ? &*beside->track : nullptr;
            if (std::string t = track_of(parts, ns, track); !t.empty()) out += ", .track = " + t;
            if (std::string reorder = reorder_of(parts, ns, b.reorder, entity); !reorder.empty()) out += ", .reorder = " + reorder;
            if (!stages.notify.empty()) out += ", .notify = " + native_detail::strings(stages.notify);
            // Columns kept as records say their chime and status in their own fields.
            if (!b.chime_field.empty()) out += ", .notify_by = " + str(b.chime_field) + ", .notify_has = " + str(b.chime_word);
            if (!b.status_field.empty()) out += ", .status_by = " + str(b.status_field);
            if (!stages.statuses.empty()) {
                std::string statuses;
                for (const auto& [stage, status] : stages.statuses) statuses += (statuses.empty() ? "" : ", ") + std::string("{") + str(stage) + ", " + str(status) + "}";
                out += ", .statuses = {" + statuses + "}";
            }
            if (std::string waits = model_.waits_on(ns, entity); !waits.empty()) out += ", .waits_on = " + str(waits);
            if (!picks.empty()) out += ", .pick = " + str(picks);
            // Its buttons, like New issue, above it, as a table has them beside its search.
            if (std::string tools = buttons(parts, ns, b.columns, screen); !tools.empty()) return "items(" + tools + ", " + out + "}))";
            return out + "})";
        }

        // A list a screen shows, a table, a board, a pipeline or a canvas: its view, its
        // list, and what it's called in a switch between them.
        struct shown {
            std::string view, list, name;
        };
        static std::optional<shown> shown_list(const language::screen_item& item) {
            if (auto* t = std::get_if<language::table_item>(&item.node); t && t->list) return shown{t->view.text(), *t->list, "Table"};
            if (auto* b = std::get_if<language::board_item>(&item.node); b && b->list) return shown{b->view.text(), *b->list, "Board"};
            if (auto* l = std::get_if<language::pipeline_item>(&item.node); l && l->list) return shown{l->view.text(), *l->list, "Pipeline"};
            if (auto* c = std::get_if<language::canvas_item>(&item.node); c && c->list) return shown{c->view.text(), *c->list, "Whiteboard"};
            return std::nullopt;
        }

        // A pipeline: its cards' rows by their stage, its stages, and the fields its lanes
        // and each kind's path are by.
        std::string pipeline(screen_parts& parts, const std::string& ns, const language::pipeline_item& c) {
            const language::entity_declaration* entity = model_.listed(ns, c.view.text(), c.list);
            std::string columns;
            for (const auto& column : c.columns) {
                auto shown = model_.shown_as(ns, c.view.text(), c.list, entity, screens::text_of(*column.value), column.label);
                columns += (columns.empty() ? "" : ", ") + std::string("{") + str(shown.key) + ", " + str(shown.label) + "}";
            }
            std::string out = "pipeline_board(p, {.view = " + view_variable(parts, screens::full_view(ns, c.view.text())) + ", .list = " + str(*c.list) + ", .by = " + str(c.by) +
                              ", .over = " + view_variable(parts, screens::full_view(ns, c.over.text())) + ", .over_list = " + str(c.over_list);
            bool titled = false;
            for (const auto& column : model_.listed_columns(ns, c.over.text(), c.over_list)) titled = titled || column == "title";
            out += std::string(", .shown = ") + (titled ? "\"title\"" : "\"name\"") + ", .columns = {" + columns + "}";
            if (c.link) out += ", " + link_settings(ns, *c.link);
            out += ", .lanes = " + str(c.lanes) + ", .path = " + str(c.path);
            if (!c.status_field.empty()) out += ", .status_by = " + str(c.status_field);
            if (std::string waits = model_.waits_on(ns, entity); !waits.empty()) out += ", .waits_on = " + str(waits);
            if (group_track_) out += ", .track = " + track_of(parts, ns, nullptr);
            return out + "})";
        }

        // A canvas: its cards' rows, where they're kept, the command placing them, and the
        // grid they snap to; by and over lay out those not placed yet.
        std::string canvas(screen_parts& parts, const std::string& ns, const language::canvas_item& c) {
            const language::entity_declaration* entity = model_.listed(ns, c.view.text(), c.list);
            std::string columns;
            for (const auto& column : c.columns) {
                auto shown = model_.shown_as(ns, c.view.text(), c.list, entity, screens::text_of(*column.value), column.label);
                columns += (columns.empty() ? "" : ", ") + std::string("{") + str(shown.key) + ", " + str(shown.label) + "}";
            }
            auto number = [](double n) { return std::format("{}", n); };
            std::string out = "canvas_board(p, {.view = " + view_variable(parts, screens::full_view(ns, c.view.text())) + ", .list = " + str(*c.list) + ", .columns = {" + columns + "}";
            if (c.link) out += ", " + link_settings(ns, *c.link);
            out += ", .x = " + str(c.x) + ", .y = " + str(c.y) + ", .w = " + str(c.w) + ", .h = " + str(c.h);
            if (!c.kind.empty()) out += ", .kind = " + str(c.kind);
            if (c.place) out += ", .place = " + str(screens::full_command(ns, *c.place));
            if (c.grid > 0) out += ", .grid = " + number(c.grid) + ", .origin_x = " + number(c.origin_x) + ", .origin_y = " + number(c.origin_y);
            if (!c.by.empty()) {
                out += ", .by = " + str(c.by);
                if (!c.over.parts.empty()) out += ", .over = " + view_variable(parts, screens::full_view(ns, c.over.text())) + ", .over_list = " + str(c.over_list);
            }
            if (std::string tint = tint_of(ns, c.view, c.list, std::nullopt); !tint.empty()) out += ", .tint = " + tint;
            if (!c.regions.empty()) out += ", .regions = " + str(c.regions);
            if (c.move) out += ", .move = " + str(screens::full_command(ns, c.move->command));
            if (c.resize) out += ", .resize = " + str(screens::full_command(ns, *c.resize));
            if (group_track_) out += ", .track = " + track_of(parts, ns, nullptr);
            // A pasted file makes a card as a create form here would, named from the file.
            if (c.paste && entity) {
                std::string given, named;
                for (const auto& field : entity->fields) {
                    if ((field.name == "title" || field.name == "name") && named.empty() && field.type && field.type->text() == "text") named = field.name;
                    if (!field.type || field.type->parts.size() != 1 || field.name == c.paste_into) continue;
                    const std::string& points = field.type->parts[0];
                    if (!screens::names_parameter(route_, points) || !model_.entity_named(ns, points)) continue;
                    given += (given.empty() ? "" : ", ") + std::string("{") + str(field.name) + ", " + id_of(parts, points) + "}";
                }
                out += ", .paste = " + str(screens::full_command(ns, *c.paste)) + ", .paste_into = " + str(c.paste_into);
                if (!named.empty()) out += ", .paste_named = " + str(named);
                if (!given.empty()) out += ", .given = {" + given + "}";
            }
            return out + "})";
        }

        std::string details(screen_parts& parts, const std::string& ns, const language::details_item& d) {
            std::string full = screens::full_view(ns, d.view.text());
            std::size_t split = full.rfind("::");
            std::string view_ns = split == std::string::npos ? "" : full.substr(0, split), name = split == std::string::npos ? full : full.substr(split + 2);
            const language::view_declaration* view = nullptr;
            const language::entity_declaration* entity = nullptr;
            if (auto scope = model_.views.find(view_ns); scope != model_.views.end()) {
                if (auto found = scope->second.find(name); found != scope->second.end()) view = found->second;
            }
            if (view && view->per) {
                if (auto scope = model_.entities.find(view_ns); scope != model_.entities.end()) {
                    if (auto found = scope->second.find(*view->per); found != scope->second.end()) entity = found->second;
                }
            }
            std::string fields;
            std::vector<std::string> choices, labels, files;
            std::string tint;
            for (const auto& f : d.fields) {
                std::string key = screens::text_of(*f.value);
                std::optional<std::string> label = f.label;
                std::string first = key.substr(0, key.find('.')), rest = key.find('.') == std::string::npos ? "" : key.substr(key.find('.') + 1);
                for (const auto& field : (entity ? entity->fields : no_fields_)) {
                    if (!label && field.name == first && (rest.empty() || rest == "name" || rest == "title") && field.label) label = field.label;
                }
                fields += (fields.empty() ? "" : ", ") + std::string("{") + str(key) + ", " + str(label ? *label : screens::label(key)) + "}";
                if (!view || !entity) continue;
                for (const auto& v : view->values) {
                    if ((v.name ? *v.name : screens::text_of(*v.value)) != key) continue;
                    auto* member = std::get_if<language::member_expression>(&v.value->node);
                    if (!member) continue;
                    for (const auto& field : entity->fields) {
                        if (field.name != member->member) continue;
                        if (!field.choices.empty()) choices.push_back(choices_entry(key, field));
                        // A list of words is labels, each drawn on its own; a file, or a list of
                        // them, is said by its name.
                        if (field.list && field.type && field.type->text() == "text") labels.push_back(key);
                        if (field.type && field.type->text() == "file") files.push_back(key);
                    }
                }
            }
            if (d.tint && view && entity) {
                for (const auto& v : view->values) {
                    if ((v.name ? *v.name : screens::text_of(*v.value)) != *d.tint) continue;
                    auto* member = std::get_if<language::member_expression>(&v.value->node);
                    for (const auto& field : entity->fields) {
                        if (member && field.name == member->member) tint = "tint_spec{" + str(*d.tint) + ", " + native_detail::strings(field.choices) + "}";
                    }
                }
            }
            std::string c;
            for (const auto& one : choices) c += (c.empty() ? "" : ", ") + one;
            std::string out = "details(p, " + view_variable(parts, full) + ", {" + fields + "}";
            if (!c.empty() || !tint.empty() || !labels.empty() || !files.empty()) out += ", {" + c + "}";
            if (!tint.empty()) out += ", " + tint;
            else if (!labels.empty() || !files.empty()) out += ", std::nullopt";
            if (!labels.empty() || !files.empty()) out += ", " + native_detail::strings(labels);
            if (!files.empty()) out += ", " + native_detail::strings(files);
            return out + ")";
        }

        std::string cards(screen_parts& parts, const std::string& ns, const language::cards_item& c) {
            const language::entity_declaration* entity = model_.listed(ns, c.view.text(), c.list);
            std::string columns;
            for (const auto& column : c.columns) {
                std::string key = screens::text_of(*column.value);
                auto shown = model_.shown_as(ns, c.view.text(), c.list, entity, key, column.label);
                columns += (columns.empty() ? "" : ", ") + std::string("{") + str(shown.key) + ", " + str(shown.label) + "}";
            }
            std::string out = "cards(p, {.view = " + view_variable(parts, screens::full_view(ns, c.view.text())) + ", .list = " + str(c.list.value_or("")) + ", .columns = {" + columns + "}";
            if (c.link) out += ", " + link_settings(ns, *c.link);
            if (c.tally) {
                // The rows it counts: those of another list whose field names a card, by what each is at.
                std::string shown, order, status;
                for (const auto& column : model_.listed_columns(ns, c.tally->text(), c.tally_list)) {
                    if (column == c.tally_and + ".title") shown = column;
                    if (column == c.tally_and + ".position") order = column;
                    if (column == c.tally_and + ".status") status = column;
                }
                out += ", .tally = tally_spec{" + view_variable(parts, screens::full_view(ns, c.tally->text())) + ", " + str(c.tally_list) + ", " + str(c.tally_by) + ", " +
                       str(c.tally_and) + ", " + str(shown) + ", " + str(order) + ", " + str(c.tally_list) + (status.empty() ? "" : ", " + str(status)) + "}";
            }
            if (!c.filters.empty()) {
                std::string filters;
                for (const auto& f : c.filters) {
                    std::string query;
                    std::function<void(const language::expression&)> walk = [&](const language::expression& e) {
                        auto* binary = std::get_if<language::binary_expression>(&e.node);
                        if (!binary) return;
                        if (binary->op == language::token_kind::logical_and) {
                            walk(*binary->left);
                            walk(*binary->right);
                            return;
                        }
                        using language::token_kind;
                        std::string op = binary->op == token_kind::less ? "<" : binary->op == token_kind::greater ? ">" : binary->op == token_kind::less_equal ? "<="
                                       : binary->op == token_kind::greater_equal ? ">=" : binary->op == token_kind::not_equal ? "!" : "";
                        std::string wanted;
                        if (auto* literal = std::get_if<language::literal_expression>(&binary->right->node)) wanted = literal->value;
                        else if (auto* name = std::get_if<language::name_expression>(&binary->right->node)) wanted = name->name.parts.back();
                        query += (query.empty() ? "" : ", ") + std::string("{") + str(screens::text_of(*binary->left) + op) + ", " + str(wanted) + "}";
                    };
                    walk(*f.condition);
                    filters += (filters.empty() ? "" : ", ") + std::string("{") + str(f.label) + ", {" + query + "}" + (f.hue.empty() ? "" : ", " + str(f.hue)) + "}";
                }
                out += ", .filters = {" + filters + "}";
            }
            std::string choices;
            for (const auto& column : c.columns) {
                std::string key = screens::text_of(*column.value);
                for (const auto& f : (entity ? entity->fields : no_fields_)) {
                    if (f.name == key && !f.choices.empty()) choices += (choices.empty() ? "" : ", ") + choices_entry(key, f);
                }
            }
            if (!choices.empty()) out += ", .choices = {" + choices + "}";
            if (c.hide_empty) out += ", .hide_empty = true";
            if (c.picture) out += ", .picture = " + str(*c.picture) + (c.picture_sits == "fit" ? ", .fit = true" : "");
            return out + "})";
        }

        static std::string chart_line_of(const language::chart_series& c) {
            return "chart_line{" + str(c.text()) + ", " + str(c.title) + (c.unit.empty() ? "" : ", " + str(c.unit)) + "}";
        }

        std::string chart(screen_parts& parts, const std::string& ns, const language::chart_item& c) {
            std::string series;
            for (const auto& line : c.series) series += (series.empty() ? "" : ", ") + chart_line_of(line);
            return "chart(p, {.view = " + view_variable(parts, screens::full_view(ns, c.view.text())) + ", .series = {" + series + "}})";
        }

        std::string monitor(screen_parts& parts, const std::string& ns, const language::monitor_item& m) {
            const language::entity_declaration* entity = model_.listed(ns, m.view.text(), std::optional<std::string>(m.list));
            std::string out = "monitor(p, {.view = " + view_variable(parts, screens::full_view(ns, m.view.text())) + ", .list = " + str(m.list) + ", .title = " + str(m.title) +
                              ", .status = " + str(m.status);
            if (!m.doing.empty()) out += ", .doing = " + str(m.doing);
            if (!m.since.empty()) out += ", .since = " + str(m.since);
            if (!m.step.empty()) out += ", .step = " + str(m.step) + ", .steps = " + str(m.steps);
            if (!m.seen.empty()) out += ", .seen = " + str(m.seen) + ", .seen_seconds = " + std::to_string(m.seen_seconds);
            if (!m.charts.empty()) {
                std::string charts;
                for (const auto& c : m.charts) charts += (charts.empty() ? "" : ", ") + chart_line_of(c);
                out += ", .charts = {" + charts + "}";
            }
            if (m.link) out += ", " + link_settings(ns, *m.link, m.view.text(), m.list);
            for (const auto& f : (entity ? entity->fields : no_fields_)) {
                if (f.name == m.status && !f.choices.empty()) out += ", .choices = " + options_of(screens::model::choice_options(f));
            }
            return out + "})";
        }

        // A grid or a diagram: a cell for each pair of the over list's things, opening
        // the entry's create where there's none, and its update and delete where there's one.
        std::string between(screen_parts& parts, const std::string& ns, const language::grid_item& g, const std::vector<language::screen_item>& screen) {
            const language::entity_declaration* entry = model_.listed(ns, g.view.text(), g.list);
            auto command = [&](const std::string& action, bool given) -> std::string {
                if (!entry) return "";
                auto known = model_.commands.find(ns);
                if (known == model_.commands.end() || !known->second.contains(entry->name + "::" + action)) return "";
                std::string full = screens::join(ns, entry->name + "::" + action);
                std::string out = "between_command{.command = " + str(full);
                if (const language::form_item* f = screens::model::form_for(ns, screen, full)) {
                    // The row and column are given by the cell, so its form doesn't ask them.
                    language::form_item asked;
                    asked.commands = f->commands;
                    for (const auto& field : f->fields) {
                        if (given && (field.name == g.from || field.name == g.to)) continue;
                        language::form_field copied;
                        copied.where = field.where;
                        copied.name = field.name;
                        copied.hint = field.hint;
                        copied.label = field.label;
                        asked.fields.push_back(std::move(copied));
                    }
                    out += ", .fields = " + fields_of(parts, ns, asked);
                    if (f->submit) out += ", .submit = " + str(*f->submit);
                }
                // What the page's address names, like the project, is sent with a create.
                if (given) {
                    std::string sent;
                    for (const auto& field : entry->fields) {
                        if (!field.type || field.type->parts.size() != 1 || field.name == g.from || field.name == g.to) continue;
                        const std::string& points = field.type->parts[0];
                        if (!screens::names_parameter(route_, points) || !model_.entity_named(ns, points)) continue;
                        sent += (sent.empty() ? "" : ", ") + std::string("{") + str(field.name) + ", " + id_of(parts, points) + "}";
                    }
                    if (!sent.empty()) out += ", .given = {" + sent + "}";
                }
                if (std::string may = allowed(parts, ns, full); !may.empty()) out += ", .allowed = " + may;
                return out + "}";
            };
            std::string out = std::string(g.drawn ? "diagram" : "grid") + "(p, {.view = " + view_variable(parts, screens::full_view(ns, g.view.text())) + ", .list = " + str(*g.list) +
                              ", .from = " + str(g.from) + ", .to = " + str(g.to);
            if (g.cell) out += ", .cell = " + str(screens::text_of(*g.cell));
            out += ", .over = " + view_variable(parts, screens::full_view(ns, g.over.text())) + ", .over_list = " + str(g.over_list);
            auto over = model_.listed_columns(ns, g.over.text(), g.over_list);
            out += std::string(", .shown = ") + (std::find(over.begin(), over.end(), "title") != over.end() ? "\"title\"" : "\"name\"");
            if (std::string c = command("create", true); !c.empty()) out += ", .create = " + c;
            if (std::string c = command("update", false); !c.empty()) out += ", .update = " + c;
            if (std::string c = command("delete", false); !c.empty()) out += ", .remove = " + c;
            return out + "})";
        }

        std::string copy(screen_parts& parts, const std::string& ns, const language::copy_item& c, const std::vector<language::screen_item>& screen) {
            std::string full = screens::full_view(ns, c.view.text());
            std::size_t split = full.rfind("::");
            std::string view_ns = split == std::string::npos ? "" : full.substr(0, split), name = split == std::string::npos ? full : full.substr(split + 2);
            const language::view_declaration* view = nullptr;
            if (auto scope = model_.views.find(view_ns); scope != model_.views.end()) {
                if (auto found = scope->second.find(name); found != scope->second.end()) view = found->second;
            }
            std::string fields, lists;
            std::vector<std::string> choices;
            if (view) {
                const language::entity_declaration* entity = nullptr;
                if (view->per) {
                    if (auto scope = model_.entities.find(view_ns); scope != model_.entities.end()) {
                        if (auto found = scope->second.find(*view->per); found != scope->second.end()) entity = found->second;
                    }
                }
                for (const auto& v : view->values) {
                    std::string key = v.name ? *v.name : screens::text_of(*v.value);
                    if (title_.find("{" + name + "." + key + "}") != std::string::npos) continue;
                    bool markdown = false, person = false;
                    auto* member = std::get_if<language::member_expression>(&v.value->node);
                    if (member && entity) {
                        for (const auto& f : entity->fields) {
                            if (f.name != member->member) continue;
                            if (f.type && f.type->text() == "markdown") markdown = true;
                            if (f.type && (f.type->text() == "user" || model_.entity_named(view_ns, f.type->text()))) person = true;
                            if (!f.choices.empty()) choices.push_back(choices_entry(key, f));
                        }
                    }
                    if (person) continue;
                    fields += (fields.empty() ? "" : ", ") + std::string("{") + str(key) + ", " + str(screens::label(key)) + (markdown ? ", true" : "") + "}";
                }
                std::set<std::string> shown_lists;
                std::function<void(const std::vector<language::screen_item>&)> lists_on = [&](const std::vector<language::screen_item>& items) {
                    for (const auto& item : items) {
                        if (auto* t = std::get_if<language::table_item>(&item.node); t && t->view.text() == c.view.text() && t->list) shown_lists.insert(*t->list);
                        if (auto* t = std::get_if<language::thread_item>(&item.node); t && t->view.text() == c.view.text()) shown_lists.insert(t->list);
                        if (auto* t = std::get_if<language::timeline_item>(&item.node); t && t->view.text() == c.view.text()) shown_lists.insert(t->list);
                        if (auto* b = std::get_if<language::content_block>(&item.node)) lists_on(b->items);
                    }
                };
                lists_on(screen);
                for (const auto& each : view->each) {
                    if (!each.name || !shown_lists.contains(*each.name)) continue;
                    std::vector<std::string> columns;
                    for (const auto& row : each.rows) columns.push_back(row.name ? *row.name : screens::text_of(*row.value));
                    auto holds_column = [&](const std::string& column) { return std::find(columns.begin(), columns.end(), column) != columns.end(); };
                    std::string kind = each.changes ? "changes" : holds_column("body") && holds_column("author.name") ? "thread" : "rows";
                    lists += (lists.empty() ? "" : ", ") + std::string("{") + str(*each.name) + ", " + str(screens::label(*each.name)) + ", " + str(kind) + ", " +
                             native_detail::strings(columns) + "}";
                }
            }
            std::string c_list;
            for (const auto& one : choices) c_list += (c_list.empty() ? "" : ", ") + one;
            return "copy(p, " + view_variable(parts, full) + ", " + str(c.label ? *c.label : "Copy") + ", {" + fields + "}, {" + lists + "}" +
                   (c_list.empty() ? "" : ", {" + c_list + "}") + ")";
        }

        std::string timeline(screen_parts& parts, const std::string& ns, const language::timeline_item& t) {
            std::string out = "timeline(p, {.view = " + view_variable(parts, screens::full_view(ns, t.view.text())) + ", .list = " + str(t.list);
            std::vector<std::string> subject;
            auto columns = model_.listed_columns(ns, t.view.text(), t.list);
            for (const auto& column : columns) {
                static const std::set<std::string> said{"field", "before", "after", "action", "created_by.name", "created_at"};
                bool through = std::any_of(columns.begin(), columns.end(), [&](const std::string& other) { return other.starts_with(column + "."); });
                if (!said.contains(column) && !through) subject.push_back(column);
            }
            if (!subject.empty()) out += ", .subject = " + native_detail::strings(subject);
            if (t.link) out += ", " + link_settings(ns, *t.link);
            if (t.title) out += ", .title = " + str(*t.title);
            if (t.fresh) {
                std::vector<std::string> views;
                std::string test = fresh_condition(parts, ns, *t.fresh, views);
                std::string read;
                for (const auto& v : views) read += (read.empty() ? "" : ", ") + v;
                out += ", .fresh_views = {" + read + "}, .fresh = [&p](const value& row, const std::vector<value>& read) { (void)p; (void)read; return " + test + "; }";
            }
            if (t.opened) out += ", .on_open = " + str(screens::full_command(ns, *t.opened));
            return out + "})";
        }

        std::string steps(screen_parts& parts, const std::string& ns, const language::button_item& button) {
            std::string command = screens::full_command(ns, button.command);
            const language::entity_declaration* entity = model_.entity_of_command(command);
            const language::command_declaration* declared = model_.command_nodes.count(command) ? model_.command_nodes.at(command) : nullptr;
            std::string field;
            if (declared) {
                for (const auto& st : declared->body) {
                    if (auto* c = std::get_if<language::changes_statement>(&st.node); c && !c->fields.empty()) field = c->fields.front();
                }
            }
            if (!entity || field.empty()) return "";
            std::string current, here;
            if (auto scope = model_.views.find(ns); scope != model_.views.end()) {
                for (const auto& [name, view] : scope->second) {
                    if (!view->per || *view->per != entity->name) continue;
                    for (const auto& v : view->values) {
                        if ((v.name ? *v.name : screens::text_of(*v.value)) == field) current = view_variable(parts, screens::join(ns, name));
                        if (v.name && screens::text_of(*v.value) == entity->name + "." + field + ".title") here = *v.name;
                    }
                }
            }
            if (current.empty()) return "";
            std::string out = "steps(p, {.command = " + str(command) + ", .id = " + id_of(parts, entity->name) + ", .field = " + str(field) + ", .current = " + current +
                              ", .steps = " + view_variable(parts, screens::full_view(ns, button.along->text())) + ", .list = " + str(button.along_list);
            std::string shown, to, from;
            for (const auto& column : model_.listed_columns(ns, button.along->parts.back(), button.along_list)) {
                if (column == "title") shown = column;
                if (column == "to.title") to = column;
                if (column == "from.title") from = column;
            }
            if (!shown.empty()) out += ", .shown = " + str(shown);
            if (!to.empty()) out += ", .to = " + str(to);
            if (!from.empty()) out += ", .from = " + str(from);
            if (!here.empty()) out += ", .here = " + str(here);
            auto holders = model_.held_by(ns, route_, button);
            if (!holders.empty()) {
                const auto& h = holders.back();
                out += ", .held = " + str(h.held) + ", .roles = " + view_variable(parts, h.view) + ", .within = " + id_of(parts, h.within) + ", .place = " + str(h.place) +
                       ", .role = " + str(h.role);
            }
            return out + "})";
        }

        // A code file beside the screen's, as words.
        std::string beside_screen(const std::string& path) const {
            std::string dir = std::filesystem::path(screen_path_).parent_path().string();
            return platform::resolve(dir.empty() ? "." : dir, path);
        }

        // Markdown pages from files beside the project, like docs/*.md, written to a file
        // of their own, src/pages/<name>.hpp, since they change whenever a page does: each
        // page's name, its title from its first heading, and its Markdown.
        struct page_set {
            std::string pattern;
            std::string name;  // from the address: /docs is docs, /guides/api is guides_api
            code::source source;
        };
        std::vector<page_set> pages_;
        std::set<std::string> page_includes_;  // the page sets the screen file being written shows
        bool shows_pages_ = false;              // whether the screen being written does, opening the first without :page
        std::map<const language::content_block*, int> steps_;  // each step of a section as steps, by its number
        bool each_alone_ = false;     // building a bar's or a toolbar's lines, each its own
        bool in_group_ = false;       // building a toolbar group's lines
        bool workspace_ = false;      // the screen being written is laid out as a workspace
        bool quiet_ = false;          // building a status line, its words said quietly
        std::optional<std::string> row_named_;  // the row an on open line's where reads, by what it's of
        std::string collage_key_;     // what its collage buttons name its collage by
        std::string paper_key_;       // and its insert buttons its paper
        bool screen_pastes_into_paper_ = false;  // its paste and add pictures buttons go into its paper, as it has no collage that pastes
        std::optional<std::string> collage_over_;  // and the pick over it, which they may name
        const language::collage_item* collage_ = nullptr;  // and the collage itself, which its layers list
        bool at_end_said_ = false;    // a group at the toolbar's end has been written
        std::set<std::string> toggles_;  // the screen's toggles, which its whens read
        std::map<std::string, std::string> choices_;  // and those over an enum, by it, each to the choice it starts at
        // and its picks, each the view and list it picks a row of, and that list's entity
        struct pick_of {
            std::string view, list, entity;
        };
        std::map<std::string, pick_of> picks_;

        // A paste or add pictures button that goes into the screen's paper.
        bool into_paper(const language::collage_button& b) const {
            using kind = language::collage_button::kind;
            return screen_pastes_into_paper_ && (b.does == kind::paste || b.does == kind::add_pictures);
        }

        stream pages_file(const page_set& pages) const {
            namespace fs = std::filesystem;
            std::string dir = fs::path(pages.source.path).parent_path().string();
            fs::path glob = platform::resolve(dir.empty() ? "." : dir, pages.pattern);
            stream out("\t");
            auto from = out.from(pages.source.path, pages.source.line);
            out.generated_from(pages.pattern);
            out.line("#pragma once");
            out.line();
            out.line("#include <one/screens/specs.hpp>");
            out.line();
            out.line("#include <vector>");
            out.line();
            out.open("namespace app {");
            out.line();
            out.open("inline const std::vector<one::screens::written_page> " + pages.name + "_pages = {");
            for (const auto& path : platform::files_in(glob.parent_path().string(), glob.extension().string())) {
                std::string markdown = platform::read_file(path).value_or("");
                std::string slug = markdown_slug(fs::path(path).stem().string());
                out.line("{" + str(slug) + ", " + str(markdown_title(markdown, slug)) + ", " + str(markdown) + "},");
            }
            out.close("};");
            out.line();
            out.close("}  // namespace app");
            return out;
        }

        std::string text_item(screen_parts& parts, const std::string& ns, const language::content_text& text) {
            if (text.type == language::content_text::kind::overline) return "";  // said with its hero's or section's title
            if (text.type == language::content_text::kind::statement) return "statement(" + parts_of(parts, ns, text.value) + ")";
            if (text.type == language::content_text::kind::text) {
                std::string shown = "text(" + parts_of(parts, ns, text.value) + (quiet_ ? ", true" : "") + ")";
                if (text.when) return condition(parts, ns, *text.when) + " ? " + shown + " : view()";
                return shown;
            }
            if (text.type == language::content_text::kind::code) {
                std::string source = platform::read_file(beside_screen(text.value)).value_or("");
                std::string language = std::filesystem::path(text.value).extension().string();
                return "code(" + str(language.empty() ? "" : language.substr(1)) + ", " + str(source) + ")";
            }
            if (text.value.starts_with("{") && text.value.ends_with("}")) {
                std::string inside = text.value.substr(1, text.value.size() - 2);
                std::size_t dot = inside.find('.');
                std::string view = view_variable(parts, screens::full_view(ns, inside.substr(0, dot)));
                if (text.subtitle) return "subtitle(p, {live(" + view + ", " + str(inside.substr(dot + 1)) + ")})";
                return "markdown(p, " + view + ", " + str(inside.substr(dot + 1)) + ")";
            }
            if (text.subtitle) return "subtitle(p, " + parts_of(parts, ns, text.value) + ")";
            std::string base = route_.substr(0, route_.find("/:"));
            std::string name = base.empty() ? "pages" : native_detail::identifier(base.substr(1));
            pages_.push_back({text.value, name, {screen_path_, item_line_}});
            page_includes_.insert(name);
            shows_pages_ = true;
            return "pages(p, " + str(base) + ", " + name + "_pages" + (text.document ? ", true" : "") + ")";
        }

        // A toolbar's commands side by side with the same words and icon, like Bold that
        // sets it and Bold that takes it away, as one tool: the first whose when holds.
        static std::vector<written> alternatives(std::vector<written> lines) {
            const std::string call = "command(p, ";
            auto said = [](const std::string& code) {
                auto part = [&](const std::string& name) {
                    auto at = code.find(", ." + name + " = \"");
                    if (at == std::string::npos) return std::string();
                    auto from = code.find('"', at) + 1;
                    return code.substr(from, code.find('"', from) - from);
                };
                return part("label") + "\n" + part("icon");
            };
            std::vector<written> out;
            std::vector<std::string> ways;
            auto finish = [&] {
                if (ways.size() > 1) {
                    std::string joined;
                    for (const auto& w : ways) joined += (joined.empty() ? "" : ", ") + std::string("command_button") + w;
                    out.back().code = "either(p, {" + joined + "})";
                }
                ways.clear();
            };
            for (auto& line : lines) {
                bool command = line.code.starts_with(call);
                if (command && !ways.empty() && said(line.code) == said(out.back().code.starts_with(call) ? out.back().code : "") && out.back().code.starts_with(call)) {
                    ways.push_back(line.code.substr(call.size(), line.code.size() - call.size() - 1));
                    continue;
                }
                finish();
                out.push_back(std::move(line));
                if (command) ways.push_back(out.back().code.substr(call.size(), out.back().code.size() - call.size() - 1));
            }
            finish();
            return out;
        }

        std::vector<written> items(screen_parts& parts, const std::string& ns, const std::vector<language::screen_item>& list, const std::vector<language::screen_item>& screen,
                                   std::size_t first = 0, std::size_t count = std::string::npos) {
            std::vector<written> out;
            // Buttons one after another sit in a row: a command's own, a form's that opens it,
            // and links.
            auto presses = [&](const language::screen_item& it) {
                if (each_alone_) return false;  // a bar's or a toolbar's lines are each their own
                if (std::holds_alternative<language::content_link>(it.node)) return true;
                if (auto* b = std::get_if<language::button_item>(&it.node)) return !b->along && !screens::model::has_form_for(screen, b->command);
                if (auto* f = std::get_if<language::form_item>(&it.node)) {
                    return screens::model::button_for(screen, f->commands.front()) != nullptr && !model_.on_rows(ns, screen, screens::full_command(ns, f->commands.front()));
                }
                return std::holds_alternative<language::copy_item>(it.node);
            };
            std::vector<std::string> row;
            int row_line = 0;
            auto finish_row = [&] {
                if (row.empty()) return;
                if (row.size() == 1) out.push_back({row.front(), row_line});
                else {
                    std::string joined;
                    for (const auto& r : row) joined += (joined.empty() ? "" : ", ") + r;
                    out.push_back({"actions(" + joined + ")", row_line});
                }
                row.clear();
            };
            std::size_t end = count == std::string::npos ? list.size() : std::min(list.size(), first + count);
            for (std::size_t at = first; at < end; ++at) {
                const auto& item = list[at];
                item_line_ = item.where.line;
                std::string code;
                // Lists of the same rows one after another, a table, a board, a pipeline or a
                // whiteboard, are one, switched between: each person's own pick, or else the
                // one whose when holds, like a board's style, or the first.
                bool grouped = false;
                if (auto here = shown_list(item)) {
                    std::size_t last = at;
                    while (last + 1 < end) {
                        auto next = shown_list(list[last + 1]);
                        if (!next || next->view != here->view || next->list != here->list) break;
                        ++last;
                    }
                    if (last > at) {
                        const language::table_item* beside = nullptr;
                        for (std::size_t i = at; i <= last; ++i) {
                            if (auto* t = std::get_if<language::table_item>(&list[i].node); t && !beside) beside = t;
                        }
                        // One track for them all, the first that says one, drawn once above
                        // the switch, its pick kept to whichever way it's shown.
                        const language::track_setting* shared = nullptr;
                        for (std::size_t i = at; i <= last && !shared; ++i) {
                            if (auto* t = std::get_if<language::table_item>(&list[i].node); t && t->track) shared = &*t->track;
                            if (auto* b = std::get_if<language::board_item>(&list[i].node); b && b->track) shared = &*b->track;
                        }
                        std::string id = screens::full_view(ns, here->view) + "." + here->list;
                        group_track_ = shared, group_key_ = "switched:" + id;
                        std::string names, ways, start;
                        for (std::size_t i = at; i <= last; ++i) {
                            const auto& one = list[i];
                            names += (names.empty() ? "" : ", ") + str(shown_list(one)->name);
                            if (auto* t = std::get_if<language::table_item>(&one.node)) ways += ", " + table(parts, ns, *t, screen);
                            else if (auto* b = std::get_if<language::board_item>(&one.node)) ways += ", " + board(parts, ns, *b, screen, beside);
                            else if (auto* c = std::get_if<language::canvas_item>(&one.node)) ways += ", " + canvas(parts, ns, *c);
                            else if (auto* l = std::get_if<language::pipeline_item>(&one.node)) ways += ", " + pipeline(parts, ns, *l);
                            if (one.when) start += "(" + condition(parts, ns, *one.when) + ") ? " + std::to_string(i - at) + " : ";
                        }
                        std::string words = screens::label(here->list);
                        if (!words.empty()) words[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(words[0])));
                        code = "switched(p, " + str(id) + ", " + str("Show " + words + " as") + ", {" + names + "}, " +
                               (start.empty() ? std::string("0") : start + "-1") + ways + ")";
                        if (shared) {
                            std::string track = track_of(parts, ns, shared, track_stages(parts, ns, list, at, last));
                            code = "tracked(p, " + track + ", " + view_variable(parts, screens::full_view(ns, here->view)) + ", " + str(here->list) + ", " + code + ")";
                        }
                        group_track_ = nullptr;
                        at = last;
                        grouped = true;
                    }
                }
                if (grouped) {
                } else if (auto* o = std::get_if<language::content_block>(&item.node); o && o->type == language::content_block::kind::options) {
                    // A tool that opens its lines, each a choice drawn with what it's for.
                    bool alone = std::exchange(each_alone_, true);
                    auto inside = alternatives(items(parts, ns, o->items, screen));
                    each_alone_ = alone;
                    code = "options(p, " + str(o->title) + ", " + str(o->icon.value_or("")) + ", [&] { return tools(" + joined_items(inside) + "); })";
                } else if (auto* g = std::get_if<language::content_block>(&item.node); g && g->type == language::content_block::kind::group) {
                    // A toolbar's group: its lines built as tools; the first at its end
                    // takes the toolbar's far end.
                    bool alone = std::exchange(each_alone_, true);
                    bool grouping = std::exchange(in_group_, true);
                    auto inside = alternatives(items(parts, ns, g->items, screen));
                    in_group_ = grouping;
                    each_alone_ = alone;
                    bool end_here = g->at_end && !at_end_said_;
                    at_end_said_ = at_end_said_ || g->at_end;
                    code = "group(p, " + str(g->title) + ", " + (g->small ? "true" : "false") + ", " + (end_here ? "true" : "false") +
                           ", [&] { return tools(" + joined_items(inside) + "); }" + (g->across ? ", " + std::to_string(g->across) : "") + ")";
                } else if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    using kind = language::content_block::kind;
                    if (block->type == kind::heading || block->type == kind::region) continue;  // drawn on the title's row, or by the layout
                    if (block->type == kind::menu) {
                        finish_row();
                        // Its links down the side, with everything after it beside them.
                        std::string links;
                        for (const auto& inside : block->items) {
                            auto* link = std::get_if<language::content_link>(&inside.node);
                            if (!link) continue;
                            std::string to = link->target.starts_with("/") ? model_.full_route(ns, link->target) : link->target;
                            links += (links.empty() ? "" : ", ") + std::string("{") + str(to) + ", " + str(link->label) + "}";
                        }
                        auto rest = items(parts, ns, list, screen, at + 1);
                        out.push_back({"menu(p, {" + links + "}, items(" + joined_items(rest) + "))", item.where.line});
                        return out;
                    }
                    // A section as steps numbers its sections, one after another.
                    if (block->steps) {
                        int number = 0;
                        for (const auto& inner : block->items) {
                            auto* step = std::get_if<language::content_block>(&inner.node);
                            if (step && step->type == kind::section) steps_[step] = ++number;
                        }
                    }
                    // Its overline goes over its title, so it's said with it.
                    std::string overline;
                    for (const auto& inner : block->items) {
                        auto* t = std::get_if<language::content_text>(&inner.node);
                        if (t && t->type == language::content_text::kind::overline) overline = t->value;
                    }
                    auto inside = items(parts, ns, block->items, screen);
                    auto step = steps_.find(block);
                    std::string title = ".title = " + str(block->title);
                    if (!block->hue.empty()) title += ", .color = " + str(block->hue);
                    if (block->icon) title += ", .icon = " + str(*block->icon);
                    if (step != steps_.end()) title += ", .step = " + std::to_string(step->second);
                    if (!overline.empty()) title += ", .overline = " + str(overline);
                    // A title and its color alone are said in order, as they always were.
                    bool more = block->icon || step != steps_.end() || !overline.empty();
                    std::string rest = inside.empty() ? "" : ", " + joined_items(inside);
                    if (block->type == kind::hero) {
                        code = "hero(" + (more ? "section_title{" + title + "}" : str(block->title)) + rest + ")";
                    } else {
                        code = "section({" + (more ? title : str(block->title) + (block->hue.empty() ? "" : ", " + str(block->hue))) + "}" + rest + ")";
                    }
                    // One a link on the page names, like #projects, is brought into view when it's followed.
                    if (block->anchor) code = "anchored(p, " + str(*block->anchor) + ", " + code + ")";
                } else if (auto* text = std::get_if<language::content_text>(&item.node)) {
                    code = text_item(parts, ns, *text);
                } else if (auto* link = std::get_if<language::content_link>(&item.node)) {
                    // What the app was built from: its release's notes, or its commit, when that's known.
                    if (link->built == "release") {
                        code = "link(p, " + said_of(parts, ns, link->label) + ", \"https://github.com/da0x/uione/releases/tag/v\" + build_version)";
                    } else if (!link->built.empty()) {
                        code = "build_commit.empty() || build_repository.empty() ? view() : link(p, " + said_of(parts, ns, link->label) + ", build_repository + \"/commit/\" + build_commit)";
                    } else {
                        std::string target = link->namespace_name ? model_.full_route(link->namespace_name->text(), "/") : link->target.starts_with("/") ? model_.full_route(ns, link->target) : link->target;
                        std::string keys = link->keys.empty() ? "" : keys_of(link->keys);
                        code = "link(p, " + said_of(parts, ns, link->label) + ", " + str(target) + (link->icon || !keys.empty() ? ", " + str(link->icon.value_or("")) : "") +
                               (keys.empty() ? "" : ", " + keys) + ")";
                    }
                } else if (auto* d = std::get_if<language::details_item>(&item.node)) {
                    code = details(parts, ns, *d);
                } else if (auto* c = std::get_if<language::copy_item>(&item.node)) {
                    code = copy(parts, ns, *c, screen);
                } else if (std::holds_alternative<language::themes_item>(item.node)) {
                    code = "themes(p)";
                } else if (auto* component = std::get_if<language::component_item>(&item.node)) {
                    // A hand-written function of the page, giving a view.
                    components_.emplace(component->name, language::component_file(screen_path_, component->name, ".cpp"));
                    code = native_detail::identifier(component->name) + "(p)";
                } else if (auto* t = std::get_if<language::thread_item>(&item.node)) {
                    code = "thread(p, {.view = " + view_variable(parts, screens::full_view(ns, t->view.text())) + ", .list = " + str(t->list) + ", .body = markdown_view})";
                } else if (auto* f = std::get_if<language::find_item>(&item.node)) {
                    std::string sources;
                    for (const auto& source : f->sources) {
                        std::string one = "{.view = " + view_variable(parts, screens::full_view(ns, source.view.text())) + ", .list = " + str(source.list) + ", .label = " + str(source.label);
                        if (source.link) one += ", " + link_settings(ns, *source.link, std::nullopt, std::nullopt, false);
                        one += ", .by = " + native_detail::strings(source.by) + "}";
                        sources += (sources.empty() ? "" : ", ") + one;
                    }
                    code = "find(p, " + str(f->label) + ", {" + sources + "})";
                } else if (auto* t = std::get_if<language::timeline_item>(&item.node)) {
                    code = timeline(parts, ns, *t);
                } else if (auto* t = std::get_if<language::table_item>(&item.node)) {
                    code = table(parts, ns, *t, screen);
                } else if (auto* g = std::get_if<language::grid_item>(&item.node)) {
                    code = between(parts, ns, *g, screen);
                } else if (auto* c = std::get_if<language::cards_item>(&item.node)) {
                    code = cards(parts, ns, *c);
                } else if (auto* c = std::get_if<language::chart_item>(&item.node)) {
                    code = chart(parts, ns, *c);
                } else if (auto* m = std::get_if<language::monitor_item>(&item.node)) {
                    code = monitor(parts, ns, *m);
                } else if (auto* b = std::get_if<language::board_item>(&item.node)) {
                    code = board(parts, ns, *b, screen);
                } else if (auto* c = std::get_if<language::calendar_item>(&item.node)) {
                    code = calendar(parts, ns, *c);
                } else if (auto* c = std::get_if<language::canvas_item>(&item.node)) {
                    code = canvas(parts, ns, *c);
                } else if (auto* c = std::get_if<language::pipeline_item>(&item.node)) {
                    code = pipeline(parts, ns, *c);
                } else if (auto* f = std::get_if<language::form_item>(&item.node)) {
                    if (model_.on_rows(ns, screen, screens::full_command(ns, f->commands.front()))) continue;  // each row's button opens it
                    code = form(parts, ns, *f, screens::model::button_for(screen, f->commands.front()));
                } else if (auto* c = std::get_if<language::confirm_item>(&item.node)) {
                    code = "confirm(p, " + str(screens::full_command(ns, c->command)) + ", " + str(c->message) + ")";
                } else if (auto* b = std::get_if<language::button_item>(&item.node); b && b->along) {
                    code = steps(parts, ns, *b);
                } else if (auto* b = std::get_if<language::button_item>(&item.node)) {
                    if (screens::model::has_form_for(screen, b->command)) continue;  // the form draws its own button
                    code = command_of(parts, ns, *b);
                } else if (auto* c = std::get_if<language::collage_item>(&item.node)) {
                    code = collage(parts, ns, *c);
                } else if (auto* l = std::get_if<language::layers_item>(&item.node); l && collage_) {
                    code = layers(parts, ns, *l, *collage_);
                } else if (auto* paper_item = std::get_if<language::paper_item>(&item.node)) {
                    code = paper(parts, ns, *paper_item);
                } else if (auto* picture = std::get_if<language::picture_item>(&item.node)) {
                    // picture poster.preview fit: as wide as its place, 320 tall.
                    std::set<std::string> read;
                    code = "picture_of(p, " + condition_of(parts, ns, *picture->source, read) + ".as_string(), " + (picture->fit ? "true" : "false") + ", 320)";
                } else if (auto* insert = std::get_if<language::paper_insert>(&item.node)) {
                    // A block made after the caret's, by the screen's paper.
                    std::string given;
                    for (const auto& v : insert->given) {
                        std::set<std::string> read;
                        given += (given.empty() ? "" : ", ") + std::string("{") + str(v.name) + ", " + condition_of(parts, ns, *v.value, read) + "}";
                    }
                    code = std::string(insert->sizes ? "paper_size(p, " : "paper_button(p, ") + "{.key = " + str(paper_key_) + ", .label = " + str(insert->label.value_or("Insert")) + ", .icon = " + str(insert->icon.value_or("")) +
                           (insert->keys.empty() ? "" : ", .keys = " + keys_of(insert->keys)) + ", .fields = value(std::map<std::string, value>{" + given + "})" +
                           (insert->sizes ? ", .sizes = " + std::to_string(insert->sizes) : "") + "})";
                } else if (auto* does = std::get_if<language::collage_button>(&item.node); does && into_paper(*does)) {
                    // Paste into the screen's paper, or pictures chosen from files: after the caret.
                    bool chooses = does->does == language::collage_button::kind::add_pictures;
                    code = "paper_button(p, {.key = " + str(paper_key_) + ", .label = " + str(language::label_of(*does)) + ", .icon = " + str(does->icon.value_or("")) +
                           (does->keys.empty() ? "" : ", .keys = " + keys_of(does->keys)) + (chooses ? ", .chooses = true})" : ", .pastes = true})");
                } else if (auto* does = std::get_if<language::collage_button>(&item.node)) {
                    std::string what = language::asked_of(*does, collage_over_), label = language::label_of(*does);
                    code = "collage_button(p, {.key = " + str(collage_key_) + ", .does = " + str(what) + ", .label = " + str(label) + ", .icon = " + str(does->icon.value_or("")) +
                           (does->keys.empty() ? "" : ", .keys = " + keys_of(does->keys)) + "})";
                } else if (auto* t = std::get_if<language::toggle_item>(&item.node); t && t->choice) {
                    // One of an enum's choices, pressed while it's the one.
                    code = "choose(p, " + str(route_ + " " + t->name) + ", " + str(*t->choice) + ", " + str(choices_.at(t->name)) + ", " +
                           str(t->label ? *t->label : screens::label(*t->choice)) + ", " + str(t->icon.value_or("")) + ", " + (t->keys.empty() ? "{}" : keys_of(t->keys)) + ")";
                } else if (auto* t = std::get_if<language::toggle_item>(&item.node)) {
                    code = "toggle(p, " + str(route_ + " " + t->name) + ", " + str(t->label ? *t->label : screens::label(t->name)) + ", " + str(t->icon.value_or("")) + ", " +
                           (t->keys.empty() ? "{}" : keys_of(t->keys)) + ")";
                } else if (auto* h = std::get_if<language::history_item>(&item.node)) {
                    code = std::string("history(p, ") + (h->redo ? "true" : "false") + ", " + str(h->label.value_or("")) + ", " + str(h->icon.value_or("")) + ", " +
                           (h->keys.empty() ? "{}" : keys_of(h->keys)) + ")";
                } else if (std::holds_alternative<language::messages_item>(item.node)) {
                    code = "messages(p)";
                } else if (auto* f = std::get_if<language::full_screen_item>(&item.node)) {
                    code = "full_screen(p, " + str(f->label.value_or("")) + ", " + str(f->icon.value_or("")) + ", " + (f->keys.empty() ? "{}" : keys_of(f->keys)) + ")";
                } else if (std::holds_alternative<language::on_open_item>(item.node)) {
                    // A screen's on open lines, said together where the first is, tried in order.
                    bool first_one = std::none_of(list.begin() + static_cast<std::ptrdiff_t>(first), list.begin() + static_cast<std::ptrdiff_t>(at),
                                                  [](const language::screen_item& it) { return std::holds_alternative<language::on_open_item>(it.node); });
                    if (!first_one) continue;
                    code = on_open(parts, ns, list);
                    out.push_back({code, item.where.line});
                    continue;
                }
                if (code.empty()) continue;
                // Drawn by a function of its own, as GCC sees a temporary in either arm of
                // a ?: outliving the line when the arm's a call given a spec.
                // In a toolbar, a group or a line that doesn't apply now is there, but off, so
                // the toolbar keeps its shape.
                bool toolbar = in_group_ || (std::holds_alternative<language::content_block>(item.node) &&
                                             std::get<language::content_block>(item.node).type == language::content_block::kind::group);
                if (item.when && !grouped && toolbar) code = "p.off_unless(" + condition(parts, ns, *item.when) + ", [&] { return " + code + "; })";
                else if (item.when && !grouped) code = "[&] { if (" + condition(parts, ns, *item.when) + ") return " + code + "; return view(); }()";
                if (presses(item)) {
                    if (row.empty()) row_line = item.where.line;
                    row.push_back(code);
                    continue;
                }
                finish_row();
                out.push_back({code, item.where.line});
            }
            finish_row();
            return out;
        }

        // Whether code reads the page it's on, p, outside the words it says.
        static bool uses_page(const std::string& code) {
            bool quoted = false;
            for (std::size_t at = 0; at < code.size(); ++at) {
                if (code[at] == '\\' && quoted) {
                    ++at;
                    continue;
                }
                if (code[at] == '"') quoted = !quoted;
                if (quoted || code[at] != 'p') continue;
                bool before = at > 0 && (std::isalnum(static_cast<unsigned char>(code[at - 1])) || code[at - 1] == '_');
                bool after = at + 1 < code.size() && (std::isalnum(static_cast<unsigned char>(code[at + 1])) || code[at + 1] == '_');
                if (!before && !after) return true;
            }
            return false;
        }

        static std::string joined_items(const std::vector<written>& all) {
            std::string out;
            for (const auto& w : all) out += (out.empty() ? "" : ", ") + w.code;
            return out;
        }

        // What a screen's builder reads, before what it builds: the ids its address
        // names, and its views.
        void declare(stream& out, screen_parts& parts, const std::string& ns) {
            // The entities its address names, which views per them are read for.
            for (const auto& [variable, view] : parts.views) {
                if (auto per = model_.per_entity(view)) parts.params.insert(*per);
            }
            for (const auto& entity : parts.params) {
                auto keyed = model_.keyed_by(ns, entity, route_);
                std::string id = native_detail::identifier(entity + "_id");
                if (!keyed.empty()) {
                    std::string list;
                    for (const auto& key : keyed) list += "p.param(" + str(key) + "), ";
                    out.line("const std::string " + id + " = key_of({" + list + "p.param(" + str(entity) + ")});");
                } else {
                    out.line("const std::string " + id + " = p.param(" + str(entity) + ");");
                }
            }
            for (const auto& [variable, view] : parts.views) {
                std::string subject;
                if (auto per = model_.per_entity(view)) subject = ", " + native_detail::identifier(*per + "_id");
                else if (std::find(model_.personal.begin(), model_.personal.end(), view) != model_.personal.end()) subject = ", p.me()";
                out.line("auto " + variable + " = p.view(" + str(view) + subject + ");");
            }
            // Each pick it reads, the row of its list picked now.
            for (const auto& pick : parts.picks) {
                const auto& of = picks_.at(pick);
                out.line("value " + native_detail::identifier("picked_" + pick) + " = picked(p, " + str(route_ + " " + pick) + ", " + view_variable(parts, of.view) + ", " + str(of.list) + ");");
            }
        }

        // one screen file

        stream screen_file(const language::file& f, const std::vector<screens::found_screen>& found, const std::string& stem) {
            screen_path_ = f.path;
            page_includes_.clear();
            stream bodies("\t");
            for (const auto& [ns, s, where] : found) {
                auto from_screen = bodies.from(f.path, where.line);
                route_ = model_.full_route(ns, s->route);
                toggles_ = toggles_in(s->items);
                choices_ = choices_in(s->items);
                at_end_said_ = false;
                collage_key_ = route_ + " collage";
                collage_over_.reset();
                collage_ = nullptr;
                paper_key_ = route_ + " paper";
                screen_pastes_into_paper_ = false;
                std::function<void(const std::vector<language::screen_item>&)> papers = [&](const std::vector<language::screen_item>& inside) {
                    for (const auto& item : inside) {
                        if (auto* paper = std::get_if<language::paper_item>(&item.node)) paper_key_ = route_ + " paper " + paper->list, screen_pastes_into_paper_ = paper->paste;
                        if (auto* block = std::get_if<language::content_block>(&item.node)) papers(block->items);
                    }
                };
                papers(s->items);
                for (const auto& item : s->items) {
                    if (auto* block = std::get_if<language::content_block>(&item.node)) {
                        for (const auto& inside : block->items) {
                            if (auto* c = std::get_if<language::collage_item>(&inside.node)) collage_key_ = collage_key(*c), collage_over_ = c->over, collage_ = c;
                        }
                    }
                    if (auto* c = std::get_if<language::collage_item>(&item.node)) collage_key_ = collage_key(*c), collage_over_ = c->over, collage_ = c;
                }
                workspace_ = (s->layout ? *s->layout : model_.project.layout) == "workspace";
                picks_.clear();
                for (const auto* pick : picks_in(s->items)) {
                    const language::entity_declaration* of = model_.listed(ns, pick->view.text(), pick->list.empty() ? std::nullopt : std::optional<std::string>(pick->list));
                    picks_[pick->name] = {screens::full_view(ns, pick->view.text()), pick->list.empty() ? "rows" : pick->list, of ? of->name : ""};
                }
                under_ = s->under ? model_.full_route(ns, *s->under) : "";
                shows_pages_ = false;
                title_ = s->title_is_name ? "" : s->title;
                screen_parts parts;
                stream body("\t");
                // What its address says above it, its title read live, and what's around the title.
                std::vector<std::string> around;
                {
                    std::string crumbs_list;
                    for (const auto& c : model_.crumbs(ns, *s, route_)) {
                        std::string fill;
                        for (const auto& filled : c.fill) {
                            fill += (fill.empty() ? "" : ", ") + std::string("{") + str(filled.parameter) + ", {" + view_variable(parts, filled.view) + ", " + str(filled.field) + ", " +
                                    std::to_string(filled.keys) + "}}";
                        }
                        crumbs_list += (crumbs_list.empty() ? "" : ", ") + std::string("{") + str(c.to) + ", " + parts_of(parts, c.title) + (fill.empty() ? "" : ", {" + fill + "}") + "}";
                    }
                    // Sorted nearest last, as the model gives them by depth.
                    if (!crumbs_list.empty()) around.push_back("crumbs(p, {" + crumbs_list + "});");
                }
                std::string title = model_.title_of(*s);
                if (!s->title_is_name && title.find('{') != std::string::npos) around.push_back("p.title(" + parts_of(parts, ns, title) + ");");
                // What a header says above every screen under its path, and a menu in it down the side.
                std::vector<written> before;
                std::string menu;
                for (const auto& [header_ns, header] : model_.headers_over(route_)) {
                    std::size_t until = header->items.size();
                    for (std::size_t at = 0; at < header->items.size(); ++at) {
                        auto* block = std::get_if<language::content_block>(&header->items[at].node);
                        if (block && block->type == language::content_block::kind::menu && until == header->items.size()) {
                            until = at;
                            std::string links;
                            for (const auto& inside : block->items) {
                                auto* link = std::get_if<language::content_link>(&inside.node);
                                if (!link) continue;
                                std::string to = link->target.starts_with("/") ? model_.full_route(header_ns, link->target) : link->target;
                                links += (links.empty() ? "" : ", ") + std::string("{") + str(to) + ", " + str(link->label) + "}";
                            }
                            menu = links;
                        }
                    }
                    for (auto& w : items(parts, header_ns, header->items, header->items, 0, until)) before.push_back(std::move(w));
                }
                for (const auto& item : s->items) {
                    auto* block = std::get_if<language::content_block>(&item.node);
                    if (!block || block->type != language::content_block::kind::heading) continue;
                    auto inside = items(parts, ns, block->items, s->items);
                    around.push_back("heading(p, actions(" + joined_items(inside) + "));");
                }
                std::vector<const language::screen_item*> regions;
                for (const auto& item : s->items) {
                    auto* block = std::get_if<language::content_block>(&item.node);
                    if (block && block->type == language::content_block::kind::region) regions.push_back(&item);
                }
                std::vector<written> shown;
                for (auto& w : before) shown.push_back(std::move(w));
                if (regions.empty()) {
                    for (auto& w : items(parts, ns, s->items, s->items)) shown.push_back(std::move(w));
                } else {
                    // A subtitle goes under the title, outside the regions, and what's done as
                    // the screen opens is its own, outside them too, said once for all its lines.
                    bool opening = false;
                    for (std::size_t at = 0; at < s->items.size(); ++at) {
                        auto* t = std::get_if<language::content_text>(&s->items[at].node);
                        bool opens = std::holds_alternative<language::on_open_item>(s->items[at].node) && !std::exchange(opening, true);
                        if ((t && t->subtitle) || opens) {
                            for (auto& w : items(parts, ns, s->items, s->items, at, 1)) shown.push_back(std::move(w));
                        }
                    }
                    std::string layout = s->layout ? *s->layout : model_.project.layout;
                    std::string inside;
                    for (const auto* item : regions) {
                        const auto& region = std::get<language::content_block>(item->node);
                        // A workspace's bar, toolbar and status line are rows, each line its own.
                        bool across = layout == "workspace" && region.title != "main" && region.title != "side";
                        bool alone = std::exchange(each_alone_, across);
                        bool quiet = std::exchange(quiet_, layout == "workspace" && region.title == "status");
                        std::string drawn;
                        if (layout == "workspace" && region.title == "tools") {
                            // A workspace's tools, a column of them down its side, each a small tool.
                            bool grouping = std::exchange(in_group_, true);
                            drawn = "tool_column(p, [&] { return tools(" + joined_items(alternatives(items(parts, ns, region.items, s->items))) + "); })";
                            in_group_ = grouping;
                        } else {
                            drawn = std::string(across ? "along(" : "items(") + joined_items(items(parts, ns, region.items, s->items)) + ")";
                        }
                        each_alone_ = alone;
                        quiet_ = quiet;
                        // side when layers: there only while it holds.
                        if (item->when) drawn = "[&] { if (" + condition(parts, ns, *item->when) + ") return " + drawn + "; return view(); }()";
                        inside += (inside.empty() ? "" : ", ") + std::string("region{") + str(region.title) + ", " + drawn + "}";
                    }
                    if (layout == "workspace") shown.push_back({"workspace(p, " + inside + ")", where.line});
                    else shown.push_back({"layout(" + str(layout) + ", " + inside + ")", where.line});
                }
                if (!menu.empty()) shown = {{"menu(p, {" + menu + "}, items(" + joined_items(shown) + "))", where.line}};

                // The screen, as the site has it: its title, its address, its place in the navigation.
                std::string info_title = s->title_is_name ? screens::label(s->title) : s->title.find('{') != std::string::npos ? "" : s->title;
                std::string route = shows_pages_ && route_.ends_with("/:page") ? route_ + "?" : route_;
                // A screen that needs a parameter opens from a link to one particular thing,
                // and the app's own name is its home: neither is listed in the navigation.
                std::string nav;
                if (!ns.empty() && !screens::needs_parameter(route) && info_title != model_.site_title() && !s->title.empty()) nav = info_title;
                // A screen of words alone doesn't use the page it's on.
                bool reads = !parts.views.empty() || !parts.params.empty() || !around.empty() ||
                             std::any_of(shown.begin(), shown.end(), [](const written& w) { return uses_page(w.code); });
                bodies.open("s.screens.push_back({" + str(info_title) + ", " + str(route) + ", " + str(nav) + (reads ? ", [](page& p) {" : ", [](page&) {"));
                declare(bodies, parts, ns);
                for (const auto& line : around) bodies.line(line);
                if (shown.empty()) {
                    bodies.line("return items();");
                } else {
                    bodies.open("return items(");
                    for (std::size_t i = 0; i < shown.size(); ++i) {
                        auto from_item = bodies.from(f.path, shown[i].line);
                        bodies.line(shown[i].code + (i + 1 < shown.size() ? "," : ""));
                    }
                    bodies.close(");");
                }
                bodies.close("}});");
            }
            stream out("\t");
            out.generated_from(std::filesystem::path(f.path).filename().string());
            {
                auto fixed = out.fixed();
                out.line("#include \"../app.hpp\"");
                for (const auto& name : page_includes_) out.line("#include \"../pages/" + name + ".hpp\"");
                out.line();
                out.open("namespace app {");
                out.line();
                out.line("// Each of " + std::filesystem::path(f.path).filename().string() + "'s screens, added to the site.");
                out.open("void " + native_detail::identifier(stem + "_screens") + "(site& s) {");
            }
            out.embed(bodies);
            {
                auto fixed = out.fixed();
                out.close("}");
                out.line();
                out.close("}  // namespace app");
            }
            return out;
        }

        // the files around the screens

        // The badges beside the app's name, from its header, and its footer, built as a
        // screen is, at the foot of every page.
        stream site_file() {
            stream bodies("\t");
            std::function<void(const std::string&, const std::vector<language::declaration>&)> walk = [&](const std::string& ns, const std::vector<language::declaration>& declarations) {
                for (const auto& d : declarations) {
                    if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) walk(screens::join(ns, n->name), n->declarations);
                    if (auto* h = std::get_if<language::header_declaration>(&d.node); h && !h->route) {
                        for (const auto& b : h->badges) {
                            auto from = bodies.from(files_path(d), b.where.line);
                            std::string full = b.view;
                            for (const auto& [view_ns, declared] : model_.views) {
                                if (declared.contains(b.view)) full = screens::join(view_ns, b.view);
                            }
                            bodies.line("s.badges.push_back({" + str(full) + ", " + str(b.value) + ", " + str(screens::label(b.value)) + "});");
                        }
                    }
                    // notify planner.reminders at remind_at { ... }: said whatever screen is open.
                    if (auto* n = std::get_if<language::notify_declaration>(&d.node)) {
                        auto from = bodies.from(files_path(d), d.where.line);
                        std::string shown, buttons;
                        for (const auto& s : n->shown) shown += (shown.empty() ? "" : ", ") + str(screens::text_of(*s.value));
                        for (const auto& b : n->buttons) {
                            std::string given;
                            for (const auto& v : b.given) given += (given.empty() ? "" : ", ") + std::string("{") + str(v.name) + ", " + plain_value(*v.value, "value()") + "}";
                            std::string label = b.label.value_or(screens::label(b.command.parts.back()));
                            buttons += (buttons.empty() ? "" : ", ") + std::string("notice_button{") + str(screens::full_command(ns, b.command)) + ", " + str(label) +
                                       (given.empty() ? "" : ", [] { return value(std::map<std::string, value>{" + given + "}); }") + "}";
                        }
                        bodies.line("s.notices.push_back({.view = " + str(screens::full_view(ns, n->view.text())) + ", .list = " + str(n->list.empty() ? "rows" : n->list) +
                                    ", .at = " + str(n->at) + ", .shown = {" + shown + "}" + (buttons.empty() ? "" : ", .buttons = {" + buttons + "}") + "});");
                    }
                    if (auto* f = std::get_if<language::footer_declaration>(&d.node)) {
                        auto from = bodies.from(files_path(d), d.where.line);
                        screen_path_ = files_path(d);
                        route_ = "";
                        title_ = "";
                        screen_parts parts;
                        auto shown = items(parts, ns, f->items, f->items);
                        bodies.open("s.footer = [](page& p) {");
                        declare(bodies, parts, ns);
                        // In one quiet line, its parts apart, or in columns.
                        bodies.open(f->layout == "columns" ? "return items(" : "return actions(");
                        for (std::size_t i = 0; i < shown.size(); ++i) {
                            auto from_item = bodies.from(files_path(d), shown[i].line);
                            bodies.line(shown[i].code + (i + 1 < shown.size() ? "," : ""));
                        }
                        bodies.close(");");
                        bodies.close("};");
                    }
                }
            };
            for (const auto& f : files_) {
                current_file_ = &f;
                walk("", f.declarations);
            }
            // The top bar's order, when the header gives one.
            std::string navigation;
            for (const auto& address : model_.navigation(files_)) navigation += (navigation.empty() ? "" : ", ") + str(address);
            if (!navigation.empty()) bodies.line("s.navigation = {" + navigation + "};");
            stream out("\t");
            out.generated_from(project_path_.empty() ? "the project" : std::filesystem::path(project_path_).filename().string());
            {
                auto fixed = out.fixed();
                out.line("#include \"app.hpp\"");
                out.line();
                out.open("namespace app {");
                out.line();
                out.open("void site_parts([[maybe_unused]] site& s) {");
            }
            out.embed(bodies);
            {
                auto fixed = out.fixed();
                out.close("}");
                out.line();
                out.close("}  // namespace app");
            }
            return out;
        }
        const language::file* current_file_ = nullptr;
        std::vector<std::string> local_sources_;  // src/local/<namespace>.cpp, for storage local
        std::vector<std::string> installs_;       // and the function each gives its source with
        std::string files_path(const language::declaration&) const { return current_file_ ? current_file_->path : ""; }

        std::string runtime() const { return target_ == "desktop" ? "one::desktop::runtime" : "one::terminal::runtime"; }

        stream app_hpp(const std::vector<std::string>& stems) const {
            stream out("\t");
            auto from = out.from(project_path_, project_ ? project_->where.line : 0);
            out.generated_from(project_path_.empty() ? "the project" : std::filesystem::path(project_path_).filename().string());
            out.line("#pragma once");
            out.line();
            if (target_ == "desktop") {
                out.line("#include <one/desktop/runtime/components.hpp>");
                out.line("#include <one/desktop/runtime/markdown.hpp>");
            } else {
                out.line("#include <one/terminal/runtime/components.hpp>");
            }
            if (model_.project.local) out.line("#include <one/local/local.hpp>");
            out.line();
            out.open("namespace app {");
            out.line();
            out.line("using namespace " + runtime() + ";");
            out.line("using view = " + std::string(target_ == "desktop" ? "one::desktop::view" : "one::terminal::view") + ";");
            out.line();
            out.line("// What the app was built from, which a footer can show: the uione release, and the");
            out.line("// commit and repository its build found.");
            out.line("inline const std::string build_version = " + str(version) + ";");
            out.line("inline const std::string build_commit = UIONE_COMMIT;");
            out.line("inline const std::string build_repository = UIONE_REPOSITORY;");
            out.line();
            out.line("// The badges beside the app's name, and its footer.");
            out.line("void site_parts(site& s);");
            for (const auto& stem : stems) out.line("void " + native_detail::identifier(stem + "_screens") + "(site& s);");
            if (!installs_.empty()) {
                out.line();
                out.line("// The entities, commands and views each namespace keeps on this machine.");
                for (const auto& install : installs_) out.line("void " + install + "(one::local::local_source& source);");
            }
            if (!components_.empty()) {
                out.line();
                out.line("// The components the screens draw, written by hand in components/.");
                for (const auto& [name, path] : components_) out.line("view " + native_detail::identifier(name) + "(page& p);");
            }
            out.line();
            out.close("}  // namespace app");
            return out;
        }

        // A theme's colors and fonts, as the desktop draws with them, light or dark.
        theme::look theme_look() const { return theme::resolve(files_, model_.project.theme.empty() ? "harbor" : model_.project.theme).value_or(theme::look{}); }

        // Fonts the theme brings, each its file's bytes, offered before anything's drawn.
        void brought_fonts(stream& out) const {
            for (const auto& [family, path] : theme_look().font_files) {
                out.open("{");
                out.line("// " + family + ".");
                out.open("static const unsigned char font[] = {");
                out.line("#embed \"../fonts/" + path.substr(path.find_last_of('/') + 1) + "\"");
                out.close("};");
                out.line("one::desktop::use_font(font);");
                out.close("}");
            }
        }

        std::vector<std::string> theme_of(bool dark = false) const {
            auto look = theme_look();
            std::vector<std::string> out{"one::desktop::theme t = one::desktop::theme::harbor();"};
            for (const auto& role : {"page", "surface", "sunken", "ink", "muted", "line", "accent", "danger", "success", "warning"}) {
                auto found = look.colors.find(role);
                if (found == look.colors.end()) continue;
                const std::string& hex = dark ? found->second.second : found->second.first;
                if (!hex.starts_with("#") || hex.size() != 7) continue;
                out.push_back(std::string("t.") + role + " = one::desktop::color::hex(0x" + hex.substr(1) + ");");
            }
            out.push_back("t.text_family = " + str(look.text) + ";");
            if (!look.heading.empty()) out.push_back("t.heading_family = " + str(look.heading) + ";");
            out.push_back("t.code_family = " + str(look.code) + ";");
            out.push_back("t.document_family = " + str(look.document + ", serif") + ";");
            out.push_back("t.corners = " + std::to_string(look.corners) + ";");
            out.push_back(std::string("t.raised = ") + (look.depth == "raised" ? "true" : "false") + ";");
            return out;
        }

        // The main of an app that keeps its data on this machine: its folder opened and
        // held, what it kept read back, and its own commands and views given to the
        // screens, with no server and nobody signing in.
        stream local_main_cpp(const std::vector<std::string>& stems) const {
            stream out("\t");
            auto from = out.from(project_path_, project_ ? project_->where.line : 0);
            out.generated_from(project_path_.empty() ? "the project" : std::filesystem::path(project_path_).filename().string());
            std::string name = model_.project.name, title = model_.site_title();
            out.line("// " + title + " as a " + target_ + " app, keeping its data on this machine, in");
            out.line("// $XDG_DATA_HOME/" + name + ".");
            out.line("//");
            out.line("//   " + name + "            its first screen");
            out.line("//   " + name + " /address   the screen at an address, like /projects");
            if (target_ == "desktop") {
                out.line("//");
                out.line("// While it's open, launching it again hands the address to the copy open.");
            }
            out.line();
            out.line("#include <print>");
            out.line();
            out.line("#include \"app.hpp\"");
            out.line();
            out.open("int main(int count, char** given) {");
            if (target_ == "desktop") {
                out.line("auto window_app = one::desktop::app::connect();");
                out.open("if (!window_app) {");
                out.line("std::println(stderr, \"" + name + ": {}\", window_app.error());");
                out.line("return 1;");
                out.close("}");
            } else {
                out.line("one::terminal::view_app window_app;");
            }
            out.line("auto kept = one::local::folder::open(" + str(name) + ");");
            out.open("if (!kept) {");
            if (target_ == "desktop") {
                out.line("// Held by a copy already open: it's handed the address instead.");
                out.line("if (one::desktop::runtime::hand_over(" + str(app_id()) + ", count > 1 ? given[1] : \"/\")) return 0;");
            }
            out.line("std::println(stderr, \"" + name + ": {}\", kept.error());");
            out.line("return 1;");
            out.close("}");
            out.line("auto source = one::local::local_source::open(std::move(*kept));");
            out.open("if (!source) {");
            out.line("std::println(stderr, \"" + name + ": {}\", source.error());");
            out.line("return 1;");
            out.close("}");
            for (const auto& install : installs_) out.line("app::" + install + "(**source);");
            out.line("app::site s{.name = " + str(title) + ", .id = " + str(app_id()) + "};");
            if (target_ == "desktop") {
                brought_fonts(out);
                out.open("{");
                for (const auto& line : theme_of()) out.line(line);
                out.line("s.look = t;");
                out.close("}");
                if (has_dark()) {
                    out.open("{");
                    for (const auto& line : theme_of(true)) out.line(line);
                    out.line("s.dark = t;");
                    out.close("}");
                }
            }
            out.line("app::site_parts(s);");
            for (const auto& stem : stems) out.line("app::" + native_detail::identifier(stem + "_screens") + "(s);");
            out.line("return app::run(s, " + std::string(target_ == "desktop" ? "*window_app" : "window_app") + ", **source, count > 1 ? given[1] : \"/\");");
            out.close("}");
            return out;
        }

        // Whether the theme says how it looks dark, as uione's own do.
        bool has_dark() const {
            auto look = theme::resolve(files_, model_.project.theme.empty() ? "harbor" : model_.project.theme).value_or(theme::look{});
            return std::ranges::any_of(look.colors, [](const auto& role) { return role.second.second.starts_with("#"); });
        }

        stream main_cpp(const std::vector<std::string>& stems) const {
            if (model_.project.local) return local_main_cpp(stems);
            stream out("\t");
            auto from = out.from(project_path_, project_ ? project_->where.line : 0);
            out.generated_from(project_path_.empty() ? "the project" : std::filesystem::path(project_path_).filename().string());
            std::string name = model_.project.name, title = model_.site_title();
            std::string server = domain_.empty() ? "http://localhost:8080" : "https://" + domain_;
            bool signs_in = !model_.project.authentication.empty() || !model_.project.has_project;
            out.line("// " + title + " as a " + target_ + " app, reading its views live from " + server + ", or ONE_SERVER.");
            out.line("//");
            out.line("//   " + name + "            its first screen");
            out.line("//   " + name + " /address   the screen at an address, like /projects");
            out.line();
            out.line("#include <cstdlib>");
            out.line("#include <print>");
            if (signs_in) {
                out.line("#include <spawn.h>");
                out.line("#include <unistd.h>");
            }
            out.line();
            out.line("#include <one/screens/client_source.hpp>");
            out.line();
            out.line("#include \"app.hpp\"");
            out.line();
            out.open("namespace {");
            out.line();
            out.open("std::string setting(const char* name, std::string otherwise) {");
            out.line("const char* given = std::getenv(name);");
            out.line("return given && *given ? given : otherwise;");
            out.close("}");
            out.line();
            out.line("// Where libember tells what it hears: on the app's loop, where the screens' states are set.");
            out.open("struct on_loop : ember::executor {");
            out.line("std::function<void(std::function<void()>)> to;");
            out.line("void post(std::function<void()> task) override { to(std::move(task)); }");
            out.close("};");
            out.line();
            out.close("}  // namespace");
            out.line();
            out.open("int main(int count, char** given) {");
            if (target_ == "desktop") {
                out.line("auto window_app = one::desktop::app::connect();");
                out.open("if (!window_app) {");
                out.line("std::println(stderr, \"" + name + ": {}\", window_app.error());");
                out.line("return 1;");
                out.close("}");
            } else {
                out.line("one::terminal::view_app window_app;");
            }
            out.line("on_loop deliver;");
            out.line(std::string("deliver.to = [&](std::function<void()> task) { window_app") + (target_ == "desktop" ? "->" : ".") + "post(std::move(task)); };");
            out.line("std::string server = setting(\"ONE_SERVER\", " + str(server) + ");");
            out.line("one::client::session session({.server = server, .project = setting(\"ONE_FIREBASE_PROJECT\", \"\"), .firestore_host = setting(\"ONE_FIRESTORE_HOST\", \"\"),");
            out.line("                              .auth_host = setting(\"ONE_AUTH_HOST\", \"\")}, &deliver);");
            if (signs_in) {
                out.line("// Signed in as before, by a service's key, or through the browser, kept for next time.");
                out.open("try {");
                out.line("std::string problem;");
                out.line("auto kept = one::client::read_kept(" + str(name) + ", problem);");
                out.open("if (std::string key = setting(\"ONE_KEY\", kept ? kept->key : \"\"); !key.empty()) {");
                out.line("session.sign_in_with_key(key);");
                out.dedent();
                out.open("} else if (kept && !kept->refresh_token.empty() && kept->server == server) {");
                out.line("session.sign_in_with_refresh_token(kept->refresh_token);");
                out.dedent();
                out.open("} else {");
                out.open("session.sign_in_with_browser([](const std::string& address) {");
                out.line("std::println(stderr, \"Signing in at {}\", address);");
                out.line("pid_t child = 0;");
                out.line("std::string copied = address;");
                out.line("char opener[] = \"xdg-open\";");
                out.line("char* arguments[] = {opener, copied.data(), nullptr};");
                out.line("posix_spawnp(&child, opener, nullptr, nullptr, arguments, environ);");
                out.close("});");
                out.line("one::client::keep(" + str(name) + ", {.server = server, .refresh_token = session.refresh_token()});");
                out.close("}");
                out.dedent();
                out.open("} catch (const std::exception& e) {");
                out.line("std::println(stderr, \"" + name + ": {}\", e.what());");
                out.line("return 3;");
                out.close("}");
            } else {
                // No one signs in to it, but a service may, with its key, as an agent's
                // people do on a board they work.
                out.open("if (std::string key = setting(\"ONE_KEY\", \"\"); !key.empty()) {");
                out.open("try {");
                out.line("session.sign_in_with_key(key);");
                out.dedent();
                out.open("} catch (const std::exception& e) {");
                out.line("std::println(stderr, \"" + name + ": {}\", e.what());");
                out.line("return 3;");
                out.close("}");
                out.close("}");
            }
            out.line("one::screens::client_source source(session, deliver.to);");
            out.line("app::site s{.name = " + str(title) + ", .id = " + str("org.uione." + name) + "};");
            if (target_ == "desktop") {
                brought_fonts(out);
                out.open("{");
                for (const auto& line : theme_of()) out.line(line);
                out.line("s.look = t;");
                out.close("}");
                if (has_dark()) {
                    out.open("{");
                    for (const auto& line : theme_of(true)) out.line(line);
                    out.line("s.dark = t;");
                    out.close("}");
                }
            } else {
                // The theme's colors, which the terminal's are set to while it runs.
                out.open("{");
                out.line("one::screens::palette::theme t;");
                auto look = theme::resolve(files_, model_.project.theme.empty() ? "harbor" : model_.project.theme).value_or(theme::look{});
                for (auto role : one::screens::palette::roles) {
                    if (auto found = look.colors.find(std::string(role)); found != look.colors.end()) {
                        out.line("t[" + str(std::string(role)) + "] = {" + str(found->second.first) + ", " + str(found->second.second) + "};");
                    }
                }
                out.line("s.look.colors = t;");
                out.close("}");
            }
            out.line("app::site_parts(s);");
            for (const auto& stem : stems) out.line("app::" + native_detail::identifier(stem + "_screens") + "(s);");
            out.line("return app::run(s, " + std::string(target_ == "desktop" ? "*window_app" : "window_app") + ", source, count > 1 ? given[1] : \"/\");");
            out.close("}");
            return out;
        }

        // The components' files, copied into the app as they are: the project's own code.
        // What they share comes along too: every other .cpp and .hpp in their folder and
        // the folders inside it, and the fonts they draw with, .ttf and .otf, which they
        // #embed so the app needs nothing beside it.
        std::vector<output_file> component_files() {
            std::vector<output_file> out;
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
                    if (it->is_regular_file() && (extension == ".cpp" || extension == ".hpp" || extension == ".ttf" || extension == ".otf")) found.push_back(it->path());
                }
                std::sort(found.begin(), found.end());
                for (const auto& path : found) {
                    auto content = platform::read_file(path.string());
                    if (!content) continue;
                    std::string relative = std::filesystem::relative(path, folder).generic_string();
                    if (path.extension() == ".cpp") component_sources_.push_back(relative);
                    output_file f{"src/components/" + relative, *content, false, {}};
                    f.sources.assign(static_cast<std::size_t>(std::count(content->begin(), content->end(), '\n')), code::source{});
                    out.push_back(std::move(f));
                }
            }
            return out;
        }
        std::vector<std::string> component_sources_;  // each .cpp copied, inside src/components

        std::string app_id() const { return "org.uione." + model_.project.name; }
        // What the app's installed as: its project's name and its kind, like tracker-desktop,
        // so the desktop and terminal apps, and a project's own command, live side by side.
        std::string installed_name() const { return model_.project.name + "-" + target_; }

        // A file copied as it is, each of its lines its own.
        static output_file copied(std::string path, std::string content, const std::string& from) {
            std::size_t lines = static_cast<std::size_t>(std::count(content.begin(), content.end(), '\n'));
            if (!content.empty() && content.back() != '\n') ++lines;
            std::vector<std::optional<code::source>> sources;
            for (std::size_t line = 1; line <= lines; ++line) sources.push_back(code::source{from, static_cast<int>(line)});
            return {std::move(path), std::move(content), false, std::move(sources)};
        }

        // The project's license, as LICENSE, LICENSE.md or COPYING, and where it is: beside
        // its .one files, or in a folder above them, up to the top of its repository.
        std::optional<std::pair<std::string, std::string>> license_text() const {
            namespace fs = std::filesystem;
            std::error_code ignored;
            for (fs::path folder = fs::absolute(project_dir_, ignored).lexically_normal(); !folder.empty(); folder = folder.parent_path()) {
                for (const char* name : {"LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING"}) {
                    if (auto text = platform::read_file((folder / name).string())) return std::pair{(folder / name).string(), *text};
                }
                if (fs::exists(folder / ".git", ignored) || folder == folder.parent_path()) break;
            }
            return std::nullopt;
        }

        // How the system lists the desktop app among its own: its name, what runs it, and
        // its icon, by the id its window gives.
        stream desktop_entry() const {
            stream out;
            auto from = out.from(project_path_, project_ ? project_->where.line : 0);
            out.line("[Desktop Entry]");
            out.line("Type=Application");
            out.line("Name=" + model_.site_title());
            out.line("Exec=" + installed_name() + " %u");
            if (!model_.project.icon.empty()) out.line("Icon=" + app_id());
            out.line("Terminal=false");
            out.line("Categories=Office;");
            out.line("StartupWMClass=" + app_id());
            return out;
        }

        stream cmake_lists(const std::vector<std::string>& stems) const {
            stream out("\t");
            auto from = out.from(project_path_, project_ ? project_->where.line : 0);
            {
                auto fixed = out.fixed();
                out.line("# Generated by one. Do not edit.");
            }
            std::string name = model_.project.name;
            out.line();
            out.line("cmake_minimum_required(VERSION 3.28)");
            out.line("project(" + native_detail::identifier(name) + "_" + target_ + " LANGUAGES C CXX)");
            out.line();
            out.line("set(CMAKE_CXX_STANDARD 26)");
            out.line("set(CMAKE_CXX_STANDARD_REQUIRED ON)");
            out.line();
            out.line("# uione's libraries: a checkout of uione named by UIONE, or the release installed with");
            out.line("# the compiler, in ~/.local/share/uione/" + std::string(version) + ".");
            out.line("# UIONE in the environment wins over what an earlier configure cached.");
            out.line("set(UIONE \"\" CACHE PATH \"a checkout of uione, or its release's libraries\")");
            out.open("if(DEFINED ENV{UIONE} AND NOT \"$ENV{UIONE}\" STREQUAL \"\")");
            out.line("set(UIONE \"$ENV{UIONE}\")");
            out.close("endif()");
            out.open("if(NOT UIONE)");
            out.line("set(UIONE \"$ENV{HOME}/.local/share/uione/" + std::string(version) + "\")");
            out.close("endif()");
            out.line("# Added once, however many apps a build holds.");
            out.open("if(NOT TARGET one_" + target_ + "_runtime)");
            out.line("add_subdirectory(${UIONE}/" + target_ + " ${CMAKE_BINARY_DIR}/uione-" + target_ + " EXCLUDE_FROM_ALL)");
            out.close("endif()");
            if (model_.project.local) {
                out.line("# What keeps its data on this machine.");
                out.open("if(NOT TARGET one_local)");
                out.line("add_subdirectory(${UIONE}/local ${CMAKE_BINARY_DIR}/uione-local EXCLUDE_FROM_ALL)");
                out.close("endif()");
            }
            out.line();
            std::string sources = "src/main.cpp";
            sources += " src/site.cpp";
            for (const auto& stem : stems) sources += " src/screens/" + stem + ".cpp";
            for (const auto& component : component_sources_) sources += " src/components/" + component;
            for (const auto& local : local_sources_) sources += " " + local;
            // Named for its target too, so one build can hold an app's desktop and terminal
            // builds both, and the program is still the app's name.
            const std::string executable = name + "_" + target_;
            out.line("add_executable(" + executable + " " + sources + ")");
            out.line("set_target_properties(" + executable + " PROPERTIES OUTPUT_NAME " + name + ")");
            out.line("target_link_libraries(" + executable + " PRIVATE one_" + target_ + "_runtime" + (model_.project.local ? " one_local" : "") + ")");
            out.line("target_compile_options(" + executable + " PRIVATE -Wno-free-nonheap-object)");
            out.line();
            out.line("# What it was built from, which its footer can show: the commit, and its repository.");
            out.line("execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)");
            out.line("execute_process(COMMAND git remote get-url origin WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE repository OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)");
            out.line("string(REGEX REPLACE \"^git@([^:]+):\" \"https://\\\\1/\" repository \"${repository}\")");
            out.line("string(REGEX REPLACE \"\\\\.git$\" \"\" repository \"${repository}\")");
            out.line("target_compile_definitions(" + executable + " PRIVATE UIONE_COMMIT=\"${commit}\" UIONE_REPOSITORY=\"${repository}\")");
            out.line();
            const std::string installed = installed_name();
            out.line("# Installing it: cmake --install out --prefix ~/.local puts it in bin as " + installed + ",");
            out.line("# with its license" + std::string(target_ == "desktop" ? ", and what lists it among the system's apps, with its icon." : "."));
            out.line("# The project's LICENSE goes with it, or it isn't installed or packaged at all.");
            out.line("install(PROGRAMS $<TARGET_FILE:" + executable + "> DESTINATION bin RENAME " + installed + ")");
            out.open("if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/LICENSE)");
            out.line("install(FILES packaging/LICENSE DESTINATION share/licenses/" + installed + ")");
            out.dedent();
            out.open("else()");
            out.line("install(CODE \"message(FATAL_ERROR \\\"" + name + " has no LICENSE beside its .one files, so it isn't installed or packaged\\\")\")");
            out.close("endif()");
            if (target_ == "desktop") {
                out.line("install(FILES packaging/" + app_id() + ".desktop DESTINATION share/applications)");
                if (!model_.project.icon.empty()) out.line("install(FILES packaging/" + app_id() + ".svg DESTINATION share/icons/hicolor/scalable/apps)");
            }
            out.line();
            out.line("# Its package: cmake --build out --target package makes " + installed + "-<version>-linux-<machine>.tar.gz,");
            out.line("# unpacked into a prefix, like tar -xzf it -C ~/.local. The version is VERSION, or the commit.");
            out.line("set(VERSION \"${commit}\" CACHE STRING \"the version a package is named for\")");
            out.line("set(CPACK_GENERATOR TGZ)");
            out.line("set(CPACK_PACKAGE_NAME " + installed + ")");
            out.line("set(CPACK_PACKAGE_FILE_NAME \"" + installed + "-${VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}\")");
            out.line("set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)");
            out.line("set(CPACK_PACKAGING_INSTALL_PREFIX \"\")");
            out.line("include(CPack)");
            return out;
        }
    };

    std::vector<output_file> generate_native(const std::vector<language::file>& files, const std::string& project_dir, const std::string& target) {
        return native_generator(files, project_dir, target).generate();
    }

}  // namespace one::generators
