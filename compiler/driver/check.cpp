// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/check.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "language/checker.hpp"
#include "language/extends.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"
#include "version.hpp"

namespace one::driver {

    void check_sources(const sources& given, language::diagnostics& found, std::vector<language::file>* kept) {
        std::size_t errors_before = found.size();
        std::vector<language::file> files;
        for (const auto& [path, source] : given) files.push_back(language::parse(path, source, found));
        // A project that extends another has one project block, its own with the
        // other's folded in.
        language::inherit_projects(files);
        if (found.size() == errors_before) language::check(files, found);
        if (kept) *kept = std::move(files);
    }

    std::vector<std::string> extended_roots(const std::string& root, language::diagnostics& found) {
        namespace fs = std::filesystem;
        auto canonical = [](const std::string& dir) {
            std::error_code ignored;
            return fs::weakly_canonical(fs::absolute(dir, ignored), ignored).string();
        };
        std::vector<std::string> out;
        std::vector<std::string> names{fs::path(canonical(root)).filename().string()};
        std::set<std::string> seen{canonical(root)};
        for (std::string at = root;;) {
            const language::setting* extends = nullptr;
            std::string from;
            language::diagnostics ignored;
            std::vector<language::file> parsed;
            for (const auto& path : platform::find_one_files({at})) {
                auto source = platform::read_file(path);
                if (source) parsed.push_back(language::parse(path, *source, ignored));
            }
            for (const auto& f : parsed) {
                for (const auto& d : f.declarations) {
                    auto* p = std::get_if<language::project_declaration>(&d.node);
                    if (!p) continue;
                    for (const auto& s : p->settings) {
                        if (s.key == "extends") extends = &s, from = f.path;
                    }
                }
            }
            if (!extends) break;
            std::string next = language::extended_folder(language::folder_of(from), *extends);
            if (!fs::is_directory(next)) break;
            if (seen.contains(canonical(next))) {
                std::string through;
                for (std::size_t i = 1; i < names.size(); ++i) through += (i == 1 ? "" : ", then ") + names[i];
                found.push_back({from, extends->where,
                                 names.front() + " extends itself" + (through.empty() ? "" : " through " + through) +
                                     "; a project builds on another, never on itself"});
                break;
            }
            seen.insert(canonical(next));
            names.push_back(fs::path(canonical(next)).filename().string());
            out.push_back(next);
            at = next;
        }
        return out;
    }

    sources read_sources(const std::string& root, language::diagnostics& found) {
        sources read;
        auto paths = platform::find_one_files({root});
        if (paths.empty()) found.push_back({root, {}, "there are no .one files here"});
        for (const auto& extended : extended_roots(root, found)) {
            for (const auto& path : platform::find_one_files({extended})) paths.push_back(path);
        }
        for (const auto& path : paths) {
            if (auto source = platform::read_file(path)) read[path] = *source;
            else found.push_back({path, {}, "can't read this file"});
        }
        return read;
    }

    std::size_t check_project(const std::string& root, language::diagnostics& found,
                              std::vector<language::file>* kept) {
        std::size_t errors_before = found.size();
        auto read = read_sources(root, found);
        if (read.empty()) return 0;
        if (found.size() == errors_before) {
            std::vector<language::file> files;
            check_sources(read, found, &files);
            // A project says which compiler it's for, one "0.18.0". The command line runs
            // that one in this one's place, so only one that can't, like the studio's,
            // or one run on purpose with UIONE_TOOLCHAIN=local, gets here with another:
            // the first says so, rather than checking it by rules it doesn't know.
            const char* toolchain = std::getenv("UIONE_TOOLCHAIN");
            bool local = toolchain && std::string_view(toolchain) == "local";
            for (const auto& f : files) {
                for (const auto& d : f.declarations) {
                    auto* p = std::get_if<language::project_declaration>(&d.node);
                    if (!p || local) continue;
                    for (const auto& setting : p->settings) {
                        if (setting.key != "one" || setting.value == version) continue;
                        found.push_back({f.path, setting.where,
                                         "this project is for one " + setting.value + ", and this is one " + std::string(version) +
                                             ", which can't check it; check it with one " + setting.value + ", or run one upgrade to move it to " +
                                             std::string(version)});
                    }
                }
            }
            if (kept) *kept = std::move(files);
        }
        return platform::find_one_files({root}).size();
    }

    checked check(const std::vector<std::string>& roots) {
        checked out;
        for (const auto& root : roots) {
            std::vector<language::file> files;
            out.files += check_project(root, out.problems, &files);
            if (!out.project) out.project = outline_of(files);
            auto screens = screens_of(files);
            out.screens.insert(out.screens.end(), screens.begin(), screens.end());
        }
        return out;
    }

} // namespace one::driver
