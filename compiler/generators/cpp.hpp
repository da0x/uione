// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes an app's C++ client: one header, cpp/<app>.hpp, that a native program, a
// command line tool or a desktop app, includes to use the app. It holds a struct for
// every entity and every view, the ids entities are stored under, a function for
// every command, and every view read live, through libember, a C++ client for
// Firebase that a machine with only g++ and a TLS library builds.
//
// What it can't say in C++ yet is left out, with a comment saying so: the backend
// never depends on it, so it never stops a build.

#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "code/stream.hpp"
#include "generators/web.hpp"
#include "language/ast.hpp"

namespace one::generators {

    namespace cpp_detail {

        // A name as a C++ identifier: a word C++ keeps for itself gets an underscore.
        inline std::string identifier(std::string name) {
            static const std::set<std::string> reserved{
                "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "concept", "const",
                "consteval", "constexpr", "constinit", "continue", "co_await", "co_return", "co_yield", "decltype", "default",
                "delete", "do", "double", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend",
                "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator",
                "or", "private", "protected", "public", "register", "requires", "return", "short", "signed", "sizeof",
                "static", "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typeid", "typename",
                "union", "unsigned", "using", "virtual", "void", "volatile", "while", "xor", "exists", "from", "get", "listen"};
            std::replace(name.begin(), name.end(), '.', '_');
            std::replace(name.begin(), name.end(), '-', '_');
            for (std::size_t at = name.find("::"); at != std::string::npos; at = name.find("::")) name.replace(at, 2, "_");
            if (reserved.contains(name)) name += "_";
            return name;
        }

        inline std::string quoted(const std::string& text) {
            std::string out = "\"";
            for (char c : text) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out + "\"";
        }

        // A C++ type, and how it's read from what Firestore holds.
        struct cpp_type {
            std::string name;  // like std::string
            std::string read;  // a function of detail:: that reads it, like text
        };

    } // namespace cpp_detail

    class cpp_generator {
    public:
        cpp_generator(const std::vector<language::file>& files, std::string project_dir) : files_(files), project_dir_(std::move(project_dir)) {}

        output_file generate() {
            for (const auto& f : files_) collect(f, "", f.declarations);
            std::string app = cpp_detail::identifier(app_name());
            code::stream out("\t");
            out.generated_from(source_name());
            {
                auto guard = out.fixed();
                out.line("// " + app + "'s C++ client: its records, the ids they're stored under, its commands,");
                out.line("// and its views, read live. Built with libember (github.com/da0x/libember):");
                out.line("//");
                out.line("//   g++ -std=c++23 program.cpp $(pkg-config --cflags --libs botan-3)");
                out.line();
                out.line("#pragma once");
                out.line();
                out.line("#include <cctype>");
                out.line("#include <chrono>");
                out.line("#include <cstdio>");
                out.line("#include <format>");
                out.line("#include <functional>");
                out.line("#include <initializer_list>");
                out.line("#include <memory>");
                out.line("#include <optional>");
                out.line("#include <stdexcept>");
                out.line("#include <string>");
                out.line("#include <utility>");
                out.line("#include <vector>");
                out.line();
                out.line("#include <ember.hpp>");
                out.line();
                out.open("namespace " + app + " {");
                out.line();
                support(out);
            }
            for (auto& [ns, at] : namespaces_) {
                if (ns.empty()) continue;
                namespace_(out, ns, at);
            }
            client(out, app);
            {
                auto guard = out.fixed();
                out.close("} // namespace " + app);
            }
            return file("cpp/" + app + ".hpp", out);
        }

    private:
        struct place {
            std::string path;
            std::vector<std::pair<const language::entity_declaration*, std::string>> entities;  // and the file each is in
            std::vector<std::pair<const language::enum_declaration*, std::pair<std::string, int>>> enums;
            std::vector<std::pair<const language::command_declaration*, std::pair<std::string, int>>> commands;
            std::vector<std::pair<const language::view_declaration*, std::pair<std::string, int>>> views;
            std::map<std::string, int> entity_lines;
        };

        const std::vector<language::file>& files_;
        std::string project_dir_;
        std::map<std::string, place> namespaces_;
        std::string project_;  // the project block's name

        void collect(const language::file& f, const std::string& ns, const std::vector<language::declaration>& declarations) {
            for (const auto& d : declarations) {
                auto& at = namespaces_[ns];
                if (at.path.empty()) at.path = f.path;
                if (auto* p = std::get_if<language::project_declaration>(&d.node)) project_ = p->name;
                if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) collect(f, ns.empty() ? n->name : ns + "::" + n->name, n->declarations);
                if (auto* e = std::get_if<language::entity_declaration>(&d.node)) {
                    at.entities.emplace_back(e, f.path);
                    at.entity_lines[e->name] = d.where.line;
                }
                if (auto* e = std::get_if<language::enum_declaration>(&d.node)) at.enums.push_back({e, {f.path, d.where.line}});
                if (auto* c = std::get_if<language::command_declaration>(&d.node)) at.commands.push_back({c, {f.path, d.where.line}});
                if (auto* v = std::get_if<language::view_declaration>(&d.node)) at.views.push_back({v, {f.path, d.where.line}});
            }
        }

        // The project's name, or, without a project block, its folder's.
        std::string app_name() const {
            if (!project_.empty()) return project_;
            std::error_code ignored;
            auto folder = std::filesystem::weakly_canonical(std::filesystem::absolute(project_dir_), ignored).filename().string();
            return folder.empty() ? "app" : folder;
        }

        std::string source_name() const { return app_name() + "/"; }

        static const language::entity_declaration* entity(const place& at, const std::string& name) {
            for (const auto& [e, _] : at.entities) {
                if (e->name == name) return e;
            }
            return nullptr;
        }

        static const language::field* field(const language::entity_declaration& e, const std::string& name) {
            for (const auto& f : e.fields) {
                if (f.name == name) return &f;
            }
            return nullptr;
        }

        // An enum named on its own, like priority, or one a field declares inline, named
        // in full, since a member of a struct may have the same name.
        std::string enum_type(const std::string& ns, const language::entity_declaration& e, const language::field& f) const {
            std::string name = f.enum_name.empty() ? e.name + "_" + f.name : f.enum_name;
            return "::" + cpp_detail::identifier(app_name()) + "::" + cpp_detail::identifier(ns) + "::" + cpp_detail::identifier(name);
        }

        std::optional<cpp_detail::cpp_type> type_of(const place& at, const std::string& ns, const language::entity_declaration& e, const language::field& f) {
            if (!f.choices.empty() && !f.list) return cpp_detail::cpp_type{enum_type(ns, e, f), "choice<" + enum_type(ns, e, f) + ">"};
            if (f.list) return cpp_detail::cpp_type{"std::vector<std::string>", "texts"};
            if (!f.type || f.type->parts.size() != 1) return std::nullopt;
            const auto& t = f.type->parts[0];
            if (t == "number" || t == "serial") return cpp_detail::cpp_type{"double", "number"};
            if (t == "boolean") return cpp_detail::cpp_type{"bool", "flag"};
            if (t == "date") return cpp_detail::cpp_type{"time", "when"};
            if (t == "text" || t == "markdown" || t == "email" || t == "slug" || t == "user" || entity(at, t)) return cpp_detail::cpp_type{"std::string", "text"};
            return cpp_detail::cpp_type{"std::string", "text"};  // a format, like AAA-9999, is text
        }

        // What every record has, besides its own fields.
        static const std::vector<std::pair<std::string, cpp_detail::cpp_type>>& record_fields() {
            static const std::vector<std::pair<std::string, cpp_detail::cpp_type>> fields{
                {"id", {"std::string", "text"}},        {"created_at", {"time", "when"}}, {"created_by", {"std::string", "text"}},
                {"updated_at", {"time", "when"}}, {"updated_by", {"std::string", "text"}}};
            return fields;
        }

        // What a view's column holds: a field of the row's entity, read through what it
        // points at, like phase.title, or what a person's profile shows.
        std::optional<cpp_detail::cpp_type> column_type(const place& at, const std::string& ns, const language::entity_declaration* e, const std::string& dotted) {
            if (!e) return std::nullopt;
            std::vector<std::string> parts;
            for (std::size_t from = 0;;) {
                std::size_t dot = dotted.find('.', from);
                parts.push_back(dotted.substr(from, dot - from));
                if (dot == std::string::npos) break;
                from = dot + 1;
            }
            const language::entity_declaration* on = e;
            bool listed = false;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                bool last = i + 1 == parts.size();
                for (const auto& [name, type] : record_fields()) {
                    if (name == parts[i] && last) return listed ? std::optional<cpp_detail::cpp_type>(cpp_detail::cpp_type{"std::vector<std::string>", "texts"}) : type;
                }
                const language::field* f = on ? field(*on, parts[i]) : nullptr;
                if (!f) return std::nullopt;
                if (last) {
                    auto t = type_of(at, ns, *on, *f);
                    if (t && listed && !f->list) return cpp_detail::cpp_type{"std::vector<std::string>", "texts"};
                    return t;
                }
                listed = listed || f->list;
                if (!f->type) return std::nullopt;
                const std::string& next = f->type->text();
                if (next == "user") {
                    // What a person's profile shows: name, picture, username, and whether
                    // they're a service.
                    if (i + 2 != parts.size()) return std::nullopt;
                    const std::string& shown = parts[i + 1];
                    if (shown == "service") return listed ? std::nullopt : std::optional<cpp_detail::cpp_type>(cpp_detail::cpp_type{"bool", "flag"});
                    if (shown == "name" || shown == "picture" || shown == "username" || shown == "id") {
                        return listed ? cpp_detail::cpp_type{"std::vector<std::string>", "texts"} : cpp_detail::cpp_type{"std::string", "text"};
                    }
                    return std::nullopt;
                }
                on = entity(at, next);
                if (!on) return std::nullopt;
            }
            return std::nullopt;
        }

        // The helpers the rest reads values with, and the key ids are made from.
        void support(code::stream& out) {
            out.line("// When something happened, to the nanosecond.");
            out.line("using time = std::chrono::sys_time<std::chrono::nanoseconds>;");
            out.line();
            out.line("// What the app said when it refused a command: why, and its HTTP status.");
            out.open("struct refused : std::runtime_error {");
            out.line("int status;");
            out.line("refused(int status, const std::string& message) : std::runtime_error(message), status(status) {}");
            out.close("};");
            out.line();
            out.open("namespace detail {");
            out.line("inline std::string text(const ember::value& v) { return v.is_string() ? v.as_string() : std::string(); }");
            out.line("inline double number(const ember::value& v) { return v.is_integer() ? double(v.as_integer()) : v.is_double() ? v.as_double() : 0; }");
            out.line("inline bool flag(const ember::value& v) { return v.is_bool() && v.as_bool(); }");
            out.line("inline time when(const ember::value& v) { return v.is_time() ? v.as_time() : time{}; }");
            out.open("inline std::vector<std::string> texts(const ember::value& v) {");
            out.line("std::vector<std::string> out;");
            out.open("if (v.is_array()) {");
            out.line("for (const auto& item : v.as_array()) out.push_back(text(item));");
            out.close("}");
            out.line("return out;");
            out.close("}");
            out.open("template <class Choice> Choice choice(const ember::value& v) {");
            out.line("Choice out{};");
            out.line("from_string(text(v), out);");
            out.line("return out;");
            out.close("}");
            out.line("// A string as JSON.");
            out.open("inline std::string json(const std::string& s) {");
            out.line("std::string out = \"\\\"\";");
            out.open("for (unsigned char c : s) {");
            out.line("if (c == '\"' || c == '\\\\') out += '\\\\', out += char(c);");
            out.line("else if (c < 0x20) { char hex[8]; std::snprintf(hex, sizeof hex, \"\\\\u%04x\", c); out += hex; }");
            out.line("else out += char(c);");
            out.close("}");
            out.line("return out + \"\\\"\";");
            out.close("}");
            out.line("inline std::string json(double n) { char s[32]; std::snprintf(s, sizeof s, \"%.17g\", n); return s; }");
            out.line("inline std::string json(bool b) { return b ? \"true\" : \"false\"; }");
            out.open("inline std::string json(const std::vector<std::string>& list) {");
            out.line("std::string out = \"[\";");
            out.line("for (std::size_t i = 0; i < list.size(); ++i) out += (i ? \",\" : \"\") + json(list[i]);");
            out.line("return out + \"]\";");
            out.close("}");
            out.open("inline std::string json(time t) {");
            out.line("return json(std::format(\"{:%FT%TZ}\", std::chrono::floor<std::chrono::milliseconds>(t)));");
            out.close("}");
            out.close("} // namespace detail");
            out.line();
            out.line("// The id an entity is stored under, from its keys in order, as the app makes it:");
            out.line("// each part escaped as in a path, and a dash after the first part escaped too, so");
            out.line("// project engine and person x-1 is engine-x%2D1.");
            out.open("inline std::string key(std::initializer_list<std::string> parts) {");
            out.line("std::string out;");
            out.line("bool first = true;");
            out.open("for (const auto& part : parts) {");
            out.line("if (!first) out += '-';");
            out.open("for (unsigned char c : part) {");
            out.line("bool plain = std::isalnum(c) || c == '_' || c == '.' || c == '~' || c == '$' || c == '&' || c == '+' || c == ':' || c == '=' || c == '@';");
            out.line("if (plain || (c == '-' && first)) { out += char(c); continue; }");
            out.line("char hex[4];");
            out.line("std::snprintf(hex, sizeof hex, \"%%%02X\", c);");
            out.line("out += hex;");
            out.close("}");
            out.line("first = false;");
            out.close("}");
            out.line("return out;");
            out.close("}");
            out.line();
            out.line("// A number as a key holds it, 42 rather than 42.000000.");
            out.open("inline std::string key_part(double n) {");
            out.line("char s[32];");
            out.line("std::snprintf(s, sizeof s, \"%.15g\", n);");
            out.line("return s;");
            out.close("}");
            out.line("inline std::string key_part(const std::string& s) { return s; }");
            out.line();
        }

        void namespace_(code::stream& out, const std::string& ns, const place& at) {
            {
                auto guard = out.fixed();
                out.open("namespace " + cpp_detail::identifier(ns) + " {");
                out.line();
            }
            enums(out, ns, at);
            for (const auto& [e, path] : at.entities) record(out, ns, at, *e, path);
            for (const auto& [c, where] : at.commands) command_input(out, ns, at, *c, where);
            for (const auto& [v, where] : at.views) view(out, ns, at, *v, where);
            auto guard = out.fixed();
            out.close("} // namespace " + cpp_detail::identifier(ns));
            out.line();
        }

        void choices(code::stream& out, const std::string& name, const std::vector<std::string>& values) {
            std::string list;
            for (const auto& c : values) list += (list.empty() ? "" : ", ") + cpp_detail::identifier(c);
            out.line("enum class " + name + " { " + list + " };");
            out.open("inline const char* to_string(" + name + " c) {");
            out.open("switch (c) {");
            for (const auto& c : values) out.line("case " + name + "::" + cpp_detail::identifier(c) + ": return " + cpp_detail::quoted(c) + ";");
            out.close("}");
            out.line("return \"\";");
            out.close("}");
            out.line("// Reads a choice by its name; one that isn't, like a choice added since, leaves it.");
            out.open("inline bool from_string(const std::string& text, " + name + "& c) {");
            for (const auto& c : values) out.line("if (text == " + cpp_detail::quoted(c) + ") return c = " + name + "::" + cpp_detail::identifier(c) + ", true;");
            out.line("return false;");
            out.close("}");
            out.line();
        }

        void enums(code::stream& out, const std::string&, const place& at) {
            for (const auto& [e, where] : at.enums) {
                auto guard = out.from(where.first, where.second);
                choices(out, cpp_detail::identifier(e->name), e->choices);
            }
            // A field's own choices, like a book's status, make an enum named for both.
            for (const auto& [e, path] : at.entities) {
                for (const auto& f : e->fields) {
                    if (f.choices.empty() || !f.enum_name.empty()) continue;
                    auto guard = out.from(path, f.where.line);
                    choices(out, cpp_detail::identifier(e->name + "_" + f.name), f.choices);
                }
            }
        }

        void record(code::stream& out, const std::string& ns, const place& at, const language::entity_declaration& e, const std::string& path) {
            auto guard = out.from(path, at.entity_lines.at(e.name));
            std::string name = cpp_detail::identifier(e.name);
            std::vector<std::tuple<std::string, std::string, cpp_detail::cpp_type, int>> members;  // name in C++, as stored, type, line
            for (const auto& [n, t] : record_fields()) members.emplace_back(cpp_detail::identifier(n), n, t, at.entity_lines.at(e.name));
            for (const auto& f : e.fields) {
                auto t = type_of(at, ns, e, f);
                if (t) members.emplace_back(cpp_detail::identifier(f.name), f.name, *t, f.where.line);
            }
            out.open("struct " + name + " {");
            for (const auto& [member, stored, type, line] : members) {
                auto field_guard = out.from(path, line);
                out.line(type.name + " " + member + "{};");
            }
            out.line();
            out.open("static " + name + " from(const ember::value& v) {");
            out.line(name + " out;");
            for (const auto& [member, stored, type, line] : members) out.line("out." + member + " = detail::" + type.read + "(v[" + cpp_detail::quoted(stored) + "]);");
            out.line("return out;");
            out.close("}");
            out.close("};");
            // Its id, from its keys, when it has some.
            std::vector<const language::field*> keys;
            for (const auto& f : e.fields) {
                if (f.key) keys.push_back(&f);
            }
            if (!keys.empty()) {
                std::string parameters, parts;
                for (const auto* k : keys) {
                    auto t = type_of(at, ns, e, *k);
                    std::string type = t && t->name == "double" ? "double" : "const std::string&";
                    parameters += (parameters.empty() ? "" : ", ") + type + " " + cpp_detail::identifier(k->name);
                    parts += (parts.empty() ? "" : ", ") + std::string("key_part(") + cpp_detail::identifier(k->name) + ")";
                }
                out.line("inline std::string " + name + "_id(" + parameters + ") { return key({" + parts + "}); }");
            }
            out.line();
        }

        // The fields a command may be sent: any of its entity's, and what it takes
        // besides, like the phase a removed phase's issues move to. The app says which
        // it takes; one it doesn't is refused with a message saying so.
        void command_input(code::stream& out, const std::string& ns, const place& at, const language::command_declaration& c, const std::pair<std::string, int>& where) {
            auto guard = out.from(where.first, where.second);
            if (c.name.parts.size() != 2) return;
            const language::entity_declaration* e = entity(at, c.name.parts[0]);
            if (!e) {
                out.line("// " + c.name.text() + " acts on an entity of another namespace, which this client doesn't run yet.");
                out.line();
                return;
            }
            std::string name = cpp_detail::identifier(c.name.parts[0] + "_" + c.name.parts[1]);
            bool creates = c.name.parts[1] == "create";
            out.line("// What " + c.name.text() + " is sent.");
            out.open("struct " + name + " {");
            std::vector<std::pair<std::string, cpp_detail::cpp_type>> sent;
            if (!creates) sent.emplace_back("id", cpp_detail::cpp_type{"std::string", "text"});
            for (const auto& f : e->fields) {
                if (auto t = type_of(at, ns, *e, f)) sent.emplace_back(f.name, *t);
            }
            for (const auto& s : c.body) {
                if (auto* input = std::get_if<language::input_statement>(&s.node)) {
                    std::string t = input->type.text();
                    cpp_detail::cpp_type type{"std::string", "text"};
                    if (t == "number") type = {"double", "number"};
                    if (t == "boolean") type = {"bool", "flag"};
                    sent.emplace_back(input->name, type);
                }
            }
            for (const auto& [n, t] : sent) {
                if (n == "id") out.line("std::string id{};");
                else out.line("std::optional<" + t.name + "> " + cpp_detail::identifier(n) + "{};");
            }
            out.line();
            out.open("std::string json() const {");
            out.line("std::string out = \"{\";");
            out.line("auto add = [&](const char* name, const std::string& value) { out += (out.size() > 1 ? \",\" : \"\") + detail::json(std::string(name)) + \":\" + value; };");
            for (const auto& [n, t] : sent) {
                std::string member = cpp_detail::identifier(n);
                if (n == "id") {
                    out.line("add(\"id\", detail::json(id));");
                } else if (t.read.starts_with("choice")) {
                    out.line("if (" + member + ") add(" + cpp_detail::quoted(n) + ", detail::json(std::string(to_string(*" + member + "))));");
                } else {
                    out.line("if (" + member + ") add(" + cpp_detail::quoted(n) + ", detail::json(*" + member + "));");
                }
            }
            out.line("return out + \"}\";");
            out.close("}");
            out.close("};");
            out.line();
        }

        // A view: a struct of what it holds, typed where the language says what each is,
        // and an ember::value where it's worked out, like a count.
        void view(code::stream& out, const std::string& ns, const place& at, const language::view_declaration& v, const std::pair<std::string, int>& where) {
            auto guard = out.from(where.first, where.second);
            std::string name = cpp_detail::identifier(v.name);
            const language::entity_declaration* subject = v.per && *v.per != "user" ? entity(at, *v.per) : nullptr;
            out.open("struct " + name + " {");
            out.line("bool exists = false;  // whether it's been made, and may be read");
            std::vector<std::tuple<std::string, std::string, std::optional<cpp_detail::cpp_type>>> values;
            for (const auto& value : v.values) {
                if (!value.value) continue;
                std::string written = web_detail::text_of(*value.value);
                std::string stored = value.name ? *value.name : written;
                if (stored.empty()) continue;
                std::optional<cpp_detail::cpp_type> type;
                // project.name, in a view per project: a field of what it's for.
                if (subject && written.starts_with(subject->name + ".")) type = column_type(at, ns, subject, written.substr(subject->name.size() + 1));
                values.emplace_back(cpp_detail::identifier(stored), stored, type);
            }
            for (const auto& [member, stored, type] : values) out.line((type ? type->name : std::string("ember::value")) + " " + member + "{};");
            struct list {
                std::string member, stored, row;
                std::vector<std::tuple<std::string, std::string, std::optional<cpp_detail::cpp_type>>> columns;
            };
            std::vector<list> lists;
            for (const auto& each : v.each) {
                auto each_guard = out.from(where.first, each.where.line);
                std::string stored = each.name ? *each.name : "rows";
                std::string source = each.source ? web_detail::text_of(*each.source) : "";
                const language::entity_declaration* rows = each.changes ? nullptr : entity(at, source);
                list l{cpp_detail::identifier(stored), stored, cpp_detail::identifier(stored) + "_row", {}};
                l.columns.emplace_back("id", "id", cpp_detail::cpp_type{"std::string", "text"});
                for (const auto& column : each.rows) {
                    if (!column.value) continue;
                    std::string written = web_detail::text_of(*column.value);
                    std::string named = column.name ? *column.name : written;
                    if (named.empty() || named == "id") continue;
                    l.columns.emplace_back(cpp_detail::identifier(named), named, column.name ? std::nullopt : column_type(at, ns, rows, written));
                }
                out.open("struct " + l.row + " {");
                for (const auto& [member, cstored, type] : l.columns) out.line((type ? type->name : std::string("ember::value")) + " " + member + "{};");
                out.open("static " + l.row + " from(const ember::value& v) {");
                out.line(l.row + " out;");
                for (const auto& [member, cstored, type] : l.columns) {
                    out.line("out." + member + " = " + (type ? "detail::" + type->read + "(v[" + cpp_detail::quoted(cstored) + "])" : "v[" + cpp_detail::quoted(cstored) + "]") + ";");
                }
                out.line("return out;");
                out.close("}");
                out.close("};");
                out.line("std::vector<" + l.row + "> " + l.member + "{};");
                lists.push_back(std::move(l));
            }
            out.line();
            // Said as struct, since a list of the view may have its name, like services.
            out.open("static struct " + name + " from(const ember::snapshot& s) {");
            out.line("struct " + name + " out;");
            out.line("out.exists = s.exists;");
            out.line("const ember::value& v = s.data;");
            for (const auto& [member, stored, type] : values) {
                out.line("out." + member + " = " + (type ? "detail::" + type->read + "(v[" + cpp_detail::quoted(stored) + "])" : "v[" + cpp_detail::quoted(stored) + "]") + ";");
            }
            for (const auto& l : lists) {
                out.open("if (v[" + cpp_detail::quoted(l.stored) + "].is_array()) {");
                out.line("for (const auto& row : v[" + cpp_detail::quoted(l.stored) + "].as_array()) out." + l.member + ".push_back(" + l.row + "::from(row));");
                out.close("}");
            }
            out.line("return out;");
            out.close("}");
            // Where it's kept: one document, or one for each thing it's per.
            std::string path = "views/" + ns + "::" + v.name;
            if (!v.per) {
                out.line("static std::string path() { return " + cpp_detail::quoted(path) + "; }");
            } else {
                out.line("static std::string path(const std::string& of) { return " + cpp_detail::quoted(path + ":") + " + of; }");
            }
            out.close("};");
            out.line();
        }

        void client(code::stream& out, const std::string& app) {
            auto guard = out.fixed();
            out.line("// A view to read once, or to listen to as it changes.");
            out.open("template <class View> class live {");
            out.line("public:");
            out.line("live(ember::database& store, std::string path) : store_(store), path_(std::move(path)) {}");
            out.line("View get() { return View::from(store_.get(path_)); }");
            out.open("ember::database::registration listen(std::function<void(const View&)> on_change, std::function<void(const ember::error&)> on_error = {}) {");
            out.line("return store_.listen(path_, [on_change](const ember::snapshot& s) { on_change(View::from(s)); }, std::move(on_error));");
            out.close("}");
            out.line("private:");
            out.line("ember::database& store_;");
            out.line("std::string path_;");
            out.close("};");
            out.line();
            out.line("// Who's asking: a person, or a service and the project and role it holds.");
            out.open("struct caller {");
            out.line("std::string id{}, name{}, place{}, role{};");
            out.line("bool service = false;");
            out.close("};");
            out.line();
            out.line("// Where the app is: its address, and, for the emulators, where they listen.");
            out.open("struct options {");
            out.line("std::string server{};          // like https://neotrac.org");
            out.line("std::string project{};         // its Firebase project; asked of the server when empty");
            out.line("std::string api_key{};         // asked of the server with the project when empty");
            out.line("std::string firestore_host{};  // the Firestore emulator, like localhost:8080");
            out.line("std::string auth_host{};       // the Auth emulator, like localhost:9099");
            out.close("};");
            out.line();
            out.line("// The app: sign in as a person or with a service's key, run its commands, and read");
            out.line("// its views live. Callbacks run on the executor given, or on libember's own thread.");
            out.open("class client {");
            out.line("public:");
            out.line("explicit client(options o, ember::executor* deliver_on = nullptr) : options_(std::move(o)), deliver_on_(deliver_on) {}");
            out.line();
            out.line("// Signs in as a service: its key runs commands, and trades for a sign-in of");
            out.line("// the service's own that reads its views.");
            out.open("void sign_in_with_key(const std::string& key) {");
            out.line("key_ = key;");
            out.line("auto token = post(\"/api/token\", \"{}\");");
            out.line("connect();");
            out.line("auth_->sign_in_with_custom_token(detail::text(ember::parse_json(token)[\"token\"]));");
            out.close("}");
            out.line();
            out.line("// Signs in as a person, with the refresh token a sign-in kept.");
            out.open("void sign_in_with_refresh_token(const std::string& token) {");
            out.line("connect();");
            out.line("auth_->sign_in_with_refresh_token(token);");
            out.close("}");
            out.line();
            out.line("// Signs in as a person, with a GitHub or Google access token, like one a device's");
            out.line("// sign-in gave: github.com or google.com.");
            out.open("void sign_in_with_provider(const std::string& provider, const std::string& access_token) {");
            out.line("connect();");
            out.line("auth_->sign_in_with_idp(provider, access_token);");
            out.close("}");
            out.line();
            out.line("std::string refresh_token() { connect(); return auth_->refresh_token(); }");
            out.line();
            out.open("caller me() {");
            out.line("auto v = ember::parse_json(get(\"/api/me\"));");
            out.line("caller out{detail::text(v[\"id\"]), detail::text(v[\"name\"]), \"\", detail::text(v[\"role\"]), detail::flag(v[\"service\"])};");
            out.line("for (const auto& [name, value] : v.as_map()) if (name != \"id\" && name != \"name\" && name != \"role\" && name != \"service\") out.place = detail::text(value);");
            out.line("return out;");
            out.close("}");
            out.line();
            for (auto& [ns, at] : namespaces_) {
                if (ns.empty()) continue;
                std::string space = cpp_detail::identifier(ns);
                for (const auto& [c, where] : at.commands) {
                    if (c->name.parts.size() != 2 || !entity(at, c->name.parts[0])) continue;
                    auto command_guard = out.from(where.first, where.second);
                    std::string name = cpp_detail::identifier(c->name.parts[0] + "_" + c->name.parts[1]);
                    std::string route = "/api/" + ns + "/" + c->name.parts[0] + "/" + c->name.parts[1];
                    out.line("// " + c->name.text() + ": the id of what it changed, or refused.");
                    out.open("std::string " + name + "(const " + space + "::" + name + "& sent) {");
                    out.line("return detail::text(ember::parse_json(post(" + cpp_detail::quoted(route) + ", sent.json()))[\"id\"]);");
                    out.close("}");
                }
                for (const auto& [v, where] : at.views) {
                    auto view_guard = out.from(where.first, where.second);
                    std::string name = cpp_detail::identifier(v->name);
                    std::string type = space + "::" + name;
                    if (!v->per) {
                        out.line("live<" + type + "> " + name + "() { connect(); return {*store_, " + type + "::path()}; }");
                    } else if (*v->per == "user") {
                        out.line("// What it shows the one signed in.");
                        out.line("live<" + type + "> " + name + "() { connect(); return {*store_, " + type + "::path(auth_->current_user().value().uid)}; }");
                    } else {
                        out.line("live<" + type + "> " + name + "(const std::string& " + cpp_detail::identifier(*v->per) + ") { connect(); return {*store_, " + type + "::path(" +
                                 cpp_detail::identifier(*v->per) + ")}; }");
                    }
                }
            }
            out.line();
            out.line("private:");
            out.line("options options_;");
            out.line("ember::executor* deliver_on_;");
            out.line("std::string key_;");
            out.line("std::unique_ptr<ember::auth> auth_;");
            out.line("std::unique_ptr<ember::database> store_;");
            out.line();
            out.line("// Finds the app's Firebase project and key, from what Firebase Hosting serves at");
            out.line("// /__/firebase/init.json, once, then makes its sign-in and its store.");
            out.open("void connect() {");
            out.line("if (store_) return;");
            out.line("ember::options o = ember::options::for_project(options_.project);");
            out.line("o.api_key = options_.api_key;");
            out.line("o.firestore_host = options_.firestore_host;");
            out.line("o.auth_host = options_.auth_host;");
            out.open("if (o.project.empty()) {");
            out.line("auto found = ember::https_get(options_.server + \"/__/firebase/init.json\", {});");
            out.line("if (found.status != 200) throw refused(found.status, \"the app's settings weren't found at \" + options_.server);");
            out.line("auto settings = ember::parse_json(found.body);");
            out.line("o.project = detail::text(settings[\"projectId\"]);");
            out.line("if (o.api_key.empty()) o.api_key = detail::text(settings[\"apiKey\"]);");
            out.close("}");
            out.line("auth_ = std::make_unique<ember::auth>(o);");
            out.line("store_ = std::make_unique<ember::database>(o, auth_.get(), deliver_on_);");
            out.close("}");
            out.line();
            out.line("// Who's asking, for a command: a service's key, or a person's sign-in.");
            out.open("std::vector<std::pair<std::string, std::string>> credentials() {");
            out.line("std::vector<std::pair<std::string, std::string>> headers{{\"Content-Type\", \"application/json\"}};");
            out.line("if (!key_.empty()) headers.emplace_back(\"Authorization\", \"Bearer \" + key_);");
            out.line("else if (auth_ && auth_->current_user()) headers.emplace_back(\"Authorization\", \"Bearer \" + auth_->id_token());");
            out.line("return headers;");
            out.close("}");
            out.line();
            out.open("std::string post(const std::string& route, const std::string& body) {");
            out.line("auto answer = ember::https_post(options_.server + route, credentials(), body);");
            out.line("if (answer.status != 200) throw refused(answer.status, why(answer));");
            out.line("return answer.body;");
            out.close("}");
            out.line();
            out.open("std::string get(const std::string& route) {");
            out.line("auto answer = ember::https_get(options_.server + route, credentials());");
            out.line("if (answer.status != 200) throw refused(answer.status, why(answer));");
            out.line("return answer.body;");
            out.close("}");
            out.line();
            out.open("static std::string why(const ember::response& answer) {");
            out.open("try {");
            out.line("auto said = detail::text(ember::parse_json(answer.body)[\"error\"]);");
            out.line("if (!said.empty()) return said;");
            out.close("} catch (...) {}");
            out.line("return \"the app answered \" + std::to_string(answer.status);");
            out.close("}");
            out.close("};");
            out.line();
            (void)app;
        }
    };

    inline output_file generate_cpp(const std::vector<language::file>& files, const std::string& project_dir) {
        return cpp_generator(files, project_dir).generate();
    }

} // namespace one::generators
