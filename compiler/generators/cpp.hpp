// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes an app's C++ client: one header, cpp/<app>.hpp, that a native program, a
// command line tool or a desktop app, includes to use the app. It holds a struct for
// every entity and every view, the ids entities are stored under, a function for
// every command, and every view read live, through libember, a C++ client for
// Firebase that a machine with only g++ and a TLS library builds.
//
// What it can't say in C++ yet is left out, with a comment saying so: the backend
// never depends on it, so it never stops a build.

#pragma once

#include <string>
#include <vector>

#include "generators/output.hpp"
#include "language/ast.hpp"

namespace one::generators {

    output_file generate_cpp(const std::vector<language::file>& files, const std::string& project_dir);

} // namespace one::generators
