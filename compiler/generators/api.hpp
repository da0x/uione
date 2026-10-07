// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes the Go backend for a project: one package per namespace, holding its
// entities, commands, views and roles as a few declarations on the one library,
// and a main.go that serves them all.
//
// The library doesn't do everything the language can say yet. Anything it can't do
// stops the build with "not supported yet" and the line it's on, because Go that
// compiles but does the wrong thing is worse than no Go at all.

#pragma once

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "code/stream.hpp"
#include "generators/web.hpp"
#include "language/ast.hpp"
#include "language/diagnostics.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace one::generators {

    namespace api_detail {

        // A Go name from a snake_case one: created_at becomes CreatedAt, and the usual
        // initialisms stay capitals, so owner_id becomes OwnerID.
        inline std::string go_name(std::string_view snake) {
            static const std::set<std::string, std::less<>> initialisms{"id", "uid", "url", "api", "http", "json", "html"};
            std::string out;
            std::size_t at = 0;
            while (at <= snake.size()) {
                std::size_t end = snake.find('_', at);
                std::string_view word = snake.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
                if (initialisms.contains(word)) {
                    for (char c : word) out += static_cast<char>(c - 'a' + 'A');
                } else if (!word.empty()) {
                    out += static_cast<char>(word[0] >= 'a' && word[0] <= 'z' ? word[0] - 'a' + 'A' : word[0]);
                    out += word.substr(1);
                }
                if (end == std::string_view::npos) break;
                at = end + 1;
            }
            return out;
        }

        inline std::string go_string(std::string_view text) { return web_detail::js_string(text); }

        // Pads text to a width, as gofmt aligns struct fields and constants.
        inline std::string pad(const std::string& text, std::size_t width) {
            return text + std::string(width > text.size() ? width - text.size() : 0, ' ');
        }

    } // namespace api_detail

    struct generated_api {
        std::vector<output_file> files;  // inside the api folder, like waitlist/waitlist.go
        language::diagnostics errors;        // what isn't supported yet; nothing is written if any
    };

    class api_generator {
    public:
        api_generator(const std::vector<language::file>& files, std::string project_dir, std::string out_dir)
            : files_(files), project_dir_(std::move(project_dir)), out_dir_(std::move(out_dir)) {}

        generated_api generate() {
            for (const auto& f : files_) {
                collecting_ = f.path;
                collect(f, "", f.declarations);
            }
            generated_api out;
            std::vector<std::string> packages;
            for (auto& [ns, pkg] : namespaces_) {
                if (ns.empty()) {
                    if (!pkg.entities.empty() || !pkg.commands.empty() || !pkg.views.empty()) {
                        unsupported(pkg.path, pkg.where, "entities, commands and views outside a namespace");
                    }
                    continue;
                }
                if (ns.find("::") != std::string::npos) {
                    unsupported(pkg.path, pkg.where, "a namespace inside another namespace");
                    continue;
                }
                if (pkg.entities.empty() && pkg.commands.empty() && pkg.views.empty()) continue;
                std::string name = package_name(ns);
                out.files.push_back(file(name + "/" + name + ".go", package_file(ns, pkg)));
                // Go written by hand, built into the same package as it is.
                for (const auto& [backend, path] : pkg.backends) {
                    output_file f{name + "/" + backend + ".go", platform::read_file(path).value_or(""), false, {}};
                    f.sources.assign(static_cast<std::size_t>(std::count(f.content.begin(), f.content.end(), '\n')), code::source{path, 1});
                    out.files.push_back(std::move(f));
                }
                packages.push_back(name);
            }
            out.files.push_back(file("main.go", main_go(packages)));
            out.files.push_back(file("go.mod", go_mod()));
            out.errors = std::move(errors_);
            if (!out.errors.empty()) out.files.clear();
            return out;
        }

    private:
        using stream = code::stream;

        struct package {
            std::string path;  // the first file that declares the namespace
            language::location where;
            code::source source;  // where the namespace is first declared
            std::vector<const language::entity_declaration*> entities;
            std::vector<std::pair<const language::command_declaration*, language::location>> commands;
            std::vector<std::pair<const language::view_declaration*, language::location>> views;
            std::vector<const language::role_declaration*> roles;
            std::vector<const language::format_declaration*> formats;
            std::vector<std::pair<const language::function_declaration*, language::location>> functions;
            std::vector<std::pair<const language::webhook_declaration*, language::location>> hooks;
            std::vector<std::pair<std::string, std::string>> backends;  // each name, and the file it's written in
        };

        const std::vector<language::file>& files_;
        std::string project_dir_;
        std::string out_dir_;
        std::map<std::string, package> namespaces_;
        language::diagnostics errors_;
        std::string path_;  // the file being written, for errors
        const package* pkg_ = nullptr;  // the package being written
        // Where each declaration is, so every generated line can say what it came from.
        std::map<const void*, code::source> declared_;
        code::source project_;  // the project block, or fixed without one
        std::string collecting_;  // the file being collected
        std::map<std::string, std::vector<std::string>> form_fields_;  // by command, like projects::issue::update

        code::source at(const void* declaration) const {
            auto it = declared_.find(declaration);
            return it == declared_.end() ? code::source{} : it->second;
        }

        // A guard that has what's written next come from line `line` of the file
        // `declaration` is in.
        static auto in(stream& out, const code::source& file, int line) { return out.from(file.path, line); }
        // The reference fields a command's body reads through, like a loan's book,
        // in the order it first does, with the entity each points at.
        std::vector<std::pair<std::string, const language::entity_declaration*>> pointed_;
        // The entity the view being written has a document per, if it has one.
        std::string subject_;
        std::string project_name_ = "app";
        std::string domain_;

        // One error per line is enough: a line the library can't do usually fails in
        // more than one place, and the first says what's wrong.
        void unsupported(const std::string& path, language::location where, const std::string& what) {
            for (const auto& e : errors_) {
                if (e.path == path && e.where.line == where.line) return;
            }
            errors_.push_back({path, where, "not supported yet: " + what});
        }

        void collect(const language::file& f, const std::string& ns, const std::vector<language::declaration>& declarations) {
            for (const auto& d : declarations) {
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                    std::string inner = web_detail::join(ns, n->name);
                    auto& pkg = namespaces_[inner];
                    if (pkg.path.empty()) {
                        pkg.path = std::filesystem::path(f.path).filename().string();
                        pkg.where = d.where;
                        pkg.source = code::source{f.path, d.where.line};
                    }
                    collect(f, inner, n->declarations);
                    continue;
                }
                auto& pkg = namespaces_[ns];
                if (pkg.path.empty()) pkg.path = std::filesystem::path(f.path).filename().string();
                std::visit([&](const auto& node) { declared_[&node] = code::source{collecting_, d.where.line}; }, d.node);
                if (auto* p = std::get_if<language::project_declaration>(&d.node)) {
                    project_name_ = p->name;
                    project_ = {collecting_, d.where.line};
                    for (const auto& s : p->settings) {
                        if (s.key == "domain") domain_ = s.value;
                    }
                } else if (auto* e = std::get_if<language::entity_declaration>(&d.node)) {
                    pkg.entities.push_back(e);
                } else if (auto* c = std::get_if<language::command_declaration>(&d.node)) {
                    pkg.commands.emplace_back(c, d.where);
                } else if (auto* v = std::get_if<language::view_declaration>(&d.node)) {
                    pkg.views.emplace_back(v, d.where);
                } else if (auto* r = std::get_if<language::role_declaration>(&d.node)) {
                    pkg.roles.push_back(r);
                } else if (auto* fmt = std::get_if<language::format_declaration>(&d.node)) {
                    pkg.formats.push_back(fmt);
                } else if (auto* function = std::get_if<language::function_declaration>(&d.node)) {
                    pkg.functions.emplace_back(function, d.where);
                } else if (auto* hook = std::get_if<language::webhook_declaration>(&d.node)) {
                    pkg.hooks.emplace_back(hook, d.where);
                } else if (auto* backend = std::get_if<language::backend_declaration>(&d.node)) {
                    pkg.backends.emplace_back(backend->name, language::backend_file(f.path, backend->name));
                } else if (auto* screen = std::get_if<language::screen_declaration>(&d.node)) {
                    collect_forms(ns, screen->items);
                }
            }
        }

        // The fields each command's forms ask for, wherever they are, by the command's
        // full name: what an update may change.
        void collect_forms(const std::string& ns, const std::vector<language::screen_item>& items) {
            for (const auto& item : items) {
                if (auto* form = std::get_if<language::form_item>(&item.node)) {
                    for (const auto& command : form->commands) {
                        std::string full = command.parts.size() > 2 ? command.text() : web_detail::join(ns, command.text());
                        auto& fields = form_fields_[full];
                        for (const auto& field : form->fields) {
                            if (std::find(fields.begin(), fields.end(), field.name) == fields.end()) fields.push_back(field.name);
                        }
                    }
                } else if (auto* block = std::get_if<language::content_block>(&item.node)) {
                    collect_forms(ns, block->items);
                }
            }
        }

        static std::string package_name(const std::string& ns) {
            std::string name;
            for (char c : ns) {
                if (c != '_') name += c;
            }
            return name;
        }

        static const language::entity_declaration* entity(const package& pkg, std::string_view name) {
            for (auto* e : pkg.entities) {
                if (e->name == name) return e;
            }
            return nullptr;
        }

        // A choice's constant: StatusOpen for open in status. When two entities in a
        // package have a field of the same name sharing a choice, like an issue's and
        // a report's status, each field's constants are named for its entity too:
        // IssueStatusOpen and ReportStatusOpen.
        std::string choice(const language::field& f, const std::string& value) const {
            const language::entity_declaration* owner = nullptr;
            bool shared = false;
            for (auto* e : pkg_ ? pkg_->entities : std::vector<const language::entity_declaration*>{}) {
                for (const auto& other : e->fields) {
                    if (&other == &f) {
                        owner = e;
                    } else if (other.name == f.name) {
                        for (const auto& c : f.choices) {
                            if (std::find(other.choices.begin(), other.choices.end(), c) != other.choices.end()) shared = true;
                        }
                    }
                }
            }
            std::string name = api_detail::go_name(f.name) + api_detail::go_name(value);
            return shared && owner ? api_detail::go_name(owner->name) + name : name;
        }

        static const language::function_declaration* function(const package& pkg, std::string_view name) {
            for (const auto& [function, _] : pkg.functions) {
                if (function->name == name) return function;
            }
            return nullptr;
        }

        static const language::field* field(const language::entity_declaration& e, std::string_view name) {
            for (const auto& f : e.fields) {
                if (f.name == name) return &f;
            }
            return nullptr;
        }

        // the files around the packages

        std::string module_path() const {
            return (domain_.empty() ? project_name_ + ".local" : domain_) + "/api";
        }

        stream main_go(const std::vector<std::string>& packages) const {
            stream out("\t");
            auto from = out.from(project_.path, project_.line);
            out.generated_from(source_name());
            out.line("package main");
            out.line();
            out.open("import (");
            out.line("\"github.com/da0x/uione/one\"");
            out.line();
            for (const auto& p : packages) out.line(api_detail::go_string(module_path() + "/" + p));
            out.close(")");
            out.line();
            std::string modules;
            for (const auto& p : packages) modules += (modules.empty() ? "" : ", ") + p + ".Module";
            out.open("func main() {");
            out.line("one.Serve(" + modules + ")");
            out.close("}");
            return out;
        }

        // The library comes from this repository when the project is inside it, and
        // otherwise from its published module.
        stream go_mod() const {
            namespace fs = std::filesystem;
            stream out("\t");
            auto from = out.from(project_.path, project_.line);
            out.line("module " + module_path());
            out.line();
            out.line("go 1.26");
            out.line();
            out.line("require github.com/da0x/uione/one v" + std::string(version));
            for (fs::path dir = fs::weakly_canonical(project_dir_); !dir.empty(); dir = dir.parent_path()) {
                if (fs::exists(dir / "one" / "go.mod")) {
                    out.line();
                    out.line("replace github.com/da0x/uione/one => " + platform::relative_import(out_dir_, (dir / "one").string()));
                    break;
                }
                if (dir == dir.parent_path()) break;
            }
            return out;
        }

        std::string source_name() const {
            std::string folder = std::filesystem::path(project_dir_).lexically_normal().filename().string();
            if (folder.empty()) folder = std::filesystem::path(project_dir_).lexically_normal().parent_path().filename().string();
            return folder + "/";
        }

        // one package

        struct go_type {
            std::string name;
            bool uses_time = false;
        };

        std::optional<go_type> type_of(const package& pkg, const language::field& f) {
            if (!f.choices.empty()) return go_type{"string"};
            if (f.list) return go_type{"[]string"};  // each one's id, or a piece of text
            if (!f.type) {
                unsupported(path_, f.where, "a field without a type, like " + f.name);
                return std::nullopt;
            }
            if (f.type->parts.size() != 1) {
                unsupported(path_, f.where, "a field whose type is in another namespace");
                return std::nullopt;
            }
            const auto& t = f.type->parts[0];
            if (t == "text" || t == "markdown" || t == "email" || t == "slug" || t == "user") return go_type{"string"};
            if (t == "date") return go_type{"time.Time", true};
            if (t == "number" || t == "serial") return go_type{"float64"};
            if (t == "boolean") return go_type{"bool"};
            for (auto* fmt : pkg.formats) {
                if (fmt->name == t) return go_type{"string"};
            }
            if (entity(pkg, t)) return go_type{"string"};  // a reference holds the other entity's id
            unsupported(path_, f.where, "the type " + t);
            return std::nullopt;
        }

        std::optional<std::string> initial_value(const language::field& f) {
            if (!f.initial) return std::string{};
            auto* name = std::get_if<language::name_expression>(&f.initial->node);
            if (name) {
                if (auto c = language::choice_of(name->name, f)) return "default=" + *c;  // status::open
            }
            if (name && name->name.parts.size() == 1) return "default=" + name->name.parts[0];
            if (web_detail::text_of(*f.initial) == "me.username") return std::string("default=me.username");
            unsupported(path_, f.initial->where, "a starting value that's worked out, rather than me, now, true, false or a choice");
            return std::nullopt;
        }

        stream package_file(const std::string& ns, const package& pkg) {
            path_ = pkg.path;
            pkg_ = &pkg;
            bool uses_time = false;
            stream body("\t");

            for (auto* e : pkg.entities) {
                auto from_entity = in(body, at(e), at(e).line);
                path_ = at(e).path.empty() ? pkg.path : at(e).path;
                struct line {
                    std::string name, type, tag;
                    int from;  // the field's line in the .one file
                };
                std::vector<line> lines;
                std::vector<std::tuple<std::string, std::string, int>> constants;
                for (const auto& f : e->fields) {
                    auto type = type_of(pkg, f);
                    if (!type) continue;
                    uses_time |= type->uses_time;
                    std::vector<std::string> rules;
                    if (f.required) rules.push_back("required");
                    if (f.key) rules.push_back("key");
                    if (f.unique) rules.push_back("unique");
                    if (f.type && f.type->text() == "serial") rules.push_back(f.per ? "serial=" + *f.per : "serial");
                    if (f.type && f.type->text() == "email") rules.push_back("email");
                    if (f.type && f.type->text() == "slug") rules.push_back("slug");
                    if (f.after) rules.push_back("after=" + *f.after);
                    if (f.type && f.type->parts.size() == 1 && entity(pkg, f.type->parts[0])) {
                        rules.push_back("refers=" + web_detail::join(ns, f.type->parts[0]));
                    } else if (f.type && f.type->text() == "user") {
                        rules.push_back("refers=user");  // a person, whose name and picture a view can show
                    }
                    if (!f.choices.empty()) {
                        std::string choices;
                        for (const auto& c : f.choices) {
                            choices += (choices.empty() ? "" : "|") + c;
                            constants.emplace_back(choice(f, c), c, f.where.line);
                        }
                        rules.push_back("choices=" + choices);
                    }
                    auto initial = initial_value(f);
                    if (!initial) continue;
                    if (!initial->empty()) rules.push_back(*initial);
                    std::string tag = "`firestore:" + api_detail::go_string(f.name);
                    if (!rules.empty()) {
                        std::string joined;
                        for (const auto& r : rules) joined += (joined.empty() ? "" : ",") + r;
                        tag += " one:" + api_detail::go_string(joined);
                    }
                    lines.push_back({api_detail::go_name(f.name), type->name, tag + "`", f.where.line});
                }
                std::size_t name_width = 0, type_width = 0;
                for (const auto& l : lines) {
                    name_width = std::max(name_width, l.name.size());
                    type_width = std::max(type_width, l.type.size());
                }
                body.open("type " + api_detail::go_name(e->name) + " struct {");
                body.line("one.Record");
                if (e->history) body.line("one.History");
                for (const auto& l : lines) {
                    auto from_field = in(body, at(e), l.from);
                    body.line(api_detail::pad(l.name, name_width) + " " + api_detail::pad(l.type, type_width) + " " + l.tag);
                }
                body.close("}");
                body.line();
                if (!constants.empty()) {
                    std::size_t width = 0;
                    for (const auto& [name, _, __] : constants) width = std::max(width, name.size());
                    body.open("const (");
                    for (const auto& [name, value, from] : constants) {
                        auto from_choice = in(body, at(e), from);
                        body.line(api_detail::pad(name, width) + " = " + api_detail::go_string(value));
                    }
                    body.close(")");
                    body.line();
                }
            }

            // commands, named after their action, or with their entity when two share one
            std::map<std::string, int> actions;
            for (const auto& [c, _] : pkg.commands) {
                if (c->name.parts.size() == 2) ++actions[c->name.parts[1]];
            }
            std::vector<std::string> members;
            for (const auto& [c, where] : pkg.commands) {
                if (c->name.parts.size() != 2) continue;
                const auto* e = entity(pkg, c->name.parts[0]);
                if (!e) continue;
                std::string var = api_detail::go_name(c->name.parts[1]);
                if (actions[c->name.parts[1]] > 1) var = api_detail::go_name(e->name) + var;
                members.push_back(var);
                auto from_command = in(body, at(c), where.line);
                path_ = at(c).path.empty() ? pkg.path : at(c).path;  // what's unsupported is said where it's written
                command(body, ns, *e, *c, var, uses_time);
                body.line();
            }

            for (const auto& [v, where] : pkg.views) {
                std::string var = api_detail::go_name(v->name);
                if (std::find(members.begin(), members.end(), var) != members.end()) var += "View";
                auto from_view = in(body, at(v), where.line);
                path_ = at(v).path.empty() ? pkg.path : at(v).path;  // what's unsupported is said where it's written
                if (view(body, pkg, *v, where, var)) members.push_back(var);
                body.line();
            }

            functions(body, pkg);
            for (const auto& [hook, where] : pkg.hooks) {
                auto from_hook = in(body, at(hook), where.line);
                if (webhook(body, pkg, *hook)) members.push_back("Webhook" + api_detail::go_name(hook->provider));
                body.line();
            }

            // the module
            std::string args = api_detail::go_string(ns);
            for (const auto& m : members) args += ", " + m;
            for (auto* r : pkg.roles) {
                std::string role = "one.Role(" + api_detail::go_string(r->name);
                for (const auto& p : r->permissions) {
                    std::string text = p.text();
                    auto at = text.rfind("::");
                    role += ", " + api_detail::go_string(at == std::string::npos ? text : text.substr(0, at) + ":" + text.substr(at + 2));
                }
                role += ")";
                if (r->per && r->from) {
                    role += ".Per(one.Entity[" + api_detail::go_name(*r->per) + "](), one.Entity[" + api_detail::go_name(*r->from) + "]())";
                }
                args += ", " + role;
            }
            {
                auto from_namespace = in(body, pkg.source, pkg.source.line);
                body.line("var Module = one.Module(" + args + ")");
            }

            stream out("\t");
            out.generated_from(pkg.path);
            auto fixed = out.fixed();  // what every package file has
            out.line("package " + package_name(ns));
            out.line();
            if (uses_time) {
                out.open("import (");
                out.line("\"time\"");
                out.line();
                out.line("\"github.com/da0x/uione/one\"");
                out.close(")");
            } else {
                out.line("import \"github.com/da0x/uione/one\"");
            }
            out.line();
            out.embed(body);
            out.drop_trailing_blank();
            return out;
        }

        // commands

        static std::string permission(const language::command_declaration& c) {
            for (const auto& s : c.body) {
                if (auto* p = std::get_if<language::permission_statement>(&s.node)) {
                    const auto& parts = p->permission.parts;
                    if (parts.size() == 1) {
                        if (parts[0] == "anyone") return "one.Anyone";
                        if (parts[0] == "authenticated") return "one.Authenticated";
                        if (parts[0] == "owner") return "one.Owner";
                    }
                    std::string text = p->permission.text();
                    auto at = text.rfind("::");
                    return api_detail::go_string(text.substr(0, at) + ":" + text.substr(at + 2));
                }
            }
            return "";
        }

        void command(stream& out, const std::string& ns, const language::entity_declaration& e, const language::command_declaration& c,
                     const std::string& var, bool& uses_time) {
            std::string head = "var " + var + " = one.Command[" + api_detail::go_name(e.name) + "](" +
                               api_detail::go_string(c.name.text()) + ")";
            std::string allow = permission(c);
            if (!allow.empty()) head += ".Allow(" + allow + ")";
            // An update changes only what its forms ask for, so editing an issue's
            // title can't also set its status.
            // It also changes what its changes statements name, for a hand-written
            // component; with neither, it may change anything but its keys.
            if (c.name.parts.back() == "update") {
                std::vector<std::string> fields;
                if (auto forms = form_fields_.find(web_detail::join(ns, c.name.text())); forms != form_fields_.end()) fields = forms->second;
                for (const auto& s : c.body) {
                    if (auto* changes = std::get_if<language::changes_statement>(&s.node)) {
                        for (const auto& name : changes->fields) {
                            if (std::find(fields.begin(), fields.end(), name) == fields.end()) fields.push_back(name);
                        }
                    }
                }
                if (!fields.empty()) {
                    std::string names;
                    for (const auto& name : fields) names += (names.empty() ? "" : ", ") + api_detail::go_string(name);
                    head += ".Fields(" + names + ")";
                }
            }

            std::vector<const language::statement*> body;
            for (const auto& s : c.body) {
                if (!std::holds_alternative<language::permission_statement>(s.node) && !std::holds_alternative<language::changes_statement>(s.node)) body.push_back(&s);
            }
            if (body.empty()) {
                out.line(head);
                return;
            }
            std::string me = e.name.substr(0, 1) == "c" ? "x" : e.name.substr(0, 1);
            pointed_.clear();
            for (const auto* s : body) find_pointed(e, *s);
            out.open(head + ".");
            out.open("Do(func(c *one.Ctx, " + me + " *" + api_detail::go_name(e.name) + ") error {");
            // What the body reads through a reference is read first, in the command's
            // own transaction, so its changes are saved with the command's.
            for (const auto& [name, target] : pointed_) {
                out.line(local_name(name) + ", err := one.Read[" + api_detail::go_name(target->name) + "](c, " + me + "." +
                         api_detail::go_name(name) + ")");
                out.open("if err != nil {");
                out.line("return err");
                out.close("}");
            }
            for (const auto* s : body) {
                auto from_statement = in(out, at(&c), s->where.line);
                statement(out, e, *s, me, uses_time);
            }
            out.line("return nil");
            out.close("})");
            out.dedent();
        }

        // A Go local variable for a snake_case name: due_at becomes dueAt.
        static std::string local_name(const std::string& snake) {
            std::string name = api_detail::go_name(snake);
            if (!name.empty()) name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
            return name;
        }

        const language::entity_declaration* target_of(const language::field& f) const {
            if (!pkg_ || !f.type || f.type->parts.size() != 1) return nullptr;
            return entity(*pkg_, f.type->parts[0]);
        }

        // report.project, in a command on report: the entity's own field, named with
        // the entity, the same as project alone. Returns the field's name.
        static std::optional<std::string> own(const language::entity_declaration& e, const language::expression& x) {
            auto* m = std::get_if<language::member_expression>(&x.node);
            if (!m) return std::nullopt;
            auto* object = std::get_if<language::name_expression>(&m->object->node);
            if (!object || object->name.parts.size() != 1 || object->name.parts[0] != e.name) return std::nullopt;
            return m->member;
        }

        void find_pointed(const language::entity_declaration& e, const language::expression& x) {
            if (auto* m = std::get_if<language::member_expression>(&x.node)) {
                auto* object = std::get_if<language::name_expression>(&m->object->node);
                auto through = own(e, *m->object);  // report.project.takes_reports
                const language::field* f = through ? field(e, *through)
                                         : object && object->name.parts.size() == 1 ? field(e, object->name.parts[0]) : nullptr;
                const language::entity_declaration* target = f ? target_of(*f) : nullptr;
                if (target && std::none_of(pointed_.begin(), pointed_.end(), [&](const auto& p) { return p.first == f->name; })) {
                    pointed_.emplace_back(f->name, target);
                }
                return;
            }
            if (auto* b = std::get_if<language::binary_expression>(&x.node)) {
                find_pointed(e, *b->left);
                find_pointed(e, *b->right);
            } else if (auto* u = std::get_if<language::unary_expression>(&x.node)) {
                find_pointed(e, *u->operand);
            }
        }

        void find_pointed(const language::entity_declaration& e, const language::statement& s) {
            if (auto* r = std::get_if<language::require_statement>(&s.node)) find_pointed(e, *r->condition);
            if (auto* a = std::get_if<language::assign_statement>(&s.node)) {
                find_pointed(e, *a->target);
                find_pointed(e, *a->value);
            }
            if (auto* c = std::get_if<language::create_statement>(&s.node)) {
                for (const auto& v : c->values) find_pointed(e, *v.value);
            }
        }

        // A field a command's body names, with the Go that reaches it: done is t.Done,
        // and book.status, through the loan's book, is book.Status.
        std::optional<std::pair<std::string, const language::field*>> resolve(const language::entity_declaration& e,
                                                                             const language::expression& x, const std::string& me) const {
            if (auto* n = std::get_if<language::name_expression>(&x.node); n && n->name.parts.size() == 1) {
                if (const auto* f = field(e, n->name.parts[0])) return std::pair{me + "." + api_detail::go_name(f->name), f};
            }
            if (auto name = own(e, x)) {  // report.title, the entity's own
                if (const auto* f = field(e, *name)) return std::pair{me + "." + api_detail::go_name(f->name), f};
            }
            if (auto* m = std::get_if<language::member_expression>(&x.node)) {
                auto* object = std::get_if<language::name_expression>(&m->object->node);
                auto through = own(e, *m->object);  // report.project.takes_reports
                if (!through && (!object || object->name.parts.size() != 1)) return std::nullopt;
                const std::string& via = through ? *through : object->name.parts[0];
                for (const auto& [name, target] : pointed_) {
                    if (name != via) continue;
                    if (const auto* f = field(*target, m->member)) {
                        return std::pair{local_name(name) + "." + api_detail::go_name(f->name), f};
                    }
                }
            }
            return std::nullopt;
        }

        void statement(stream& out, const language::entity_declaration& e, const language::statement& s, const std::string& me, bool& uses_time) {
            if (auto* r = std::get_if<language::require_statement>(&s.node)) {
                auto failing = negate(e, *r->condition, me);
                if (!failing) return;
                out.open("if " + *failing + " {");
                out.line("return c.Fail(" + api_detail::go_string(r->message) + ")");
                out.close("}");
            } else if (auto* a = std::get_if<language::assign_statement>(&s.node)) {
                auto target = resolve(e, *a->target, me);
                if (!target) {
                    unsupported(path_, s.where, "a command that changes anything but its own fields and those of what it points at");
                    return;
                }
                auto value = expression(e, *a->value, me, target->second);
                if (value) out.line(target->first + " = " + *value);
            } else if (auto* l = std::get_if<language::list_statement>(&s.node)) {
                // add me to assignees, remove me from assignees.
                const language::field* list = field(e, l->list);
                if (!list) return;
                auto value = expression(e, *l->value, me, nullptr);
                if (!value) return;
                std::string target = me + "." + api_detail::go_name(list->name);
                out.line(target + " = one." + (l->adds ? "Add" : "Remove") + "(" + target + ", " + *value + ")");
            } else if (auto* made = std::get_if<language::create_statement>(&s.node)) {
                // create member { ... }: made in the command's own transaction.
                const language::entity_declaration* target = pkg_ ? entity(*pkg_, made->entity) : nullptr;
                if (!target) {
                    unsupported(path_, s.where, "creating an entity from another namespace");
                    return;
                }
                std::string fields;
                for (const auto& v : made->values) {
                    const language::field* f = field(*target, v.name);
                    if (!f) continue;
                    auto value = expression(e, *v.value, me, f);
                    if (!value) return;
                    fields += (fields.empty() ? "" : ", ") + api_detail::go_name(f->name) + ": " + *value;
                }
                out.open("if err := one.Create(c, &" + api_detail::go_name(target->name) + "{" + fields + "}); err != nil {");
                out.line("return err");
                out.close("}");
            } else if (auto* cl = std::get_if<language::clear_statement>(&s.node)) {
                for (const auto& name : cl->fields) {
                    const language::field* f = field(e, name);
                    if (!f) continue;
                    bool is_time = f->type && f->type->text() == "date";
                    uses_time |= is_time;
                    out.line(me + "." + api_detail::go_name(name) + " = " + (is_time ? "time.Time{}" : zero(*f)));
                }
            } else {
                unsupported(path_, s.where, "this statement in a command");
            }
        }

        static std::string zero(const language::field& f) {
            if (f.list) return "[]string{}";
            if (f.type && (f.type->text() == "number" || f.type->text() == "serial")) return "0";
            if (f.type && f.type->text() == "boolean") return "false";
            if (f.type && f.type->text() == "date") return "time.Time{}";
            return "\"\"";
        }

        // The Go for a condition being false, which is when a require fails. A
        // negation is dropped and a comparison flipped, so the code reads plainly.
        std::optional<std::string> negate(const language::entity_declaration& e, const language::expression& condition, const std::string& me) {
            if (auto* u = std::get_if<language::unary_expression>(&condition.node); u && u->op == language::token_kind::logical_not) {
                return expression(e, *u->operand, me, nullptr);
            }
            if (auto* b = std::get_if<language::binary_expression>(&condition.node)) {
                static const std::map<language::token_kind, std::string> flipped{
                    {language::token_kind::equal, "!="}, {language::token_kind::not_equal, "=="}, {language::token_kind::less, ">="},
                    {language::token_kind::greater, "<="}, {language::token_kind::less_equal, ">"}, {language::token_kind::greater_equal, "<"}};
                if (auto it = flipped.find(b->op); it != flipped.end()) {
                    auto comparison = compare(e, *b, me, it->second);
                    if (comparison) return comparison;
                    return std::nullopt;
                }
            }
            auto value = expression(e, condition, me, nullptr);
            if (!value) return std::nullopt;
            return "!(" + *value + ")";
        }

        // A comparison with the operator given. Comparing a date with none asks
        // whether it's zero, since Go's time has no "none".
        std::optional<std::string> compare(const language::entity_declaration& e, const language::binary_expression& b, const std::string& me, const std::string& op) {
            auto* right = std::get_if<language::name_expression>(&b.right->node);
            auto left = resolve(e, *b.left, me);
            const language::field* f = left ? left->second : nullptr;
            if (right && right->name.text() == "none" && f) {
                const std::string& value = left->first;
                if (f->type && f->type->text() == "date") return (op == "==" ? "" : "!") + value + ".IsZero()";
                return value + " " + op + " " + zero(*f);
            }
            auto l = expression(e, *b.left, me, nullptr);
            auto r = expression(e, *b.right, me, f);
            if (!l || !r) return std::nullopt;
            return *l + " " + op + " " + *r;
        }

        // An expression inside a command's body, in Go. `beside` is the field it's
        // compared with or assigned to, so a choice's name becomes its constant.
        std::optional<std::string> expression(const language::entity_declaration& e, const language::expression& x, const std::string& me, const language::field* beside) {
            if (auto* lit = std::get_if<language::literal_expression>(&x.node)) {
                return lit->type == language::literal_expression::kind::string ? api_detail::go_string(lit->value) : lit->value;
            }
            // A choice of the field it's beside: status::closed.
            if (auto* n = std::get_if<language::name_expression>(&x.node); n && beside) {
                if (auto c = language::choice_of(n->name, *beside)) return choice(*beside, *c);
            }
            if (auto* n = std::get_if<language::name_expression>(&x.node); n && n->name.parts.size() == 1) {
                const auto& name = n->name.parts[0];
                if (name == "now") return std::string("c.Now()");
                if (name == "me") return std::string("c.Me()");
                if (name == "id") return me + ".ID";
                if (name == "true" || name == "false") return name;
                if (name == "none" && beside) return zero(*beside);
                if (beside && std::find(beside->choices.begin(), beside->choices.end(), name) != beside->choices.end()) {
                    return choice(*beside, name);
                }
                if (field(e, name)) return me + "." + api_detail::go_name(name);
            }
            if (std::holds_alternative<language::member_expression>(x.node)) {
                if (auto through = resolve(e, x, me)) return through->first;
                unsupported(path_, x.where, "reading a field of something the entity doesn't point at, like " + web_detail::text_of(x));
                return std::nullopt;
            }
            if (auto* u = std::get_if<language::unary_expression>(&x.node); u && u->op == language::token_kind::logical_not) {
                auto inner = expression(e, *u->operand, me, nullptr);
                if (inner) return "!" + *inner;
                return std::nullopt;
            }
            if (auto* b = std::get_if<language::binary_expression>(&x.node)) {
                static const std::map<language::token_kind, std::string> ops{
                    {language::token_kind::equal, "=="}, {language::token_kind::not_equal, "!="}, {language::token_kind::less, "<"},
                    {language::token_kind::greater, ">"}, {language::token_kind::less_equal, "<="}, {language::token_kind::greater_equal, ">="},
                    {language::token_kind::logical_and, "&&"}, {language::token_kind::logical_or, "||"}, {language::token_kind::plus, "+"},
                    {language::token_kind::minus, "-"}, {language::token_kind::star, "*"}};
                if (auto it = ops.find(b->op); it != ops.end()) {
                    if (it->second == "==" || it->second == "!=") return compare(e, *b, me, it->second);
                    auto l = expression(e, *b->left, me, nullptr);
                    auto r = expression(e, *b->right, me, nullptr);
                    if (l && r) return *l + " " + it->second + " " + *r;
                    return std::nullopt;
                }
            }
            unsupported(path_, x.where, "this expression in a command; a command can use its own fields, now, me, none, and plain values");
            return std::nullopt;
        }

        // views

        // A query: every entity of a kind, or those that meet each condition joined
        // by &&. A condition compares one of the entity's fields with == or != to the
        // person reading (user.id), the row a value is for (book.id, in a value of
        // the shelf's rows), none, or a plain value. `kind` is Where, or First for a
        // value taken from the earliest match.
        std::optional<std::string> query(const package& pkg, const language::expression& source, const language::expression* condition,
                                         const std::string& kind = "Where", const std::string& row_entity = "", bool changes = false) {
            auto* name = std::get_if<language::name_expression>(&source.node);
            const language::entity_declaration* e =
                name && name->name.parts.size() == 1 ? entity(pkg, name->name.parts[0]) : nullptr;
            if (!e) {
                unsupported(path_, source.where, "a view reading an entity from another namespace");
                return std::nullopt;
            }
            // The changes of an entity are asked by what each holds: the entity's id
            // under its own name, and what it points at.
            std::string type = changes ? "one.ChangeOf[" + api_detail::go_name(e->name) + "]" : api_detail::go_name(e->name);
            static const language::field itself{};
            std::vector<const language::expression*> parts;
            std::function<void(const language::expression&)> split = [&](const language::expression& x) {
                if (auto* b = std::get_if<language::binary_expression>(&x.node); b && b->op == language::token_kind::logical_and) {
                    split(*b->left);
                    split(*b->right);
                } else {
                    parts.push_back(&x);
                }
            };
            if (condition) split(*condition);
            if (parts.empty()) {
                if (kind != "Where") {
                    unsupported(path_, source.where, "first(...) without a condition");
                    return std::nullopt;
                }
                return "one.All[" + type + "]()";
            }
            std::string out;
            for (const auto* part : parts) {
                auto* b = std::get_if<language::binary_expression>(&part->node);
                bool except = b && b->op == language::token_kind::not_equal;
                bool has = b && b->op == language::token_kind::has;
                if (!b || (b->op != language::token_kind::equal && !except && !has)) {
                    unsupported(path_, part->where, "a view condition other than ==, != or has joined by &&");
                    return std::nullopt;
                }
                // The field, written plainly (returned_at) or through the entity (loan.book).
                std::string field_name;
                if (auto* n = std::get_if<language::name_expression>(&b->left->node); n && n->name.parts.size() == 1) {
                    field_name = n->name.parts[0];
                } else if (auto* m = std::get_if<language::member_expression>(&b->left->node)) {
                    if (web_detail::text_of(*m->object) == e->name) field_name = m->member;
                }
                const language::field* f = field_name.empty() ? nullptr : field(*e, field_name);
                if (changes) {
                    if (field_name == e->name) f = &itself;
                    else if (f && !(f->type && (f->type->text() == "user" || entity(pkg, f->type->text())))) f = nullptr;
                }
                if (!f) {
                    unsupported(path_, part->where, "a view condition that doesn't start with one of " + e->name + "'s fields");
                    return std::nullopt;
                }
                std::string right = web_detail::text_of(*b->right);
                std::string value;
                auto* rn = std::get_if<language::name_expression>(&b->right->node);
                if (right == "user.id") {
                    value = "one.Viewer";
                } else if (!row_entity.empty() && right == row_entity + ".id") {
                    value = "one.Row";
                } else if (!subject_.empty() && right == subject_ + ".id") {
                    value = "one.Subject";
                } else if (right == "none") {
                    value = "nil";
                } else if (auto* lit = std::get_if<language::literal_expression>(&b->right->node)) {
                    value = lit->type == language::literal_expression::kind::string ? api_detail::go_string(lit->value) : lit->value;
                } else if (rn && (right == "true" || right == "false")) {
                    value = right;
                } else if (rn && language::choice_of(rn->name, *f)) {
                    value = choice(*f, *language::choice_of(rn->name, *f));
                } else {
                    unsupported(path_, part->where, "a view condition comparing with " + right);
                    return std::nullopt;
                }
                std::string args = "(" + api_detail::go_string(f == &itself ? field_name : f->name) + ", " + value + ")";
                if (out.empty()) {
                    if ((except || has) && kind != "Where") {
                        unsupported(path_, part->where, "first(...) whose first condition is != or has");
                        return std::nullopt;
                    }
                    if (except) out = "one.All[" + type + "]().Except" + args;
                    else if (has) out = "one.All[" + type + "]().Has" + args;
                    else out = "one." + kind + "[" + type + "]" + args;
                } else {
                    out += (except ? ".Except" : has ? ".Has" : ".And") + args;
                }
            }
            return out;
        }

        // A value worked out for each row: first(loan where ...).member, the member of
        // the earliest loan that matches.
        std::optional<std::string> row_value(const package& pkg, const language::view_value& row, const std::string& each_entity) {
            auto* m = std::get_if<language::member_expression>(&row.value->node);
            auto* call = m ? std::get_if<language::call_expression>(&m->object->node) : nullptr;
            auto* callee = call ? std::get_if<language::name_expression>(&call->callee->node) : nullptr;
            auto* w = call && call->arguments.size() == 1 ? std::get_if<language::where_expression>(&call->arguments[0]->node) : nullptr;
            if (!callee || callee->name.text() != "first" || !w) {
                unsupported(path_, row.where, "a row value other than first(... where ...).field");
                return std::nullopt;
            }
            auto q = query(pkg, *w->source, w->condition.get(), "First", each_entity);
            if (!q) return std::nullopt;
            return "Value(" + api_detail::go_string(*row.name) + ", " + *q + ", " + api_detail::go_string(m->member) + ")";
        }

        // webhooks

        bool webhook(stream& out, const package& pkg, const language::webhook_declaration& w) {
            const language::entity_declaration* scope = entity(pkg, w.scope);
            const language::entity_declaration* issue = nullptr;
            for (auto* e : pkg.entities) {
                std::vector<const language::field*> keys;
                for (const auto& f : e->fields) {
                    if (f.key) keys.push_back(&f);
                }
                if (keys.size() == 2 && keys[0]->type && keys[0]->type->text() == w.scope && keys[1]->per == keys[0]->name) issue = e;
            }
            if (!scope || !issue) {
                unsupported(path_, w.scope_where, "a webhook whose project and issues aren't in its namespace");
                return false;
            }
            out.open("var Webhook" + api_detail::go_name(w.provider) + " = one.GitHub(" + api_detail::go_string(w.route) + ").For(one.Entity[" +
                     api_detail::go_name(scope->name) + "](), " + api_detail::go_string(w.repository) + ").Mentions(one.Entity[" +
                     api_detail::go_name(issue->name) + "]()).");
            for (std::size_t i = 0; i < w.handlers.size(); ++i) {
                const auto& h = w.handlers[i];
                auto from_handler = in(out, at(&w), h.where.line);
                out.open(std::string(h.event == "commit" ? "OnCommit" : "OnPullRequest") + "(func(c *one.Ctx, m one.Mention) error {");
                for (const auto& s : h.body) {
                    auto* made = std::get_if<language::create_statement>(&s.node);
                    const language::entity_declaration* target = made ? entity(pkg, made->entity) : nullptr;
                    if (!target) {
                        unsupported(path_, s.where, "this statement in a webhook");
                        return false;
                    }
                    auto from_statement = in(out, at(&w), s.where.line);
                    std::string fields;
                    for (const auto& v : made->values) {
                        const language::field* f = field(*target, v.name);
                        if (!f) continue;
                        auto value = mentioned(*v.value, *f);
                        if (!value) {
                            unsupported(path_, v.value->where, "giving " + v.name + " anything but what the webhook sends, a choice, or now");
                            return false;
                        }
                        fields += (fields.empty() ? "" : ", ") + api_detail::go_name(f->name) + ": " + *value;
                    }
                    out.open("if err := one.Create(c, &" + api_detail::go_name(target->name) + "{" + fields + "}); err != nil {");
                    out.line("return err");
                    out.close("}");
                }
                out.line("return nil");
                out.close(i + 1 < w.handlers.size() ? "})." : "})");
            }
            out.dedent();
            return true;
        }

        // What a webhook's handler gives a field: what GitHub sent, a choice, or now.
        std::optional<std::string> mentioned(const language::expression& x, const language::field& target) const {
            static const std::map<std::string, std::string> sent{
                {"mentioned", "m.Issue"}, {"message", "m.Message"}, {"title", "m.Title"}, {"url", "m.URL"},
                {"author", "m.Author"},   {"sha", "m.SHA"},         {"number", "m.Number"}};
            auto* n = std::get_if<language::name_expression>(&x.node);
            if (n) {
                if (auto c = language::choice_of(n->name, target)) return choice(target, *c);
            }
            if (!n || n->name.parts.size() != 1) return std::nullopt;
            const auto& name = n->name.parts[0];
            if (auto it = sent.find(name); it != sent.end()) return it->second;
            if (name == "now") return std::string("c.Now()");
            if (std::find(target.choices.begin(), target.choices.end(), name) != target.choices.end()) {
                return choice(target, name);
            }
            return std::nullopt;
        }

        // functions

        // A function's parameters take their Go types from where it's used: in
        // order sort_title(title), title is a text field, so it's a string.
        std::optional<std::vector<std::string>> parameter_types(const package& pkg, const language::function_declaration& function) {
            std::vector<std::string> types(function.parameters.size());
            for (const auto& [v, _] : pkg.views) {
                for (const auto& each : v->each) {
                    auto* source = std::get_if<language::name_expression>(&each.source->node);
                    const language::entity_declaration* e = source ? entity(pkg, source->name.text()) : nullptr;
                    if (!e) continue;
                    for (const auto& key : each.order) {
                        auto* call = std::get_if<language::call_expression>(&key->node);
                        auto* callee = call ? std::get_if<language::name_expression>(&call->callee->node) : nullptr;
                        if (!callee || callee->name.text() != function.name || call->arguments.size() != types.size()) continue;
                        for (std::size_t i = 0; i < types.size(); ++i) {
                            auto* arg = std::get_if<language::name_expression>(&call->arguments[i]->node);
                            const language::field* f = arg && arg->name.parts.size() == 1 ? field(*e, arg->name.parts[0]) : nullptr;
                            if (!f) continue;
                            if (auto t = type_of(pkg, *f)) types[i] = t->name;
                        }
                    }
                }
            }
            for (const auto& t : types) {
                if (t.empty()) return std::nullopt;
            }
            return types;
        }

        // An expression in a function's body, in Go, with its Go type.
        std::optional<std::pair<std::string, std::string>> function_expression(const language::expression& x,
                                                                         const std::map<std::string, std::string>& params) {
            if (auto* lit = std::get_if<language::literal_expression>(&x.node)) {
                if (lit->type == language::literal_expression::kind::string) return std::pair{api_detail::go_string(lit->value), std::string("string")};
                bool whole = lit->value.find('.') == std::string::npos;
                return std::pair{lit->value, std::string(whole ? "int" : "float64")};
            }
            if (auto* n = std::get_if<language::name_expression>(&x.node); n && n->name.parts.size() == 1) {
                const auto& name = n->name.parts[0];
                if (auto it = params.find(name); it != params.end()) return std::pair{local_name(name), it->second};
                if (name == "true" || name == "false") return std::pair{name, std::string("bool")};
            }
            if (auto* call = std::get_if<language::call_expression>(&x.node)) {
                auto* callee = std::get_if<language::name_expression>(&call->callee->node);
                std::vector<std::string> args;
                for (const auto& a : call->arguments) {
                    auto arg = function_expression(*a, params);
                    if (!arg) return std::nullopt;
                    args.push_back(arg->first);
                }
                std::string joined;
                for (const auto& a : args) joined += (joined.empty() ? "" : ", ") + a;
                std::string name = callee ? callee->name.text() : "";
                if (name == "starts_with" && args.size() == 2) return std::pair{"one.StartsWith(" + joined + ")", std::string("bool")};
                if (name == "drop" && args.size() == 2) return std::pair{"one.Drop(" + joined + ")", std::string("string")};
            }
            if (auto* u = std::get_if<language::unary_expression>(&x.node); u && u->op == language::token_kind::logical_not) {
                if (auto inner = function_expression(*u->operand, params)) return std::pair{"!" + inner->first, std::string("bool")};
                return std::nullopt;
            }
            if (auto* b = std::get_if<language::binary_expression>(&x.node)) {
                static const std::map<language::token_kind, std::string> ops{
                    {language::token_kind::equal, "=="}, {language::token_kind::not_equal, "!="},
                    {language::token_kind::logical_and, "&&"}, {language::token_kind::logical_or, "||"}};
                auto l = function_expression(*b->left, params);
                auto r = function_expression(*b->right, params);
                if (auto it = ops.find(b->op); it != ops.end() && l && r) {
                    return std::pair{l->first + " " + it->second + " " + r->first, std::string("bool")};
                }
            }
            unsupported(path_, x.where, "this in a function; a function can use its parameters, plain values, starts_with and drop");
            return std::nullopt;
        }

        // The type a function returns: that of the first thing it returns.
        std::optional<std::string> return_type(const std::vector<language::statement>& body, const std::map<std::string, std::string>& params) {
            for (const auto& s : body) {
                if (auto* r = std::get_if<language::return_statement>(&s.node); r && r->value) {
                    if (auto value = function_expression(*r->value, params)) return value->second;
                    return std::nullopt;
                }
                if (auto* i = std::get_if<language::if_statement>(&s.node)) {
                    if (auto t = return_type(i->then_body, params)) return t;
                }
            }
            return std::nullopt;
        }

        void function_statements(stream& out, const std::vector<language::statement>& body, const std::map<std::string, std::string>& params,
                           const code::source& file) {
            for (const auto& s : body) {
                auto from_statement = in(out, file, s.where.line);
                if (auto* r = std::get_if<language::return_statement>(&s.node); r && r->value) {
                    if (auto value = function_expression(*r->value, params)) out.line("return " + value->first);
                } else if (auto* i = std::get_if<language::if_statement>(&s.node)) {
                    auto condition = function_expression(*i->condition, params);
                    if (!condition) continue;
                    out.open("if " + condition->first + " {");
                    function_statements(out, i->then_body, params, file);
                    if (!i->else_body.empty()) {
                        out.dedent();
                        out.open("} else {");
                        function_statements(out, i->else_body, params, file);
                    }
                    out.close("}");
                } else {
                    unsupported(path_, s.where, "this statement in a function; a function can use if and return");
                }
            }
        }

        void functions(stream& out, const package& pkg) {
            for (const auto& [function, where] : pkg.functions) {
                auto types = parameter_types(pkg, *function);
                if (!types) {
                    unsupported(path_, where, "a function whose parameters' types can't be told from where it's used");
                    continue;
                }
                std::map<std::string, std::string> params;
                std::string list;
                for (std::size_t i = 0; i < types->size(); ++i) {
                    params[function->parameters[i]] = (*types)[i];
                    list += (list.empty() ? "" : ", ") + local_name(function->parameters[i]) + " " + (*types)[i];
                }
                auto returns = return_type(function->body, params);
                if (!returns) {
                    unsupported(path_, where, "a function that returns nothing");
                    continue;
                }
                auto from_function = in(out, at(function), where.line);
                out.open("func " + api_detail::go_name(function->name) + "(" + list + ") " + *returns + " {");
                function_statements(out, function->body, params, at(function));
                out.close("}");
                out.line();
            }
        }

        bool view(stream& out, const package& pkg, const language::view_declaration& v, language::location where, const std::string& var) {
            std::string head = "var " + var + " = one.View(" + api_detail::go_string(v.name) + ")";
            if (v.is_public) head += ".Public()";
            if (v.per) {
                if (*v.per == "user") {
                    head += ".PerUser()";
                } else if (entity(pkg, *v.per)) {
                    head += ".Per(one.Entity[" + api_detail::go_name(*v.per) + "]())";
                } else {
                    unsupported(path_, where, "a view per " + *v.per + ", which isn't an entity in this namespace");
                    return false;
                }
            }
            std::vector<std::pair<std::string, int>> calls;  // each with the line it came from
            subject_ = v.per && *v.per != "user" ? *v.per : "";
            if (v.readers) head += ".Readers(one.Entity[" + api_detail::go_name(*v.readers) + "]())";
            for (const auto& people : v.reader_people) {
                if (auto* m = std::get_if<language::member_expression>(&people->node)) head += ".ReadersFrom(" + api_detail::go_string(m->member) + ")";
            }
            if (v.public_when) {
                // public when issue.project.visibility == public: the path below the
                // view's own entity, and the value it's compared with.
                auto* b = std::get_if<language::binary_expression>(&v.public_when->node);
                std::string path = b ? web_detail::text_of(*b->left) : "";
                std::string value;
                if (b) {
                    // The value is a choice, written with its enum: visibility::public.
                    if (auto* n = std::get_if<language::name_expression>(&b->right->node); n && n->name.parts.size() <= 2) {
                        value = api_detail::go_string(n->name.parts.back());
                    } else if (auto* lit = std::get_if<language::literal_expression>(&b->right->node)) {
                        value = lit->type == language::literal_expression::kind::string ? api_detail::go_string(lit->value) : lit->value;
                    }
                }
                if (!path.starts_with(subject_ + ".") || value.empty()) {
                    unsupported(path_, v.public_when->where, "public when anything but a field of the view's entity, or of what it points at, compared with a value");
                    return false;
                }
                head += ".PublicWhen(" + api_detail::go_string(path.substr(subject_.size() + 1)) + ", " + value + ")";
            }
            for (const auto& value : v.values) {
                // A field of the entity the view is for: title = book.title.
                if (auto* m = std::get_if<language::member_expression>(&value.value->node); m && !subject_.empty()) {
                    if (web_detail::text_of(*m->object) == subject_) {
                        calls.push_back({"Copy(" + api_detail::go_string(*value.name) + ", " + api_detail::go_string(m->member) + ")",
                                         value.where.line});
                        continue;
                    }
                    // Through what it points at: lifecycle = issue.project.lifecycle.
                    auto* through = std::get_if<language::member_expression>(&m->object->node);
                    if (through && web_detail::text_of(*through->object) == subject_) {
                        calls.push_back({"Copy(" + api_detail::go_string(*value.name) + ", " + api_detail::go_string(through->member + "." + m->member) + ")",
                                         value.where.line});
                        continue;
                    }
                }
                auto* call = std::get_if<language::call_expression>(&value.value->node);
                auto* callee = call ? std::get_if<language::name_expression>(&call->callee->node) : nullptr;
                if (callee && callee->name.text() == "github_secret") {
                    calls.push_back({"GitHubSecret(" + api_detail::go_string(*value.name) + ")", value.where.line});
                    continue;
                }
                if (!callee || callee->name.text() != "count" || call->arguments.size() != 1) {
                    unsupported(path_, value.where, "a view value other than count(...), or a field of the entity a view per entity is for, or of what it points at");
                    return false;
                }
                const auto& argument = *call->arguments[0];
                std::optional<std::string> q;
                if (auto* w = std::get_if<language::where_expression>(&argument.node)) {
                    q = query(pkg, *w->source, w->condition.get());
                } else {
                    q = query(pkg, argument, nullptr);
                }
                if (!q) return false;
                calls.push_back({"Count(" + api_detail::go_string(*value.name) + ", " + *q + ")", value.where.line});
            }
            for (const auto& each : v.each) {
                auto q = query(pkg, *each.source, each.condition.get(), "Where", "", each.changes);
                if (!q) return false;
                // The list without a name is the view's rows; any other is a List.
                calls.push_back({each.name ? "List(" + api_detail::go_string(*each.name) + ", " + *q + ")" : "Each(" + *q + ")",
                                 each.where.line});
                std::vector<std::string> order;
                for (const auto& key : each.order) {
                    bool reverse = false;
                    const language::expression* e = key.get();
                    if (auto* u = std::get_if<language::unary_expression>(&e->node); u && u->op == language::token_kind::minus) {
                        reverse = true;
                        e = u->operand.get();
                    }
                    if (auto* call = std::get_if<language::call_expression>(&e->node)) {
                        auto* callee = std::get_if<language::name_expression>(&call->callee->node);
                        auto* arg = call->arguments.size() == 1 ? std::get_if<language::name_expression>(&call->arguments[0]->node) : nullptr;
                        if (!reverse && callee && arg && arg->name.parts.size() == 1 && function(pkg, callee->name.text())) {
                            order.push_back("one.By(" + api_detail::go_string(arg->name.parts[0]) + ", " + api_detail::go_name(callee->name.text()) + ")");
                            continue;
                        }
                    }
                    auto* n = std::get_if<language::name_expression>(&e->node);
                    if (!n || n->name.parts.size() != 1) {
                        unsupported(path_, key->where, "ordering by anything but a field, or a function of one");
                        return false;
                    }
                    order.push_back(api_detail::go_string((reverse ? "-" : "") + n->name.parts[0]));
                }
                if (!order.empty()) {
                    std::string joined;
                    for (const auto& o : order) joined += (joined.empty() ? "" : ", ") + o;
                    calls.push_back({"Order(" + joined + ")", each.order.front()->where.line});
                }
                if (each.limit) calls.push_back({"Limit(" + std::to_string(*each.limit) + ")", each.where.line});
                // A row holds what its block lists: the entity's own fields, fields read
                // through a reference (book.title), and values worked out for it.
                auto* source = std::get_if<language::name_expression>(&each.source->node);
                std::string each_entity = source ? source->name.text() : "";
                std::string fields;
                std::vector<std::pair<std::string, int>> values;
                int fields_line = 0;
                for (const auto& row : each.rows) {
                    if (row.name) {
                        auto value = row_value(pkg, row, each_entity);
                        if (!value) return false;
                        values.push_back({*value, row.where.line});
                        continue;
                    }
                    if (fields_line == 0) fields_line = row.where.line;
                    std::string written = web_detail::text_of(*row.value);
                    if (std::count(written.begin(), written.end(), '.') > 1) {
                        unsupported(path_, row.where, "a row that reads through two references, like " + written);
                        return false;
                    }
                    if (written.empty()) {
                        unsupported(path_, row.where, "a row that isn't a field, a field read through another entity, or a named value");
                        return false;
                    }
                    fields += (fields.empty() ? "" : ", ") + api_detail::go_string(written);
                }
                if (!fields.empty()) calls.push_back({"Fields(" + fields + ")", fields_line});
                for (const auto& value : values) calls.push_back(value);
            }
            if (calls.empty()) {
                out.line(head);
                return true;
            }
            out.line(head + ".");
            for (std::size_t i = 0; i < calls.size(); ++i) {
                auto from_call = in(out, at(&v), calls[i].second);
                out.line("\t" + calls[i].first + (i + 1 < calls.size() ? "." : ""));
            }
            return true;
        }
    };

    // The Go backend for a project whose files have parsed and checked cleanly.
    inline generated_api generate_api(const std::vector<language::file>& files, const std::string& project_dir, const std::string& out_dir) {
        return api_generator(files, project_dir, out_dir).generate();
    }

} // namespace one::generators
