// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Reading a project's files and checking them, with what they extend.

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "driver/outline.hpp"
#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::driver {

    struct checked {
        language::diagnostics problems;
        std::size_t files = 0;
        std::optional<driver::outline> project;  // when a project block parsed
        std::vector<outlined_screen> screens;    // when everything parsed
    };

    // A project's .one files, by path, as text.
    using sources = std::map<std::string, std::string>;

    // Checks a project's files as given, which may be edits not yet written, all
    // together. The checker only runs when everything parsed, since a half-read file
    // would only cause follow-on errors.
    void check_sources(const sources& given, language::diagnostics& found, std::vector<language::file>* kept = nullptr);

    // The folders of the projects a project extends, nearest first: the one its
    // project block names, the one that one names, and so on. One that's missing is
    // the checker's to say; one that comes back round is said here, and goes no
    // further.
    std::vector<std::string> extended_roots(const std::string& root, language::diagnostics& found);

    // Reads a project's .one files, and those of the projects it extends, saying
    // which can't be read.
    sources read_sources(const std::string& root, language::diagnostics& found);

    // Reads one project's files and checks them together.
    std::size_t check_project(const std::string& root, language::diagnostics& found,
                              std::vector<language::file>* kept = nullptr);

    // Each root is a project, checked on its own.
    checked check(const std::vector<std::string>& roots);

} // namespace one::driver
