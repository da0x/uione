// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Everything one project generates, in one place, so `one build` writes it and
// `one show` reads it the same way.

#pragma once

#include <string>
#include <vector>

#include "generators/output.hpp"
#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::generators {

    struct generated_project {
        std::vector<output_file> files;  // inside the build folder, like web/src/app.tsx
        language::diagnostics errors;    // what the backend can't do yet; nothing is written then
        std::string note;                // something worth saying that isn't an error
    };

    // The project's files have parsed and checked cleanly. `out_dir` is where the
    // build folder goes, since imports between it and the project are worked out.
    // The views that say who holds which roles (roles.hpp) are added to the files
    // first, so every generator sees them.
    generated_project generate_project(std::vector<language::file>& files, const std::string& project_dir,
                                       const std::string& out_dir, const std::vector<std::string>& targets = {"web"});

} // namespace one::generators
