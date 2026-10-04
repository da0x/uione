// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "language/token.hpp"

namespace one::language {

    // An exact edit that resolves a mistake, for a mistake the compiler can see the
    // one answer to: replace `length` characters at `where` with `text`. `one upgrade`
    // applies these, so a change to the language comes with its own fix.
    struct fix {
        location where;
        std::size_t length = 0;
        std::string text;
    };

    struct diagnostic {
        std::string path;
        location where;
        std::string message;
        std::optional<language::fix> fix = {};
    };

    using diagnostics = std::vector<diagnostic>;

    // path:line:column: error: message, the shape editors and terminals know how to
    // turn into a link to the right place.
    inline std::string format(const diagnostic& d) {
        return d.path + ":" + std::to_string(d.where.line) + ":" +
               std::to_string(d.where.column) + ": error: " + d.message;
    }

} // namespace one::language
