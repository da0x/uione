// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a project says in short, said in full before it's checked, so the checker and
// the generators see one form:
//     each issue in project          each issue where project == project.id
//     each change in issue in project    each change of issue where project == project.id
//     me, in a view                  user.id, the person reading it
//     define role maintainer "Maintainer" in project { ... }
//                                    the role and member entities each project's roles
//                                    are kept in, and the roles it starts with

#pragma once

#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"

namespace one::language {

    void lower(std::vector<file>& files, diagnostics& out);

} // namespace one::language
