// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A recursive-descent parser for .one files. Each parse_ function reads one kind of
// thing and leaves the parser just after it.
//
// When something is wrong, the parser reports it and skips to the next declaration,
// so one mistake doesn't hide the rest of the file's errors.

#pragma once

#include <string>
#include <string_view>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::language {

    // Reads one .one file. Anything wrong is added to `out`; the tree holds whatever
    // could be read.
    file parse(std::string path, std::string_view source, diagnostics& out);

} // namespace one::language
