// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Each define agent as a program of its own, in agents/<name>: a main that signs in
// with its service's key and hands one::client::work what it does in each stage, and
// the CMakeLists that builds it against uione's client library.

#pragma once

#include <vector>

#include "generators/output.hpp"
#include "language/ast.hpp"

namespace one::generators {

    std::vector<output_file> generate_agents(const std::vector<language::file>& files);

} // namespace one::generators
