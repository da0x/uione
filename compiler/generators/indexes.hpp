// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The indexes Firestore needs to read a list kept to its newest rows, newest first,
// as the one library asks it to: for each way the list is picked by, the fields its
// conditions compare, then created_at, newest first. A list read whole needs none.
// A list a find box searches by words needs one too: the same fields, and the words.
// They're written to firestore.indexes.json, and the deploy declares each one, with
// the collections whose records expire, like suspensions after 90 days.

#pragma once

#include <vector>

#include "generators/output.hpp"
#include "language/ast.hpp"

namespace one::generators {

    // firestore.indexes.json: each index a list kept to its newest rows needs.
    output_file indexes_file(const std::vector<language::file>& files);

} // namespace one::generators
