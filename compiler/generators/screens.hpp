// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a project's screens mean, whatever draws them: the web app, the desktop app
// or the terminal app. Each generator asks this model, then writes its own code.
//
// It holds the project, indexed once: its settings, entities, views and commands,
// where each namespace's screens live, the roles held within places, and every
// screen. Its answers are plain data:
//   - a screen's address, its title with the live values in it, and its trail of
//     pages above, with what fills a page's address this one doesn't say;
//   - how an address names what a view is read for, by an id or by a key's parts;
//   - a list's columns as they're shown, its tint and its track;
//   - a form's fields, with their kinds and where their choices come from;
//   - whether the person reading may run a command, and who may take each step.
//
// Nothing here writes code. A view a generator reads is named in full, like
// projects::issue_page, and a route parameter by its name, like project.

#pragma once

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

#include "code/stream.hpp"
#include "generators/roles.hpp"
#include "language/ast.hpp"
#include "language/names.hpp"
#include "language/written.hpp"
#include "platform/files.hpp"

namespace one::generators::screens {

    inline std::string join(const std::string& ns, const std::string& name) {
        return language::qualified(ns, name);
    }

    // A readable label, the same way @uione/react makes one: created_at becomes
    // "Created at".
    inline std::string label(std::string_view name) {
        std::string out;
        for (char c : name) out += c == '_' ? ' ' : c;
        if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
        return out;
    }

    using language::text_of;

    // Whether a route has a parameter: /projects/:project has project.
    inline bool names_parameter(const std::string& route, const std::string& name) {
        std::string wanted = "/:" + name;
        for (std::size_t at = route.find(wanted); at != std::string::npos; at = route.find(wanted, at + 1)) {
            std::size_t end = at + wanted.size();
            if (end == route.size() || route[end] == '/') return true;
        }
        return false;
    }

    // An address's parts: /projects/:project gives projects and :project.
    inline std::vector<std::string> segments(const std::string& path) {
        std::vector<std::string> out;
        for (std::size_t at = 0; at < path.size();) {
            std::size_t end = path.find('/', at + 1);
            std::string part = path.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
            if (!part.empty()) out.push_back(part);
            at = end == std::string::npos ? path.size() : end;
        }
        return out;
    }

    // Whether an address can't be opened without saying something, like /books/:book;
    // /docs/:page? opens without.
    inline bool needs_parameter(const std::string& route) {
        for (std::size_t at = route.find("/:"); at != std::string::npos; at = route.find("/:", at + 1)) {
            std::size_t end = route.find('/', at + 1);
            if (route.substr(at, end == std::string::npos ? std::string::npos : end - at).back() != '?') return true;
        }
        return false;
    }

    // What a name written inside a namespace refers to, in full. A command written
    // as entity::action, or a view written by its name alone, is in the same
    // namespace; one written with its namespace stays as written.
    // The color a button is drawn as, a circle of it, in a toolbar: one with no icon
    // whose one given value is a color written #rrggbb, like White's { color = "#ffffff" }.
    inline std::string swatch_of(const language::button_item& button) {
        if (button.icon || button.given.size() != 1) return "";
        auto* literal = std::get_if<language::literal_expression>(&button.given.front().value->node);
        if (!literal || literal->type != language::literal_expression::kind::string) return "";
        const std::string& v = literal->value;
        bool hex = v.size() == 7 && v[0] == '#' && std::all_of(v.begin() + 1, v.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
        return hex ? v : "";
    }

    inline std::string full_command(const std::string& ns, const language::qualified_name& name) {
        return name.parts.size() == 2 ? join(ns, name.text()) : name.text();
    }

    inline std::string full_view(const std::string& ns, const std::string& name) {
        return name.find("::") == std::string::npos ? join(ns, name) : name;
    }

    // A piece of a title: plain text, or a live value, a view's field, like
    // {issue_page.title}.
    struct title_part {
        std::string text;
        std::string view;  // in full; empty for plain text
        std::string field;
        bool live() const { return !view.empty(); }
    };

    // A page above a screen, nearest last: where it is, and what it's called.
    struct crumb {
        std::string to;
        std::vector<title_part> title;
        // A page its address doesn't name, like an issue's board: each parameter that
        // address has and this one doesn't, read from a view this screen reads, by
        // its own part when it's keyed by several.
        struct filled {
            std::string parameter, view, field;
            int keys = 1;
        };
        std::vector<filled> fill;
        std::size_t depth = 0;  // how many parts its address has; 1000 and more when it's the page a screen is under
    };

    // Whether the person reading may run a command: the roles they hold within the
    // place the page's address names, read from `view`, a <member>_roles view.
    struct permission {
        std::string view;     // in full, like projects::member_roles
        std::string field;    // a row's field naming the place, like project
        std::string within;   // the route parameter naming it, like project
        // A project's own roles: `command` within the namespace, allowed by what a
        // role's record lists, `allows`, like role.may.
        std::string command;
        std::string allows;
        // Roles the project declares: those of them that grant it.
        std::vector<std::string> roles;
        bool by_record() const { return !allows.empty(); }
    };

    // Whose roles may take each step a button goes along: the step's list of roles,
    // the view of the person's roles, where they're held, and the role's field.
    struct step_holders {
        std::string held;    // the step's field listing roles, like roles
        std::string view;    // the person's roles, in full
        std::string within;  // the route parameter naming the place
        std::string place;   // a role row's field naming it
        std::string role;    // a role row's field naming the role, like role
    };

    // A column as it's shown.
    struct shown_column {
        std::string key;
        std::string label;
        std::optional<std::string> picture;
    };

    // A field's choices, the first the most urgent, which color what shows it.
    struct tint {
        std::string field;
        std::vector<std::string> order;
    };

    // A list's records as cards in a row: picking one keeps the rows whose field is it.
    struct track {
        std::string field;
        std::string view;  // in full
        std::string list;
        std::string shown;  // title or name
        std::vector<std::pair<std::string, std::string>> columns;  // field, label
    };

    // A list of an entity on the screen, which a form's field picks from: the view
    // (in full), the list, and what each is shown by. For people, the field that's
    // who they are, with `shown` their name.
    struct listing {
        std::string view, list, shown;
        std::optional<std::string> person;
        std::string within;  // the route parameter the view is read for
    };

    // A field a form asks for.
    struct form_field {
        std::string name;
        std::optional<std::string> label, hint;
        // text, email, date, number, slider (a number from one to another), markdown,
        // boolean, list, file, files, choice, pick (one of a listing) or choices
        // (several, from a listing or `options`).
        std::string type = "text";
        std::optional<language::field::number_range> range;  // a slider's, as written
        // Its choices: those it's given, each a value and how it's shown, or a listing's.
        std::optional<std::vector<std::pair<std::string, std::string>>> options;
        std::optional<listing> from;
        std::optional<std::string> start;  // a new one's choice
        bool required = false;             // a choice that's always one of them
        // Every listing looked at, in order, the one chosen last; a generator reading
        // views as it meets them reads these.
        std::vector<listing> looked_at;
    };

    // A screen as found, in its namespace.
    struct found_screen {
        std::string ns;
        const language::screen_declaration* screen;
        language::location where;
    };

    struct settings {
        std::string name = "app";
        bool has_project = false;  // a project block, which says whether people sign in at all
        std::string title;         // the name shown at the top of every page, when it isn't the project's
        std::string theme;         // the component set's theme
        bool display = false;      // display settings beside light and dark
        std::string appearance;    // light or dark; empty for the system's
        std::string layout = "single";
        std::string ui = "radix";
        std::vector<std::string> authentication;  // google, github, microsoft
        bool analytics = false;
        code::source project;      // the project block, or fixed without one
        std::string icon;          // an .svg file, when the project names one
        code::source icon_source;
        std::string served;        // a folder served as it is
        code::source served_source;
        std::vector<std::string> targets;
        bool local = false;        // storage local: its data kept on the machine its app runs on
    };

    class model {
    public:
        explicit model(const std::vector<language::file>& files) : held(roles_of(files).held) {
            for (const auto& f : files) place("", f.declarations);
            for (const auto& f : files) {
                indexing_ = f.path;
                index("", f.declarations);
            }
            for (const auto& f : files) find("", f.declarations, every_screen, &path_headers);
        }

        settings project;
        std::map<std::string, std::map<std::string, const language::entity_declaration*>> entities;
        std::map<std::string, std::map<std::string, const language::view_declaration*>> views;
        std::map<std::string, std::map<std::string, const language::define_workflow_declaration*>> workflows;  // by namespace and entity
        std::set<std::string> agent_stages;  // namespace::entity::stage of each stage an agent works
        std::set<std::string> failure_stages;  // namespace::entity::stage where an agent sends what it fails
        std::map<std::string, std::set<std::string>> commands;  // namespace to entity::command
        std::map<std::string, const language::command_declaration*> command_nodes;  // by full name
        std::set<std::string> open;  // commands anyone may run, signed in or not, in full
        std::vector<std::string> personal;  // views with one document per person
        std::vector<held_roles> held;
        std::map<std::string, std::string> prefixes;  // where a namespace's screens are, when it says
        std::vector<found_screen> every_screen;
        std::vector<std::pair<std::string, const language::header_declaration*>> path_headers;  // header /:project { … }, by namespace

        // A board's columns when its stages are written in the code, as a field's
        // choices, in their order: each stage and how it's shown; the steps between them
        // a person takes, those out of the stages no agent works, since an agent moves
        // on what it works by itself; and the stages that notify when a card arrives.
        struct written_stages {
            std::vector<std::pair<std::string, std::string>> stages;
            std::vector<std::pair<std::string, std::string>> steps;
            std::vector<std::string> notify;
            std::vector<std::pair<std::string, std::string>> statuses;  // done where its work is done, failed where failures go
        };
        // The field listing what a card waits on, when its workflow depends on one, like
        // waits_on; empty when it doesn't.
        std::string waits_on(const std::string& ns, const language::entity_declaration* card) const {
            if (!card) return "";
            auto scope = workflows.find(ns);
            if (scope == workflows.end() || !scope->second.contains(card->name)) return "";
            return scope->second.at(card->name)->depends_on;
        }

        written_stages stages_written(const std::string& ns, const language::entity_declaration& card, const std::string& by) const {
            written_stages out;
            const language::field* along = nullptr;
            for (const auto& f : card.fields) {
                if (f.name == by) along = &f;
            }
            if (!along) return out;
            for (std::size_t i = 0; i < along->choices.size(); ++i) {
                const std::string& choice = along->choices[i];
                out.stages.emplace_back(choice, i < along->choice_labels.size() && !along->choice_labels[i].empty() ? along->choice_labels[i] : label(choice));
            }
            auto scope = workflows.find(ns);
            auto found = scope == workflows.end() ? nullptr : scope->second.contains(card.name) ? scope->second.at(card.name) : nullptr;
            if (!found) return out;
            for (const auto& step : found->moves) {
                if (!agent_stages.contains(join(ns, card.name + "::" + step.from))) out.steps.emplace_back(step.from, step.to);
            }
            for (const auto& notice : found->notices) {
                if (std::ranges::contains(notice.ways, std::string("chime"))) out.notify.push_back(notice.stage);
            }
            for (const auto& [id, title] : out.stages) {
                if (id == found->done_at) out.statuses.emplace_back(id, "success");
                else if (failure_stages.contains(join(ns, card.name + "::" + id))) out.statuses.emplace_back(id, "failed");
            }
            return out;
        }

        // The addresses the top bar lists first, in order, from the header's navigation:
        // each namespace's screens there, by their address before any parameter, like
        // /docs for /docs/:page.
        std::vector<std::string> navigation(const std::vector<language::file>& files) const {
            std::vector<std::string> names;
            std::function<void(const std::vector<language::declaration>&)> header = [&](const std::vector<language::declaration>& declarations) {
                for (const auto& d : declarations) {
                    if (auto* h = std::get_if<language::header_declaration>(&d.node); h && !h->route) {
                        for (const auto& n : h->navigation) names.push_back(n.text());
                    }
                    if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) header(n->declarations);
                }
            };
            for (const auto& f : files) header(f.declarations);
            std::vector<std::string> out;
            for (const auto& name : names) {
                for (const auto& f : files) {
                    for (const auto& found : screens_in(f)) {
                        if (found.ns != name && !found.ns.starts_with(name + "::")) continue;
                        std::string route = full_route(found.ns, found.screen->route);
                        std::string base = route.substr(0, route.find("/:"));
                        std::string title = title_of(*found.screen);
                        if (base.empty() || title.find('{') != std::string::npos || title == site_title()) continue;
                        if (std::find(out.begin(), out.end(), base) == out.end()) out.push_back(base);
                    }
                }
            }
            return out;
        }

        // A file's screens, in order.
        std::vector<found_screen> screens_in(const language::file& f) const {
            std::vector<found_screen> out;
            find("", f.declarations, out, nullptr);
            return out;
        }

        // Where a namespace's screens are: under its name, like /docs, unless it says
        // otherwise with at, like namespace studio at /.
        std::string prefix_of(const std::string& ns) const {
            if (ns.empty()) return "";
            if (auto it = prefixes.find(ns); it != prefixes.end()) return it->second;
            std::size_t cut = ns.rfind("::");
            std::string outer = cut == std::string::npos ? "" : ns.substr(0, cut);
            return prefix_of(outer) + "/" + ns.substr(cut == std::string::npos ? 0 : cut + 2);
        }

        std::string full_route(const std::string& ns, const std::string& route) const {
            std::string prefix = prefix_of(ns);
            if (prefix.empty()) return route;
            return route == "/" ? prefix : prefix + route;
        }

        // A screen's title as it reads: its own, its name made readable, or the app's.
        std::string title_of(const language::screen_declaration& s) const {
            return s.title_is_name ? label(s.title) : s.title.empty() ? site_title() : s.title;
        }
        std::string site_title() const { return project.title.empty() ? project.name : project.title; }

        // The headers above a screen at `route`: those whose path it's under, in order.
        std::vector<std::pair<std::string, const language::header_declaration*>> headers_over(const std::string& route) const {
            std::vector<std::pair<std::string, const language::header_declaration*>> out;
            for (const auto& [header_ns, header] : path_headers) {
                std::string path = full_route(header_ns, *header->route);
                if (!route.starts_with(path) || (route.size() != path.size() && route[path.size()] != '/' && path != "/")) continue;
                out.emplace_back(header_ns, header);
            }
            return out;
        }

        // A title with {view.field} in it: the plain parts, and each value as the view
        // it's read from and its field. {name} with no dot stays as it's written.
        std::vector<title_part> title_parts(const std::string& ns, std::string_view text) const {
            std::vector<title_part> out;
            std::size_t at = 0;
            while (at < text.size()) {
                std::size_t open = text.find('{', at);
                std::size_t close = open == std::string_view::npos ? open : text.find('}', open);
                if (close == std::string_view::npos) {
                    out.push_back({std::string(text.substr(at)), "", ""});
                    break;
                }
                if (open > at) out.push_back({std::string(text.substr(at, open - at)), "", ""});
                std::string_view value = text.substr(open + 1, close - open - 1);
                std::size_t dot = value.find('.');  // a view's name has none; its value may, like stage.title
                if (dot == std::string_view::npos) {
                    out.push_back({std::string(text.substr(open, close - open + 1)), "", ""});
                } else {
                    out.push_back({"", full_view(ns, std::string(value.substr(0, dot))), std::string(value.substr(dot + 1))});
                }
                at = close + 1;
            }
            return out;
        }

        // A title as a trail says it: its parts when it shows live values, or as it's
        // written, even when that's nothing.
        std::vector<title_part> titled(const std::string& ns, const std::string& title) const {
            if (title.find('{') != std::string::npos) return title_parts(ns, title);
            std::vector<title_part> out;
            out.push_back({title, "", ""});
            return out;
        }

        // Where a form that deletes what a screen's address names goes once it's done:
        // the page the screen is under, when it says one that doesn't name it, else the
        // nearest page above its address, /:project for /:project/boards/:board, since
        // /:project/boards may be no page at all.
        std::string after_delete(const std::string& route, const std::string& under, const std::string& entity) const {
            if (!under.empty() && !names_parameter(under, entity)) return under;
            std::string wanted = "/:" + entity, above = route;
            for (std::size_t at = route.find(wanted); at != std::string::npos; at = route.find(wanted, at + 1)) {
                std::size_t end = at + wanted.size();
                if (end == route.size() || route[end] == '/') {
                    above = route.substr(0, at);
                    break;
                }
            }
            for (auto mine = segments(above); !mine.empty(); mine.pop_back()) {
                for (const auto& [their_ns, s, _] : every_screen) {
                    if (segments(full_route(their_ns, s->route)) == mine) return full_route(their_ns, s->route);
                }
            }
            return "/";
        }

        // The pages above a screen at `here`: those whose address its own goes on
        // from, like /projects/:project above /projects/:project/boards/:board, in the
        // order the project declares them; sort by depth for nearest last. The app's
        // home isn't one; its name is.
        std::vector<crumb> crumbs(const std::string& ns, const language::screen_declaration& screen, const std::string& here) const {
            // Under a page its address doesn't name, like an issue under its board: that
            // page's trail, then that page, called as this screen says.
            std::string route = screen.under ? full_route(ns, *screen.under) : here;
            auto mine = segments(route);
            std::vector<crumb> above;
            for (const auto& [their_ns, s, _] : every_screen) {
                std::string theirs_route = full_route(their_ns, s->route);
                auto theirs = segments(theirs_route);
                if (theirs.empty() || theirs.size() >= mine.size() || !std::equal(theirs.begin(), theirs.end(), mine.begin())) continue;
                std::string title = title_of(*s);
                above.push_back({theirs_route, titled(their_ns, title), {}, theirs.size()});
            }
            if (screen.under) {
                crumb under{route, titled(ns, screen.under_title), {},
                            above.size() + 1000};
                for (std::size_t at = route.find("/:"); at != std::string::npos; at = route.find("/:", at + 1)) {
                    std::size_t end = route.find('/', at + 1);
                    std::string name = route.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2);
                    if (names_parameter(here, name)) continue;
                    int keys = 1;
                    if (auto scope = entities.find(ns); scope != entities.end() && scope->second.contains(name)) {
                        keys = 0;
                        for (const auto& f : scope->second.at(name)->fields) keys += f.key ? 1 : 0;
                    }
                    if (auto scope = views.find(ns); scope != views.end()) {
                        for (const auto& [view_name, view] : scope->second) {
                            if (!view->per || !names_parameter(here, *view->per)) continue;
                            bool holds = std::any_of(view->values.begin(), view->values.end(), [&](const language::view_value& v) { return v.name && *v.name == name; });
                            if (!holds) continue;
                            under.fill.push_back({name, join(ns, view_name), name, std::max(keys, 1)});
                            break;
                        }
                    }
                }
                above.push_back(std::move(under));
            }
            return above;
        }

        // What a command is sent besides its entity's fields, like the address typed to
        // delete a project: never stored, so a form starts it empty.
        std::set<std::string> inputs_of(const std::string& command) const {
            std::set<std::string> out;
            auto it = command_nodes.find(command);
            if (it == command_nodes.end()) return out;
            for (const auto& st : it->second->body) {
                if (auto* input = std::get_if<language::input_statement>(&st.node)) out.insert(input->name);
            }
            return out;
        }

        // The view per entity that holds every field an update form asks for.
        std::optional<std::string> edit_view(const std::string& ns, const language::entity_declaration& entity, const language::form_item& form) const {
            auto scope = views.find(ns);
            if (scope == views.end()) return std::nullopt;
            auto inputs = inputs_of(join(ns, form.commands.front().text()));
            for (const auto& [name, view] : scope->second) {
                if (!view->per || *view->per != entity.name) continue;
                bool holds = std::all_of(form.fields.begin(), form.fields.end(), [&](const language::form_field& f) {
                    return inputs.contains(f.name) ||
                           std::any_of(view->values.begin(), view->values.end(), [&](const language::view_value& v) { return v.name && *v.name == f.name; });
                });
                if (holds) return join(ns, name);
            }
            return std::nullopt;
        }

        // The entity a view has one document per, like book, if it's a view per entity.
        std::optional<std::string> per_entity(const std::string& full) const {
            std::size_t cut = full.rfind("::");
            std::string ns = cut == std::string::npos ? "" : full.substr(0, cut);
            std::string name = cut == std::string::npos ? full : full.substr(cut + 2);
            auto scope = views.find(ns);
            if (scope == views.end()) return std::nullopt;
            auto view = scope->second.find(name);
            if (view == scope->second.end() || !view->second->per || *view->second->per == "user") return std::nullopt;
            return *view->second->per;
        }

        // The field each of a collage's parts is kept in, by part, as the checker found
        // them: what its runtimes are handed, so they read and send parts, never fields.
        std::map<std::string, std::string> collage_fields(const std::string& ns, const language::collage_item& c) const {
            const language::view_declaration* view = nullptr;
            if (auto scope = views.find(ns); scope != views.end()) {
                if (auto v = scope->second.find(c.view.text()); v != scope->second.end()) view = v->second;
            }
            const language::view_each* layers = nullptr;
            if (view) {
                for (const auto& each : view->each) {
                    if (each.name && *each.name == c.list) layers = &each;
                }
            }
            return language::collage_fields(c, layers);
        }

        // A view per an entity that holds one of its fields at its top, like poster_page's
        // background: where a button's given value is compared with what's stored, to
        // draw it pressed while they're the same.
        std::optional<std::string> view_holding(const std::string& ns, const std::string& entity, const std::string& field) const {
            auto scope = views.find(ns);
            if (scope == views.end()) return std::nullopt;
            for (const auto& [name, view] : scope->second) {
                if (!view->per || *view->per != entity) continue;
                for (const auto& v : view->values) {
                    std::string held = v.name ? *v.name : language::text_of(*v.value);
                    if (held == field) return join(ns, name);
                }
            }
            return std::nullopt;
        }

        // Whether a view lists everyone who has signed up, read a page at a time
        // rather than kept: each user, when the namespace has no entity of that name.
        bool people_view(const std::string& full) const {
            std::size_t cut = full.rfind("::");
            std::string ns = cut == std::string::npos ? "" : full.substr(0, cut);
            std::string name = cut == std::string::npos ? full : full.substr(cut + 2);
            auto scope = views.find(ns);
            if (scope == views.end()) return false;
            auto view = scope->second.find(name);
            if (view == scope->second.end() || entity_named(ns, "user")) return false;
            for (const auto& each : view->second->each) {
                auto* source = std::get_if<language::name_expression>(&each.source->node);
                if (source && source->name.text() == "user") return true;
            }
            return false;
        }

        bool entity_named(const std::string& ns, const std::string& name) const {
            auto scope = entities.find(ns);
            return scope != entities.end() && scope->second.contains(name);
        }

        // The key's parts before the last, when an address names an entity by them:
        // owner, in /:owner/:project, for a project keyed owner and slug. Empty when
        // the address names it by its whole id, or it has a key of one part.
        std::vector<std::string> keyed_by(const std::string& ns, const std::string& entity, const std::string& route) const {
            auto scope = entities.find(ns);
            if (scope == entities.end() || !scope->second.contains(entity)) return {};
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

        // The parameters of a link before its last that name something keyed by
        // several parts, like a board by its project and name, with how many: a row
        // holding its id fills the parameter with its last part.
        std::vector<std::pair<std::string, int>> named_by(const std::string& ns, const std::string& route) const {
            std::vector<std::pair<std::string, int>> named;
            std::size_t last = route.rfind("/:");
            for (std::size_t at = route.find("/:"); at != std::string::npos && at < last; at = route.find("/:", at + 1)) {
                std::size_t end = route.find('/', at + 1);
                std::string name = route.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2);
                auto scope = entities.find(ns);
                if (scope == entities.end() || !scope->second.contains(name)) continue;
                int keys = 0;
                for (const auto& f : scope->second.at(name)->fields) keys += f.key ? 1 : 0;
                if (keys > 1) named.emplace_back(name, keys);
            }
            return named;
        }

        const language::entity_declaration* entity_of_command(const std::string& full) const {
            auto cut = full.rfind("::");
            if (cut == std::string::npos) return nullptr;
            auto entity_end = cut;
            auto entity_start = full.rfind("::", entity_end - 1);
            std::string ns = entity_start == std::string::npos ? "" : full.substr(0, entity_start);
            std::string entity = full.substr(entity_start == std::string::npos ? 0 : entity_start + 2,
                                             entity_end - (entity_start == std::string::npos ? 0 : entity_start + 2));
            auto scope = entities.find(ns);
            if (scope == entities.end()) return nullptr;
            auto e = scope->second.find(entity);
            return e == scope->second.end() ? nullptr : e->second;
        }

        // The entity a view's list is of: book, for the shelf's each book.
        const language::entity_declaration* listed(const std::string& ns, const std::string& view, const std::optional<std::string>& list) const {
            auto scope = views.find(ns);
            if (scope == views.end()) return nullptr;
            auto v = scope->second.find(view);
            if (v == scope->second.end()) return nullptr;
            auto each = std::find_if(v->second->each.begin(), v->second->each.end(), [&](const auto& e) { return e.name == list; });
            if (each == v->second->each.end() || each->changes) return nullptr;  // a change has no commands, and no people of its own
            auto* source = std::get_if<language::name_expression>(&each->source->node);
            if (!source || source->name.parts.size() != 1) return nullptr;
            auto found = entities.find(ns);
            if (found == entities.end()) return nullptr;
            auto e = found->second.find(source->name.parts[0]);
            return e == found->second.end() ? nullptr : e->second;
        }

        // What a find box finds a list's rows by that they don't hold: searchable fields,
        // like a wiki page's body, which the server searches by their words.
        std::vector<std::string> searched_by(const std::string& ns, const std::string& view, const std::optional<std::string>& list,
                                             const std::vector<std::string>& by) const {
            std::vector<std::string> searched;
            const auto* entity = listed(ns, view, list);
            if (!entity) return searched;
            auto columns = listed_columns(ns, view, list);
            for (const auto& name : by) {
                if (std::find(columns.begin(), columns.end(), name) != columns.end()) continue;
                for (const auto& f : entity->fields) {
                    if (f.name == name && f.searchable) searched.push_back(name);
                }
            }
            return searched;
        }

        // The columns a view's list holds, as written, like issue.number.
        std::vector<std::string> listed_columns(const std::string& ns, const std::string& view, const std::optional<std::string>& list) const {
            std::vector<std::string> columns;
            auto scope = views.find(ns);
            if (scope == views.end()) return columns;
            auto v = scope->second.find(view);
            if (v == scope->second.end()) return columns;
            for (const auto& each : v->second->each) {
                if (each.name != list) continue;
                for (const auto& row : each.rows) columns.push_back(row.name ? *row.name : text_of(*row.value));
            }
            return columns;
        }

        // A table column naming one of the commands on its rows' entity, in full: an
        // action on every row.
        std::optional<std::string> row_command(const std::string& ns, const std::string& view, const std::optional<std::string>& list,
                                               const std::string& column) const {
            const language::entity_declaration* entity = listed(ns, view, list);
            if (!entity) return std::nullopt;
            std::string command = entity->name + "::" + column;
            auto found = commands.find(ns);
            if (found == commands.end() || !found->second.contains(command)) return std::nullopt;
            return join(ns, command);
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

        // The form a screen has for a command, written anywhere on it.
        static const language::form_item* form_for(const std::string& ns, const std::vector<language::screen_item>& items, const std::string& command) {
            for (const auto& item : items) {
                if (auto* form = std::get_if<language::form_item>(&item.node)) {
                    if (full_command(ns, form->commands.front()) == command) return form;
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    if (auto* found = form_for(ns, block->items, command)) return found;
                }
            }
            return nullptr;
        }

        static const language::button_item* button_for(const std::vector<language::screen_item>& items, const language::qualified_name& command) {
            for (const auto& item : items) {
                if (auto* button = std::get_if<language::button_item>(&item.node)) {
                    if (button->command.text() == command.text()) return button;
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    if (auto* found = button_for(block->items, command)) return found;
                }
            }
            return nullptr;
        }

        // Whether a command is a button on each row of a table on the screen, like
        // stage::update "Rename", or in a table's or board's toolbar, or a grid's cell:
        // its form then opens from there rather than being drawn on its own.
        bool on_rows(const std::string& ns, const std::vector<language::screen_item>& items, const std::string& command) const {
            for (const auto& item : items) {
                // A grid's cells open its entity's create and update.
                if (auto* grid = std::get_if<language::grid_item>(&item.node)) {
                    if (const language::entity_declaration* entry = listed(ns, grid->view.text(), grid->list)) {
                        if (command == join(ns, entry->name + "::create") || command == join(ns, entry->name + "::update")) return true;
                    }
                }
                // A command in a table's or a board's block is a button in its toolbar.
                std::vector<const language::table_column*> tools;
                if (auto* t = std::get_if<language::table_item>(&item.node)) {
                    for (const auto& column : t->columns) tools.push_back(&column);
                } else if (auto* b = std::get_if<language::board_item>(&item.node)) {
                    for (const auto& column : b->columns) tools.push_back(&column);
                }
                for (const auto* column : tools) {
                    std::string key = text_of(*column->value);
                    if (key.find("::") != std::string::npos && join(ns, key) == command) return true;
                }
                if (auto* table = std::get_if<language::table_item>(&item.node)) {
                    for (const auto& column : table->columns) {
                        std::string key = text_of(*column.value);
                        if (key.find('.') != std::string::npos) continue;
                        if (auto found = row_command(ns, table->view.text(), table->list, key); found && *found == command) return true;
                    }
                }
                if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    if (on_rows(ns, block->items, command)) return true;
                }
            }
            return false;
        }

        // Whether the person reading may run a command a role grants, on a page whose
        // address, `route`, names where the role is held, like the project in
        // /:project/issues/:issue. Nothing when no role grants it or the page doesn't
        // say where; then anyone the command's own rules let in may.
        std::optional<permission> allowed(const std::string& ns, const std::string& route, const std::string& command) const {
            for (const auto& h : held) {
                // A project's own roles allow what their records say now: any command of
                // theirs but one anyone may run.
                if (!h.allows.empty()) {
                    if (!command.starts_with(h.ns + "::") || h.open.contains(command)) continue;
                    if (!names_parameter(route, h.within) || !entity_named(ns, h.within)) continue;
                    return permission{h.view, h.field, h.within, command.substr(h.ns.size() + 2), h.allows, {}};
                }
                auto granting = h.granting.find(command);
                if (granting == h.granting.end()) continue;
                if (!names_parameter(route, h.within) || !entity_named(ns, h.within)) continue;
                return permission{h.view, h.field, h.within, "", "", granting->second};
            }
            return std::nullopt;
        }

        // Whose roles may take each step a button goes along, for each list of roles
        // the steps hold; the last is the one that counts.
        std::vector<step_holders> held_by(const std::string& ns, const std::string& route, const language::button_item& button) const {
            std::vector<step_holders> out;
            const language::view_declaration* view = nullptr;
            if (auto scope = views.find(ns); scope != views.end()) {
                if (auto it = scope->second.find(button.along->parts.back()); it != scope->second.end()) view = it->second;
            }
            for (std::size_t at = 0; view && at < view->each.size(); ++at) {
                const auto& each = view->each[at];
                if (!each.name || *each.name != button.along_list) continue;
                auto* source = std::get_if<language::name_expression>(&each.source->node);
                const language::entity_declaration* step = nullptr;
                if (auto scope = entities.find(ns); source && scope != entities.end()) {
                    if (auto it = scope->second.find(source->name.text()); it != scope->second.end()) step = it->second;
                }
                for (const auto& row : each.rows) {
                    std::string column = row.name ? *row.name : text_of(*row.value);
                    for (const auto& h : held) {
                        if (h.allows.empty() || h.ns != ns || !step || !names_parameter(route, h.within)) continue;
                        for (const auto& f : step->fields) {
                            if (f.name == column && f.list && f.type && f.type->text() == h.record) {
                                out.push_back({column, h.view, h.within, h.field, h.allows.substr(0, h.allows.find('.'))});
                            }
                        }
                    }
                }
            }
            return out;
        }

        // A column as it's shown: author, a person, is their name, with their picture
        // beside it when the rows hold it; and it's called as the screen says, or as its
        // field is everywhere, like number "#", or by its name.
        shown_column shown_as(const std::string& ns, const std::string& view, const std::optional<std::string>& list, const language::entity_declaration* entity,
                              const std::string& written, const std::optional<std::string>& said) const {
            shown_column out{written, "", std::nullopt};
            std::vector<std::string> columns = listed_columns(ns, view, list);
            auto has = [&](const std::string& name) { return std::find(columns.begin(), columns.end(), name) != columns.end(); };
            // A person is shown by their name even where the rows hold who they are too.
            bool person = false;
            for (const auto& f : entity ? entity->fields : nothing_) {
                if (f.name == written && f.type && f.type->text() == "user") person = true;
            }
            if ((!has(written) || person) && has(written + ".name")) {
                out.key = written + ".name";
                if (has(written + ".picture")) out.picture = written + ".picture";
            }
            std::string first = written.substr(0, written.find('.'));
            std::string rest = written.find('.') == std::string::npos ? "" : written.substr(written.find('.') + 1);
            std::optional<std::string> field_label;
            for (const auto& f : entity ? entity->fields : nothing_) {
                if (f.name == first && (rest.empty() || rest == "name" || rest == "title")) field_label = f.label;
            }
            std::string name = out.key.substr(out.key.rfind('.') == std::string::npos ? 0 : out.key.rfind('.') + 1);
            out.label = said ? *said : field_label ? *field_label : label(rest == "" && out.key != written ? written : name);
            return out;
        }

        // A list's records as cards in a row, with what each card shows.
        track track_of(const std::string& ns, const language::track_setting& setting) const {
            auto over = listed_columns(ns, setting.over.text(), setting.over_list);
            track out{setting.field, full_view(ns, setting.over.text()), setting.over_list,
                      std::find(over.begin(), over.end(), "title") != over.end() ? "title" : "name", {}};
            for (const auto& column : setting.columns) {
                std::string key = text_of(*column.value);
                std::string name = key.substr(key.rfind('.') == std::string::npos ? 0 : key.rfind('.') + 1);
                out.columns.emplace_back(key, column.label ? *column.label : label(name));
            }
            return out;
        }

        static tint tint_of(const std::string& name, const language::field& f) { return {name, f.choices}; }

        // A list's tint, from the field of its rows' entity that the block names.
        std::optional<tint> tint_on(const std::string& ns, const language::qualified_name& view, const std::optional<std::string>& list,
                                    const std::optional<std::string>& field) const {
            if (!field) return std::nullopt;
            const language::entity_declaration* rows = listed(ns, view.text(), list);
            for (const auto& f : rows ? rows->fields : nothing_) {
                if (f.name == *field) return tint_of(*field, f);
            }
            return std::nullopt;
        }

        // A choice field's choices, each with how it's shown: mit as MIT.
        static std::vector<std::pair<std::string, std::string>> choice_options(const language::field& f) {
            std::vector<std::pair<std::string, std::string>> out;
            for (std::size_t i = 0; i < f.choices.size(); ++i) {
                out.emplace_back(f.choices[i], i < f.choice_labels.size() && !f.choice_labels[i].empty() ? f.choice_labels[i] : label(f.choices[i]));
            }
            return out;
        }

        // A list of an entity, like a project's roles, in a view per something the
        // screen's address names, like project_page. People come from a list whose rows
        // hold one, with their name, like a project's members, each offered once; not
        // from a list of what the form makes, `asking`.
        std::optional<listing> listed_on_screen(const std::string& ns, const std::string& route, const std::string& entity, const std::string& asking = "") const {
            auto scope = views.find(ns);
            if (scope == views.end()) return entity == "user" ? std::nullopt : own_list_of(ns, entity);
            if (entity == "user") {
                for (const auto& [name, view] : scope->second) {
                    if (!view->per || !names_parameter(route, *view->per)) continue;
                    for (const auto& each : view->each) {
                        auto* source = std::get_if<language::name_expression>(&each.source->node);
                        if (!each.name || each.changes || !source || source->name.text() == asking) continue;
                        auto found = entities.find(ns);
                        if (found == entities.end() || !found->second.contains(source->name.text())) continue;
                        std::vector<std::string> columns;
                        for (const auto& row : each.rows) columns.push_back(row.name ? *row.name : text_of(*row.value));
                        for (const auto& f : found->second.at(source->name.text())->fields) {
                            if (f.list || !f.type || f.type->text() != "user") continue;
                            bool both = std::find(columns.begin(), columns.end(), f.name) != columns.end() &&
                                        std::find(columns.begin(), columns.end(), f.name + ".name") != columns.end();
                            if (!both) continue;
                            return listing{join(ns, name), *each.name, f.name + ".name", f.name, *view->per};
                        }
                    }
                }
                return std::nullopt;
            }
            for (const auto& [name, view] : scope->second) {
                if (!view->per || !names_parameter(route, *view->per)) continue;
                for (const auto& each : view->each) {
                    auto* source = std::get_if<language::name_expression>(&each.source->node);
                    if (!each.name || each.changes || !source || source->name.text() != entity) continue;
                    std::string shown;
                    for (const auto& row : each.rows) {
                        std::string column = row.name ? *row.name : text_of(*row.value);
                        if (column == "title" || (column == "name" && shown.empty())) shown = column;
                    }
                    if (shown.empty()) continue;
                    return listing{join(ns, name), *each.name, shown, std::nullopt, *view->per};
                }
            }
            return own_list_of(ns, entity);
        }

        // A person's own list of rows pointing at an entity, in any namespace, like the
        // organizations they're in: a view per user whose rows hold a field of that
        // entity and its name or title, offered by it. It's read as theirs, by no
        // address.
        std::optional<listing> own_list_of(const std::string& ns, const std::string& entity) const {
            const std::string wanted = entity.find("::") == std::string::npos ? join(ns, entity) : entity;
            for (const auto& [view_ns, named] : views) {
                auto kinds = entities.find(view_ns);
                if (kinds == entities.end()) continue;
                for (const auto& [name, view] : named) {
                    if (!view->per || *view->per != "user") continue;
                    for (const auto& each : view->each) {
                        auto* source = std::get_if<language::name_expression>(&each.source->node);
                        if (each.changes || !source || !kinds->second.contains(source->name.text())) continue;
                        std::vector<std::string> columns;
                        for (const auto& row : each.rows) columns.push_back(row.name ? *row.name : text_of(*row.value));
                        auto has = [&](const std::string& column) { return std::find(columns.begin(), columns.end(), column) != columns.end(); };
                        for (const auto& f : kinds->second.at(source->name.text())->fields) {
                            if (f.list || !f.type) continue;
                            std::string type = f.type->text();
                            if ((type.find("::") == std::string::npos ? join(view_ns, type) : type) != wanted || !has(f.name)) continue;
                            for (const char* label : {"name", "title"}) {
                                if (has(f.name + "." + label)) return listing{join(view_ns, name), each.name ? *each.name : "rows", f.name + "." + label, f.name, ""};
                            }
                        }
                    }
                }
            }
            return std::nullopt;
        }

        // What a form asks for, on a screen at `route`: each field with its kind and
        // its choices.
        std::vector<form_field> form_fields(const std::string& ns, const std::string& route, const language::form_item& form) const {
            std::string command = full_command(ns, form.commands.front());
            const language::entity_declaration* entity = entity_of_command(command);
            std::vector<form_field> out;
            for (const auto& f : form.fields) {
                form_field field;
                field.name = f.name;
                field.hint = f.hint;
                // Its choices said again replace any said before.
                auto unchosen = [&] {
                    field.options.reset();
                    field.from.reset();
                    field.start.reset();
                    field.required = false;
                };
                auto pick_from = [&](std::optional<listing> found, const std::string& type) {
                    if (!found) return;
                    field.looked_at.push_back(*found);
                    unchosen();
                    field.type = type;
                    field.from = std::move(found);
                };
                for (const auto& declared : entity ? entity->fields : nothing_) {
                    if (declared.name == f.name && declared.type && declared.type->parts.size() == 1) {
                        const auto& t = declared.type->parts[0];
                        if (t == "email" || t == "date" || t == "number" || t == "markdown" || t == "boolean") field.type = t;
                        // A number from one to another is slid along, starting where it starts.
                        if (t == "number" && declared.range && !declared.list) {
                            field.type = "slider";
                            field.range = declared.range;
                            field.start = declared.range->from;
                            auto* n = declared.initial ? std::get_if<language::literal_expression>(&declared.initial->node) : nullptr;
                            if (n && n->type == language::literal_expression::kind::number) field.start = n->value;
                        }
                        if (declared.list) field.type = "list";
                        // A file is chosen and uploaded, or several.
                        if (t == "file") field.type = declared.list ? "files" : "file";
                    }
                    for (const auto& h : held) {
                        if (declared.name != f.name || !declared.type || h.allows.empty() || h.ns != ns) continue;
                        // What a role allows: a box to tick for each command there is.
                        if (declared.list && declared.type->text() == "permission") {
                            unchosen();
                            field.type = "choices";
                            field.options.emplace();
                            if (auto c = commands.find(ns); c != commands.end()) {
                                for (const auto& name : c->second) {
                                    if (h.open.contains(join(ns, name))) continue;
                                    std::string action = name.substr(name.rfind("::") + 2), on = name.substr(0, name.rfind("::"));
                                    std::replace(on.begin(), on.end(), '_', ' ');
                                    field.options->emplace_back(name, label(action) + " " + on);
                                }
                            }
                        }
                    }
                    // Another entity, like a person's role or a step's stages: one of
                    // those a view on the screen lists, or several for a list.
                    if (declared.name == f.name && declared.type && !field.options && !field.from) {
                        pick_from(listed_on_screen(ns, route, declared.type->text(), entity->name), declared.list ? "choices" : "pick");
                    }
                    // A choice is picked from its choices, not typed.
                    if (declared.name == f.name && !declared.choices.empty()) {
                        unchosen();
                        field.type = "choice";
                        field.options = choice_options(declared);
                        // A new one starts on the field's own starting choice.
                        if (auto* start = declared.initial ? std::get_if<language::name_expression>(&declared.initial->node) : nullptr) {
                            field.start = start->name.parts.back();
                        }
                        // A required choice is always one of them, so there's no empty one.
                        field.required = declared.required;
                    }
                }
                // What the command is sent besides its entity's fields, like the stage
                // a removed stage's issues move to: picked like a field of its type.
                if (auto c = command_nodes.find(command); c != command_nodes.end()) {
                    for (const auto& st : c->second->body) {
                        auto* input = std::get_if<language::input_statement>(&st.node);
                        if (!input || input->name != f.name) continue;
                        const std::string t = input->type.text();
                        if (t == "number" || t == "date" || t == "boolean" || t == "email") {
                            field.type = t;
                        } else {
                            pick_from(listed_on_screen(ns, route, t), "pick");
                        }
                    }
                }
                // Called as the form says, or as its field is everywhere.
                field.label = f.label;
                for (const auto& declared : entity ? entity->fields : nothing_) {
                    if (!field.label && declared.name == f.name && declared.label) field.label = declared.label;
                }
                out.push_back(std::move(field));
            }
            return out;
        }

    private:
        std::string indexing_;  // the file being indexed
        const std::vector<language::field> nothing_;  // the fields of no entity

        // Where each namespace is, read first, since one of its files says it for all.
        void place(const std::string& ns, const std::vector<language::declaration>& declarations) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    if (n->at) prefixes[join(ns, n->name)] = *n->at == "/" ? "" : *n->at;
                    place(join(ns, n->name), n->declarations);
                }
            }
        }

        void index(const std::string& ns, const std::vector<language::declaration>& declarations) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    index(join(ns, n->name), n->declarations);
                } else if (auto* p = std::get_if<language::project_declaration>(&d.node)) {
                    project.name = p->name;
                    project.has_project = true;
                    project.project = {indexing_, d.where.line};
                    for (const auto& s : p->settings) {
                        if (s.key == "ui") project.ui = s.value;
                        if (s.key == "title") project.title = s.value;
                        if (s.key == "theme") project.theme = s.value;
                        if (s.key == "appearance" && s.value != "system") project.appearance = s.value;
                        if (s.key == "layout") project.layout = s.value;
                        if (s.key == "signin") project.authentication.push_back(s.value);
                        if (s.key == "analytics") project.analytics = s.value == "google";
                        if (s.key == "accessibility") project.display = s.value == "menu";
                        if (s.key == "target") project.targets.push_back(s.value);
                        if (s.key == "storage") project.local = s.value == "local";
                        if (s.key == "serve") {
                            std::string dir = std::filesystem::path(indexing_).parent_path().string();
                            project.served = platform::resolve(dir.empty() ? "." : dir, s.value);
                            project.served_source = {indexing_, s.where.line};
                        }
                        if (s.key == "icon") {
                            std::string dir = std::filesystem::path(indexing_).parent_path().string();
                            project.icon = platform::resolve(dir.empty() ? "." : dir, s.value);
                            project.icon_source = {indexing_, s.where.line};
                        }
                    }
                } else if (auto* w = std::get_if<language::define_workflow_declaration>(&d.node)) {
                    workflows[ns][w->entity] = w;
                } else if (auto* a = std::get_if<language::define_agent_declaration>(&d.node)) {
                    for (const auto& rule : a->rules) {
                        agent_stages.insert(join(ns, rule.entity + "::" + rule.stage));
                        if (!rule.otherwise.empty()) failure_stages.insert(join(ns, rule.entity + "::" + rule.otherwise));
                    }
                } else if (auto* e = std::get_if<language::entity_declaration>(&d.node)) {
                    entities[ns][e->name] = e;
                } else if (auto* v = std::get_if<language::view_declaration>(&d.node)) {
                    views[ns][v->name] = v;
                    if (v->per == "user") personal.push_back(join(ns, v->name));
                } else if (auto* c = std::get_if<language::command_declaration>(&d.node)) {
                    commands[ns].insert(c->name.text());
                    command_nodes[join(ns, c->name.text())] = c;
                    for (const auto& s : c->body) {
                        auto* p = std::get_if<language::permission_statement>(&s.node);
                        if (p && p->permission.text() == "anyone") open.insert(join(ns, c->name.text()));
                    }
                }
            }
        }

        // A file's screens, and the headers over paths when they're wanted.
        static void find(const std::string& ns, const std::vector<language::declaration>& declarations, std::vector<found_screen>& found,
                         std::vector<std::pair<std::string, const language::header_declaration*>>* headers) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    find(join(ns, n->name), n->declarations, found, headers);
                } else if (auto* s = std::get_if<language::screen_declaration>(&d.node)) {
                    found.push_back({ns, s, d.where});
                } else if (auto* h = std::get_if<language::header_declaration>(&d.node); h && h->route && headers) {
                    headers->emplace_back(ns, h);
                }
            }
        }
    };

}  // namespace one::generators::screens
