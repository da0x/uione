// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Where a project keeps its data: on Firebase with a Go service, as 0001 says, or,
// when its project block says storage local, on the machine its app runs on (0015).
// A local project has nobody signing in and no network, so what needs either is an
// error here, at its line, until local storage can do what it means.

#pragma once

#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::language {

    // Whether a project's block says storage local.
    bool stores_locally(const std::vector<file>& files);

    // Everything a project that says storage local says is something one machine can
    // keep, or it's an error saying what, at its line.
    void check_storage(const std::vector<file>& files, diagnostics& out);

}  // namespace one::language
