// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes the React app for a project: a Vite project whose screens are built on
// @uione/react and drawn by the component set the project picks with `ui`.
//
// Each .one file with screens becomes one file under src/screens, and each screen
// becomes one screen(...) in it. Everything a screen does is in @uione/react, so a
// generated screen is only the components that make it up, about as long as the
// .one it came from (decision 0002).
//
// Screens read their views live from Firestore and send commands to the Go backend
// that `one build` writes next to the app, through @uione/react/firebase.

#pragma once

#include <string>
#include <vector>

#include "generators/javascript.hpp"
#include "generators/output.hpp"
#include "language/ast.hpp"

namespace one::generators {

    // The web app for a project whose files have parsed and checked cleanly.
    std::vector<output_file> generate_web(const std::vector<language::file>& files, const std::string& project_dir,
                                          const std::string& out_dir);

} // namespace one::generators
