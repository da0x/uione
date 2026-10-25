// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Building a project, and showing what a line of it became.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "generators/output.hpp"
#include "language/diagnostics.hpp"

namespace one::driver {

    struct built {
        language::diagnostics problems;  // why nothing was generated, if it wasn't
        std::string refusal;             // the same, in a sentence
        std::vector<generators::output_file> files;
        std::string note;
        std::string environment;         // the one it's built for, when the project has them
        std::vector<std::string> targets;  // the kinds of app it built, in the order the project says them
    };

    // Checks a project, and only when it's clean, generates everything it becomes,
    // for the environment named, or the first. Nothing is written; that's up to the
    // caller.
    built build(const std::string& project, const std::string& out, const std::string& environment = "",
                std::optional<std::vector<std::string>> targets = std::nullopt);

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
    shown show(const std::string& where);

} // namespace one::driver
