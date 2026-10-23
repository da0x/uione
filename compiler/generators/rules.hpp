// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes a project's Firestore security rules. Browsers only ever read views, and
// every view document says who may read it, so the rules are the same for every
// project: they read that answer off the document.

#pragma once

#include <string>

#include "generators/output.hpp"

namespace one::generators {

    std::string generate_rules(const std::string& project_dir);

    // The rules as a generated file. Every line is the same for every project.
    output_file rules_file(const std::string& project_dir);

} // namespace one::generators
