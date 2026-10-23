// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Bringing a project to this compiler: its fixes applied, and its version recorded.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "driver/check.hpp"
#include "language/diagnostics.hpp"

namespace one::driver {

    // Says in a project's block which compiler it's for, `one "0.4.0"`: changing the
    // line when there is one, and adding it under the block's first line when there
    // isn't, lined up with the settings beside it. False when there's no project block.
    bool record_version(sources& files, std::string_view version);









    struct upgraded {
        sources changed;                 // the files it changed, as they are now
        language::diagnostics problems;  // what's left that has no fix; nothing is changed while there's any
        std::size_t fixes = 0;
        bool recorded = false;           // whether the project block now names this compiler
    };

    // Brings a project to this compiler: applies the fixes its mistakes come with, and
    // checks again, until it's clean, then records this compiler's version in its
    // project block. It changes nothing unless it ends clean.
    upgraded upgrade(const std::string& root, std::string_view version);

} // namespace one::driver
