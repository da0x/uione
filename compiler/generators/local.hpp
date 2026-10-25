// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes what a project that says storage local runs its commands and views with,
// inside its native app (decision 0015): one file under src/local for each
// namespace, its entities, commands and views described on one::local's package as
// the Go service describes them on one, and an install function the app's main
// calls to give them to its local_source. A command's body is C++; everything else
// is a line of description.
//
// What local storage can't do yet is an error at its line, so nothing half-works.

#pragma once

#include <string>
#include <vector>

#include "generators/output.hpp"
#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::generators {

    struct generated_local {
        std::vector<output_file> files;     // src/local/<namespace>.cpp
        std::vector<std::string> installs;  // each file's install function, like install_library
        language::diagnostics errors;       // what it can't write yet; nothing's written then
    };

    generated_local generate_local(const std::vector<language::file>& files);

} // namespace one::generators
