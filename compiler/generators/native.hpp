// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes the desktop and the terminal apps for a project: C++ on one::screens, drawn
// by one::desktop::runtime in a window or one::terminal::runtime in a terminal.
//
// Each .one file with screens becomes one file under src/screens, and each screen
// becomes one site screen in it, a function building it from the page it's on:
// the views it reads, its title, and its items as one::screens' components with
// their specs, worked out here as the web generator works out its props. The
// screens are the same C++ for both apps; only src/app.hpp, naming the runtime,
// and src/main.cpp, opening a window or a terminal, differ.
//
// What a screen reads is read live through one::client from the app's backend, as
// signed in through the browser, a sign-in kept from before, or a service's key.

#pragma once

#include <string>
#include <vector>

#include "generators/output.hpp"
#include "language/ast.hpp"

namespace one::generators {

    std::vector<output_file> generate_native(const std::vector<language::file>& files, const std::string& project_dir, const std::string& target);

} // namespace one::generators
