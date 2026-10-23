// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/themes.hpp"

#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "language/library.hpp"
#include "language/parser.hpp"

namespace one::driver {

    themes_shown themes(const sources& given, const std::string& scope) {
        themes_shown out;
        language::diagnostics found;
        std::vector<language::file> files;
        for (const auto& [path, source] : given) files.push_back(language::parse(path, source, found));
        if (!found.empty()) return out;
        out.read = true;
        std::function<void(const std::vector<language::declaration>&, const std::string&, bool)> each =
            [&](const std::vector<language::declaration>& declarations, const std::string& path, bool own) {
                for (const auto& d : declarations) {
                    if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                        each(n->declarations, path, own);
                    } else if (auto* p = std::get_if<language::project_declaration>(&d.node)) {
                        out.project_path = path;
                        for (const auto& s : p->settings) {
                            if (s.key == "theme") out.theme = s.value;
                        }
                    } else if (auto* t = std::get_if<language::theme_declaration>(&d.node)) {
                        theme_shown shown{t->name, t->title, t->from, own ? "" : path, d.where.line, own, {}, ""};
                        if (auto look = generators::theme::resolve(files, t->name)) {
                            shown.look = *look;
                            shown.css = generators::theme::scoped_css(*look, scope);
                        }
                        out.themes.push_back(std::move(shown));
                    }
                }
            };
        language::diagnostics ignored;
        auto library = language::parse(std::string(language::library_path), language::library_source, ignored);
        each(library.declarations, "", true);
        for (const auto& f : files) each(f.declarations, f.path, false);
        return out;
    }

    themes_shown themes(const std::string& root, const std::string& scope) {
        language::diagnostics found;
        auto read = read_sources(root, found);
        if (!found.empty()) return {};
        return themes(read, scope);
    }

} // namespace one::driver
