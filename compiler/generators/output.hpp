// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a generator writes: a file's path and content, and where each line came from.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "code/stream.hpp"

namespace one::generators {

    struct output_file {
        std::string path;  // inside the web folder, like src/app.tsx
        std::string content;
        bool executable = false;  // a script, to be run as it is
        std::vector<std::optional<code::source>> sources = {};  // where each line came from
    };

    inline output_file file(std::string path, const code::stream& written, bool executable = false) {
        return {std::move(path), written.str(), executable, written.sources()};
    }

} // namespace one::generators
