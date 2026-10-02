// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What the compiler does, apart from where its answers go: check, build and show
// return what they found, and the command line and the WebAssembly build each
// report it their own way.

#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "generators/project.hpp"
#include "language/checker.hpp"
#include "language/diagnostics.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

namespace one::driver {

    struct checked {
        language::diagnostics problems;
        std::size_t files = 0;
    };

    // Reads one project's files and checks them together. The checker only runs when
    // everything parsed, since a half-read file would only cause follow-on errors.
    inline std::size_t check_project(const std::string& root, language::diagnostics& found,
                                     std::vector<language::file>* kept = nullptr) {
        auto paths = platform::find_one_files({root});
        if (paths.empty()) {
            found.push_back({root, {}, "there are no .one files here"});
            return 0;
        }
        std::size_t errors_before = found.size();
        std::vector<language::file> files;
        for (const auto& path : paths) {
            auto source = platform::read_file(path);
            if (!source) {
                found.push_back({path, {}, "can't read this file"});
                continue;
            }
            files.push_back(language::parse(path, *source, found));
        }
        if (found.size() == errors_before) language::check(files, found);
        if (kept) *kept = std::move(files);
        return paths.size();
    }

    // Each root is a project, checked on its own.
    inline checked check(const std::vector<std::string>& roots) {
        checked out;
        for (const auto& root : roots) out.files += check_project(root, out.problems);
        return out;
    }

    struct built {
        language::diagnostics problems;  // why nothing was generated, if it wasn't
        std::string refusal;             // the same, in a sentence
        std::vector<generators::output_file> files;
        std::string note;
    };

    // Checks a project, and only when it's clean, generates everything it becomes.
    // Nothing is written; that's up to the caller.
    inline built build(const std::string& project, const std::string& out) {
        built result;
        std::vector<language::file> files;
        std::size_t count = check_project(project, result.problems, &files);
        if (!result.problems.empty() || count == 0) {
            result.refusal = "nothing was built, because the project has errors";
            return result;
        }
        auto generated = generators::generate_project(files, project, out);
        if (!generated.errors.empty()) {
            result.problems = std::move(generated.errors);
            result.refusal = "nothing was built, because the backend can't be generated yet";
            return result;
        }
        result.files = std::move(generated.files);
        result.note = std::move(generated.note);
        return result;
    }

    struct shown_file {
        std::string path;
        std::vector<std::pair<std::size_t, std::string>> lines;  // numbered from 1
    };

    struct shown {
        language::diagnostics problems;
        bool understood = true;  // false when `where` isn't file:line or file:line-line
        std::vector<shown_file> files;
    };

    // The generated code that came from a line, or a range of lines, of a .one file,
    // in every output: what each part of the language turns into, the way an
    // assembly view shows a block of C++.
    inline shown show(const std::string& where) {
        namespace fs = std::filesystem;
        shown result;
        auto colon = where.rfind(':');
        if (colon == std::string::npos) {
            result.understood = false;
            return result;
        }
        std::string path = where.substr(0, colon), range = where.substr(colon + 1);
        int from = 0, to = 0;
        try {
            auto dash = range.find('-');
            from = std::stoi(range.substr(0, dash));
            to = dash == std::string::npos ? from : std::stoi(range.substr(dash + 1));
        } catch (const std::exception&) {
            result.understood = false;
            return result;
        }
        if (from < 1 || to < from) {
            result.understood = false;
            return result;
        }
        std::string project = fs::path(path).parent_path().string();
        if (project.empty()) project = ".";
        std::vector<language::file> files;
        check_project(project, result.problems, &files);
        if (!result.problems.empty()) return result;
        auto generated = generators::generate_project(files, project, project + "/build");
        if (!generated.errors.empty()) {
            result.problems = std::move(generated.errors);
            return result;
        }

        auto wanted = fs::weakly_canonical(path);
        for (const auto& file : generated.files) {
            shown_file found{file.path, {}};
            std::size_t at = 0;
            for (std::size_t n = 0; n < file.sources.size() && at < file.content.size(); ++n) {
                std::size_t end = file.content.find('\n', at);
                const auto& source = file.sources[n];
                bool blank = at == end;
                if (!blank && source && !source->fixed() && source->line >= from && source->line <= to &&
                    fs::weakly_canonical(source->path) == wanted) {
                    found.lines.emplace_back(n + 1, file.content.substr(at, end - at));
                }
                at = end + 1;
            }
            if (!found.lines.empty()) result.files.push_back(std::move(found));
        }
        return result;
    }

} // namespace one::driver
