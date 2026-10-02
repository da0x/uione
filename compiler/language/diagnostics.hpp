// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <string>
#include <vector>

#include "language/token.hpp"

namespace one::language {

    struct diagnostic {
        std::string path;
        location where;
        std::string message;
    };

    using diagnostics = std::vector<diagnostic>;

    // path:line:column: error: message, the shape editors and terminals know how to
    // turn into a link to the right place.
    inline std::string format(const diagnostic& d) {
        return d.path + ":" + std::to_string(d.where.line) + ":" +
               std::to_string(d.where.column) + ": error: " + d.message;
    }

} // namespace one::language
