// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/editor.hpp"

#include <map>
#include <optional>
#include <regex>
#include <string>
#include <variant>
#include <vector>

#include "language/checker.hpp"
#include "language/library.hpp"
#include "language/parser.hpp"

namespace one::driver {

    definition define(const sources& given, const std::string& path, int line, int column) {
        language::diagnostics found;
        std::vector<language::file> files;
        for (const auto& [file, source] : given) files.push_back(language::parse(file, source, found));
        if (!found.empty()) return {};
        language::meanings meant;
        language::check(files, found, meant);
        const language::meaning* best = nullptr;
        for (const auto& m : meant) {
            int start = m.where.column, end = m.where.column + static_cast<int>(m.length);
            if (m.path != path || m.where.line != line || column < start || column >= end) continue;
            if (!best || m.length < best->length) best = &m;
        }
        if (!best) return {};
        return {true, best->says, best->to_path, best->to.line, best->to.column, best->section, best->where.column,
                best->where.column + static_cast<int>(best->length)};
    }

    definition define(const std::string& root, const std::string& path, int line, int column) {
        language::diagnostics found;
        auto read = read_sources(root, found);
        if (!found.empty()) return {};
        return define(read, path, line, column);
    }

    completions complete(const std::string& text, int line, int column) {
        // The library's settings and enums, by name.
        static const auto library = [] {
            struct known {
                std::map<std::string, const language::settings_declaration*> settings;
                std::map<std::string, const language::enum_declaration*> enums;
                language::file parsed;
            };
            auto out = std::make_shared<known>();
            language::diagnostics ignored;
            out->parsed = language::parse(std::string(language::library_path), language::library_source, ignored);
            for (const auto& d : out->parsed.declarations) {
                auto* n = std::get_if<language::namespace_declaration>(&d.node);
                if (!n) continue;
                for (const auto& inner : n->declarations) {
                    if (auto* s = std::get_if<language::settings_declaration>(&inner.node)) out->settings[s->name] = s;
                    if (auto* e = std::get_if<language::enum_declaration>(&inner.node)) out->enums[e->name] = e;
                }
            }
            return out;
        }();

        // The lines up to the cursor, and the blocks open there, each by what opens it.
        std::vector<std::string> lines;
        for (std::size_t at = 0; at <= text.size();) {
            std::size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            lines.push_back(text.substr(at, end - at));
            at = end + 1;
        }
        if (line < 1 || line > static_cast<int>(lines.size())) return {};
        std::string before = lines[static_cast<std::size_t>(line - 1)];
        before = before.substr(0, std::min(before.size(), static_cast<std::size_t>(std::max(column - 1, 0))));
        std::vector<std::string> open;
        auto follow = [&](const std::string& l) {
            std::string header;
            bool quoted = false;
            for (std::size_t i = 0; i < l.size(); ++i) {
                char c = l[i];
                if (quoted) {
                    if (c == '\\') ++i;
                    else if (c == '"') quoted = false;
                    continue;
                }
                if (c == '"') quoted = true;
                else if (c == '/' && i + 1 < l.size() && l[i + 1] == '/') break;
                else if (c == '{') {
                    std::size_t start = header.find_first_not_of(" \t");
                    open.push_back(start == std::string::npos ? "" : header.substr(start, header.find_last_not_of(" \t") + 1 - start));
                    header.clear();
                } else if (c == '}') {
                    if (!open.empty()) open.pop_back();
                    header.clear();
                } else {
                    header += c;
                }
            }
        };
        for (int i = 0; i + 1 < line; ++i) follow(lines[static_cast<std::size_t>(i)]);
        follow(before);

        static const std::regex project_block(R"(project\s+\w+)"), environment_block(R"(environment\s+\w+)");
        const language::settings_declaration* in = nullptr;
        if (open.size() == 1 && std::regex_match(open[0], project_block)) {
            in = library->settings["project"];
        } else if (open.size() == 2 && std::regex_match(open[0], project_block) && std::regex_match(open[1], environment_block)) {
            in = library->settings["environment"];
        }

        completions out;
        std::smatch m;
        static const std::regex naming(R"(^(\s*)(\w*)$)"), valuing(R"(^\s*(\w+)\s+((?:\w+::)*)(\w*)$)");
        if (open.empty() && std::regex_match(before, m, naming)) {
            out.from = static_cast<int>(m[1].length()) + 1;
            out.items.push_back({"import one", "uione's own library", "Says what a project may say: its settings, and the choices each takes."});
            return out;
        }
        if (!in) return out;
        if (std::regex_match(before, m, naming)) {
            out.from = static_cast<int>(m[1].length()) + 1;
            for (const auto& f : in->fields) {
                std::string type = f.type ? f.type->text() : "text";
                out.items.push_back({f.name, f.list ? "list of " + type : type, language::library_comment(f.where.line)});
            }
            if (in->name == "project") out.items.push_back({"environment", "a place it runs", "A place the project runs, deployed on its own, with settings of its own."});
            return out;
        }
        if (std::regex_match(before, m, valuing)) {
            out.from = static_cast<int>(before.size() - m[3].length()) + 1;
            for (const auto& f : in->fields) {
                if (f.name != m[1].str() || !f.type) continue;
                auto e = library->enums.find(f.type->text());
                if (e == library->enums.end()) continue;
                for (std::size_t i = 0; i < e->second->choices.size(); ++i) {
                    const auto& label = i < e->second->choice_labels.size() ? e->second->choice_labels[i] : std::string();
                    out.items.push_back({e->second->choices[i], label.empty() ? e->second->choices[i] : label,
                                         i < e->second->choice_where.size() ? language::library_comment(e->second->choice_where[i].line) : std::string()});
                }
            }
        }
        return out;
    }

} // namespace one::driver
